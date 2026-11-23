"""The level-11 card table is pinned to the newest published game data (patch 0048).

`data/published_stats_15_535_13.json` is generated from the FirstLight-Zero
`card_specs.json` / `projectiles.json` of release 15.535.13: one row per card,
form and field the engine models (hit points, shield, damage, hit speed, first
hit, speed, range, sight, deploy time, lifetime, count, spell radius and
projectile speed, death damage, charge multiplier, splash), level 11 being
floor(level-1 x 2.56). `unit_proj_speed` is the speed of a ranged troop's
shot (ProjectileSpeeds.h, patch 0049). Rows of the field `collision` (per-card
collision radius) are carried for reference and are pinned by the patch that
models them, if any.

DATING. That release is the game right after the 4 Aug 2026 balance update: it
holds every change of that date and none of the later ones, and the "old"
numbers in the later official notes equal its level-11 values exactly. So the
current game is the table plus the official notes dated after it (26 Aug, 8 Sep,
16 Sep, 6 Oct and 19 Oct 2026). `NOTES` lists those, and the engine must carry
the note's value exactly. `EXCEPTIONS` lists the fields the engine models
differently on purpose, each with its reason; an exception that stops being a
mismatch fails the test, so the list cannot go stale.

A field is a mismatch when it differs by more than 1 % AND by more than one unit
(one unit of damage or hit points, 50 ms, 0.01 tile).
"""
import json
import os

import python_ai  # noqa: F401
import clash_royale_env as E

FIXTURE = os.path.join(os.path.dirname(__file__), "data", "published_stats_15_535_13.json")

# field -> (unit, how the engine exposes it)
FIELDS = {
    "hp": (1, lambda e: e["hp"]),
    "shield": (1, lambda e: e["shield_hp"]),
    "damage": (1, lambda e: e["damage"]),
    "hit_speed_ms": (50, lambda e: e["attack_cooldown_ticks"] * 100),
    "first_hit_ms": (50, lambda e: e["first_hit_windup_ticks"] * 100),
    "speed": (0.5, lambda e: round(e["speed_wiki_tiles_per_min"])),
    "range": (0.01, lambda e: e["attack_range"]),
    "sight": (0.01, lambda e: e["sight_range"]),
    "deploy_ms": (50, lambda e: e["deploy_ticks"] * 100),
    "lifetime_ms": (50, lambda e: e["lifetime_ticks"] * 100),
    "count": (0, lambda e: e["count"]),
    "roll_half_width": (0.01, lambda e: e["roll_width"] / 2.0),
    "radius": (0.01, lambda e: e["spell_radius"]),
    "proj_speed": (1, lambda e: e["spell_projectile_speed"]),
    "death_damage": (1, lambda e: e["death_effect"]["damage"]),
    "charge_mult": (0.01, lambda e: e["charge_multiplier"]),
    "splash": (0.01, lambda e: e["splash_radius"]),
    "proj_radius": (0.01, lambda e: e["splash_radius"]),
    "unit_proj_speed": (1, lambda e: e["projectile_speed"]),
}
NOT_PINNED_HERE = {"collision"}

