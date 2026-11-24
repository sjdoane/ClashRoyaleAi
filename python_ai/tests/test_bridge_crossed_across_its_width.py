"""A bridge is crossed across its width, not through one point (patch 0060).

Until this patch every walker was sent to the centre of its bridge's mouth
(x 2.5 or 14.5) and from there straight across, although the deck is two tiles
wide: a squad crossed in single file and a unit that was already in the lane
stepped sideways to the centre line first.

Nothing published describes the path. The evidence is four tracks of our own
troops in the recordings of 2026-11-20 (runs/live/20261120-093139 and
20261120-185026; x = placement tile + the column shift of the detection box,
good to about 0.4 of a tile; rows converted to engine rows):

  Ice Golem from tile (8, 10) to the left bridge:
      x 4.8, 4.0 on the two rows before the river, 3.5 and 3.4 in it,
      3.3, 3.2, 3.2 after it.            (centre routing: 4.1, 3.1, 2.5)
  Musketeer from tile (9, 9) to the right bridge, two tracks:
      13.3-13.5 through the river.       (centre routing: 14.5)
  Ice Golem placed in the lane on tile (14, 12):
      13.6-13.7 on every row, flat.      (centre routing drifts to 14.5)

So a walker keeps its own line where that line crosses the deck, and otherwise
heads for the nearest point of the mouth, the deck's near corner.
"""
import python_ai  # noqa: F401
import clash_royale_env as E
import pytest

NOOP = 4
DECK = [125, 168, 129, 15, 40, 72, 33, 7]
KNIGHT, GIANT, MUSKETEER, BARBARIANS, ELITE_BARBARIANS, ICE_GOLEM = 0, 2, 6, 8, 17, 40
RIVER_START, RIVER_END = 15.5, 17.5
DECKS = ((1.5, 3.5), (13.5, 15.5))            # the two bridges, edge to edge


