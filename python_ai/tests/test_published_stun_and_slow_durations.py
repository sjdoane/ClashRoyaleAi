"""Stun and slow durations follow the published buff times (patch 0051).

The game data (FirstLight-Zero card_specs.json / projectiles.json, release
15.535.13) names the buff a card applies and for how long. `ZapFreeze` and
`Freeze` are full stops (factor 0), `IceWizardSlowDown` is the Ice Wizard's
35 % slow (factor 0.65), whoever applies it. The official notes after the
release changed two of the times (Freeze 4 -> 3.5 s, Ice Golem 2 -> 2.5 s, both
8 Sep 2026).
"""
import python_ai  # noqa: F401
import clash_royale_env as E

# card id, form, path to the effect, published buff, ticks, factor
PUBLISHED = [
    (29, "base", ("spell_on_hit",), "Zap: ZapFreeze 500 ms", 5, 0.0),
    (124, "base", ("spell_on_hit",), "Zap (Evolution's base form): ZapFreeze 500 ms", 5, 0.0),
    (124, "evolved", ("spell_on_hit",), "Zap Evolution: ZapFreeze 500 ms", 5, 0.0),
    (31, "base", ("spell_on_hit",), "Lightning: LighningSpell ZapFreeze 500 ms", 5, 0.0),
    (75, "base", ("on_hit",), "Electro Spirit: ZapFreeze 500 ms", 5, 0.0),
    (79, "base", ("on_hit",), "Zappies: BuffOnDamage ZapFreeze 500 ms", 5, 0.0),
    (57, "base", ("on_hit",), "Electro Dragon: ZapFreeze 500 ms", 5, 0.0),
    (72, "base", ("on_hit",), "Ice Spirit: Freeze 1100 ms", 11, 0.0),
    (34, "base", ("on_hit",), "Ice Wizard: IceWizardSlowDown 2500 ms", 25, 0.65),
    (183, "base", ("on_hit",), "Hero Ice Wizard: as the Ice Wizard", 25, 0.65),
    (100, "base", ("spell_on_hit",), "Giant Snowball: IceWizardSlowDown 3000 ms", 30, 0.65),
    (158, "base", ("spell_on_hit",), "Giant Snowball (Evolution's base form)", 30, 0.65),
    (107, "base", ("spell_on_hit",), "Freeze: 3.5 s since 8 Sep 2026 (release 4000 ms)", 35, 0.0),
    (40, "base", ("death_effect", "on_hit"), "Ice Golem: slow 2.5 s since 8 Sep 2026", 25, None),
]


def test_stun_and_slow_durations_are_the_published_buff_times():
    wrong = []
    for cid, form, path, source, ticks, factor in PUBLISHED:
        node = E.get_card_stats(cid)[form]
        for key in path:
            node = node.get(key) if isinstance(node, dict) else None
        if not isinstance(node, dict) or node.get("type") != "freeze":
            wrong.append((cid, source, "no stun or slow at all", node))
            continue
        if node["ticks"] != ticks or (factor is not None and abs(node["slow_factor"] - factor) > 1e-6):
            wrong.append((cid, source, node))
    assert not wrong, "\n".join(map(str, wrong))


def test_a_stun_is_a_full_stop_and_the_ice_wizards_slow_is_one_strength():
    assert E.get_card_stats(29)["base"]["spell_on_hit"]["slow_factor"] == 0.0
    ice_wizard = E.get_card_stats(34)["base"]["on_hit"]["slow_factor"]
    snowball = E.get_card_stats(100)["base"]["spell_on_hit"]["slow_factor"]
    assert ice_wizard == snowball


def test_only_the_unit_published_with_load_first_hit_lands_unloaded():
    """Patch 0052. Release 15.535.13 has `LoadFirstHit` on one unit, ZapMachine
    (Sparky): she lands unloaded and waits a whole hit speed. Every other unit
    lands loaded and owes only its first-hit time."""
    unloaded = []
    for cid in E.get_all_card_ids():
        stats = E.get_card_stats(cid)
        for form in ("base", "evolved"):
            if stats.get(form) and stats[form].get("lands_unloaded"):
                unloaded.append(stats["name"])
    assert sorted(set(unloaded)) == ["Sparky"]
    sparky = E.get_card_stats(60)["base"]
    assert sparky["attack_cooldown_ticks"] == 40 and sparky["load_ticks"] == 30


# Patch 0053: the units published with IgnorePushback (release 15.535.13).
IGNORE_PUSHBACK = {
    "Bowler", "Cannon Cart", "Dark Prince", "Electro Giant", "Elixir Golem", "Giant", "Giant Skeleton",
    "Goblin Giant", "Goblin Machine", "Goblinstein", "Golem", "Lava Hound", "Mega Knight", "Mighty Miner",
    "P.E.K.K.A.", "Prince", "Royal Giant", "Rune Giant", "Skeleton King", "Sparky",
}
# Published Pushback of the spells, thousandths of a tile.
SPELL_PUSHBACK = {"Fireball": 1000, "Rocket": 1800, "Giant Snowball": 1800}


def test_knockback_immunity_and_distances_follow_the_published_fields():
    immune = set()
    for cid in E.get_all_card_ids():
        stats = E.get_card_stats(cid)
        name = stats["name"]
        if stats["base"].get("ignores_pushback"):
            immune.add(name[5:] if name.startswith("Hero ") else name)
        if name in SPELL_PUSHBACK:
            assert round(stats["base"]["spell_knockback"] * 1000) == SPELL_PUSHBACK[name], name
    assert immune == IGNORE_PUSHBACK
    # The Log: LogProjectileRolling 700, PushbackAll.
    assert round(E.get_card_stats(33)["base"]["roll_knockback"] * 1000) == 700