# (card, field) -> (current value, the official note). Applies to the card's
# Evolution and Hero rows too, not to a second unit of the card. "derived" = the note changes the hit speed and
# the first hit follows from the unchanged load time.
NOTES = {
    ("Archer Queen", "damage"): (232, "26 Aug 2026: 225 -> 232"),
    ("Electro Giant", "damage"): (184, "26 Aug 2026: 163 -> 184"),
    ("Goblinstein", "hp"): (2240, "26 Aug 2026: Monster hit points 2385 -> 2240"),
    ("Ice Golem", "hp"): (1228, "8 Sep 2026: 1315 -> 1228"),
    ("Wizard", "damage"): (304, "8 Sep 2026: 281 -> 304"),
    ("Wizard", "first_hit_ms"): (500, "8 Sep 2026: first attack 0.4 -> 0.5 s"),
    ("Wizard", "shield"): (176, "8 Sep 2026: Evolution shield 192 -> 176"),
    ("Bomber", "damage"): (212, "8 Sep 2026: 225 -> 212"),
    ("Battle Ram", "sight"): (6.5, "8 Sep 2026: 5.5 -> 6.5"),
    ("Golem", "sight"): (7.5, "8 Sep 2026: 7 -> 7.5"),
    ("Ronin", "hit_speed_ms"): (1300, "8 Sep 2026: hit speed 1.4 -> 1.3 s"),
    ("Ronin", "first_hit_ms"): (300, "derived: 1.3 s less the 1.0 s load"),
    ("Zappies", "hit_speed_ms"): (2300, "8 Sep 2026: hit speed 2.2 -> 2.3 s"),
    ("Zappies", "first_hit_ms"): (900, "derived: 2.3 s less the 1.4 s load"),
    ("Fire Spirit", "damage"): (215, "16 Sep 2026: 207 -> 215"),
    ("Lava Hound", "damage"): (71, "6 Oct 2026: 53 -> 71"),
    ("Lava Hound", "hit_speed_ms"): (1500, "6 Oct 2026: hit speed 1.3 -> 1.5 s"),
    ("Lava Hound", "first_hit_ms"): (1200, "derived: 1.5 s less the 0.3 s load"),
    ("Ram Rider", "damage"): (271, "6 Oct 2026: 250 -> 271"),
    ("Wall Breakers", "damage"): (302, "6 Oct 2026: 281 -> 302"),
    ("X-Bow", "damage"): (61, "6 Oct 2026: 58 -> 61"),
    ("Golden Knight", "damage"): (168, "6 Oct 2026: 161 -> 168"),
    ("Royal Ghost", "hp"): (1152, "6 Oct 2026: 1210 -> 1152"),
    ("Three Musketeers", "hp"): (906, "6 Oct 2026: 883 -> 906"),
    ("Elixir Collector", "lifetime_ms"): (110000, "6 Oct 2026: lifetime 93 -> 110 s"),
    ("Dart Goblin", "damage"): (143, "19 Oct 2026: 151 -> 143"),
    ("Cannon Cart", "damage"): (202, "19 Oct 2026: 212 -> 202"),
    ("Flying Machine", "damage"): (179, "19 Oct 2026: 171 -> 179"),
    ("Electro Dragon", "first_hit_ms"): (500, "19 Oct 2026: first hit 0.7 -> 0.5 s"),
}

# (card, form or None for every form, field) -> why the engine differs.
RAMP = "a ramping beam: the registry stores the final stage, the table the first"
SPAWNER = "a spawner: the table's hit speed is its spawn period, modelled as a periodic effect"
EXCEPTIONS = {
    ("Inferno Tower", None, "damage"): RAMP,
    ("Inferno Dragon", None, "damage"): RAMP,
    ("Mighty Miner", None, "damage"): RAMP,
    ("Goblin Curse", None, "damage"): "the table's figure is the whole curse, the registry's is one tick of it",
    ("Suspicious Bush", None, "first_hit_ms"): "a walking kamikaze spawner: it has no attack of its own here",
    ("Suspicious Bush", None, "range"): "as above",
    ("Barbarian Hut", None, "first_hit_ms"): SPAWNER,
    ("Goblin Hut", None, "first_hit_ms"): SPAWNER,
    ("Tombstone", None, "first_hit_ms"): SPAWNER,
    ("Hero Tombstone", None, "first_hit_ms"): SPAWNER,
    ("Goblin Cage", None, "first_hit_ms"): SPAWNER,
    ("Goblin Cage", "evolved", "hit_speed_ms"): "the evolved cage pulls and hits; the table row is the plain cage's",
    ("Goblin Cage", "evolved", "range"): "as above",
    ("Goblin Cage", "evolved", "sight"): "as above",
    ("Goblin Drill", None, "hit_speed_ms"): SPAWNER,
    ("Goblins", None, "first_hit_ms"): "the two published values disagree and a recording decides (patch 0038)",
    ("Goblin Gang", "base", "first_hit_ms"): "as Goblins",
    ("Hero Goblins", None, "first_hit_ms"): "as Goblins",
    ("Battle Ram", "evolved", "hit_speed_ms"): "the evolved ram keeps charging; its row is the Barbarians' swing",
    ("Executioner", None, "hit_speed_ms"): "the axe's flight out and back is folded into the hit speed",
    ("Goblin Barrel", None, "radius"): "the landing spread is the spawn offsets (patch 0037), not the spell radius",
    ("Hunter", None, "splash"): "ten pellets in a cone (each 0.07 wide), modelled as one splash",
    ("Mega Knight", None, "proj_radius"): "2.2 is the jump and the deploy slam (both carry it); the plain hit is the 1.3 of the splash row",
    ("Princess", None, "splash"): "the arrow volley's own radius (2.0, the proj_radius row) governs",
}

