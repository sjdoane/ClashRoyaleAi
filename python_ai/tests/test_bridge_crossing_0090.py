"""River crossing against real frames: patch 0090 (switches `jumpedge`, `walklegs`, `hogspeed`).

MEASURED 2026-11-26 on recorded live matches (r5 detector tracks, runs/bridge_1126,
266 own Hog crossings from the corner tiles, sprite bias removed):

* A Hog played on a back corner, engine (1,15) or (16,15), jumps on a straight line
  to x 2.2 / 14.8 (0.8 tile outside the lane column 3.0 / 14.0, i.e. near the
  bridge's outer edge), keeps that x straight ahead to real row 18 (engine 20), and
  only then curves in.  gen2_j lands it at 2.57 / 14.43 (0.35-0.42 tile inward).
* A Hog played beside the bridge, engine (2,15) / (15,15), does not step onto the
  column before the bank as the 0087 `jumpcol` rule makes it do.
* Its timing was already right (+0.05..-0.10 s on rows 17-21), so the fix must not
  change it.
* Walkers lost the rest of their step at every waypoint (about 4 % slow over the
  river); `walklegs` lets them spend it on the next leg like jumpers do.
* The Hog runs 2.34 tiles/s on open ground past the river (174 clean tracks, real
  rows 18.6-21.6, median along the path 2.340, 95 % interval 2.325-2.352), not
  S/50 = 2.40; Ice Golem, Skeletons and Knight match S/50 on the same rows, so it
  is the card, not the screen mapping.  `hogspeed` sets its land speed to 2.34 and
  leaves its speed over the water alone (the crossing above stays as it is).

The switches are read once per process from CR_P0088, so every case runs in its
own subprocess with the environment it needs.  Run from the build folder:

    cd build/gen2_k/engine && nice -n 10 ../../../.venv/bin/python -m pytest \
        python_ai/tests/test_bridge_crossing_0090.py -q

On gen2_j the 0090 cases fail (the switches do not exist there); to see it, copy this file
out of the build tree and run it with CR_ENGINE_UNDER_TEST=<abs path>/build/gen2_j/engine.
"""
import json
import os
import subprocess
import sys

import pytest

# CR_ENGINE_UNDER_TEST points the subprocesses at another build (to show the cases fail on gen2_j).
ENGINE = os.environ.get("CR_ENGINE_UNDER_TEST") or \
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASE = "tornado,jump,log,turret,king,speed,bankrows,stomp,jumpcol,pocketcells,kingpath"
ON = BASE + ",jumpedge,walklegs"
HOGSPEED = ON + ",hogspeed"
HOG, ICE_GOLEM, KNIGHT, LUMBERJACK, ROYAL_HOGS = 15, 40, 0, 21, 82
DECK = [25, 15, 0, 1, 2, 3, 4, 5]
HZ = 10


def _probe(specs):
    """Child process: one unit alone on an empty board per spec, its path in the attacker frame."""
    sys.path.insert(0, ENGINE)
    import python_ai  # noqa: F401
    import clash_royale_env as E
    out = {"active": E.p0088_active() if hasattr(E, "p0088_active") else {}, "runs": []}
    for card, x, y, team, secs in specs:
        env = E.ClashRoyaleEnv(list(DECK), list(DECK), 3600)
        env.seed(1)
        env.step_self_play(4, 0.0, 0.0, 4, 0.0, 0.0, 30)
        before = {u["id"] for u in env.get_units()}
        env.inject(int(card), float(x), float(y), int(team), -1.0, -1)
        uid, path = None, []
        for k in range(int(secs * HZ)):
            env.step_self_play(4, 0.0, 0.0, 4, 0.0, 0.0, 1)
            U = {u["id"]: u for u in env.get_units()}
            if uid is None:
                new = [u for u in U.values() if u["id"] not in before and not u["building"]
                       and u["team"] == team]
                if not new:
                    continue
                uid = min(new, key=lambda u: (u["x"] - x) ** 2 + (u["y"] - y) ** 2)["id"]
            u = U.get(uid)
            if u is None:
                break
            ux, uy = (u["x"], u["y"]) if team == 0 else (17.0 - u["x"], 33.0 - u["y"])
            path.append(((k + 1) / HZ, ux, uy))
        out["runs"].append(path)
    return out


_CACHE = {}


def run(switches, specs):
    key = (switches, json.dumps(specs))
    if key not in _CACHE:
        env = dict(os.environ, CR_P0088=switches, CR_POCKET_TABLE="verified", PYTHONDONTWRITEBYTECODE="1")
        r = subprocess.run([sys.executable, os.path.abspath(__file__), json.dumps(specs)], env=env,
                           capture_output=True, text=True, check=True)
        _CACHE[key] = json.loads(r.stdout.strip().splitlines()[-1])
    return _CACHE[key]


