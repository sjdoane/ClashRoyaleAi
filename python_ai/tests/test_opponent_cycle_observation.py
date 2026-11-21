"""The opponent card-cycle blocks, from the Python side.

`tests/core/test_card_cycle_observation.cpp` pins the engine. This pins what
the C++ suite cannot see: `MicroRoyaleNet` sizes its scalar input from the
engine, and every consumer of the observation tail still reads the tail now
that the cycle blocks sit behind it. A tail offset counted back from the end
would silently read card-recency floats, and `enemy_tower_hp` feeds the
reward's tower potential.
"""
import numpy as np
import pytest

import clash_royale_env
from python_ai import engine_constants as EC
from python_ai.envs import gym_wrapper
from python_ai.models.net import MicroRoyaleNet

_E = clash_royale_env.ClashRoyaleEnv
CE = clash_royale_env.ClashRoyaleEnv
DECK = list(gym_wrapper.DEFAULT_DECK)


@pytest.fixture(scope="module")
def env():
    e = CE(DECK, DECK, 3600)
    e.reset()
    return e


def _obs(e, team=0):
    return np.asarray(e.get_observation_for_team(team), dtype=np.float32)


def test_the_engine_exposes_the_cycle_block_geometry():
    """Bound, not restated."""
    assert _E.NUM_CYCLE_BLOCKS == 2
    assert _E.CYCLE_BLOCK_SIZE == _E.NUM_CYCLE_BLOCKS * _E.NUM_CARD_IDS
    assert _E.CYCLE_RECENCY_TAU_TICKS > 0


def test_observation_size_grew_by_exactly_the_cycle_blocks(env):
    obs = _obs(env)
    assert len(obs) == env.observation_size()
    expected = (_E.BOARD_WIDTH * _E.BOARD_HEIGHT * _E.NUM_CHANNELS
                + 1 + _E.HAND_SIZE + _E.HAND_SIZE * _E.NUM_CARD_IDS
                + _E.NUM_EXTRA_SCALARS + _E.CYCLE_BLOCK_SIZE
                + _E.NUM_LEVEL_SCALARS           # item 44, appended after
                + _E.OPP_STATE_SIZE)             # item 46, appended after that
    assert env.observation_size() == expected


def test_the_network_sizes_its_scalar_input_from_the_engine():
    """The net absorbs the new block automatically, rather than failing on shape
    or (worse) loading a stale checkpoint that feeds cycle floats into weights
    fitted without them.
    """
    net = MicroRoyaleNet(num_ability_slots=0)
    assert net.cycle_block_size == _E.CYCLE_BLOCK_SIZE
    assert net.spatial_size + net.scalar_size == CE(DECK, DECK, 100).observation_size()
    # The blocks sit after the extra scalars, so the offset into the tail is
    # unmoved.
    assert net.cycle_start == net.extra_start + net.num_extra_scalars


def test_the_tower_hp_scalars_still_read_as_tower_hp(env):
    """The regression that would have been silent. On a fresh board the tower
    scalars are strictly positive while the card-recency floats are all zero,
    so an offset that slid into the cycle blocks fails immediately.
    """
    obs = _obs(env)
    start = EC.EXTRA_SCALARS_START
    own = obs[start + 3:start + 6]
    enemy = obs[start + 6:start + 9]
    assert np.all(own > 0.0), f"own tower HP scalars read {own}, expected all > 0"
    assert np.all(enemy > 0.0), f"enemy tower HP scalars read {enemy}"
    # Each is hp over that tower's own max (UPSTREAM item 44): 1.0 when full.
    assert np.allclose(own, 1.0) and np.allclose(enemy, 1.0), (own, enemy)


def test_a_fresh_board_has_an_entirely_empty_cycle(env):
    e = CE(DECK, DECK, 3600)
    e.reset()
    obs = _obs(e)
    block = obs[EC.CYCLE_START:EC.CYCLE_START + _E.CYCLE_BLOCK_SIZE]
    assert block.shape[0] == _E.CYCLE_BLOCK_SIZE
    assert np.all(block == 0.0), "nothing has been played, so nothing may be seen"