# Pins the table rows cannot express: (card id, path into get_card_stats, value, source).
EXTRA = [
    (7, ("base", "spell_crown_tower_damage"), 159, "8 Sep 2026: Fireball Crown Tower damage 172 -> 159"),
    (98, ("base", "spawn_effect_crown_tower_damage"), 20, "6 Oct 2026: Goblin Drill Crown Tower spawn damage 0 -> 20"),
    (111, ("base", "damage"), 384, "4 Aug 2026: Royal Delivery 437 -> 384"),
    (39, ("base", "death_effect", "damage"), 688, "release: Giant Skeleton bomb floor(269 x 2.56)"),
    (99, ("base", "lifetime_ticks"), 1100, "6 Oct 2026: Elixir Collector lifetime 110 s"),
    (176, ("base", "damage"), 153, "19 Oct 2026: Minion Giant 168 -> 153"),
    (176, ("base", "attack_cooldown_ticks"), 17, "6 Oct 2026: Minion Giant hit speed 1.5 -> 1.7 s"),
    (129, ("evolved", "spawn_effect_damage"), 261, "6 Oct 2026: Cannon Evolution barrage 281 -> 261"),
    (40, ("base", "death_effect", "damage"), 84, "release: Ice Golem death damage floor(33 x 2.56)"),
]


def _base_name(label):
    for prefix in ("evo ", "Hero "):
        if label.startswith(prefix):
            label = label[len(prefix):]
    return label


def _engine_row(stats, form):
    if form in ("base", "hero-base"):
        return stats["base"]
    if form == "evolved":
        return stats["evolved"]
    assert form == "second", form
    return stats["base"]["secondary_unit"]


def _exception(label, form, field):
    for name in (label, _base_name(label)):
        for f in (form, None):
            if (name, f, field) in EXCEPTIONS:
                return (name, f, field)
    return None


def _rows():
    with open(FIXTURE) as fh:
        return json.load(fh)["rows"]


def test_every_published_field_matches_or_is_a_dated_note_or_a_reasoned_exception():
    rows = _rows()
    assert len(rows) > 1500
    wrong, used_notes, used_exceptions, compared = [], set(), set(), 0
    for cid, label, form, field, published in rows:
        if field in NOT_PINNED_HERE:
            continue
        unit, get = FIELDS[field]
        engine = get(_engine_row(E.get_card_stats(cid), form))
        compared += 1
        diff = abs(engine - published)
        mismatch = diff > 0.01 * abs(published) + 1e-6 and diff > unit + 1e-6
        note = None if form == "second" else NOTES.get((label, field)) or NOTES.get((_base_name(label), field))
        exc = _exception(label, form, field)
        if note is not None:
            used_notes.add((_base_name(label), field) if (_base_name(label), field) in NOTES else (label, field))
            if abs(engine - note[0]) > 1e-6:
                wrong.append((cid, label, form, field, engine, "note says %s (%s)" % note))
        elif exc is not None:
            used_exceptions.add(exc)
            if not mismatch:
                wrong.append((cid, label, form, field, engine, "listed as an exception but it matches %s" % published))
        elif mismatch:
            wrong.append((cid, label, form, field, engine, "published %s" % published))
    assert not wrong, "\n".join(map(str, wrong))
    assert compared > 1500
    assert used_notes == set(NOTES), set(NOTES) - used_notes
    assert used_exceptions == set(EXCEPTIONS), set(EXCEPTIONS) - used_exceptions


