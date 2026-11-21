"""Live frames and taps through macOS iPhone Mirroring (macOS 15+, iOS 18+).

The iPhone shows up as a window on the Mac. Frames are captures of that one
window (CGWindowListCreateImage by window id, so other windows overlapping it
do not matter), and taps are mouse clicks posted at the HID level, which
iPhone Mirroring turns into touches. No Xcode and no signing are needed; the
process running this needs two macOS permissions, in System Settings >
Privacy & Security: Screen Recording (frames) and Accessibility (clicks).

The click is move, down, a one-pixel drag, then up: the same sequence
jev-royal (github.com/Amrit-Nigam/jev-royal) found registers in Clash Royale
through Mirroring.

The game frame inside the window is the same crop as on WDA
(capture/wda.IosGeometry, from tools/calibrate_ios.py --mirror), with
`scale` = window-capture pixels per screen point. Taps map through it and are
offset by the window's on-screen origin, read fresh at every placement, so
moving the window mid-match is harmless.
"""
from __future__ import annotations

import time
from typing import Iterator

import numpy as np

from capture.source import Frame, FrameSource
from capture.wda import IosGeometry

import os

# Overridable only to test against another window; play uses the real app.
OWNER = os.environ.get("CR_MIRROR_OWNER", "iPhone Mirroring")


class MirrorError(RuntimeError):
    pass


def _quartz():
    try:
        import Quartz
    except ImportError as exc:                              # pragma: no cover
        raise MirrorError("pyobjc missing: uv pip install pyobjc-framework-Quartz") from exc
    return Quartz


class MirrorWindow:
    """The iPhone Mirroring window: find it, capture it, click inside it."""

    def __init__(self, owner: str | None = None):
        self.Q = _quartz()
        self.owner = owner or OWNER
        self.wid, self.bounds = self._find()

    def _find(self) -> tuple[int, tuple[float, float, float, float]]:
        Q = self.Q
        # All windows, not only on-screen ones: the window still captures while
        # it sits on another Space (e.g. you switched to the terminal).
        wins = Q.CGWindowListCopyWindowInfo(Q.kCGWindowListOptionAll,
                                            Q.kCGNullWindowID) or []
        best = None
        for w in wins:
            if w.get("kCGWindowOwnerName") != self.owner or w.get("kCGWindowLayer", 0):
                continue
            b = w["kCGWindowBounds"]
            rect = (float(b["X"]), float(b["Y"]), float(b["Width"]), float(b["Height"]))
            # The phone window is portrait; the app also owns small helper windows.
            if rect[3] < 300 or rect[3] < 1.5 * rect[2]:
                continue
            key = (bool(w.get("kCGWindowIsOnscreen")), rect[2] * rect[3])
            if best is None or key > best[0]:
                best = (key, int(w["kCGWindowNumber"]), rect)
        if best is not None:
            best = best[1:]
        if best is None:
            raise MirrorError("no iPhone Mirroring window on screen. Open the iPhone "
                              "Mirroring app, connect to the phone and leave the window "
                              "visible (not minimised)")
        return best

    def refresh(self) -> tuple[float, float, float, float]:
        self.wid, self.bounds = self._find()
        return self.bounds

    def capture(self) -> np.ndarray:
        """BGR image of the window's content (Retina pixels)."""
        Q = self.Q
        img = None
        for attempt in range(5):                             # transient None happens
            img = Q.CGWindowListCreateImage(
                Q.CGRectNull, Q.kCGWindowListOptionIncludingWindow, self.wid,
                Q.kCGWindowImageBoundsIgnoreFraming)
            if img is not None:
                break
            time.sleep(0.05 * (attempt + 1))
            self.refresh()
        if img is None:
            raise MirrorError("window capture failed (window closed?)")
        w, h = Q.CGImageGetWidth(img), Q.CGImageGetHeight(img)
        stride = Q.CGImageGetBytesPerRow(img)
        data = Q.CGDataProviderCopyData(Q.CGImageGetDataProvider(img))
        arr = np.frombuffer(data, np.uint8).reshape(h, stride // 4, 4)[:, :w, :3]
        return np.ascontiguousarray(arr)                     # BGRA -> BGR

    def scale(self) -> float:
        return self.capture().shape[1] / self.bounds[2]

    @staticmethod
    def permissions() -> dict[str, bool]:
        Q = _quartz()
        out = {"screen_recording": bool(Q.CGPreflightScreenCaptureAccess())}
        try:
            from ApplicationServices import AXIsProcessTrusted
            out["accessibility"] = bool(AXIsProcessTrusted())
        except ImportError:
            out["accessibility"] = True                      # cannot tell; try anyway
        return out

    def activate(self) -> None:
        try:
            from AppKit import NSRunningApplication, NSWorkspace
        except ImportError:
            return
        front = NSWorkspace.sharedWorkspace().frontmostApplication()
        if front is not None and front.localizedName() == self.owner:
            return
        for app in NSWorkspace.sharedWorkspace().runningApplications():
            if app.localizedName() == self.owner:
                app.activateWithOptions_(1 << 1)             # ignoring other apps
                time.sleep(0.25)                             # focus settles before input
                return

    def _click(self, x: float, y: float, hold_ms: int) -> None:
        Q = self.Q
        src = Q.CGEventSourceCreate(Q.kCGEventSourceStateHIDSystemState)
        p, nudge = (x, y), (x + 1, y)
        for kind, at, wait in ((Q.kCGEventMouseMoved, p, 0.02),
                               (Q.kCGEventLeftMouseDown, p, hold_ms / 2000),
                               (Q.kCGEventLeftMouseDragged, nudge, hold_ms / 2000),
                               (Q.kCGEventLeftMouseUp, p, 0.0)):
            ev = Q.CGEventCreateMouseEvent(src, kind, at, Q.kCGMouseButtonLeft)
            Q.CGEventPost(Q.kCGHIDEventTap, ev)
            if wait:
                time.sleep(wait)


class MirrorClient:
    """WdaClient's tap interface over a MirrorWindow, for live/wda_actuator."""

    backend = "iphone-mirroring"

    def __init__(self, window: MirrorWindow):
        self.window = window

    @property
    def session(self) -> str:
        perms = MirrorWindow.permissions()
        if not perms["accessibility"]:
            raise MirrorError("this terminal lacks Accessibility permission, so clicks "
                              "are dropped: System Settings > Privacy & Security > "
                              "Accessibility, add the terminal app, then restart it")
        self.window.activate()
        return "mirror"

    def tap_sequence(self, points: list[tuple[float, float]],
                     hold_ms: int = 60, gap_ms: int = 80) -> None:
        """Click each point (window-relative screen points) in order."""
        self.window.activate()
        ox, oy, _, _ = self.window.refresh()
        for i, (x, y) in enumerate(points):
            if i:
                time.sleep(gap_ms / 1000)
            self.window._click(ox + x, oy + y, hold_ms)


class MirrorSource(FrameSource):
    """One window capture per read, cropped to the game frame."""

    def __init__(self, window: MirrorWindow, geometry: IosGeometry):
        self._win = window
        self._geo = geometry
        self._t0 = time.perf_counter()
        self._index = 0
        self._stamps: list[float] = []
        first = self._grab()[1]
        if float(first.std()) < 2.0:
            raise MirrorError("the captured frame is blank: grant this terminal Screen "
                              "Recording permission (System Settings > Privacy & "
                              "Security > Screen Recording), then restart it")
        self._size = first.shape[1::-1]

    def _grab(self) -> tuple[float, np.ndarray]:
        stamp = time.perf_counter()
        image = self._win.capture()
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
