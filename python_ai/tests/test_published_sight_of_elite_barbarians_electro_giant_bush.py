"""Three sight ranges the registry had at the default 5.5 (patch 0045).

Published (FirstLight-Zero card_specs.json, release 15.535.13): AngryBarbarian
(Elite Barbarians) SightRange 6000; ElectroGiant 7500 with SightClip /
SightClipSide 2000 / 2000; SuspiciousBush 7500 with 2000 / 2000. The Electro
Giant and Bush are building-targeters, so their sight is what a defensive
building pulls them by: at 5.5 a Cannon in the middle of the arena did not.
"""
import python_ai  # noqa: F401
import clash_royale_env as E
import pytest

NOOP = 4
ELITE_BARBARIANS, EVO_ELITE_BARBARIANS, ELECTRO_GIANT, BUSH, CANNON = 17, 178, 84, 85, 129
OURS = [129, 168, 125, 15, 40, 72, 33, 7]


def test_the_values_are_the_published_ones():
    for card in (ELITE_BARBARIANS, EVO_ELITE_BARBARIANS):
        stats = E.get_card_stats(card)
        for form in ("base", "evolved"):
            if stats.get(form):
                assert stats[form]["sight_range"] == pytest.approx(6.0), (card, form)
    for card in (ELECTRO_GIANT, BUSH):
        base = E.get_card_stats(card)["base"]
        assert base["sight_range"] == pytest.approx(7.5)
        assert (base["sight_clip"], base["sight_clip_side"]) == pytest.approx((2.0, 2.0))


def test_a_cannon_in_the_middle_pulls_an_electro_giant():
    # Lane x 3, Cannon at x 9: six tiles to the side. Inside 7.5 - 2.0 = 5.5
    # body to body (6.9 centre), outside the old 5.5 disc at any useful depth.
    deck = [ELECTRO_GIANT, 4, 12, 112, 61, 20, 14, 26]
    env = E.ClashRoyaleEnv(list(OURS), deck, 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    env.set_elixir_for_team(0, 10.0)
    env.set_elixir_for_team(1, 10.0)
    assert env.set_hand_for_team(0, [CANNON, 168, 125, 15])
    assert env.set_hand_for_team(1, deck[:4])
    env.step_self_play(0, 9.0, 12.0, NOOP, 0.0, 0.0, 12)
    env.step_self_play(NOOP, 0.0, 0.0, 0, 3.0, 15.0, 1)
    pulled = False
    for _ in range(150):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        units = env.get_units()
        cannon = {u["id"] for u in units if u["team"] == 0 and u["name"] == "Cannon"}
        if any(u["team"] == 1 and u["name"] == "Electro Giant" and u["target_id"] in cannon for u in units):
            pulled = True
            break
    assert pulled
