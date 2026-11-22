"""A Crown Tower is chosen as a target by its body, like any building (patch 0043).

Target selection is "nearest footprint in sight". Until this patch a Princess
Tower's footprint for that comparison was 3.3 tiles (King 4.4) against a body
of 1.5 (published collision radius 1.0), so a troop preferred the tower over a
defender up to 1.8 tiles nearer. The number had been fitted to one observation
(a Cannon 7-8 tiles off-lane does not pull a Hog Rider), which is the published
sight clip (patch 0042). What it broke is ordinary defence: an Ice Golem,
Skeletons or a Cannon played in front of the tower when the attacker is already
close did not distract it.
"""
import python_ai  # noqa: F401
import clash_royale_env as E

NOOP = 4
KNIGHT, HOG, CANNON, ICE_GOLEM, SKELETONS = 0, 15, 129, 40, 125
OURS = [129, 168, 125, 15, 40, 72, 33, 7]


def _defend(attacker, card, x, y, delay, ticks=400):
    """Team 1 drops `attacker` at its left bridge; `delay` ticks later team 0
    answers with `card` at (x, y). -> (Princess Tower HP lost, whether the
    attacker ever targeted a team-0 unit that is not a Crown Tower)."""
    deck = [attacker] + [c for c in (4, 12, 112, 61, 20, 14, 26, 24) if c != attacker][:7]
    env = E.ClashRoyaleEnv(list(OURS), deck, 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    env.set_elixir_for_team(0, 10.0)
    env.set_elixir_for_team(1, 10.0)
    assert env.set_hand_for_team(0, [card] + [c for c in OURS if c != card][:3])
    assert env.set_hand_for_team(1, deck[:4])
    before = env.get_tower_hp(0, 1)
    env.step_self_play(NOOP, 0.0, 0.0, 0, 3.0, 15.0, delay)
    env.step_self_play(0, float(x), float(y), NOOP, 0.0, 0.0, 1)
    distracted = False
    for _ in range(ticks):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        units = env.get_units()
        towers = {u["id"] for u in units if u["tower"]}
        ours = {u["id"] for u in units if u["team"] == 0}
        for u in units:
            if u["team"] == 1 and u["card_id"] == attacker and u["target_id"] in ours - towers:
                distracted = True
    return before - env.get_tower_hp(0, 1), distracted


def test_an_ice_golem_played_beside_the_tower_takes_a_knight_off_it():
    # The Knight is 3 s past the bridge, about 7 tiles from the tower; the Ice
    # Golem lands two tiles in front of the tower. The Knight reaches the Golem
    # first, and under the tower's fire never gets to the tower. (Before: it
    # walked past the Golem and did a lone Knight's full 750.)
    alone, _ = _defend(KNIGHT, ICE_GOLEM, 16, 2, 30)      # Golem far away: no help
    lost, distracted = _defend(KNIGHT, ICE_GOLEM, 3, 8, 30)
    assert alone >= 600
    assert distracted
    assert lost == 0


def test_a_late_cannon_in_front_of_the_tower_still_pulls_a_hog():
    # Cannon 1.5 s after the Hog, two tiles in front of the tower and two to
    # the side. Its body is nearer than the tower's the whole way down the
    # lane. (Before: the Hog ignored it and hit the tower twice.)
    lost, distracted = _defend(HOG, CANNON, 5, 8, 15)
    assert distracted
    assert lost == 0


def test_a_tower_still_wins_when_it_is_the_nearer_one():
    # A Cannon behind the Princess Tower does not pull a Hog off it.
    lost, distracted = _defend(HOG, CANNON, 7, 3, 15)
    assert not distracted
    assert lost >= 317
