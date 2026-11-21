"""Live frames from an iPhone, via WebDriverAgent (XCUITest) over USB.

iOS has no adb. WebDriverAgent, the XCUITest server Appium uses, is built onto
the phone with Xcode and serves screenshots and synthesised touches over HTTP;
`iproxy 8100 8100` forwards it to localhost over the USB cable.

An iPhone screen is not the 9:16 the live path's readers were built against
(BlueStacks at 1080x1920), so a frame is a CROP of the screenshot: the
rectangle that matches the 9:16 layout, resized to 549x976. The same rectangle
maps taps back (live/wda_actuator.py), so capture and touch cannot disagree.
The rectangle comes from tools/calibrate_ios.py and lives in a JSON file:

    {"crop": [x, y, w, h],     # in screenshot pixels
     "scale": 3.0,             # screenshot pixels per WDA point
     "tray_dy": 0}             # optional, see below

A tall screen can put extra space between the arena and the card tray. Then
the tray (the frame's rows below SPLIT) sits `tray_dy` screenshot pixels lower
than one 9:16 rectangle says; the frame is stitched from the two bands and taps
in the tray are shifted by the same amount.
"""
from __future__ import annotations

import base64
import json
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

import cv2
import numpy as np

from capture.source import Frame, FrameSource

GAME_SIZE = (549, 976)
DEFAULT_URL = "http://127.0.0.1:8100"
# Fraction of the frame's height where the card tray starts (row 790 of 976).
SPLIT = 790 / 976


class WdaError(RuntimeError):
    pass


