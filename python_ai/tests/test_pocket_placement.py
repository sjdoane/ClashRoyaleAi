"""The pocket: ground that opens on the enemy side when a Princess Tower falls.

GameManager::inPocket (2026-11-23). MEASURED on 19 recorded live matches, not
published: every placement made past the river after a tower fell was on the
fallen tower's half of the width and none was deeper than engine row 11 of the
loser's half (row 22 seen from team 0).
"""
import python_ai  # noqa: F401
import clash_royale_env as E
import pytest

DECK = [15, 6, 25, 40, 24, 72, 33, 7]
KNIGHT, CANNON, FIREBALL, LOG, BARB_BARREL = 6, 25, 7, 33, 101   # 6 is the Musketeer: any troop


def _env():
    g = E.ClashRoyaleEnv(list(DECK), list(DECK), 3600)
    g.seed(1)
    return g


def _cells(g, card, team):
    return {(x, y) for y in range(34) for x in range(18)
            if g.is_valid_placement(card, float(x), float(y), team)}


@pytest.mark.parametrize("card", [KNIGHT, CANNON])
def test_nothing_past_the_river_while_both_towers_stand(card):
    g = _env()
    assert max(y for _, y in _cells(g, card, 0)) <= 15
    assert min(y for _, y in _cells(g, card, 1)) >= 18


@pytest.mark.parametrize("slot,lane,bridge", [(1, range(0, 9), (2, 3)),
                                               (2, range(9, 18), (14, 15))])
def test_a_fallen_tower_opens_its_own_half_of_the_width(slot, lane, bridge):
    g = _env()
    before = _cells(g, KNIGHT, 0)
    assert g.destroy_tower(1, slot)
    gained = _cells(g, KNIGHT, 0) - before
    assert gained
    assert all(x in lane for x, _ in gained)
    assert all(16 <= y <= 22 for _, y in gained)          # never deeper than row 22
    assert {(x, y) for x, y in gained if y in (16, 17)} == \
        {(x, y) for x in bridge for y in (16, 17)}          # river: the bridge only
    for y in range(18, 23):
        assert (lane[4], y) in gained


def test_team_1_gets_the_mirror():
    g = _env()
    before = _cells(g, KNIGHT, 1)
    assert g.destroy_tower(0, 1)
    gained = _cells(g, KNIGHT, 1) - before
    assert gained and all(x <= 8 and 11 <= y <= 17 for x, y in gained)
    assert (4, 11) in gained and (4, 10) not in gained


def test_both_towers_down_opens_the_whole_width():
    g = _env()
    g.destroy_tower(1, 1), g.destroy_tower(1, 2)
    cells = _cells(g, KNIGHT, 0)
    assert all((x, 20) in cells for x in range(18))
    assert not any(y > 22 for _, y in cells)


def test_a_building_and_a_roller_may_use_it_and_spells_are_unchanged():
    g = _env()
    fire = _cells(g, FIREBALL, 0)
    assert not g.is_valid_placement(BARB_BARREL, 4.0, 20.0, 0)
    assert not g.is_valid_placement(LOG, 4.0, 20.0, 0)
    g.destroy_tower(1, 1)
    assert g.is_valid_placement(CANNON, 4.0, 20.0, 0)
    assert g.is_valid_placement(BARB_BARREL, 4.0, 20.0, 0)
    assert g.is_valid_placement(LOG, 4.0, 20.0, 0)
    assert not g.is_valid_placement(BARB_BARREL, 13.0, 20.0, 0)   # the other lane
    assert not g.is_valid_placement(LOG, 4.0, 24.0, 0)            # past the pocket
    assert _cells(g, FIREBALL, 0) == fire


def test_a_troop_played_in_the_pocket_lands_there():
    g = _env()
    g.destroy_tower(1, 1)
    g.set_elixir_for_team(0, 10.0)
    assert g.set_hand_for_team(0, [6, 15, 25, 40])
    g.step_self_play(0, 4.0, 20.0, 4, 0.0, 0.0, 1)
    mine = [u for u in g.get_units() if not u["tower"] and u["team"] == 0]
    assert len(mine) == 1
    assert abs(mine[0]["x"] - 4.0) < 0.6 and abs(mine[0]["y"] - 20.0) < 0.6


def test_the_same_play_is_refused_while_the_tower_stands():
    g = _env()
    g.set_elixir_for_team(0, 10.0)
    assert g.set_hand_for_team(0, [6, 15, 25, 40])
    g.step_self_play(0, 4.0, 20.0, 4, 0.0, 0.0, 1)
    assert not [u for u in g.get_units() if not u["tower"] and u["team"] == 0]
