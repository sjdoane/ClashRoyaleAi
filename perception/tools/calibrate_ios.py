"""Fit the crop that turns an iPhone screenshot into the live path's 9:16 frame.

    python tools/calibrate_ios.py --mirror --out ../../ios/geometry.json   # iPhone Mirroring
    python tools/calibrate_ios.py --out ../../ios/geometry.json            # live, via WDA
    python tools/calibrate_ios.py --png shot.png --scale 3 --out geo.json  # from a file

Take it during a battle (Training Camp is fine), towers standing, hand
visible. The reference is a BlueStacks 549x976 battle frame
(tests/assets/tower_numerals_2030_549x976.jpg). Its arena band and its card
tray band are each matched separately against the screenshot over a sweep of
scales, on gradient magnitude, so the arena skin and the cards in hand matter
less than the layout.

Each band gets its own x and y scale and origin: a tall phone stretches the
arena more vertically than the 9:16 reference and puts the tray elsewhere.
capture/wda.IosGeometry rebuilds the 9:16 frame from the two, taps included.

Writes both bands, the scale (screenshot pixels per WDA point) and an overlay PNG
next to the JSON, to check by eye.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import cv2
import numpy as np

_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_ROOT))

from capture.wda import GAME_SIZE, SPLIT, IosGeometry, WdaClient  # noqa: E402

REFERENCE = _ROOT / "tests" / "assets" / "tower_numerals_2030_549x976.jpg"
# Rows of the 549x976 reference: the arena below the name/clock strip, and the
# card tray. Generous margins are left out so units in play matter less.
ARENA_ROWS = (70, 780)
TRAY_ROWS = (800, 976)
# The arena's outer columns (scenery) may run off a narrow screen; match the
# middle, which holds the towers, bridges and lanes.
ARENA_COLS = (60, 489)
SPLIT_ROW = int(round(SPLIT * GAME_SIZE[1]))
MIN_SCORE = 0.3


def gradient(gray: np.ndarray) -> np.ndarray:
    gx = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
    return cv2.GaussianBlur(cv2.magnitude(gx, gy), (5, 5), 0)


def best_fit(shot_g: np.ndarray, ref_g: np.ndarray, rows: tuple[int, int],
             cols: tuple[int, int], sxs, sys_) -> tuple[float, float, float, float, float]:
    """(score, sx, sy, ox, oy): the best placement of the reference patch
    rows x cols, as a frame-to-screenshot affine (x = ox + sx*u, y = oy + sy*v)."""
    patch = ref_g[rows[0]:rows[1], cols[0]:cols[1]]
    best = (-1.0, 0.0, 0.0, 0.0, 0.0)
    for sx in sxs:
        for sy in sys_:
            tw = int(round((cols[1] - cols[0]) * sx))
            th = int(round((rows[1] - rows[0]) * sy))
            if th > shot_g.shape[0] or tw > shot_g.shape[1] or tw < 8 or th < 8:
                continue
            templ = cv2.resize(patch, (tw, th), interpolation=cv2.INTER_AREA)
            res = cv2.matchTemplate(shot_g, templ, cv2.TM_CCOEFF_NORMED)
            _, score, _, (x, y) = cv2.minMaxLoc(res)
            if score > best[0]:
                best = (float(score), float(sx), float(sy),
                        x - cols[0] * sx, y - rows[0] * sy)
    return best


def fit_band(shot_g, ref_g, rows, cols, lo, hi) -> tuple[float, float, float, float, float]:
    """Coarse search at half resolution, then a fine one at full."""
    half = cv2.resize(shot_g, None, fx=0.5, fy=0.5, interpolation=cv2.INTER_AREA)
    grid = np.arange(lo, hi, 0.04)
    _, sx, sy, _, _ = best_fit(half, ref_g, rows, cols, grid / 2, grid / 2)
    sx, sy = 2 * sx, 2 * sy
    return best_fit(shot_g, ref_g, rows, cols,
                    np.arange(sx - 0.04, sx + 0.041, 0.008),
                    np.arange(sy - 0.04, sy + 0.041, 0.008))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--png", type=Path, help="screenshot file instead of WDA")
    ap.add_argument("--scale", type=float, default=None,
                    help="screenshot pixels per point (iPhone: 3, some: 2); "
                         "read from WDA when grabbing live")
    ap.add_argument("--mirror", action="store_true",
                    help="capture the macOS iPhone Mirroring window instead of WDA")
    ap.add_argument("--wda-url", default="http://127.0.0.1:8100")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    if args.png:
        shot = cv2.imread(str(args.png))
        if shot is None:
            raise SystemExit(f"cannot read {args.png}")
        if args.scale is None:
            raise SystemExit("--scale is required with --png")
        scale = args.scale
    elif args.mirror:
        from capture.mirror import MirrorWindow
        win = MirrorWindow()
        shot = win.capture()
        if float(shot.std()) < 2.0:
            raise SystemExit("blank capture: grant this terminal Screen Recording "
                             "permission and restart it")
        scale = args.scale or shot.shape[1] / win.bounds[2]
        args.out.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(args.out.with_suffix(".screenshot.png")), shot)
    else:
        client = WdaClient(args.wda_url)
        png = client.screenshot_png()
        shot = cv2.imdecode(np.frombuffer(png, np.uint8), cv2.IMREAD_COLOR)
        win_w, _ = client.window_size()
        scale = args.scale or shot.shape[1] / win_w
        args.out.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(args.out.with_suffix(".screenshot.png")), shot)

    H, W = shot.shape[:2]
    print(f"screenshot {W}x{H}, {scale:g} px/pt")
    ref = cv2.imread(str(REFERENCE), cv2.IMREAD_GRAYSCALE)
    shot_g = gradient(cv2.cvtColor(shot, cv2.COLOR_BGR2GRAY))
    ref_g = gradient(ref)

    # Frame px -> screenshot px per band. The scale range allows a frame from
    # 0.6x to 1.6x the screenshot width, x and y independently (tall phones
    # stretch the arena more vertically than the 9:16 reference).
    lo, hi = 0.6 * W / GAME_SIZE[0], 1.6 * W / GAME_SIZE[0]
    arena = fit_band(shot_g, ref_g, ARENA_ROWS, ARENA_COLS, lo, hi)
    tray = fit_band(shot_g, ref_g, TRAY_ROWS, (0, GAME_SIZE[0]), lo, hi)
    for name, (score, sx, sy, ox, oy) in (("arena", arena), ("tray", tray)):
        print(f"{name:5s} band: score {score:.3f}, scale x {sx:.3f} y {sy:.3f}, "
              f"origin ({ox:.0f}, {oy:.0f})")

    geo = IosGeometry(arena[1:], tray[1:], scale)
    overlay = shot.copy()
    for (score, sx, sy, ox, oy), rows, colour in ((arena, (0, SPLIT_ROW), (0, 0, 255)),
                                                 (tray, (SPLIT_ROW, GAME_SIZE[1]), (255, 0, 0))):
        cv2.rectangle(overlay, (int(ox), int(oy + rows[0] * sy)),
                      (int(ox + GAME_SIZE[0] * sx), int(oy + rows[1] * sy)), colour, 4)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    ov_path = args.out.with_suffix(".overlay.png")
    cv2.imwrite(str(ov_path), overlay)
    print(f"overlay: {ov_path}  (red = arena, blue = tray)")

    if arena[0] < MIN_SCORE or tray[0] < MIN_SCORE:
        print(f"match is weak (arena {arena[0]:.2f}, tray {tray[0]:.2f}); take the "
              "screenshot mid-battle with all towers standing and the hand visible")
        return 1
    if tray[4] + SPLIT_ROW * tray[2] < arena[4] + SPLIT_ROW * arena[2] - 20:
        print("the tray fit lies above the arena; send the overlay")
        return 1
    try:
        frame = geo.frame(shot)
    except Exception as exc:                                # noqa: BLE001
        print(f"fitted frame does not fit the screen: {exc}")
        return 1
    cv2.imwrite(str(args.out.with_suffix(".frame.png")), frame)
    args.out.write_text(json.dumps({**geo.to_json(), "screenshot": [W, H]}, indent=2) + "\n")
    print(f"wrote {args.out}; check {args.out.with_suffix('.frame.png')} "
          "looks like a normal battle")
    return 0

if __name__ == "__main__":
    sys.exit(main())
