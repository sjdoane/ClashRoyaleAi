"""get_units / set_tower_hit_log / get_tower_hits (patch 0039).

Read-only forensics for replay analysis and scenario tests: every body on the
board with its target, and every hit on a Crown Tower with the body that dealt
it. They must not change the simulation.
"""
import python_ai  # noqa: F401
import clash_royale_env as E

NOOP = 4
HOG = 15
DECK = [15, 6, 25, 40, 24, 72, 33, 7]
MIRROR_Y = E.ClashRoyaleEnv.BOARD_HEIGHT - 1


def _hog_match(log):
    env = E.ClashRoyaleEnv(list(DECK), list(DECK), 3600)
    env.seed(3)
    if log:
        env.set_tower_hit_log(True)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    env.set_elixir_for_team(1, 10.0)
    assert env.set_hand_for_team(1, DECK[:4])
    env.step_self_play(NOOP, 0.0, 0.0, 0, 3.0, 15.0, 1)     # a Hog at team 1's bridge
    return env


def test_units_lists_every_body_with_its_target():
    env = _hog_match(True)
    units = env.get_units()
    towers = [u for u in units if u["tower"]]
    assert len(towers) == 6 and all(u["building"] for u in towers)
    assert sorted(u["team"] for u in towers) == [0, 0, 0, 1, 1, 1]
    kings = [u for u in towers if "King" in u["name"]]
    assert len(kings) == 2 and not any(u["awake"] for u in kings)
    hogs = [u for u in units if u["card_id"] == HOG]
    assert len(hogs) == 1
    hog = hogs[0]
    assert hog["team"] == 1 and hog["deploy_ticks"] > 0 and hog["targetable"]
    # Board coordinates are team 0's frame: team 1's (3, 15) is the mirror row.
    assert abs(hog["x"] - 3.0) < 0.6 and abs(hog["y"] - (MIRROR_Y - 15.0)) < 0.6
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 60)
    by_id = {u["id"]: u for u in env.get_units()}
    hog = by_id[hog["id"]]
    left = next(u for u in towers if u["team"] == 0 and "King" not in u["name"] and u["x"] < 8.5)
    assert hog["target_id"] == left["id"]          # the Hog is locked on the tower it walked to
    assert by_id[left["id"]]["target_id"] == hog["id"]


def test_tower_hits_name_the_attacker_and_add_up_to_the_damage():
    env = _hog_match(True)
    before = env.get_tower_hp(0, 1)
    assert env.get_tower_hits() == []
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 150)
    hits = env.get_tower_hits()
    lost = before - env.get_tower_hp(0, 1)
    assert lost > 0
    on_left = [h for h in hits if h[4] == 0]
    assert sum(h[6] for h in on_left) == lost
    assert {h[2] for h in on_left} == {HOG} and {h[1] for h in on_left} == {1}
    assert [h[0] for h in hits] == sorted(h[0] for h in hits)
    # reset() clears the log and keeps the setting
    env.reset()
    assert env.get_tower_hits() == []


def test_the_log_is_off_by_default_and_changes_nothing():
    a, b = _hog_match(False), _hog_match(True)
    for _ in range(30):
        a.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 10)
        b.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 10)
        b.get_units()
        assert [a.get_tower_hp(t, s) for t in (0, 1) for s in (0, 1, 2)] == \
               [b.get_tower_hp(t, s) for t in (0, 1) for s in (0, 1, 2)]
    assert a.get_tower_hits() == []
    assert len(b.get_tower_hits()) > 0
