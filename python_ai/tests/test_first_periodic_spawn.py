"""The first periodic spawn comes at the published SpawnStartTime (patch 0083).

Every periodic spawner fired first only after one whole interval: a Goblin
Drill's first Goblin came out 4 s after the Drill was placed and stabbed at
5.4 s, a Witch's first Skeletons 8 s after she was played. Published
(RoyaleAPI cr-api-data cards_stats.json): GoblinDrill spawn_start_time 1000,
spawn_pause_time 3000; Witch 1000 / 7000; DarkWitch (Night Witch) 1000 / 5000.

Recorded (runs/live/20261123-104801, three Goblin Drills on our Princess
Tower): the first Goblin's first stab lands 2.1-2.4 s after the Drill's deploy
stopwatch appears and the next Goblin follows about 3 s later. The Drill's
Goblin keeps its 1 s deploy here, so the Drill fires on the first tick after
its own deploy and the first stab comes 2.5-3.0 s after the play, depending on
how far the Goblin has to step. Before: 5.4-5.9 s.
"""
import python_ai  # noqa: F401
import clash_royale_env as E

ID = {E.get_card_info(c)["name"]: c for c in E.get_all_card_ids()}
NOOP = 4
REST = [4, 12, 112, 61, 20, 14, 2]
PLAIN = [15, 6, 25, 40, 24, 72, 33, 7]
MIRROR_Y = E.ClashRoyaleEnv.BOARD_HEIGHT - 1


def _play(card, x, y, team=1):
    deck = [ID[card]] + REST
    decks = (PLAIN, deck) if team == 1 else (deck, PLAIN)
    env = E.ClashRoyaleEnv(list(decks[0]), list(decks[1]), 3600)
    env.seed(1)
    env.set_tower_hit_log(True)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    env.set_elixir_for_team(team, 10.0)
    assert env.set_hand_for_team(team, deck[:4])
    spent = env.get_elixir_spent(team)
    if team == 1:
        env.step_self_play(NOOP, 0.0, 0.0, 0, x, y, 1)
    else:
        env.step_self_play(0, x, y, NOOP, 0.0, 0.0, 1)
    assert env.get_elixir_spent(team) > spent, f"{card} was refused"
    return env


def _tick(env):
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)


def _drill(dx, dy):
    """(ticks from the play to each 125 stab on the defender's tower) for one
    unanswered Goblin Drill placed (dx, dy) from our right Princess Tower."""
    env = _play("Goblin Drill", 14.0 + dx, MIRROR_Y - (E.arena_princess_y(0) + dy))
    n0 = len(env.get_tower_hits())
    for _ in range(140):
        _tick(env)
    return [h[0] - 21 for h in env.get_tower_hits()[n0:] if h[4] == 0 and h[6] == 125]


def test_the_drills_first_goblin_stabs_within_three_seconds():
    for cell in ((0, 3), (2, 2), (-2, 2), (0, -3), (2.5, 0)):
        stabs = _drill(*cell)
        assert 22 <= stabs[0] <= 30, (cell, stabs[:4])      # recorded 2.1-2.4 s
        # the same Goblin again one hit speed (1.1 s) later, before the second
        # Goblin (out 3 s after the first) can join in
        assert [b - a for a, b in zip(stabs, stabs[1:3])] == [11, 11], (cell, stabs[:5])


def _first_spawn(card, child):
    """Ticks from the play to the first `child` body on the board, and to the
    second batch."""
    env = _play(card, 9.0, 8.0, team=0)
    seen, first, second = set(), None, None
    for t in range(1, 140):
        _tick(env)
        ids = {u["id"] for u in env.get_units() if u["team"] == 0 and u["name"] == child}
        new = ids - seen
        if new and first is None:
            first = t
        elif new and second is None and t > first + 5:
            second = t
        seen |= ids
    return first, second


def test_the_witch_raises_her_first_skeletons_one_second_after_deploying():
    first, second = _first_spawn("Witch", "Skeletons")
    assert 19 <= first <= 22, first                         # 1 s deploy + 1 s
    assert second - first == 70                             # then every 7 s


def test_the_night_witch_releases_her_first_bats_one_second_after_deploying():
    first, second = _first_spawn("Night Witch", "Bats")
    assert 19 <= first <= 22, first
    assert second - first == 50
