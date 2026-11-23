"""Published body radius of every troop and building (patch 0057).

The game's unit table gives each body a CollisionRadius in thousandths of a
tile (FirstLight-Zero card_specs.json, release 15.535.13): 400 for the spirits
and Wall Breakers, 500 for most troops, 600 for the mounted and mid-sized ones,
700 for the Ice Golem, 750 for the giants, 1000 for Sparky and the Skeleton
King; buildings 500 (Tesla, Goblin Drill), 600 (Cannon, Bomb Tower, Inferno
Tower, X-Bow, Mortar), 1000 (huts, Tombstone, Goblin Cage, Elixir Collector).
Before the patch every troop was 0.4 and every building 1.0. The table itself is
include/core/BodySizes.h; the numbers below are the published ones, not a copy
of an engine constant. The Crown Towers have no record in the pack and keep
their 1.5 / 2.0.

One radius serves contact between bodies, attack reach and sight
(CombatEntity::effectiveRadiusOf), so the tests check all three.
"""
import math

import python_ai  # noqa: F401
import clash_royale_env as E
import pytest

NOOP = 4
DECK = [125, 168, 129, 15, 40, 72, 33, 7]
KNIGHT, GIANT, MUSKETEER, HOG, ICE_GOLEM, ICE_SPIRIT, CANNON = 0, 2, 6, 15, 40, 72, 25
SPELLS_AND_NO_RECORD = {"Minion Giant"}

PUBLISHED = [
    # (card id, published CollisionRadius)
    (72, 400), (144, 400),            # Ice Spirit and its evolution
    (5, 450), (170, 450),             # Mini P.E.K.K.A. and its hero
    (0, 500), (135, 500),             # Knight and its evolution
    (24, 500), (125, 500),            # Skeletons
    (6, 500), (140, 500), (168, 500),  # Musketeer, evolution, hero
    (17, 500), (178, 500),            # Elite Barbarians
    (15, 600), (82, 600), (57, 600), (90, 600),   # Hog Rider, Royal Hogs, Electro Dragon, Elixir Golem
    (40, 700),                        # Ice Golem
    (2, 750), (19, 750), (91, 750), (81, 750), (113, 750), (121, 750),
    (117, 1000),                      # Skeleton King
    (26, 500), (154, 500),            # Tesla
    (25, 600), (129, 600), (92, 600),  # Cannon, its evolution, X-Bow
    (96, 1000), (97, 1000),           # Tombstone, Goblin Cage
]


def _match():
    env = E.ClashRoyaleEnv(list(DECK), list(DECK), 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    return env


def _bodies(env, team=None):
    return [u for u in env.get_units() if not u["tower"] and (team is None or u["team"] == team)]


def _dist(a, b):
    return math.hypot(a["x"] - b["x"], a["y"] - b["y"])


@pytest.mark.parametrize("card,thousandths", PUBLISHED)
def test_a_card_carries_its_published_body(card, thousandths):
    base = E.get_card_stats(card)["base"]
    assert base["published_collision_radius"] == thousandths
    assert base["body_radius"] == pytest.approx(thousandths / 1000.0)
    if E.get_card_info(card)["is_evolution"]:
        return                                   # inject() places base forms only
    env = _match()
    env.inject(card, 9.0, 8.0, 0, -1.0, -1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 2)
    spawned = [u for u in _bodies(env, 0) if u["card_id"] == card]
    assert spawned
    assert {round(u["body_radius"], 3) for u in spawned} == {thousandths / 1000.0}


def test_every_troop_and_building_card_has_a_published_row_but_one():
    missing = set()
    for card in range(0, 220):
        try:
            stats = E.get_card_stats(card)
        except Exception:
            continue
        if not stats or E.get_card_info(card)["is_spell"]:
            continue
        if stats["base"]["published_collision_radius"] == 0:
            missing.add(stats["base"]["name"])
    assert missing == SPELLS_AND_NO_RECORD


def test_the_second_body_of_a_two_body_card_has_its_own_radius():
    # Rascals: the Boy 750, the Girls 500. Goblinstein: the Monster 750, the
    # Doctor 500. Both pairs share one registry name.
    for card in (113, 121):
        env = _match()
        env.inject(card, 9.0, 8.0, 0, -1.0, -1)
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 2)
        radii = {u["card_id"] == card: round(u["body_radius"], 3) for u in _bodies(env, 0)}
        assert radii == {True: 0.75, False: 0.5}


