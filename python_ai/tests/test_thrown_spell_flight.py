"""A thrown spell flies from the caster's King Tower, and its flight is seen.

Fireball, Arrows, Rocket, Giant Snowball and Goblin Barrel take a time that
grows with the distance from the King Tower (published projectile speed / 48
real tiles per second). The numbers pinned here are the measured ones: a
Goblin Barrel thrown onto a Princess Tower was in the air 2.72-2.81 s in eight
recorded throws.
"""
import pytest

import python_ai  # noqa: F401
import clash_royale_env as E

ID = {E.get_card_info(c)["name"]: c for c in E.get_all_card_ids()}
NOOP = 4
THROWN = {"Fireball": 600, "Arrows": 1100, "Rocket": 350, "Giant Snowball": 800,
          "Goblin Barrel": 400}
FIXED = ("The Log", "Zap", "Lightning", "Poison", "Earthquake", "Tornado",
         "Freeze", "Rage", "Graveyard", "Royal Delivery", "Barbarian Barrel")

OURS = [15, 6, 25, 40, 24, 72, 33, 7]                  # holds The Log and Fireball
BAIT = [ID["Goblin Barrel"], 4, 12, 112, 61, 20, 14, 2]
LEFT_X, RIGHT_X, CENTER_X = 3.0, 14.0, 8.5


def _env(deck0=OURS, deck1=BAIT):
    env = E.ClashRoyaleEnv(list(deck0), list(deck1), 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    return env


def _wait(env, ticks=1):
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, ticks)


def _cast(env, team, card, x, y, deck):
    """Play `card` for `team` at board point (x, y) on this tick."""
    env.set_elixir_for_team(team, 10.0)
    assert env.set_hand_for_team(team, [card] + [c for c in deck if c != card][:3])
    spent = env.get_elixir_spent(team)
    if team == 0:
        env.step_self_play(0, x, y, NOOP, 0.0, 0.0, 1)
    else:   # team 1 acts in its own frame: y mirrored
        env.step_self_play(NOOP, 0.0, 0.0, 0, x, (E.ClashRoyaleEnv.BOARD_HEIGHT - 1) - y, 1)
    assert env.get_elixir_spent(team) > spent, "the play was refused"


def test_the_thrown_spells_carry_their_published_speeds():
    for name, speed in THROWN.items():
        assert E.get_card_stats(ID[name])["base"]["spell_projectile_speed"] == speed, name
    for name in FIXED:
        assert E.get_card_stats(ID[name])["base"]["spell_projectile_speed"] == 0, name


def test_flight_grows_with_the_distance_from_the_king_tower():
    # The enemy Princess Towers are 23.2 real tiles from our King Tower, the
    # enemy King Tower 26, our own Princess Tower 6.5.
    far = E.arena_princess_y(1)
    barrel, fireball = ID["Goblin Barrel"], ID["Fireball"]
    assert E.spell_flight_ticks(barrel, LEFT_X, far) == 28      # measured 2.76 s
    assert E.spell_flight_ticks(barrel, RIGHT_X, far) == 28
    assert E.spell_flight_ticks(barrel, CENTER_X, E.arena_king_y(1)) == 31
    assert E.spell_flight_ticks(fireball, LEFT_X, far) == 19
    assert E.spell_flight_ticks(fireball, LEFT_X, E.arena_princess_y(0)) == 5
    assert E.spell_flight_ticks(ID["Rocket"], LEFT_X, far) == 32
    assert E.spell_flight_ticks(ID["Giant Snowball"], LEFT_X, far) == 14
    assert E.spell_flight_ticks(ID["Arrows"], LEFT_X, far) == 10

    for name in THROWN:
        ticks = [E.spell_flight_ticks(ID[name], CENTER_X, float(y)) for y in range(3, 34)]
        assert ticks == sorted(ticks), name
        assert ticks[0] >= 1 and ticks[-1] > ticks[0], name


def test_flight_is_the_same_for_both_teams():
    for name in THROWN:
        for x, y in ((3.0, 27.0), (14.0, 6.0), (8.5, 16.5), (0.0, 33.0), (11.0, 14.0)):
            assert (E.spell_flight_ticks(ID[name], x, y, 0)
                    == E.spell_flight_ticks(ID[name], 17.0 - x, 33.0 - y, 1)), (name, x, y)


