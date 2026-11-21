"""Taps on an iPhone through WebDriverAgent, with AdbActuator's interface.

Also drives iPhone Mirroring: capture/mirror.MirrorClient has the same
`session` / `tap_sequence` surface, in window-relative screen points.

Taps are computed exactly as for Android (card_centre / engine_tile_centre, in
CRBAB's 720x1280 display space), then mapped through the same crop rectangle
the frame source uses (capture/wda.IosGeometry), so a tap lands on the pixel
the detector saw. Off the caller's thread, depth-one queue, drop-not-block:
the same policy as AdbActuator, for the same reasons.
"""
from __future__ import annotations

import queue
import threading

from clashroyalebuildabot.constants import DISPLAY_HEIGHT, DISPLAY_WIDTH

from capture.wda import IosGeometry, WdaClient
from live.actuator import (
    TAP_GAP_S,
    TOUCH_HOLD_S,
    Tap,
    card_centre,
    engine_tile_centre,
    tile_centre,
)


class WdaActuator:
    def __init__(self, client: WdaClient, geometry: IosGeometry,
                 dry_run: bool = True):
        self.dry_run = dry_run
        self.client = client
        self.backend = getattr(client, "backend", "wda-xcuitest")
        self.geo = geometry
        self.taps: list[Tap] = []
        self.dropped = 0
        self.errors = 0
        self.last_error: BaseException | None = None
        self._q: queue.Queue = queue.Queue(maxsize=1)
        self._stop = threading.Event()
        self._worker: threading.Thread | None = None
        if not dry_run:
            self.client.session  # fail now, not on the first placement
            self._worker = threading.Thread(target=self._run, name="actuator",
                                            daemon=True)
            self._worker.start()

    backend = "wda-xcuitest"

    def points(self, tap: Tap) -> tuple[float, float]:
        return self.geo.to_points(tap.x / DISPLAY_WIDTH, tap.y / DISPLAY_HEIGHT)

    def _enqueue(self, card: Tap, target: Tap) -> tuple[Tap, Tap]:
        self.taps.extend((card, target))
        if not self.dry_run:
            try:
                self._q.put_nowait((card, target))
            except queue.Full:
                self.dropped += 1
        return card, target

    def play(self, slot: int, tile_x: int, tile_y: int, *,
             engine_frame: bool = True) -> tuple[Tap, Tap]:
        target = (engine_tile_centre(tile_x, tile_y) if engine_frame
                  else tile_centre(tile_x, tile_y))
        return self._enqueue(card_centre(slot), target)

    def play_pixel(self, slot: int, px: int, py: int) -> tuple[Tap, Tap]:
        return self._enqueue(card_centre(slot), Tap(int(px), int(py)))

    def flush(self) -> None:
        if self._worker is not None:
            self._q.join()

    def close(self, timeout: float = 5.0) -> None:
        if self._worker is None:
            return
        self._q.join()
        self._stop.set()
        self._worker.join(timeout=timeout)

    @property
    def pending(self) -> int:
        return self._q.qsize()

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                card, target = self._q.get(timeout=0.2)
            except queue.Empty:
                continue
            try:
                self.client.tap_sequence(
                    [self.points(card), self.points(target)],
                    hold_ms=int(TOUCH_HOLD_S * 1000), gap_ms=int(TAP_GAP_S * 1000))
            except Exception as exc:                        # noqa: BLE001
                self.errors += 1
                self.last_error = exc
            finally:
                self._q.task_done()
