"""Published reduced Crown Tower damage, applied as one rule (patch 0047).

The game-data table (FirstLight-Zero card_specs.json and projectiles.json,
release 15.535.13) gives some damage sources a `CrownTowerDamagePercent`.
`include/core/CrownTowerDamage.h` holds every such row whose card the engine
has and `CardRegistry::add` / `addEvolution` apply it, so no card row carries
its own literal. Level-11 damage is the registry's (level-1 base x 2.56).

Before the patch: the Miner hit Crown Towers for his full 194 (38 published),
Fireball took 159 (172), Arrows 31 a wave (25), Rocket 371 (342), Lightning 286
(264), the Zap and Giant Snowball Evolutions 58 and 54 (48 and 45, the base
card's row; the release does not resolve a separate Evolution record), and the
Goblin Machine's rocket its full 304 (152).
"""
import python_ai  # noqa: F401
import clash_royale_env as E
import pytest

NOOP = 4
OURS = [129, 168, 125, 15, 40, 72, 33, 7]

# card id -> (published percent, level-11 damage per application, Crown Tower damage)
SPELLS = {
    3: (-80, 123, 25),      # Arrows, per wave
    7: (-75, 689, 172),     # Fireball
    29: (-75, 192, 48),     # Zap
    30: (-77, 1485, 342),   # Rocket
    31: (-75, 1057, 264),   # Lightning, per strike
    33: (-87, 269, 35),     # The Log
    100: (-75, 179, 45),    # Giant Snowball
}


@pytest.mark.parametrize("card", sorted(SPELLS))
def test_a_spell_row_is_the_published_share_of_the_registry_damage(card):
    percent, damage, tower = SPELLS[card]
    base = E.get_card_stats(card)["base"]
    assert base["damage"] == damage
    assert tower == round(damage * (100 + percent) / 100.0)
    assert base["spell_crown_tower_damage"] == tower


def test_an_evolution_inherits_its_base_cards_row():
    seen = 0
    for evo in E.get_evolution_card_ids():
        stats = E.get_card_stats(evo)
        for form in ("base", "evolved"):
            row = stats.get(form)
            if not row or row["id"] not in SPELLS or row["archetype"] != stats["base"]["archetype"]:
                continue
            if row["spell_crown_tower_damage"] < 0:
                continue
            percent, _damage, _tower = SPELLS[row["id"]]
            assert row["spell_crown_tower_damage"] == round(row["damage"] * (100 + percent) / 100.0), (evo, form)
            seen += 1
    assert seen >= 4   # Zap and Giant Snowball, base and evolved form


def test_the_electro_wizards_deploy_zap_does_nothing_to_a_crown_tower():
    for card in (35, 186):
        base = E.get_card_stats(card)["base"]
        assert base["spawn_effect_damage"] > 0
        assert base["spawn_effect_crown_tower_damage"] == 0


def _env(card, defender=None):
    deck = [card] + [c for c in (4, 12, 112, 61, 20, 14, 26, 0) if c != card][:7]
    env = E.ClashRoyaleEnv(list(defender or OURS), deck, 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    env.set_elixir_for_team(1, 10.0)
    assert env.set_hand_for_team(1, deck[:4])
    env.set_tower_hit_log(True)
    return env


def _tower_hits(env, ticks):
    """Damage of every hit a team-0 Crown Tower takes in `ticks`, in order."""
    for _ in range(ticks):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
    # (tick, attacker_team, attacker_card_id, attacker_id, target_team, target_id, amount)
    return [h[6] for h in env.get_tower_hits() if h[4] == 0]


@pytest.mark.parametrize("card,expected", [
    (7, [172]), (30, [342]), (3, [25, 25, 25]), (29, [48]), (100, [45]),
])
def test_a_spell_cast_on_a_princess_tower_takes_the_published_amount(card, expected):
    env = _env(card)
    # team 1's frame: its enemy's left Princess Tower is at (3, 27)
    env.step_self_play(NOOP, 0.0, 0.0, 0, 3.0, 27.0, 1)
    assert _tower_hits(env, 80) == expected


def test_the_miner_hits_a_crown_tower_for_a_fifth_and_a_building_in_full():
    env = _env(52)
    env.step_self_play(NOOP, 0.0, 0.0, 0, 3.0, 25.0, 1)
    hits = _tower_hits(env, 120)
    assert len(hits) >= 5 and set(hits) == {38}      # 194 x 20%, truncated

    cannon = [129, 168, 125, 15, 40, 72, 33, 7]
    env = _env(52, cannon)
    env.set_elixir_for_team(0, 10.0)
    assert env.set_hand_for_team(0, cannon[:4])
    env.step_self_play(0, 8.0, 10.0, NOOP, 0.0, 0.0, 15)
    building = next(u for u in env.get_units() if u["team"] == 0 and u["building"] and not u["tower"])
    env.step_self_play(NOOP, 0.0, 0.0, 0, 6.0, 23.0, 1)   # two tiles from it
    losses, hp = [], building["hp"]
    for _ in range(150):      # he burrows there first
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        now = next((u["hp"] for u in env.get_units() if u["id"] == building["id"]), None)
        if now is None:
            break
        # a building also decays every tick; a Miner hit is the large step
        if hp - now > 50:
            losses.append(hp - now)
        hp = now
    assert losses and all(190 <= d <= 200 for d in losses), losses


def test_the_goblin_machines_rocket_takes_half_from_a_crown_tower():
    env = _env(55)
    env.step_self_play(NOOP, 0.0, 0.0, 0, 3.0, 15.0, 1)
    hits = _tower_hits(env, 400)
    assert 152 in hits            # the rocket: 304 x 50%
    assert 304 not in hits
    assert 212 in hits            # the body's own hits are not reduced
