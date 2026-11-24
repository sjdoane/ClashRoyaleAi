"""Which building or tower a troop turns to (patch 0086).

Three rules, fitted to 84 decided pull events read frame by frame from the 19
live matches recorded on 2026-11-23 (before: 60 right, 6 wrong, 18 within
measurement error; with all three: 70 right, 0 wrong, 14 within error):

  * a Crown Tower is RANKED against other targets by its published body
    (1.0 Princess, 1.4 King), not the engine's 1.5 / 2.0. A building therefore
    pulls a building-targeter when its edge is nearer than the tower's
    published edge: a Cannon whose centre is more than 0.4 nearer than the
    tower's, where it used to need 0.9. The fit accepts 0.4 to 1.1 and
    excludes 1.5; 1.0 is the published value inside that band.
  * the published SightClip no longer cuts the sight box ALONG the arena. With
    the cut a Hog Rider leaving the bridge could not see the Princess Tower it
    was running at (9 tiles ahead) but could see a building in the middle of
    the arena, and turned to it. Recorded Hogs in that position ran straight
    to the tower (events m9-j, m12-n, m12-p; Ice Golem m9-i).
  * a Tesla is underground, and cannot be picked as a target, unless an enemy
    is inside its own sight (events m11-g, m11-l0). It is above ground for its
    whole deploy and for 0.8 s (published hide time) after the last enemy left.

The geometry below is in engine tiles; the enemy right Princess Tower is at
(14, 27), the right bridge at x 14.5.
"""
import python_ai  # noqa: F401
import clash_royale_env as E
import numpy as np

NOOP = 4
KNIGHT, HOG, CANNON, TESLA, ROYAL_GHOST = 0, 15, 25, 26, 47
OURS = [129, 168, 125, 15, 40, 72, 33, 7]


def _env():
    env = E.ClashRoyaleEnv(list(OURS), list(OURS), 3600)
    env.seed(1)
    env.reset()
    return env


def _step(env, n=1):
    for _ in range(n):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)


def _targets(troop_xy, building, building_xy, settle=0, building_deploy=0, ticks=160):
    """Team 1 has `building` at `building_xy` for `settle` ticks; then a team-0
    Hog Rider appears at `troop_xy`. -> the names of what the Hog targets, in
    order, without repeats."""
    env = _env()
    env.inject(building, building_xy[0], building_xy[1], 1, -1.0, building_deploy)
    _step(env, settle)
    env.inject(HOG, troop_xy[0], troop_xy[1], 0, -1.0, 0)
    seen = []
    for _ in range(ticks):
        _step(env)
        units = env.get_units()
        by_id = {u["id"]: u for u in units}
        hog = [u for u in units if u["team"] == 0 and u["card_id"] == HOG]
        if not hog:
            break
        target = by_id.get(hog[0]["target_id"])
        name = target["name"] if target else None
        if not seen or seen[-1] != name:
            seen.append(name)
    return seen


def _tesla(env):
    return [u for u in env.get_units() if u["name"] == "Tesla"][0]


# ---- a Crown Tower is ranked by its published body -------------------------

def test_a_cannon_0p6_nearer_than_the_tower_pulls_a_hog():
    # Hog at (14.5, 19): tower centre 8.02 away, Cannon at (9, 24) 7.43 away.
    # Edges: Cannon 6.83, tower 7.02 by its published body (6.52 by the
    # engine's 1.5, which sent the Hog to the tower).
    assert _targets((14.5, 19.0), CANNON, (9.0, 24.0)) == ["Cannon"]


def test_a_cannon_only_0p2_nearer_than_the_tower_does_not():
    # Cannon at (8.5, 24): 7.81 away, edge 7.21 against the tower's 7.02.
    assert _targets((14.5, 19.0), CANNON, (8.5, 24.0)) == ["Princess Tower"]


def test_a_cannon_further_than_the_tower_does_not():
    assert _targets((14.5, 19.0), CANNON, (9.0, 25.0)) == ["Princess Tower"]


# ---- no cut along the arena -------------------------------------------------

def test_a_hog_leaving_the_bridge_runs_at_the_tower_it_can_see():
    # Hog at the far end of the bridge (14.5, 18): tower 9.01 away, a Cannon
    # in the middle at (8, 24) 8.85 away, its edge further than the tower's.
    # With the along cut the tower was outside the sight box and the Hog went
    # to the Cannon.
    assert _targets((14.5, 18.0), CANNON, (8.0, 24.0)) == ["Princess Tower"]