def x_at_y(path, y):
    """x where the path first reaches engine row y (attacker frame), linear interpolation."""
    for (ta, xa, ya), (tb, xb, yb) in zip(path, path[1:]):
        if ya < y <= yb:
            f = (y - ya) / (yb - ya)
            return xa + f * (xb - xa)
    raise AssertionError(f"path never reaches y {y}")


def t_at_y(path, y):
    for (ta, xa, ya), (tb, xb, yb) in zip(path, path[1:]):
        if ya < y <= yb:
            return ta + (y - ya) / (yb - ya) * (tb - ta)
    raise AssertionError(f"path never reaches y {y}")


def real_row(y):
    return y - min(max((y - 13) / 2, 0), 1) - min(max((y - 18) / 2, 0), 1)


def anchored_t(path, y):
    """seconds from the first reach of deploy row + 0.3 (real rows) to engine row y: the
    anchor used for the real tracks."""
    y0 = path[0][2]
    r0 = real_row(y0) + 0.3
    pts = [(t, real_row(yy)) for t, _, yy in path]
    for (ta, ra), (tb, rb) in zip(pts, pts[1:]):
        if ra < r0 <= rb:
            t0 = ta + (r0 - ra) / (rb - ra) * (tb - ta)
            return t_at_y(path, y) - t0
    raise AssertionError("never left the deploy row")


# attacker-frame start -> (team, injected tile)
CORNERS = {"left_team0": (0, 1, 15), "right_team0": (0, 16, 15),
           "left_team1": (1, 16, 18), "right_team1": (1, 1, 18)}


def corner_specs():
    return [[HOG, x, y, team, 4.0] for team, x, y in CORNERS.values()]


def test_switches_are_exact_names():
    a = run(BASE + ",all", [])["active"]
    assert a.get("jumpedge") is False and a.get("walklegs") is False
    b = run(ON, [])["active"]
    assert b.get("jumpedge") is True and b.get("walklegs") is True


@pytest.mark.parametrize("i,name", list(enumerate(CORNERS)))
def test_corner_hog_lands_on_the_bridge_edge_and_runs_straight(i, name):
    path = run(ON, corner_specs())["runs"][i]
    want = 2.2 if name.startswith("left") else 14.8
    for y in (17.5, 18.0, 19.0, 20.0):     # real rows 16.5, 17.0, 17.5, 18.0
        assert abs(x_at_y(path, y) - want) <= 0.1, (name, y, x_at_y(path, y))
    # then it curves in toward the tower (real: 2.48 / 14.52 at engine row 22)
    lane = 3.0 if want < 8.5 else 14.0
    assert abs(x_at_y(path, 22.0) - lane) < abs(want - lane) - 0.1, x_at_y(path, 22.0)
    assert abs(x_at_y(path, 22.0) - (2.48 if want < 8.5 else 14.52)) <= 0.15, x_at_y(path, 22.0)


@pytest.mark.parametrize("i,name", list(enumerate(CORNERS)))
def test_corner_hog_timing_is_unchanged(i, name):
    on = run(ON, corner_specs())["runs"][i]
    off = run(BASE, corner_specs())["runs"][i]
    for y in (18.0, 20.0, 21.0):
        assert abs(anchored_t(on, y) - anchored_t(off, y)) <= 0.06, (name, y)
    # real (median of 266 tracks): 1.57 s from the anchor to real row 18 (engine 20)
    assert abs(anchored_t(on, 20.0) - 1.57) <= 0.12, anchored_t(on, 20.0)


@pytest.mark.parametrize("team,x,y,side", [(0, 2, 15, "left"), (0, 15, 15, "right"),
                                           (1, 15, 18, "left"), (1, 2, 18, "right")])
def test_hog_beside_the_bridge_does_not_step_onto_the_column(team, x, y, side):
    path = run(ON, [[HOG, x, y, team, 4.0]])["runs"][0]
    xb = x_at_y(path, 15.5)       # the near bank
    if side == "left":
        assert xb <= 2.6, xb
    else:
        assert xb >= 14.4, xb


