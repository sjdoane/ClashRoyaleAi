"""Sight is measured body to body, like attack range (patch 0044).

The published tables give sight equal to range for the Musketeer (6000 / 6000),
Wizard and Cannon (5500 / 5500): a unit can hit exactly what it can see, so the
two are one measure, and range is body to body (attacker radius + target
radius on top of the number; the Princess Tower's 7.5 reaching 9.4 was checked
against recordings in patch 0033). Before this patch sight was centre to
centre, so a melee troop with the standard 5.5 saw 0.8 tiles short. The play
it broke is the kite: an Ice Golem or Knight dropped in the middle of the
arena, 5.5 tiles to the side of a lane push and a little ahead of it, is 6+
tiles away centre to centre and was ignored.
"""
import python_ai  # noqa: F401
import clash_royale_env as E

NOOP = 4
ELITE_BARBARIANS, MINI_PEKKA, ICE_GOLEM = 17, 5, 40
OURS = [129, 168, 125, 15, 40, 72, 33, 7]


def _kite(attacker, x, y, delay=15, ticks=300, window=50):
    """Team 1 drops `attacker` at its left bridge (x 3); `delay` ticks later
    team 0 plays an Ice Golem at (x, y). -> (did an attacker unit target the
    Golem within `window` ticks of the play, ticks from the play to the first
    damage on the Princess Tower or None)."""
    deck = [attacker] + [c for c in (4, 12, 112, 61, 20, 14, 26, 24) if c != attacker][:7]
    env = E.ClashRoyaleEnv(list(OURS), deck, 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    env.set_elixir_for_team(0, 10.0)
    env.set_elixir_for_team(1, 10.0)
    assert env.set_hand_for_team(0, [ICE_GOLEM, 168, 125, 15])
    assert env.set_hand_for_team(1, deck[:4])
    before = env.get_tower_hp(0, 1)
    env.step_self_play(NOOP, 0.0, 0.0, 0, 3.0, 15.0, delay)
    env.step_self_play(0, float(x), float(y), NOOP, 0.0, 0.0, 1)
    pulled, first_damage = False, None
    for t in range(ticks):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        if t < window:
            units = env.get_units()
            golem = {u["id"] for u in units if u["team"] == 0 and u["name"] == "Ice Golem"}
            if any(u["team"] == 1 and u["target_id"] in golem for u in units):
                pulled = True
        if first_damage is None and env.get_tower_hp(0, 1) < before:
            first_damage = t
            break
    return pulled, first_damage


def test_an_ice_golem_in_the_middle_pulls_a_lane_push():
    # Golem at x 8, 5 tiles to the side of the lane and level with the push as
    # it comes off the bridge: 6.0 tiles centre to centre at its nearest,
    # inside 5.5 + 0.4 + 0.4 = 6.3, outside a centre-to-centre 5.5.
    # The pull buys time: the tower is first hit seconds later than with the
    # Golem out of the way.
    for attacker in (ELITE_BARBARIANS, MINI_PEKKA):
        alone = _kite(attacker, 16, 2)[1]
        pulled, first = _kite(attacker, 8, 13)
        assert pulled, attacker
        assert first is None or first >= alone + 20, (attacker, first, alone)


def test_sight_still_ends():
    # Two tiles further out the Golem is 7.5+ tiles away and nobody turns.
    for attacker in (ELITE_BARBARIANS, MINI_PEKKA):
        pulled, _ = _kite(attacker, 11, 13)
        assert not pulled, attacker
