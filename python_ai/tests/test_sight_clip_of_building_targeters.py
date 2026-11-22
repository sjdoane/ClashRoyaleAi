"""Published sight clip of the building-targeting troops (patch 0042).

The game's unit table gives the Hog Rider SightRange 9500, SightClip 4000,
SightClipSide 4000 and the Giant 7500 / 2000 / 2000 (FirstLight-Zero
card_specs.json release 15.535.13; RoyaleAPI cards_stats.json agrees). Read as
a box cut out of the sight disc (include/core/SightClips.h), a building pulls
one of these troops only from within sightRange - clip: 5.5 tiles sideways for
both. So a Cannon in the middle of the arena pulls a Hog Rider or a Giant from
either lane, and one placed towards the other lane does not, although it is
inside the 9.5 / 7.5 sight radius. Before the patch only the disc existed.
"""
import python_ai  # noqa: F401
import clash_royale_env as E
import pytest

NOOP = 4
HOG, GIANT, BALLOON, BATTLE_RAM, CANNON = 15, 2, 45, 81, 129
OURS = [129, 168, 125, 15, 40, 72, 33, 7]


def _first_target(attacker, cannon_x, cannon_y=12.0, ticks=420):
    """Team 0 has a Cannon standing at (cannon_x, cannon_y); team 1 then drops
    `attacker` at its left bridge (x 3). -> (name of the first thing the
    attacker hits or None, Princess Tower HP lost, Cannon HP lost)."""
    deck = [attacker] + [c for c in (4, 12, 112, 61, 20, 14, 26, 24) if c != attacker][:7]
    env = E.ClashRoyaleEnv(list(OURS), deck, 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    env.set_elixir_for_team(0, 10.0)
    env.set_elixir_for_team(1, 10.0)
    assert env.set_hand_for_team(0, [CANNON, 168, 125, 15])
    assert env.set_hand_for_team(1, deck[:4])
    tower0 = env.get_tower_hp(0, 1)
    env.step_self_play(0, float(cannon_x), float(cannon_y), NOOP, 0.0, 0.0, 12)
    env.step_self_play(NOOP, 0.0, 0.0, 0, 3.0, 15.0, 1)
    cannon_max, first = None, None
    cannon_lost = 0
    for _ in range(ticks):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        cannon = [u for u in env.get_units() if u["team"] == 0 and u["name"] == "Cannon"]
        if cannon:
            hp = cannon[0]["hp"]
            if cannon_max is None:
                cannon_max = hp
        tower_lost = tower0 - env.get_tower_hp(0, 1)
        if first is None:
            # the Cannon decays, so "hit" is a drop larger than a tick of decay
            if tower_lost > 0:
                first = "tower"
            elif cannon and cannon_max - hp > 150:
                first = "cannon"
            if cannon:
                cannon_max = hp
        if first is not None:
            break
    return first


def test_a_balloon_is_clipped_sideways_only():
    # Balloon: SightClipSide 2000 on 7700 (5.7 to the side), no forward clip.
    assert _first_target(BALLOON, 9, 14.0) == "cannon"
    assert _first_target(BALLOON, 11, 14.0) == "tower"


def test_the_clip_values_are_the_published_ones():
    hog = E.get_card_stats(HOG)["base"]
    assert (hog["sight_range"], hog["sight_clip"], hog["sight_clip_side"]) == pytest.approx((9.5, 4.0, 4.0))
    giant = E.get_card_stats(GIANT)["base"]
    assert (giant["sight_range"], giant["sight_clip"], giant["sight_clip_side"]) == pytest.approx((7.5, 2.0, 2.0))
    balloon = E.get_card_stats(BALLOON)["base"]
    assert (balloon["sight_clip"], balloon["sight_clip_side"]) == pytest.approx((0.0, 2.0))
    ram = E.get_card_stats(BATTLE_RAM)["base"]
    assert (ram["sight_clip"], ram["sight_clip_side"]) == (0.0, 0.0)   # published without one


@pytest.mark.parametrize("attacker", [HOG, GIANT])
def test_a_cannon_in_the_middle_pulls_from_the_lane(attacker):
    # lane x 3; the middle of the arena is x 8-9, 5-6 tiles to the side
    for x in (5, 7, 9):
        assert _first_target(attacker, x) == "cannon", (attacker, x)


@pytest.mark.parametrize("attacker", [HOG, GIANT])
def test_a_cannon_towards_the_other_lane_does_not_pull(attacker):
    # 7+ tiles to the side: inside the Hog's 9.5 sight disc, outside the box.
    # (Before the patch a Hog left its lane for a Cannon at x 10.)
    for x in (10, 11, 13):
        assert _first_target(attacker, x) == "tower", (attacker, x)