def test_the_sideways_cut_is_kept():
    # Published SightClipSide: a Cannon 7 tiles to the side is inside the 9.5
    # sight disc and still does not pull (patch 0042).
    assert _targets((14.5, 19.0), CANNON, (7.0, 20.0)) == ["Princess Tower"]
    stats = E.get_card_stats(HOG)["base"]
    assert stats["sight_clip_side"] == 4.0


# ---- the Tesla --------------------------------------------------------------

def test_the_tesla_publishes_its_hide_time_and_nothing_else_hides():
    assert E.get_card_stats(TESLA)["base"]["hide_after_idle_ticks"] == 8
    assert E.get_card_stats(CANNON)["base"]["hide_after_idle_ticks"] == 0
    assert E.get_card_stats(ROYAL_GHOST)["base"]["hide_after_idle_ticks"] == 0


def test_a_tesla_is_up_while_it_deploys_and_goes_under_0p8_s_later():
    env = _env()
    env.inject(TESLA, 9.0, 22.0, 1)            # the card's own 1 s deploy
    up = []
    for _ in range(30):
        _step(env)
        up.append(_tesla(env)["targetable"])
    # ticks 1-10 deploying, 8 more ticks counted from the last deploy tick
    assert all(up[:17]) and not any(up[17:])


def test_a_tesla_comes_up_for_an_enemy_in_its_sight_and_hides_after_it():
    env = _env()
    env.inject(TESLA, 9.0, 22.0, 1, -1.0, 0)
    _step(env, 30)
    assert not _tesla(env)["targetable"]
    env.inject(KNIGHT, 9.0, 12.0, 0, 1.0, 0)   # 10 tiles off: out of its sight
    _step(env, 5)
    assert not _tesla(env)["targetable"]
    env.inject(KNIGHT, 9.0, 17.6, 0, 1.0, 0)   # 4.4 tiles: in sight, 1 hit point
    _step(env)
    assert _tesla(env)["targetable"]
    gone = None
    for tick in range(1, 20):
        _step(env)
        near = [u for u in env.get_units()
                if u["team"] == 0 and u["name"] == "Knight" and u["y"] > 16]
        if gone is None and not near:
            gone = tick                          # the Tesla shot it
        if gone is not None and tick < gone + 8:
            assert _tesla(env)["targetable"]
        if gone is not None and tick >= gone + 8:
            assert not _tesla(env)["targetable"]
    assert gone is not None


def test_an_underground_tesla_does_not_pull_a_hog_past_it():
    # Event m11-g: the Hog runs the lane past a Tesla that is underground. At
    # the bridge the Tesla at (9, 24) is nearer than the tower, but the Hog is
    # 7.8 tiles from it, outside its sight (5.5 + bodies = 6.6); by the time
    # the Tesla sees the Hog the tower is the nearer one.
    assert _targets((14.5, 18.0), TESLA, (9.0, 24.0), settle=40) == ["Princess Tower"]


def test_a_tesla_still_deploying_pulls_at_once():
    assert _targets((14.5, 18.0), TESLA, (9.0, 24.0), building_deploy=10)[0] == "Tesla"


def test_a_hog_turns_to_a_tesla_when_it_surfaces():
    # Events m11-l, m16-b: the Hog starts for the tower and turns as the Tesla
    # comes up for it.
    assert _targets((14.5, 18.0), TESLA, (9.0, 22.0), settle=40) == ["Princess Tower", "Tesla"]


def test_an_underground_tesla_is_still_in_the_observation():
    # It is drawn on the real screen (lower in its pit); only a cloaked unit is
    # left out of the opponent's observation.
    spatial = E.ClashRoyaleEnv.BOARD_WIDTH * E.ClashRoyaleEnv.BOARD_HEIGHT * 21
    with_tesla, empty = _env(), _env()
    with_tesla.inject(TESLA, 9.0, 22.0, 1, -1.0, 0)
    _step(with_tesla, 30)
    _step(empty, 30)
    assert not _tesla(with_tesla)["targetable"]
    a = np.array(with_tesla.get_observation_for_team(0))[:spatial]
    b = np.array(empty.get_observation_for_team(0))[:spatial]
    assert (a != b).any()