def test_a_spell_that_is_not_thrown_keeps_one_delay_everywhere():
    for name in FIXED:
        ticks = {E.spell_flight_ticks(ID[name], x, y)
                 for x, y in ((3.0, 27.0), (8.5, 3.0), (14.0, 16.0))}
        assert len(ticks) == 1, name
    assert E.spell_flight_ticks(ID["The Log"], 3.0, 27.0) == 8
    with pytest.raises(ValueError):
        E.spell_flight_ticks(ID["Knight"], 3.0, 27.0)


def test_a_goblin_barrel_is_pending_until_it_lands_and_then_the_goblins_appear():
    env = _env()
    target_y = E.arena_princess_y(0)
    hp = env.get_tower_hp(0, 1)
    assert env.get_pending_spells() == []
    _cast(env, 1, ID["Goblin Barrel"], LEFT_X, target_y, BAIT)

    flight = E.spell_flight_ticks(ID["Goblin Barrel"], LEFT_X, target_y, 1)
    assert flight == 28
    for elapsed in range(1, flight):                 # ticks since the cast step
        (spell,) = env.get_pending_spells()
        assert spell["team"] == 1 and spell["card_id"] == ID["Goblin Barrel"]
        assert spell["thrown"] is True
        assert (spell["x"], spell["y"]) == (LEFT_X, target_y)
        assert spell["ticks_to_impact"] == flight - elapsed + 1
        assert env.get_tower_hp(0, 1) == hp
        _wait(env)
    (spell,) = env.get_pending_spells()
    assert spell["ticks_to_impact"] == 1             # the next step lands it
    _wait(env)
    assert env.get_pending_spells() == []

    # The Goblins deploy for a second and stab after their first-hit wind-up
    # (hit speed 1.1 s - load time 0.7 s, LoadTimes.h): the tower is first
    # hurt 1.5 s after the landing (1.43-1.53 s measured).
    for after_landing in range(1, 30):
        _wait(env)
        if env.get_tower_hp(0, 1) < hp:
            break
    assert after_landing == 15


def test_a_fireball_hits_only_when_its_flight_is_over():
    env = _env()
    x, y = 12.0, 24.0
    env.inject(ID["Knight"], x, y, 1, -1.0, 0)
    _wait(env)
    _cast(env, 0, ID["Fireball"], x, y, OURS)
    flight = E.spell_flight_ticks(ID["Fireball"], x, y, 0)
    assert 10 < flight < 28
    (spell,) = env.get_pending_spells()
    assert spell == {"team": 0, "card_id": ID["Fireball"], "x": x, "y": y,
                     "ticks_to_impact": flight, "thrown": True}
    dealt = env.get_damage_dealt_by_card(ID["Fireball"], 0)
    _wait(env, flight - 1)
    assert env.get_damage_dealt_by_card(ID["Fireball"], 0) == dealt
    _wait(env)
    assert env.get_damage_dealt_by_card(ID["Fireball"], 0) > dealt
    assert env.get_pending_spells() == []


def test_the_log_is_listed_while_it_waits_but_is_not_thrown():
    env = _env()
    _cast(env, 0, ID["The Log"], LEFT_X, 10.0, OURS)
    (spell,) = env.get_pending_spells()
    assert spell["card_id"] == ID["The Log"] and spell["thrown"] is False
    assert spell["ticks_to_impact"] == 8
    _wait(env, 8)
    assert env.get_pending_spells() == []            # rolling now, no longer pending


def test_a_snapshot_taken_mid_flight_lands_on_the_same_tick():
    env = _env()
    _cast(env, 1, ID["Goblin Barrel"], RIGHT_X, E.arena_princess_y(0), BAIT)
    _wait(env, 9)
    copy = env.snapshot()
    assert copy.get_pending_spells() == env.get_pending_spells()
    for _ in range(60):
        _wait(env)
        _wait(copy)
        assert copy.get_pending_spells() == env.get_pending_spells()
        assert copy.get_tower_hp(0, 2) == env.get_tower_hp(0, 2)
    assert env.get_tower_hp(0, 2) < env.get_tower_max_hp(0, 2)
