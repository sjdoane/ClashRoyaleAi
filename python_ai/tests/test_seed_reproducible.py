"""One seed, one match: reset(seed=s) replays a teacher match bit for bit,
whatever the env played before (UPSTREAM_REQUESTS.md item 45).

Before item 45 the UtilityTeacher's generator was never seeded, so its
profile, lane bias and epsilon draws differed between runs of the same seed.
"""
import numpy as np
import pytest

from python_ai.envs.gym_wrapper import MicroRoyaleEnv


def _match(env, seed, steps=60):
    rng = np.random.default_rng(seed)
    obs, _ = env.reset(seed=seed)
    trace = [np.asarray(obs, np.float32).tobytes()]
    for _ in range(steps):
        a = {"card_index": int(rng.integers(5)), "target_x": float(rng.uniform(0, 17)),
             "target_y": float(rng.uniform(0, 14))}
        obs, r, term, trunc, _ = env.step(a)
        trace.append(np.asarray(obs, np.float32).tobytes() + np.float64(r).tobytes())
        if term or trunc:
            break
    return trace


@pytest.mark.parametrize("cfg", [
    {"opponent": "teacher", "teacher_stage": 2},
    {"opponent": "teacher", "teacher_stage": 0, "deck_pool": True},
    {"opponent": "teacher", "teacher_stage": 2, "randomize_opp_deck": True},
    {"opponent": "builtin"},
])
def test_same_seed_same_match(cfg):
    a = MicroRoyaleEnv(dict(cfg, scenario_seed=5))
    b = MicroRoyaleEnv(dict(cfg, scenario_seed=99))
    _match(b, 1234)                      # b has history a lacks
    assert _match(a, 777) == _match(b, 777)


def test_teacher_draws_follow_the_seed():
    env = MicroRoyaleEnv({"opponent": "teacher", "teacher_stage": 2, "scenario_seed": 3})
    picks = []
    for _ in range(2):
        env.reset(seed=42)
        picks.append((env.teacher.lane_bias, id(env.teacher.profile) and sorted(env.teacher.profile.items())))
    assert picks[0] == picks[1]