def test_walker_keeps_its_speed_over_the_river():
    """Ice Golem on the column, bank to bank (engine 15.5 -> 18.0, 2.5 tiles)."""
    specs = [[ICE_GOLEM, 3, 13, 0, 9.0], [ICE_GOLEM, 3, 9, 0, 9.0]]
    on = run(ON, specs)["runs"]
    off = run(BASE, specs)["runs"]
    open_on = (t_at_y(on[1], 13.0) - t_at_y(on[1], 10.5)) / 2.5        # open ground, s per tile
    river_on = (t_at_y(on[0], 18.0) - t_at_y(on[0], 15.5)) / 2.5
    river_off = (t_at_y(off[0], 18.0) - t_at_y(off[0], 15.5)) / 2.5
    assert river_on <= open_on * 1.02, (river_on, open_on)
    assert river_on < river_off, (river_on, river_off)


def test_switches_off_keep_the_gen2_j_line():
    """Regression guard: without the switches the corner Hog lands where gen2_j put it."""
    runs = run(BASE, corner_specs())["runs"]
    for path, want in zip(runs, (2.526, 14.474, 2.526, 14.474)):
        assert abs(x_at_y(path, 18.0) - want) <= 0.02, x_at_y(path, 18.0)


def chord_speed(path, y0, y1):
    """tiles/s on the straight line between the first reaches of engine rows y0 and y1."""
    xa, xb = x_at_y(path, y0), x_at_y(path, y1)
    return ((y1 - y0) ** 2 + (xb - xa) ** 2) ** 0.5 / (t_at_y(path, y1) - t_at_y(path, y0))


def test_hogspeed_is_an_exact_name():
    a = run(ON + ",all", [])["active"]
    assert a.get("hogspeed") is False
    b = run(HOGSPEED, [])["active"]
    assert b.get("hogspeed") is True and abs(b.get("hog_land_scale") - 0.975) < 1e-4


# attacker frame: past the river (engine 20.6 -> 23.4 = real 18.6 -> 21.4) and on its own half
LAND = {"far_team0": (0, 3, 20.5, 20.6, 23.4), "far_team1": (1, 14, 12.5, 20.6, 23.4),
        "own_team0": (0, 3, 8, 8.6, 12.4), "own_team1": (1, 14, 25, 8.6, 12.4)}


@pytest.mark.parametrize("name", list(LAND))
def test_hog_walks_at_the_measured_speed_on_land(name):
    team, x, y, y0, y1 = LAND[name]
    spec = [[HOG, x, y, team, 5.0]]
    v_on = chord_speed(run(HOGSPEED, spec)["runs"][0], y0, y1)
    v_off = chord_speed(run(ON, spec)["runs"][0], y0, y1)
    assert abs(v_on - 2.34) <= 0.015, (name, v_on)
    assert abs(v_off - 2.40) <= 0.015, (name, v_off)


def test_hog_jump_and_crossing_are_unchanged():
    """Over the water the Hog keeps its speed: the transit time (engine 15.5 -> 18.0, clear
    of the boosted tick that leaves the bank rows) and the corner Hog's arrival on real
    rows 17-18 stay within a fraction of a tick of the 0090 line (real: 1.57 s to real row 18)."""
    specs = [[HOG, 3, 12, 0, 5.0]] + [[HOG, x, y, team, 6.0] for team, x, y in CORNERS.values()]
    on, h = run(ON, specs)["runs"], run(HOGSPEED, specs)["runs"]
    tr_on = t_at_y(on[0], 18.0) - t_at_y(on[0], 15.5)
    tr_h = t_at_y(h[0], 18.0) - t_at_y(h[0], 15.5)
    assert abs(tr_h - tr_on) <= 0.01, (tr_on, tr_h)   # the exit tick finishes its step on land
    for a, b in zip(on[1:], h[1:]):
        for y in (18.0, 19.0, 20.0):          # real rows 17, 17.5, 18
            assert 0.0 <= anchored_t(b, y) - anchored_t(a, y) <= 0.03, (y, anchored_t(a, y), anchored_t(b, y))
        assert abs(anchored_t(b, 20.0) - 1.57) <= 0.12, anchored_t(b, 20.0)
        # the far half costs 2.5 % more: engine 20 -> 24 (4 tiles, 1.67 s at 2.40)
        d = (anchored_t(b, 24.0) - anchored_t(b, 20.0)) - (anchored_t(a, 24.0) - anchored_t(a, 20.0))
        assert 0.02 <= d <= 0.07, d


def test_hogspeed_leaves_other_units_alone():
    specs = [[KNIGHT, 3, 5, 0, 8.0], [LUMBERJACK, 14, 5, 0, 6.0], [ROYAL_HOGS, 3, 10, 0, 6.0],
             [ICE_GOLEM, 3, 9, 0, 9.0]]
    assert run(ON, specs)["runs"] == run(HOGSPEED, specs)["runs"]


if __name__ == "__main__":
    print(json.dumps(_probe(json.loads(sys.argv[1]))))
