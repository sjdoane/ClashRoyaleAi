"""The Log is a rolling body, not a corridor that stays live behind it.

A Goblin Barrel thrown at our Princess Tower lands 28 ticks after the cast. A
Log cast on the tile in front of the tower as the Barrel is thrown has rolled
by before the Goblins land and does not touch them; one cast two seconds later
meets them. (Body 1.2 tiles long: published projectile_radius_y 600.)

Patch 0037 put one of the three Goblins behind the tower, where the real barrel
lands it. The Log starts there, so it leaves that Goblin first: a Log cast
about a second after the throw now gets the two river-side Goblins and misses
the one behind.
"""
import pytest

import python_ai  # noqa: F401
import clash_royale_env as E

ID = {E.get_card_info(c)["name"]: c for c in E.get_all_card_ids()}
NOOP = 4
OURS = [15, 6, 25, 40, 24, 72, 33, 7]
BAIT = [ID["Goblin Barrel"], 4, 12, 112, 61, 20, 14, 2]
LEFT_X = 3.0
MIRROR_Y = E.ClashRoyaleEnv.BOARD_HEIGHT - 1


def _tower_loss(log_delay, log_tile=(LEFT_X, 4.0)):
    """HP our left Princess Tower loses to a Barrel answered by a Log cast
    `log_delay` ticks after the Barrel (None: no Log)."""
    env = E.ClashRoyaleEnv(list(OURS), list(BAIT), 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    hp = env.get_tower_hp(0, 1)
    barrel, log = ID["Goblin Barrel"], ID["The Log"]
    for team, card, deck in ((1, barrel, BAIT), (0, log, OURS)):
        env.set_elixir_for_team(team, 10.0)
        assert env.set_hand_for_team(team, [card] + [c for c in deck if c != card][:3])
    target_y = E.arena_princess_y(0)
    for tick in range(120):
        a0 = (NOOP, 0.0, 0.0)
        a1 = (NOOP, 0.0, 0.0)
        if tick == 0:
            a1 = (0, LEFT_X, MIRROR_Y - target_y)
        if log_delay is not None and tick == log_delay:
            a0 = (0, log_tile[0], log_tile[1])
        spent = (env.get_elixir_spent(0), env.get_elixir_spent(1))
        env.step_self_play(*a0, *a1, 1)
        if tick == 0:
            assert env.get_elixir_spent(1) > spent[1], "the Barrel was refused"
        if log_delay is not None and tick == log_delay:
            assert env.get_elixir_spent(0) > spent[0], "the Log was refused"
    return hp - env.get_tower_hp(0, 1)


def test_the_rollers_carry_their_body_depth():
    for name in ("The Log", "Barbarian Barrel"):
        assert E.get_card_stats(ID[name])["base"]["roll_body_depth"] == pytest.approx(1.2), name


def test_an_unanswered_barrel_hurts_the_tower():
    assert _tower_loss(None) > 0


def test_a_log_cast_as_the_barrel_is_thrown_rolls_past_before_the_goblins_land():
    assert _tower_loss(0) == _tower_loss(None)
    assert _tower_loss(5) == _tower_loss(None)


def test_a_log_cast_two_seconds_after_the_throw_saves_the_tower():
    for delay in (18, 20, 24):
        assert _tower_loss(delay) == 0, delay


def test_a_log_cast_a_second_after_the_throw_misses_the_goblin_behind_the_tower():
    unanswered = _tower_loss(None)
    for delay in (10, 14):
        assert 0 < _tower_loss(delay) < unanswered / 3, delay


def test_a_log_cast_after_the_first_stab_is_too_late():
    assert _tower_loss(40) > 0
