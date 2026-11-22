"""Spells and splash hit invisible units (patch 0041).

The game's unit table marks the Royal Ghost (`Ghost`) and the Archer Queen
`allow_area_dmg_when_invisible: True`: invisibility stops a unit being picked
as a target, it does not stop a Fireball, a Log or a splash landing on it.
Before the patch every area effect skipped untargetable units, so a cloaked
Ghost or a Suspicious Bush walked through spells unharmed.
"""
import python_ai  # noqa: F401
import clash_royale_env as E

NOOP = 4
GHOST, BUSH, FIREBALL, LOG, VALK = 47, 85, 7, 33, 10
OURS = [FIREBALL, LOG, VALK, 40, 24, 72, 15, 6]


def _env(card):
    deck = [card] + [c for c in (4, 12, 112, 61, 20, 14, 2, 26) if c != card][:7]
    env = E.ClashRoyaleEnv(list(OURS), deck, 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    env.set_elixir_for_team(0, 10.0)
    env.set_elixir_for_team(1, 10.0)
    assert env.set_hand_for_team(1, deck[:4])
    assert env.set_hand_for_team(0, OURS[:4])
    env.step_self_play(NOOP, 0.0, 0.0, 0, 3.0, 15.0, 1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 12)      # deployed and walking
    return env


def _unit(env, card):
    u = [x for x in env.get_units() if x["card_id"] == card]
    return u[0] if u else None


def _cast(env, slot, lead, ticks):
    """Team 0 plays hand slot `slot` `lead` tiles ahead of the invisible unit.
    -> (HP before, unit after `ticks` or None, whether it was ever targetable)."""
    card = GHOST if _unit(env, GHOST) else BUSH
    u = _unit(env, card)
    assert not u["targetable"]
    hp = u["hp"]
    spent = env.get_elixir_spent(0)
    env.step_self_play(slot, round(u["x"]), round(u["y"] - lead), NOOP, 0.0, 0.0, 1)
    assert env.get_elixir_spent(0) > spent, "the spell was refused"
    seen = False
    for _ in range(ticks):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        v = _unit(env, card)
        seen = seen or bool(v and v["targetable"])
    return hp, _unit(env, card), seen


def test_a_fireball_hits_a_cloaked_royal_ghost():
    env = _env(GHOST)
    dmg = E.get_card_stats(FIREBALL)["base"]["damage"]
    hp, ghost, seen = _cast(env, 0, 2.0, 25)
    assert not seen, "the Ghost was revealed; this test wants it cloaked"
    assert ghost is not None and hp - ghost["hp"] == dmg


def test_the_log_hits_a_cloaked_royal_ghost():
    env = _env(GHOST)
    dmg = E.get_card_stats(LOG)["base"]["damage"]
    hp, ghost, seen = _cast(env, 1, 4.0, 30)
    assert not seen
    assert ghost is not None and hp - ghost["hp"] == dmg


def test_a_fireball_destroys_a_suspicious_bush():
    env = _env(BUSH)
    hp, bush, seen = _cast(env, 0, 1.0, 25)
    assert bush is None, "the Bush walked through a Fireball"


def test_a_cloaked_ghost_is_still_not_a_target():
    env = _env(GHOST)
    for _ in range(20):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        ghost = _unit(env, GHOST)
        towers = [u for u in env.get_units() if u["tower"] and u["team"] == 0]
        if ghost["targetable"]:
            break
        assert all(t["target_id"] != ghost["id"] for t in towers)
