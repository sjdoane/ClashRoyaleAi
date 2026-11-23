"""The units of a played multi-unit card deploy one published delay apart
(patch 0059).

The game's card records carry SummonDeployDelay, the time between one unit of
the card and the next (FirstLight-Zero card_specs.json, release 15.535.13):
100 ms for Archers, Minions, Barbarians, Spear Goblins, Minion Horde, Guards,
Three Musketeers, Goblin Gang, Elite Barbarians, Royal Recruits, Bats, Zappies,
Wall Breakers, Royal Hogs and Skeleton Dragons; 200 ms for Goblins; none for
Skeletons and the Skeleton Army. Rascals and Goblinstein carry
SummonDeployDelaySecond 100 for their second body. The numbers below are the
published ones, not a copy of an engine constant.

The reading (unit k finishes deploying k delays after the first) is inferred
from the field name; see the engine notes.
"""
import python_ai  # noqa: F401
import clash_royale_env as E
import pytest

NOOP = 4
DECK = [125, 168, 129, 15, 40, 72, 33, 7]

# (card id, published SummonDeployDelay in ms, units of the card)
STAGGERED = [
    (1, 100, 2),     # Archers
    (41, 100, 3),    # Minions
    (8, 100, 5),     # Barbarians
    (23, 100, 3),    # Spear Goblins
    (42, 100, 6),    # Minion Horde
    (76, 100, 3),    # Guards
    (80, 100, 3),    # Three Musketeers
    (17, 100, 2),    # Elite Barbarians
    (77, 100, 6),    # Royal Recruits
    (78, 100, 5),    # Bats
    (79, 100, 3),    # Zappies
    (83, 100, 2),    # Wall Breakers
    (82, 100, 4),    # Royal Hogs
    (65, 100, 2),    # Skeleton Dragons
    (4, 200, 4),     # Goblins
]
TOGETHER = [(24, 3), (12, 15)]   # Skeletons, Skeleton Army: no published delay


def _match(deck=DECK):
    env = E.ClashRoyaleEnv(list(deck), list(deck), 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    return env


def _bodies(env, team=0):
    return [u for u in env.get_units() if not u["tower"] and u["team"] == team]


def _offsets(units):
    """Deploy ticks left, relative to the unit that is ready first."""
    left = sorted(u["deploy_ticks"] for u in units)
    return [t - left[0] for t in left]


def _inject(card, team=0, deploy_ticks=-1):
    env = _match()
    env.inject(card, 9.0, 10.0 if team == 0 else 23.0, team, -1.0, deploy_ticks)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
    return env


@pytest.mark.parametrize("card,delay_ms,count", STAGGERED)
def test_a_card_with_a_published_delay_deploys_its_units_that_far_apart(card, delay_ms, count):
    ticks = delay_ms // 100
    base = E.get_card_stats(card)["base"]
    assert base["summon_deploy_delay_ticks"] == ticks
    assert base["summon_deploy_delay_second_ticks"] == 0
    for team in (0, 1):
        units = [u for u in _bodies(_inject(card, team), team) if u["card_id"] == card]
        assert len(units) == count
        assert _offsets(units) == [k * ticks for k in range(count)]


@pytest.mark.parametrize("card,count", TOGETHER)
def test_a_card_with_no_published_delay_lands_together(card, count):
    assert E.get_card_stats(card)["base"]["summon_deploy_delay_ticks"] == 0
    units = _bodies(_inject(card))
    assert len(units) == count
    assert _offsets(units) == [0] * count


def test_one_unit_cards_and_buildings_report_no_delay():
    for card in (0, 15, 40, 72, 168, 25, 7, 33, 109, 81):
        base = E.get_card_stats(card)["base"]
        assert (base["summon_deploy_delay_ticks"], base["summon_deploy_delay_second_ticks"]) == (0, 0), card


def test_the_second_body_follows_the_first_by_the_published_second_delay():
    # Rascals: the Boy, then the two Girls 100 ms apart. Goblinstein: the
    # Monster, then the Doctor.
    for card, second in ((113, [1, 2]), (121, [1])):
        assert E.get_card_stats(card)["base"]["summon_deploy_delay_second_ticks"] == 1
        units = _bodies(_inject(card))
        first = [u["deploy_ticks"] for u in units if u["card_id"] == card]
        others = sorted(u["deploy_ticks"] for u in units if u["card_id"] != card)
        assert len(first) == 1
        assert [t - first[0] for t in others] == second


def test_goblin_gang_staggers_its_goblins_and_not_its_spear_goblins():
    # SummonDeployDelay 100 and no SummonDeployDelaySecond.
    units = _bodies(_inject(112))
    goblins = [u for u in units if u["card_id"] == 112]
    spears = [u for u in units if u["card_id"] != 112]
    assert _offsets(goblins) == [0, 1, 2]
    assert _offsets(spears) == [0, 0, 0]
    assert min(u["deploy_ticks"] for u in spears) == min(u["deploy_ticks"] for u in goblins)


def test_each_unit_becomes_active_on_its_own_tick():
    env = _match()
    env.inject(8, 9.0, 10.0, 0, -1.0, -1)            # Barbarians
    ready = {}
    for k in range(1, 21):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        for u in _bodies(env):
            if u["deploy_ticks"] == 0:
                ready.setdefault(u["id"], k)
    assert sorted(ready.values()) == [10, 11, 12, 13, 14]


def test_a_card_played_from_the_hand_is_staggered():
    deck = [8, 77, 82, 80, 42, 17, 78, 4]
    env = E.ClashRoyaleEnv(list(deck), list(deck), 3600)
    env.seed(1)
    env.set_elixir_for_team(0, 10.0)
    card = env.get_hand()[0]
    env.step_self_play(0, 9.0, 10.0, NOOP, 0.0, 0.0, 1)
    units = [u for u in _bodies(env) if u["card_id"] == card]
    ticks = E.get_card_stats(card)["base"]["summon_deploy_delay_ticks"]
    assert ticks in (1, 2) and len(units) >= 2
    assert _offsets(units) == [k * ticks for k in range(len(units))]


def test_units_spawned_by_something_else_are_not_staggered():
    # A Goblin Barrel's Goblins: the barrel is the card, the Goblins are what
    # it spawns, and no delay is published for them.
    env = _match()
    env.inject(109, 9.0, 10.0, 0, -1.0, -1)
    goblins = []
    for _ in range(40):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        goblins = _bodies(env)
        if goblins:
            break
    assert len(goblins) == 3
    assert _offsets(goblins) == [0, 0, 0]
    # A Battle Ram's Barbarians.
    env = _match()
    env.inject(81, 14.5, 24.0, 0, 1.0, 0)            # 1 HP, under the enemy Princess Tower
    barbarians = []
    for _ in range(60):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        barbarians = [u for u in _bodies(env) if u["card_id"] != 81]
        if barbarians:
            break
    assert len(barbarians) == 2
    assert _offsets(barbarians) == [0, 0]


def test_an_injected_deploy_time_overrides_the_stagger():
    # inject(..., deploy_ticks >= 0) is a test hook that sets every unit's
    # deploy time outright.
    units = _bodies(_inject(8, deploy_ticks=0))
    assert [u["deploy_ticks"] for u in units] == [0] * 5
