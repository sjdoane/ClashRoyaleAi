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