class WdaClient:
    """The few WebDriverAgent endpoints the live loop needs."""

    def __init__(self, url: str = DEFAULT_URL, timeout: float = 15.0):
        self.url = url.rstrip("/")
        self.timeout = timeout
        self._session: str | None = None

    def _call(self, method: str, path: str, body: dict | None = None) -> dict:
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(self.url + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                return json.loads(resp.read() or b"{}")
        except OSError as exc:
            raise WdaError(f"WebDriverAgent at {self.url} not reachable ({exc}). "
                           "Is WDA running on the phone and `iproxy 8100 8100` "
                           "up?") from exc

    def status(self) -> dict:
        return self._call("GET", "/status").get("value", {})

    @property
    def session(self) -> str:
        if self._session is None:
            out = self._call("POST", "/session", {"capabilities": {}})
            sid = out.get("sessionId") or out.get("value", {}).get("sessionId")
            if not sid:
                raise WdaError(f"WDA refused a session: {out}")
            self._session = sid
        return self._session

    def screenshot_png(self) -> bytes:
        value = self._call("GET", "/screenshot").get("value")
        if not value:
            raise WdaError("WDA returned an empty screenshot")
        return base64.b64decode(value)

    def window_size(self) -> tuple[float, float]:
        v = self._call("GET", f"/session/{self.session}/window/size")["value"]
        return float(v["width"]), float(v["height"])

    def tap_sequence(self, points: list[tuple[float, float]],
                     hold_ms: int = 60, gap_ms: int = 80) -> None:
        """Tap each point in order (WDA points), in ONE W3C actions request, so
        a placement's two taps cost one round trip and cannot interleave."""
        actions = []
        for i, (x, y) in enumerate(points):
            if i:
                actions.append({"type": "pause", "duration": gap_ms})
            actions += [
                {"type": "pointerMove", "duration": 0, "x": round(x), "y": round(y)},
                {"type": "pointerDown", "button": 0},
                {"type": "pause", "duration": hold_ms},
                {"type": "pointerUp", "button": 0},
            ]
        self._call("POST", f"/session/{self.session}/actions", {"actions": [{
            "type": "pointer", "id": "finger1",
            "parameters": {"pointerType": "touch"}, "actions": actions}]})


Band = tuple[float, float, float, float]   # (sx, sy, ox, oy)


@dataclass(frozen=True)
class IosGeometry:
    """Where the 549x976 game frame's pixels sit in the phone's screenshot.

    Two bands, each an axis-aligned affine map from a frame pixel (u, v) to a
    screenshot pixel (ox + sx*u, oy + sy*v): the arena (frame rows above
    SPLIT) and the card tray (rows below). A tall iPhone scales the arena
    differently in x and y and moves the tray, so one rectangle cannot serve.
    """
    arena: Band
    tray: Band
    scale: float

    @classmethod
    def from_crop(cls, crop, scale: float, tray_dy: int = 0) -> "IosGeometry":
        x, y, w, h = crop
        sx, sy = w / GAME_SIZE[0], h / GAME_SIZE[1]
        return cls((sx, sy, x, y), (sx, sy, x, y + tray_dy), scale)

    @classmethod
    def load(cls, path: Path) -> "IosGeometry":
        blob = json.loads(Path(path).read_text())
        if "arena" in blob:
            return cls(tuple(blob["arena"]), tuple(blob["tray"]), float(blob["scale"]))
        return cls.from_crop([int(v) for v in blob["crop"]], float(blob["scale"]),
                             int(blob.get("tray_dy", 0)))

    def to_json(self) -> dict:
        return {"arena": list(self.arena), "tray": list(self.tray), "scale": self.scale}

    @property
    def crop(self) -> tuple[int, int, int, int]:
        """Bounding box of the whole frame in the screenshot (for logs)."""
        sx, sy, ox, oy = self.arena
        tsx, tsy, tox, toy = self.tray
        bottom = toy + tsy * GAME_SIZE[1]
        return (int(ox), int(oy), int(sx * GAME_SIZE[0]), int(bottom - oy))

    def _band(self, fy: float) -> Band:
        return self.arena if fy < SPLIT else self.tray

    def maps(self, shape: tuple[int, int]) -> tuple[np.ndarray, np.ndarray]:
        cached = getattr(self, "_maps", None)
        if cached is not None and cached[0] == shape:
            return cached[1]
        W, H = GAME_SIZE
        u = np.arange(W, dtype=np.float32)
        v = np.arange(H, dtype=np.float32)
        cut = int(round(SPLIT * H))
        mx = np.empty((H, W), np.float32)
        my = np.empty((H, W), np.float32)
        for rows, (sx, sy, ox, oy) in ((slice(0, cut), self.arena),
                                       (slice(cut, H), self.tray)):
            mx[rows] = ox + sx * u[None, :]
            my[rows] = (oy + sy * v[rows])[:, None]
        h, w = shape
        inside = (mx >= -0.5) & (mx <= w - 0.5) & (my >= -0.5) & (my <= h - 0.5)
        # Mostly off-screen means the geometry is for another screen size.
        if inside.mean() < 0.85:
            raise WdaError(f"geometry maps only {inside.mean():.0%} of the frame onto "
                           f"the {w}x{h} screenshot; recalibrate")
        object.__setattr__(self, "_maps", (shape, (mx, my)))
        return mx, my

    def frame(self, screenshot: np.ndarray) -> np.ndarray:
        mx, my = self.maps(screenshot.shape[:2])
        return cv2.remap(screenshot, mx, my, cv2.INTER_LINEAR,
                         borderMode=cv2.BORDER_REPLICATE)

    def to_points(self, fx: float, fy: float) -> tuple[float, float]:
        """A position as fractions (0..1) of the game frame -> screen points."""
        sx, sy, ox, oy = self._band(fy)
        return ((ox + sx * fx * GAME_SIZE[0]) / self.scale,
                (oy + sy * fy * GAME_SIZE[1]) / self.scale)


class WdaSource(FrameSource):
    """One WDA screenshot per read. The stamp is taken before the request."""

    def __init__(self, client: WdaClient, geometry: IosGeometry):
        self._client = client
        self._geo = geometry
        self._t0 = time.perf_counter()
        self._index = 0
        self._stamps: list[float] = []
        self._size = self._grab()[1].shape[1::-1]

    def _grab(self) -> tuple[float, np.ndarray]:
        stamp = time.perf_counter()
        png = self._client.screenshot_png()
        image = cv2.imdecode(np.frombuffer(png, np.uint8), cv2.IMREAD_COLOR)
        if image is None:
            raise WdaError(f"undecodable screenshot ({len(png)} bytes)")
        self._stamps = (self._stamps + [stamp])[-120:]
        return stamp, self._geo.frame(image)

    @property
    def fps(self) -> float:
        if len(self._stamps) < 2:
            return 0.0
        return (len(self._stamps) - 1) / (self._stamps[-1] - self._stamps[0])

    @property
    def size(self) -> tuple[int, int]:
        return self._size

    @property
    def frame_count(self) -> int:
        return -1

    def __iter__(self) -> Iterator[Frame]:
        while True:
            yield self.read()

    def read(self) -> Frame:
        stamp, image = self._grab()
        frame = Frame(index=self._index,
                      wall_time_ms=(stamp - self._t0) * 1000.0, image=image)
        self._index += 1
        return frame

    def read_new(self, timeout_s: float = 1.0) -> Frame | None:
        return self.read()

    def close(self) -> None:
        pass
