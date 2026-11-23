"""A knockback stops a charge: The Log on a Prince running at a tower.

Published for The Log against Prince and Dark Prince. The Prince's first hit
on the Princess Tower is the doubled one only if nothing knocked him back on
the way (or he had the room to run his three tiles again afterwards).
"""
import python_ai  # noqa: F401
import clash_royale_env as E

ID = {E.get_card_info(c)["name"]: c for c in E.get_all_card_ids()}
NOOP = 4
OURS = [15, 6, 25, 40, 100, 72, 33, 7]                 # holds The Log and Fireball
THEIRS = [ID["Goblin Barrel"], 4, 12, 112, 61, 20, ID["Prince"], 2]
LEFT_X = 3.0


def _first_tower_hit(start_y, card=None, at=None, cast_tick=None, charger="Prince"):
    """Damage and tick of the charger's first hit on our left Princess Tower."""
    env = E.ClashRoyaleEnv(list(OURS), list(THEIRS), 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    env.inject(ID[charger], LEFT_X, start_y, 1, -1.0, 0)
    hp = env.get_tower_hp(0, 1)
    for tick in range(1, 140):
        if card is not None and tick == cast_tick + 1:
            env.set_elixir_for_team(0, 10.0)
            assert env.set_hand_for_team(0, [card] + [c for c in OURS if c != card][:3])
            spent = env.get_elixir_spent(0)
            env.step_self_play(0, at[0], at[1], NOOP, 0.0, 0.0, 1)
            assert env.get_elixir_spent(0) > spent, "the play was refused"
        else:
            env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        now = env.get_tower_hp(0, 1)
        if now < hp:
            return hp - now, tick
    raise AssertionError("the tower was never hit")


def test_an_unanswered_prince_lands_the_doubled_hit():
    plain, _ = _first_tower_hit(start_y=10.5)          # too short a run to charge
    charged, _ = _first_tower_hit(start_y=16.0)
    assert charged == 2 * plain


def test_a_log_that_catches_him_near_the_tower_costs_him_the_charge():
    plain, _ = _first_tower_hit(start_y=10.5)
    _, unanswered_tick = _first_tower_hit(start_y=16.0)
    for cast_tick in (15, 20, 25):
        dealt, tick = _first_tower_hit(16.0, ID["The Log"], (LEFT_X, 8.0), cast_tick)
        assert dealt == plain, cast_tick
        assert tick > unanswered_tick, cast_tick       # and he arrives later


def test_a_log_far_from_the_tower_leaves_him_room_to_charge_again():
    plain, _ = _first_tower_hit(start_y=10.5)
    _, unanswered_tick = _first_tower_hit(start_y=16.0)
    dealt, tick = _first_tower_hit(16.0, ID["The Log"], (LEFT_X, 11.0), 3)
    assert tick > unanswered_tick                      # the Log did catch him
    assert dealt == 2 * plain


def test_a_fireball_does_not_move_a_prince_and_his_charge_survives():
    """Patch 0053. The Prince is published with IgnorePushback: an ordinary
    knockback (Fireball 1000) neither moves nor interrupts him. The Log is a
    PushbackAll and does (the tests above)."""
    plain, _ = _first_tower_hit(start_y=10.5)
    dealt, _ = _first_tower_hit(16.0, ID["Fireball"], (LEFT_X, 10.0), 25)
    assert dealt == 2 * plain


def test_an_ordinary_knockback_costs_a_battle_ram_its_charge():
    """The Battle Ram has no IgnorePushback: a Giant Snowball (Pushback 1800)
    throws it back and it has to run its distance again."""
    plain, _ = _first_tower_hit(start_y=10.5, charger="Battle Ram")
    charged, _ = _first_tower_hit(start_y=16.0, charger="Battle Ram")
    assert charged == 2 * plain
    # Thrown late enough to land when the Ram is inside its last three tiles.
    dealt, _ = _first_tower_hit(16.0, ID["Giant Snowball"], (LEFT_X, 10.0), 30, charger="Battle Ram")
    assert dealt == plain
