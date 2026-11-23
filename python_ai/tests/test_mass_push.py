"""Two touching troops give way by their published masses (patch 0058).

The game's unit table gives every troop a Mass (FirstLight-Zero
card_specs.json, release 15.535.13): 1 for Skeletons and the spirits, 4 for the
Hog Rider, 6 for the Ice Golem and the Knight, 18 for the giants, 20 for the
Golem; none for a building. The table is include/core/BodySizes.h; the numbers
below are the published ones, not a copy of an engine constant.

The table gives the masses, not the rule. The engine shares the overlap of two
touching troops so that each moves by the OTHER's mass over the sum. The one
pair that can be read off a recording is a Hog Rider behind an Ice Golem
(runs/live/20261120-185026): the Golem is pushed ahead at about 1.6 rows a
second against its own 1.0, is the first body the Princess Tower shoots, and
the Hog's first hit comes 0.45-0.67 s later than a lone Hog's. That recording
does not tell this split from an equal one (see the engine notes).
"""
import python_ai  # noqa: F401
import clash_royale_env as E
import pytest

NOOP = 4
DECK = [125, 168, 129, 15, 40, 72, 33, 7]
SKELETONS, KNIGHT, GIANT, GOBLINS, MUSKETEER, HOG, GOLEM, CANNON, ICE_GOLEM, ICE_SPIRIT = (
    24, 0, 2, 4, 6, 15, 19, 25, 40, 72)

PUBLISHED = [
    # (card id, published Mass)
    (24, 1), (125, 1),                 # Skeletons and their evolution
    (72, 1),                           # Ice Spirit
    (4, 2), (82, 2),                   # Goblins, Royal Hogs
    (15, 4), (17, 4),                  # Hog Rider, Elite Barbarians
    (6, 5), (140, 5), (168, 5),        # Musketeer, evolution, hero
    (0, 6), (40, 6),                   # Knight, Ice Golem
    (57, 7),                           # Electro Dragon
    (117, 10),                         # Skeleton King
    (90, 15),                          # Elixir Golem
    (2, 18), (84, 18),                 # Giant, Electro Giant
    (19, 20),                          # Golem
]


def _match():
    env = E.ClashRoyaleEnv(list(DECK), list(DECK), 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    return env


def _bodies(env, team=0):
    return [u for u in env.get_units() if not u["tower"] and u["team"] == team]


def _one(env, card, team=0):
    found = [u for u in _bodies(env, team) if u["card_id"] == card]
    return found[0] if found else None


@pytest.mark.parametrize("card,mass", PUBLISHED)
def test_a_card_carries_its_published_mass(card, mass):
    base = E.get_card_stats(card)["base"]
    assert base["published_mass"] == mass
    assert base["body_mass"] == pytest.approx(float(mass))
    if E.get_card_info(card)["is_evolution"]:
        return                                   # inject() places base forms only
    env = _match()
    env.inject(card, 9.0, 8.0, 0, -1.0, -1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 2)
    spawned = [u for u in _bodies(env) if u["card_id"] == card]
    assert spawned
    assert {u["body_mass"] for u in spawned} == {float(mass)}


def test_buildings_and_towers_have_no_mass():
    base = E.get_card_stats(CANNON)["base"]
    assert base["published_mass"] == 0
    assert base["body_mass"] == 0.0
    env = _match()
    env.inject(CANNON, 9.0, 8.0, 0, -1.0, -1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 2)
    assert _one(env, CANNON)["body_mass"] == 0.0
    assert {u["body_mass"] for u in env.get_units() if u["tower"]} == {0.0}


def test_the_second_body_of_a_two_body_card_has_its_own_mass():
    # Rascals: the Boy 10, the Girls 2. Goblinstein: the Monster 18, the Doctor 4.
    for card, first, second in ((113, 10.0, 2.0), (121, 18.0, 4.0)):
        env = _match()
        env.inject(card, 9.0, 8.0, 0, -1.0, -1)
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 2)
        masses = {u["card_id"] == card: u["body_mass"] for u in _bodies(env)}
        assert masses == {True: first, False: second}