def test_the_values_the_table_rows_cannot_express():
    for cid, path, value, source in EXTRA:
        node = E.get_card_stats(cid)
        for key in path:
            node = node[key]
        assert node == value, (cid, path, node, value, source)


def test_the_elixir_collector_pays_one_every_fifteen_seconds_and_one_when_it_goes():
    """6 Oct 2026 notes (lifetime 110 s, production 15 s) and the release's
    ManaOnDeath 1: seven while it stands and one at the end, eight for six."""
    stats = E.get_card_stats(99)["base"]
    assert stats["lifetime_ticks"] == 1100
    assert stats["death_effect"] is not None


def test_the_rascal_girls_and_the_doctor_are_their_own_published_rows():
    girl = E.get_card_stats(113)["base"]["secondary_unit"]
    assert (girl["hp"], girl["damage"], girl["attack_cooldown_ticks"], girl["first_hit_windup_ticks"]) == (261, 125, 10, 5)
    assert round(girl["speed_wiki_tiles_per_min"]) == 60
    doctor = E.get_card_stats(121)["base"]["secondary_unit"]
    assert (doctor["damage"], doctor["first_hit_windup_ticks"]) == (135, 5)


def test_a_hero_without_a_row_of_its_own_loads_like_its_base_card():
    """LoadTimes::find: "Hero X" falls back to X. Seven Heroes had no first-hit
    wind-up at all before."""
    by_name = {}
    for cid in range(1, 190):
        try:
            row = E.get_card_stats(cid)
        except Exception:
            continue
        if not row["is_evolution"]:
            by_name[row["base"]["name"]] = row["base"]
    checked = 0
    for name in ("Balloon", "Valkyrie", "Ice Wizard", "Dark Prince", "Bowler", "Electro Wizard", "Berserker"):
        hero, base = by_name["Hero " + name], by_name[name]
        assert hero["first_hit_windup_ticks"] == base["first_hit_windup_ticks"] > 0, name
        checked += 1
    assert checked == 7


def test_a_ranged_troops_shot_flies_at_its_published_speed():
    """Patch 0049: published speed / 480 tiles a tick, the thrown-spell divisor."""
    rows = [r for r in _rows() if r[3] == "unit_proj_speed"]
    assert len(rows) >= 60
    assert {r[4] for r in rows} >= {170, 400, 500, 550, 600, 700, 800, 1000, 1400}
    musketeer = E.get_card_stats(168)["base"]  # Hero Musketeer: flies like the Musketeer
    assert musketeer["projectile_speed"] == 1000
    assert abs(musketeer["projectile_tiles_per_tick"] - 1000 / 480.0) < 1e-6
    # Projectile records named after the unit that no unit record links.
    assert E.get_card_stats(87)["base"]["secondary_unit"]["projectile_speed"] == 600   # RamRiderBola
    assert E.get_card_stats(55)["base"]["secondary_unit"]["projectile_speed"] == 350   # GoblinMachineRocketProjectile
    # No published projectile: the engine's old 1.5 tiles a tick.
    zappies = E.get_card_stats(79)["base"]
    assert zappies["projectile_speed"] == 0 and zappies["projectile_tiles_per_tick"] == 1.5