def _tracks(card, x, y, team=0, ticks=240):
    """One [(tick, x, y), ...] per unit of the card."""
    env = E.ClashRoyaleEnv(list(DECK), list(DECK), 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    env.inject(card, x, y, team, -1.0, -1)
    tracks = {}
    for k in range(ticks):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        for u in env.get_units():
            if not u["tower"] and u["team"] == team and u["card_id"] == card:
                tracks.setdefault(u["id"], []).append((k + 1, u["x"], u["y"]))
    return list(tracks.values())


def _x_at(track, row, team=0):
    """x on the first tick the unit is at or past `row` (None = never)."""
    for _, x, y in track:
        if (y >= row) if team == 0 else (y <= row):
            return x
    return None


def _tick_at(track, row, team=0):
    for t, _, y in track:
        if (y >= row) if team == 0 else (y <= row):
            return t
    return None


def _on_a_deck(x, slack=1e-3):
    return any(lo - slack <= x <= hi + slack for lo, hi in DECKS)


def test_the_bindings_still_describe_a_two_tile_bridge():
    # The decks above are the engine's, not a second copy to trust blindly.
    half = (DECKS[0][1] - DECKS[0][0]) / 2
    assert DECKS[0][0] + half == pytest.approx(E.ARENA_LEFT_BRIDGE_X)
    assert DECKS[1][0] + half == pytest.approx(E.ARENA_RIGHT_BRIDGE_X)


def test_an_ice_golem_from_the_middle_walks_to_the_near_corner_and_along_the_edge():
    (track,) = _tracks(ICE_GOLEM, 8.0, 10.0)
    # The recorded track, within the 0.4 the detection boxes are good for.
    for row, recorded in ((13.9, 4.8), (14.9, 4.0), (15.5, 3.5), (16.5, 3.4),
                          (18.9, 3.3), (19.9, 3.2), (20.9, 3.2)):
        assert _x_at(track, row) == pytest.approx(recorded, abs=0.4), row
    # The corner itself, and never the centre line.
    assert _x_at(track, RIVER_START) == pytest.approx(3.5, abs=0.02)
    assert min(x for _, x, y in track if y >= RIVER_START and y <= 21.0) > 3.1


def test_a_musketeer_from_the_middle_crosses_on_the_inner_edge_of_the_right_bridge():
    (track,) = _tracks(MUSKETEER, 9.0, 9.0)
    for row in (RIVER_START, 16.5, RIVER_END):
        x = _x_at(track, row)
        assert x == pytest.approx(13.4, abs=0.4), row      # recorded 13.3-13.5
        assert _on_a_deck(x)
    assert _x_at(track, RIVER_START) == pytest.approx(13.5, abs=0.02)


def test_a_unit_placed_in_the_lane_crosses_where_it_stands():
    (track,) = _tracks(ICE_GOLEM, 14.0, 12.0)
    xs = [x for _, x, y in track if y <= 21.0]
    assert max(xs) - min(xs) < 0.1            # recorded: flat on every row
    assert xs[0] == pytest.approx(14.0, abs=0.05)
    # Patch 0087: the outer tile of a deck is not walked (recorded crossings
    # all lie on the tower-side tile, x 2.5-3.5 and 13.5-14.5). A Giant on the
    # outer tile of the left bridge steps in to the edge of that corridor, not
    # to its centre.
    (giant,) = _tracks(GIANT, 2.0, 15.0)
    assert _x_at(giant, RIVER_START) == pytest.approx(2.5, abs=0.1)


def test_a_pair_placed_abreast_crosses_abreast():
    # Elite Barbarians land a tile apart. Through one point the second reached
    # the far bank 0.6 s after the first, on the same x.
    first, second = sorted(_tracks(ELITE_BARBARIANS, 14.5, 12.0), key=lambda t: _tick_at(t, RIVER_END))
    # (0.49 apart since patch 0087 narrowed the walked part to one tile.)
    assert abs(_x_at(first, RIVER_END) - _x_at(second, RIVER_END)) > 0.4
    assert _tick_at(second, RIVER_END) - _tick_at(first, RIVER_END) <= 5      # 4 before patch 0087
    for track in (first, second):
        assert all(_on_a_deck(x) for _, x, y in track if RIVER_START < y < RIVER_END)


def test_five_barbarians_use_the_width_of_the_deck():
    tracks = _tracks(BARBARIANS, 14.5, 12.0)
    assert len(tracks) == 5
    xs = [_x_at(t, RIVER_END) for t in tracks]
    ticks = [_tick_at(t, RIVER_END) for t in tracks]
    assert None not in xs
    assert max(xs) - min(xs) > 1.0            # single file: 0.3
    assert max(ticks) - min(ticks) <= 20      # single file: 29
    for track in tracks:
        assert all(_on_a_deck(x) for _, x, y in track if RIVER_START < y < RIVER_END)


def test_team_one_crosses_the_mirrored_way():
    (ours,) = _tracks(ICE_GOLEM, 8.0, 10.0, 0)
    (theirs,) = _tracks(ICE_GOLEM, 17.0 - 8.0, 33.0 - 10.0, 1)
    for row in (13.0, 14.0, RIVER_START, 16.5, RIVER_END, 19.0, 21.0):
        assert 17.0 - _x_at(theirs, 33.0 - row, 1) == pytest.approx(_x_at(ours, row), abs=0.02), row
        assert _tick_at(theirs, 33.0 - row, 1) == _tick_at(ours, row), row


@pytest.mark.parametrize("team", (0, 1))
def test_a_walker_from_any_column_crosses_on_a_deck_and_does_not_stall(team):
    # Every half column, two rows behind the bank. A Knight (Medium) has at
    # most 6 tiles to a corner and 2 across: 1 s deploy + 8 / 1.33 s = 7 s.
    y = 13.5 if team == 0 else 19.5
    far = RIVER_END if team == 0 else RIVER_START
    for i in range(35):
        x = i * 0.5
        (track,) = _tracks(KNIGHT, x, y, team, ticks=90)
        assert _tick_at(track, far, team) is not None, x
        assert all(_on_a_deck(px) for _, px, py in track if RIVER_START < py < RIVER_END), x