def test_a_body_with_no_published_record_takes_the_median_five():
    # Golemites have no record in the pack; 5 is the median Mass of its troops.
    env = _match()
    env.inject(GOLEM, 9.0, 16.0, 1, 1.0, -1)       # a Golem on 1 HP
    env.inject(MUSKETEER, 9.0, 12.0, 0, -1.0, 0)
    seen = {}
    for _ in range(80):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        for u in _bodies(env, 1):
            seen[u["name"]] = u["body_mass"]
    assert seen == {"Golem": 20.0, "Golemite": 5.0}


def _push(front_card, back_card, ticks=20):
    """`front_card` walks up the right lane, clear of our towers; `back_card`
    starts right behind it, in contact. Returns how far the front body got with
    the one behind it, and alone."""
    def run(with_back):
        env = _match()
        env.inject(front_card, 14.5, 10.0, 0, -1.0, 0)
        if with_back:
            env.inject(back_card, 14.5, 8.8, 0, -1.0, 0)
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, ticks)
        return _one(env, front_card)["y"]
    return run(True), run(False)


def test_a_hog_rider_pushes_an_ice_golem_ahead_of_it():
    # Very Fast, mass 4, behind Slow, mass 6: the Golem is carried forward
    # (two rows alone in these 2 s, most of a row more with the Hog behind).
    pushed, alone = _push(ICE_GOLEM, HOG)
    assert pushed - alone > 0.6


def test_a_hog_rider_moves_a_golem_much_less():
    # Mass 4 behind mass 20: the Golem takes a sixth of every overlap where the
    # Ice Golem takes two fifths. With an equal split the two were carried the
    # same distance.
    pushed, alone = _push(GOLEM, HOG)
    ice_pushed, ice_alone = _push(ICE_GOLEM, HOG)
    assert 0.0 <= pushed - alone < 0.6 * (ice_pushed - ice_alone)


def _hog_on_the_tower(golem_tile, hog_tile, lead=11):
    """Ice Golem on `golem_tile` (None = no Golem), Hog Rider on `hog_tile`
    `lead` ticks later. Ticks are counted from the Hog's landing."""
    env = _match()
    env.set_tower_hit_log(True)
    start = env.get_current_tick()
    if golem_tile is None:
        lead = 0
    last, first_arrow = {}, {}
    for k in range(420):
        if k == 0 and golem_tile is not None:
            env.inject(ICE_GOLEM, golem_tile[0], golem_tile[1], 0, -1.0, -1)
        if k == lead:
            env.inject(HOG, hog_tile[0], hog_tile[1], 0, -1.0, -1)
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        for card in (HOG, ICE_GOLEM):
            unit = _one(env, card)
            if unit is None:
                continue
            if card in last and unit["hp"] < last[card]:
                first_arrow.setdefault(card, k + 1 - lead)
            last[card] = unit["hp"]
    hits = [h[0] - start - lead for h in env.get_tower_hits() if h[1] == 0 and h[2] == HOG]
    return hits, first_arrow


def test_a_hog_rider_behind_an_ice_golem_in_the_lane_hits_later_and_longer():
    # The recorded pair: the Golem is the first body in the tower's reach and
    # takes its arrows, the Hog's first hit comes 0.45-0.67 s later than a lone
    # Hog's (0.4-1.0 s asked for here: one tile and one seed), and it lands
    # more hits than a lone Hog because the tower is busy.
    lone_hits, lone_arrow = _hog_on_the_tower(None, (15.0, 14.0))
    hits, arrow = _hog_on_the_tower((15.0, 15.0), (15.0, 14.0))
    assert 4 <= hits[0] - lone_hits[0] <= 10
    assert arrow[ICE_GOLEM] < lone_arrow[HOG] + 6          # the tower opens on the Golem
    assert arrow[HOG] - arrow[ICE_GOLEM] >= 50             # and stays on it
    assert len(hits) >= len(lone_hits) + 3