def test_a_noted_play_appears_in_the_opponents_view_only():
    """`note_played_card` lands in the other team's observation, never the
    player's own (the agent already sees its own hand).
    """
    e = CE(DECK, DECK, 3600)
    e.reset()
    card = DECK[0]
    e.note_played_card(1, card)

    seen_by_0 = _obs(e, 0)[EC.CYCLE_START + card]
    seen_by_1 = _obs(e, 1)[EC.CYCLE_START + card]
    assert seen_by_0 == 1.0, "team 0 must see what team 1 played"
    assert seen_by_1 == 0.0, "team 1 must not be handed its own cycle here"


def test_recency_decays_and_seen_does_not():
    e = CE(DECK, DECK, 3600)
    e.reset()
    card = DECK[0]
    e.note_played_card(1, card)
    played_at = e.get_last_played_tick(1, card)
    assert played_at >= 0

    tau = int(_E.CYCLE_RECENCY_TAU_TICKS)
    rec_idx = EC.CYCLE_START + _E.NUM_CARD_IDS + card

    e.set_current_tick(played_at)
    assert _obs(e)[rec_idx] == pytest.approx(1.0)

    e.set_current_tick(played_at + tau)
    assert _obs(e)[rec_idx] == pytest.approx(np.exp(-1.0), abs=1e-4)
    assert _obs(e)[EC.CYCLE_START + card] == 1.0, "seen must not decay"


def test_every_cycle_value_stays_in_the_unit_interval():
    """Every other channel is in [0, 1]; these must be too."""
    e = CE(DECK, DECK, 3600)
    e.reset()
    for i, card in enumerate(DECK):
        e.note_played_card(1, card)
        e.set_current_tick(i * 37)
    block = _obs(e)[EC.CYCLE_START:EC.CYCLE_START + _E.CYCLE_BLOCK_SIZE]
    assert np.all(np.isfinite(block))
    assert block.min() >= 0.0 and block.max() <= 1.0


def test_an_evolution_or_hero_play_lights_the_card_live_perception_reports():
    """Patch 0014: the cycle block is keyed by the card the screen shows. Live
    `OppCycleMirror` notes the base id (all YOLO reports); a simulated
    Evolution or Hero play must light the same index, or the policy reads a
    slot in training that is never lit live.
    """
    face = clash_royale_env.face_card_id_of
    evos = clash_royale_env.get_evolution_card_ids()
    heroes = [c for c in clash_royale_env.get_all_card_ids()
              if clash_royale_env.get_card_info(c)["is_hero"]]
    for card in (evos[0], heroes[0]):
        base = face(card)
        assert base != card
        assert clash_royale_env.get_card_info(base)["name"].replace(".", "").replace(" ", "").lower() \
            in clash_royale_env.get_card_info(card)["name"].replace(".", "").replace(" ", "").lower()
        e = CE(DECK, DECK, 3600)
        e.reset()
        e.note_played_card(1, card)
        o = _obs(e, 0)
        assert o[EC.CYCLE_START + base] == 1.0
        assert o[EC.CYCLE_START + card] == 0.0
        # The getter folds its query, so a poller keyed by deck ids still sees it.
        assert e.get_last_played_tick(1, card) == e.get_last_played_tick(1, base) >= 0


def test_evolution_ids_come_from_the_engine():
    """No Python copy of the Evolution id range (it moved in 0014)."""
    from python_ai import deck
    evos = clash_royale_env.get_evolution_card_ids()
    assert evos == sorted(evos)
    assert all(clash_royale_env.get_card_info(c)["is_evolution"] for c in evos)
    assert not any(clash_royale_env.get_card_info(c)["is_evolution"]
                   for c in clash_royale_env.get_all_card_ids())
    name = clash_royale_env.get_card_info(evos[-1])["name"]
    assert deck._lookup("evo:" + name) == evos[-1]
    assert not hasattr(deck, "_EVOLUTION_IDS")
