"""An invisible unit stays visible for as long as it keeps attacking (patch 0040).

Published (the game's own unit table, `Ghost`): `buff_when_not_attacking:
Invisibility`, `buff_when_not_attacking_time: 1800` ms, hit speed 1800 ms. The
Royal Ghost is invisible until its first hit and cloaks again only 1.8 s after
its last attack, so a Ghost that is hitting a Princess Tower is shot by it the
whole time and dies to it alone: 1210 HP against a 109-damage arrow every 0.8 s
is twelve arrows, about ten seconds, five or six 261-damage hits.

Before the patch the base form was visible for 0.5 s of every 1.8 s. The tower
never completed a first hit on it: in replays a lone Ghost stood at the tower
at full health for 13 s and more (the largest per-match source of tower damage
the real game does not have, with Elite Barbarians).
"""
import python_ai  # noqa: F401
import clash_royale_env as E

NOOP = 4
GHOST, BUSH = 47, 85
PLAIN = [15, 6, 25, 40, 24, 72, 33, 7]


def _lone(card, ticks=400):
    """Team 1 drops `card` at its bridge and nobody answers. -> (per-tick
    targetable flags from its first hit until it dies, tower HP lost, ticks it
    lived after the first hit or None if it never died)."""
    deck = [card] + [c for c in (4, 12, 112, 61, 20, 14, 2, 26) if c != card][:7]
    env = E.ClashRoyaleEnv(list(PLAIN), deck, 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    env.set_elixir_for_team(1, 10.0)
    assert env.set_hand_for_team(1, deck[:4])
    before = env.get_tower_hp(0, 1)
    env.step_self_play(NOOP, 0.0, 0.0, 0, 3.0, 15.0, 1)
    flags, first, died = [], None, None
    for t in range(ticks):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        u = [x for x in env.get_units() if x["card_id"] == card]
        lost = before - env.get_tower_hp(0, 1)
        if first is None and lost > 0:
            first = t
        if first is not None:
            if not u:
                died = t - first
                break
            flags.append(u[0]["targetable"])
    return flags, before - env.get_tower_hp(0, 1), died


def test_a_royal_ghost_hitting_a_tower_stays_visible_and_dies_to_it():
    flags, lost, died = _lone(GHOST)
    assert flags and all(flags), "the Ghost cloaked again between two hits"
    assert died is not None, "the tower never killed a lone Royal Ghost"
    # twelve arrows 0.8 s apart after the tower's first hit: 9 to 11 s
    assert 85 <= died <= 115
    assert lost in (5 * 261, 6 * 261, 7 * 261)


def test_a_royal_ghost_is_invisible_until_its_first_hit():
    deck = [GHOST, 4, 12, 112, 61, 20, 14, 2]
    env = E.ClashRoyaleEnv(list(PLAIN), deck, 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    env.set_elixir_for_team(1, 10.0)
    assert env.set_hand_for_team(1, deck[:4])
    env.step_self_play(NOOP, 0.0, 0.0, 0, 3.0, 15.0, 1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 25)
    ghost = next(x for x in env.get_units() if x["card_id"] == GHOST)
    assert not ghost["targetable"]


def test_a_suspicious_bush_at_a_tower_is_shot():
    flags, lost, died = _lone(BUSH)
    assert died is not None and died <= 25, "the tower never shot the Bush it was being hit by"
    assert all(flags)
