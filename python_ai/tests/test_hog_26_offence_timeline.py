"""The Hog 2.6 offence, end to end, against published values and two recordings.

Patch 0056 changes no behaviour. It pins what was checked when the simulated
Hog 2.6 side was found to score too few crowns, so that a later change to
movement, tower range or the first hit shows up here:

- Hog Rider (game file: hit speed 1600, load time 1000, speed 120, range 800;
  317 a hit at level 11). In the two recordings of 2026-11-20 a Hog placed on
  the bridge corner (engine tile 16, 15) started moving 1.0 s after it landed
  and the first numeral change on the Princess Tower came 4.2-4.5 s after that,
  then every 1.59-1.69 s, 317 each.
- Ice Spirit (projectile: Freeze, BuffTime 1100, no Crown Tower reduction): a
  tower loses 1.1 s of shooting and takes the full 110.
- Fireball on a Princess Tower 159 (the 8 Sep 2026 note), The Log 35 (-87%).

What is NOT pinned because the engine does not have it: a Hog placed beside
an Ice Golem pushes it ahead in the real game (collision radii 0.6 and 0.7, one
tile apart), so the Golem takes the tower's arrows; here the bodies do not
touch and the Hog is shot first. See the engine notes, patch 0056.
"""
import python_ai  # noqa: F401
import clash_royale_env as E

NOOP = 4
HOG, ICE_GOLEM, ICE_SPIRIT, LOG, FIREBALL = 15, 40, 72, 33, 7
DECK = [125, 168, 129, 15, 40, 72, 33, 7]
RIGHT_PRINCESS = 2


def _match():
    env = E.ClashRoyaleEnv(list(DECK), list(DECK), 3600)
    env.seed(1)
    env.set_tower_hit_log(True)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    return env, env.get_current_tick()


def _run(env, schedule, ticks):
    """Injects team 0 units by tick; returns (tick the Hog first moved, ticks its HP dropped)."""
    moved, drops, last_pos, last_hp = None, [], None, None
    for k in range(ticks):
        for card, x, y in schedule.get(k, []):
            env.inject(card, x, y, 0, -1.0, -1)
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        hog = [u for u in env.get_units() if u["card_id"] == HOG and u["team"] == 0]
        if not hog:
            continue
        pos, hp = (hog[0]["x"], hog[0]["y"]), hog[0]["hp"]
        if moved is None and last_pos is not None and pos != last_pos:
            moved = k + 1
        if last_hp is not None and hp < last_hp:
            drops.append(k + 1)
        last_pos, last_hp = pos, hp
    return moved, drops


def _hits(env, t0, card):
    return [(h[0] - t0, h[6]) for h in env.get_tower_hits() if h[1] == 0 and h[2] == card]


def test_a_lone_hog_from_the_bridge_corner_hits_the_tower_on_the_recorded_timeline():
    env, t0 = _match()
    moved, _ = _run(env, {0: [(HOG, 16.0, 15.0)]}, 200)
    hits = _hits(env, t0, HOG)
    stats = E.get_card_stats(HOG)["base"]
    assert moved == stats["deploy_ticks"] + 1 == 11           # 1.0 s deploy
    first = hits[0][0] - moved
    assert 40 <= first <= 45                                  # recordings: 4.2-4.5 s
    gaps = {b[0] - a[0] for a, b in zip(hits, hits[1:])}
    assert gaps == {stats["attack_cooldown_ticks"]} == {16}   # 1.6 s, recordings 1.59-1.69
    assert {amount for _, amount in hits} == {stats["damage"]} == {317}
    assert len(hits) >= 6                                     # 16 arrows of 109 kill it


def test_an_ice_spirit_costs_the_tower_its_published_1_1_s_and_takes_full_damage():
    alone, t0 = _match()
    _, plain = _run(alone, {0: [(HOG, 16.0, 15.0)]}, 130)
    env, t0 = _match()
    # The spirit two tiles to the side: on the Hog's own tile the two bodies
    # (0.6 and 0.4, patch 0057) overlap and the Hog is pushed off its line.
    _, frozen = _run(env, {0: [(HOG, 16.0, 15.0)], 5: [(ICE_SPIRIT, 14.0, 15.0)]}, 130)
    spirit = _hits(env, t0, ICE_SPIRIT)
    freeze = E.get_card_stats(ICE_SPIRIT)["base"]["on_hit"]
    assert freeze["type"] == "freeze" and freeze["ticks"] == 11
    assert [amount for _, amount in spirit] == [110]
    landed = spirit[0][0]
    # Every arrow lands when it would have, or the freeze later; the only
    # arrow after the spirit that is on time is one already in the air.
    late = [f - p for f, p in zip(frozen, plain)]
    assert len(late) >= 8 and set(late) == {0, freeze["ticks"]} and late == sorted(late)
    first_late = late.index(freeze["ticks"])
    assert plain[first_late] > landed
    assert sum(1 for t in plain[:first_late] if t > landed) <= 1


def test_fireball_and_log_chip_a_princess_tower_for_159_and_35():
    for card, x, y, amount in ((FIREBALL, 14.0, 27.0, 159), (LOG, 14.0, 17.0, 35)):
        env, t0 = _match()
        assert env.set_hand_for_team(0, [card, HOG, ICE_GOLEM, ICE_SPIRIT])
        env.set_elixir_for_team(0, 10.0)
        before = env.get_tower_hp(1, RIGHT_PRINCESS)
        env.step_self_play(0, x, y, NOOP, 0.0, 0.0, 1)
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 80)
        assert before - env.get_tower_hp(1, RIGHT_PRINCESS) == amount
        assert [a for _, a in _hits(env, t0, card)] == [amount]
