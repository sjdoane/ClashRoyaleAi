"""Live frames from the device, via `adb exec-out screencap`.

The non-Windows path: Windows.Graphics.Capture does not exist on macOS. The
original measurement rejected adb at 1.4-4.9 s per frame on Windows BlueStacks;
BlueStacks Air on Apple Silicon returns a 1080x1920 PNG in ~0.4 s, inside the
1 Hz budget. A frame is resized to the 549x976 the live path's readers were
built against, the same 9:16 as the device.
"""
from __future__ import annotations

import subprocess
import time
from pathlib import Path
from typing import Iterator

import cv2
import numpy as np

from capture.source import Frame, FrameSource

GAME_SIZE = (549, 976)


class AdbSource(FrameSource):
    """One screencap per read, so every frame is a new observation. The stamp is
    taken before the capture: the board is at least that old when it arrives.
    """

    def __init__(self, adb: Path, serial: str | None = None):
        self._cmd = [str(adb)] + (["-s", serial] if serial else [])
        self._t0 = time.perf_counter()
        self._index = 0
        self._stamps: list[float] = []
        self._size = self._grab()[1].shape[1::-1]

    def _grab(self) -> tuple[float, np.ndarray]:
        stamp = time.perf_counter()
        done = subprocess.run(self._cmd + ["exec-out", "screencap", "-p"],
                              capture_output=True, timeout=15)
        if done.returncode != 0:
            raise RuntimeError(f"adb screencap failed ({done.returncode}): "
                               f"{done.stderr.decode(errors='replace').strip()}")
        image = cv2.imdecode(np.frombuffer(done.stdout, np.uint8), cv2.IMREAD_COLOR)
        if image is None:
            raise RuntimeError(f"adb screencap returned {len(done.stdout)} "
                               "undecodable bytes")
        self._stamps = (self._stamps + [stamp])[-120:]
        return stamp, cv2.resize(image, GAME_SIZE, interpolation=cv2.INTER_AREA)

    @property
    def fps(self) -> float:
        """Achieved rate; every Frame carries its own capture time."""
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