def test_a_body_with_no_published_record_takes_the_common_half_tile():
    # Golemites have no record in the pack; 0.5 is the value of 101 of its 158
    # published forms.
    env = _match()
    env.inject(19, 9.0, 16.0, 1, 1.0, -1)          # a Golem on 1 HP
    env.inject(MUSKETEER, 9.0, 12.0, 0, -1.0, 0)
    seen = {}
    for _ in range(80):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        for u in _bodies(env, 1):
            seen[u["name"]] = round(u["body_radius"], 3)
    assert seen == {"Golem": 0.75, "Golemite": 0.5}


def test_the_crown_towers_keep_their_own_bodies():
    env = _match()
    radii = {(u["name"], round(u["body_radius"], 3)) for u in env.get_units() if u["tower"]}
    assert radii == {("King Tower", 2.0), ("Princess Tower", 1.5)}


def test_two_bodies_rest_at_the_sum_of_their_radii():
    # A Hog Rider (0.6) catching an Ice Golem (0.7) in the same lane: 1.3
    # between centres, where the old 0.4 + 0.4 let them close to 0.8.
    env = _match()
    env.inject(ICE_GOLEM, 15.0, 15.0, 0, -1.0, -1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 11)
    env.inject(HOG, 15.0, 13.0, 0, -1.0, -1)
    gaps = []
    for _ in range(60):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        golem = [u for u in _bodies(env, 0) if u["card_id"] == ICE_GOLEM]
        hog = [u for u in _bodies(env, 0) if u["card_id"] == HOG]
        gaps.append(_dist(golem[0], hog[0]))
    assert min(gaps) == pytest.approx(1.3, abs=1e-3)
    assert sum(1 for g in gaps if g < 1.31) >= 20       # in contact, not a glancing touch


def test_reach_on_a_tower_is_range_plus_both_bodies():
    # Hog Rider: 0.8 range + 0.6 body + the Princess Tower's 1.5.
    env = _match()
    env.inject(HOG, 16.0, 15.0, 0, -1.0, -1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 70)
    hog = [u for u in _bodies(env, 0) if u["card_id"] == HOG][0]
    tower = min((u for u in env.get_units() if u["tower"] and u["team"] == 1), key=lambda u: _dist(u, hog))
    reach = E.get_card_stats(HOG)["base"]["attack_range"] + 0.6 + 1.5
    step = 0.27                                          # one tick of a Hog Rider
    assert reach - step <= _dist(hog, tower) <= reach + 1e-3


def test_reach_on_a_building_uses_the_buildings_published_body():
    # Knight: 1.2 range + 0.5 body + the Cannon's 0.6 = 2.3 (2.6 with the old
    # 0.4 and 1.0).
    env = _match()
    env.inject(CANNON, 9.0, 10.0, 1, -1.0, -1)
    env.inject(KNIGHT, 9.0, 6.0, 0, -1.0, 0)
    closest, targeted = 99.0, False
    for _ in range(40):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        knight = [u for u in _bodies(env, 0) if u["card_id"] == KNIGHT]
        cannon = [u for u in _bodies(env, 1) if u["card_id"] == CANNON]
        if not cannon:
            break                                        # our towers shoot it too
        targeted = targeted or knight[0]["target_id"] == cannon[0]["id"]
        closest = min(closest, _dist(knight[0], cannon[0]))
    assert targeted
    reach = E.get_card_stats(KNIGHT)["base"]["attack_range"] + 0.5 + 0.6
    assert reach - 0.14 <= closest <= reach + 1e-3       # one tick of a Knight is 0.13


def test_placement_still_keeps_a_whole_tile_clear_of_a_building():
    # The placement footprint is not the body: Building::COLLISION_RADIUS (1.0)
    # for every building, as before the patch.
    base = E.get_card_stats(CANNON)["base"]
    assert base["body_radius"] == pytest.approx(0.6)
    assert E.get_card_info(CANNON)["placement_radius"] == pytest.approx(1.0)
