"""An unanswered Goblin Barrel on a Princess Tower (patches 0034-0037).

Measured on the recording of 2026-11-20 (runs/live/20261120-185026, two
unanswered barrels, towers and Goblins level 11): the Goblins stab 3, 2, 1, 1
times, seven stabs, while the tower's arrows land 0.8 s apart and kill them two
arrows each. Gen-2 before these patches let one stab through: the tower fired
on the tick it acquired a target (no first hit), its arrow flew 20 tiles/s, and
the Goblins landed where the OTHER Princess Tower could reach them.
"""
import python_ai  # noqa: F401
import clash_royale_env as E

ID = {E.get_card_info(c)["name"]: c for c in E.get_all_card_ids()}
NOOP = 4
BARREL = ID["Goblin Barrel"]
BAIT = [BARREL, 4, 12, 112, 61, 20, 14, 2]
PLAIN = [15, 6, 25, 40, 24, 72, 33, 7]
MIRROR_Y = E.ClashRoyaleEnv.BOARD_HEIGHT - 1
LEFT_X, RIGHT_X = 3.0, 14.0


def _barrel(caster, x):
    """Tower HP lost by each of the defender's three towers (King, left,
    right) to one unanswered Barrel thrown by `caster` at the Princess Tower in
    column x."""
    decks = (BAIT, PLAIN) if caster == 0 else (PLAIN, BAIT)
    env = E.ClashRoyaleEnv(list(decks[0]), list(decks[1]), 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    defender = 1 - caster
    before = [env.get_tower_hp(defender, s) for s in (0, 1, 2)]
    env.set_elixir_for_team(caster, 10.0)
    assert env.set_hand_for_team(caster, BAIT[:4])
    spent = env.get_elixir_spent(caster)
    # Both sides aim in their own frame: the enemy Princess Towers are at the
    # mirror of our own row.
    y = MIRROR_Y - E.arena_princess_y(0)
    if caster == 0:
        env.step_self_play(0, x, y, NOOP, 0.0, 0.0, 1)
    else:
        env.step_self_play(NOOP, 0.0, 0.0, 0, x, y, 1)
    assert env.get_elixir_spent(caster) > spent, "the Barrel was refused"
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 150)
    return [b - env.get_tower_hp(defender, s) for b, s in zip(before, (0, 1, 2))]


def test_seven_stabs_reach_the_tower_and_only_that_tower():
    left = _barrel(1, LEFT_X)
    assert left[0] == 0 and left[2] == 0
    assert left[1] == 7 * 125                  # a level-11 Goblin stabs for 125
    right = _barrel(1, RIGHT_X)
    assert right == [0, 0, left[1]]


def test_the_barrel_is_the_same_for_both_sides():
    ours = _barrel(0, LEFT_X)
    theirs = _barrel(1, LEFT_X)
    assert ours == theirs
    assert _barrel(0, RIGHT_X) == _barrel(1, RIGHT_X)
