"""The Miner stays a card the teacher sends at a tower (patch 0047).

With the published Crown Tower reduction the Miner takes 101 tower HP per
elixir alone, and a deck that also holds Wall Breakers (302) resolves to those
as its win condition. Without a rule for it that would have made the Miner a
plain troop, which `_cells_for` only ever offers a cell on its own half: over 4
matches of each deck below the teacher then played 20 and 50 Miners and not one
of them at an enemy tower (29 of 30 and 31 of 31 before the correction).

The rule is general: a troop the engine lets us place on the enemy half, and
that takes tower HP from there, is offered the cell beside the weaker enemy
tower whatever role the deck gives it.
"""
import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))

import python_ai  # noqa: E402,F401
import clash_royale_env as E  # noqa: E402
from python_ai import engine_constants as EC  # noqa: E402
from python_ai.opponents import teacher as T  # noqa: E402
from python_ai.opponents.deck_pool import load_pool  # noqa: E402

MINER = 52
MATCHES = 3


@pytest.fixture(scope="module")
def pool():
    return {d.name: list(d.card_ids) for d in load_pool()}


def _miner_plays(deck, opponent, seed):
    """(plays, plays beside an enemy Crown Tower) of the Miner by team 0."""
    env = E.ClashRoyaleEnv(deck, opponent, 3600)
    env.seed(seed)
    env.reset()
    ours = T.UtilityTeacher(deck, team=0, seed=seed)
    ours.reset()
    theirs = T.UtilityTeacher(opponent, team=1, seed=seed + 1000)
    theirs.reset()
    towers = [(float(int(EC.LEFT_LANE_X)), float(EC.princess_y(1))),
              (float(int(EC.RIGHT_LANE_X)), float(EC.princess_y(1)))]
    plays = beside = 0
    while not env.is_game_over():
        o0 = np.asarray(env.get_observation_for_team(0), np.float32)
        o1 = np.asarray(env.get_observation_for_team(1), np.float32)
        hand = list(env.get_hand_for_team(0))
        a0 = ours.act(env, o0)
        a1 = theirs.act(env, o1)
        if a0[0] < len(hand) and hand[a0[0]] == MINER:
            plays += 1
            beside += any(abs(a0[1] - tx) <= 2.0 and abs(a0[2] - ty) <= 5.0
                          for tx, ty in towers)
        env.step_self_play(a0[0], a0[1], a0[2], a1[0], a1[1], a1[2], 10)
    return plays, beside


@pytest.mark.parametrize("deck_name", ["miner_poison_control", "wall_breakers_cycle"])
def test_the_teacher_still_sends_its_miner_at_an_enemy_tower(pool, deck_name):
    deck = pool[deck_name]
    roles = T.card_roles(deck)
    assert roles[MINER] != "wincon", "the premise: the deck resolves to another card"
    plays = beside = 0
    for seed in range(MATCHES):
        p, b = _miner_plays(deck, pool["hog_26_mirror"], seed)
        plays += p
        beside += b
    # regularly: at least twice a match, and at least two Miners in five
    assert beside >= 2 * MATCHES, (plays, beside)
    assert beside * 5 >= plays * 2, (plays, beside)


def test_a_deploy_anywhere_troop_is_offered_the_cell_beside_the_tower(pool):
    deck = pool["wall_breakers_cycle"]
    t = T.UtilityTeacher(deck, team=0, horizon_ticks=0, k_cells=2)
    t.reset()
    env = E.ClashRoyaleEnv(deck, deck, 3600)
    env.reset()
    obs = np.asarray(env.get_observation_for_team(0), np.float32)
    cells = t._cells_for(t.roles[MINER], MINER, obs)
    assert cells and cells[0][1] > float(EC.princess_y(1)) - 6.0, cells
    # control: a troop that must cross the bridge is not
    walker = next(c for c, r in t.roles.items()
                  if r in ("melee", "ranged") and c != MINER)
    assert all(y <= 17.0 for _x, y in t._cells_for(t.roles[walker], walker, obs))
