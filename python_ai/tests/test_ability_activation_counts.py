"""The engine's own count of ability activations, per team (patch 0081).

`get_elixir_spent` counts cards only and a raised ability flag is not an
activation (not ready, too little elixir, still deploying), so a measurement of
"presses per match" needs the engine's count. No gameplay change: the two
readers expose `MatchStatistics::championAbilityActivations` and
`championAbilityElixirSpent`, which the engine already kept.
"""
import pytest

from test_ability_charges_and_deploy import H, _game, _play, _wait_ready


def test_the_engine_counts_activations_and_their_elixir_per_team():
    env, deck = _game(168)                               # Hero Musketeer, 3 elixir
    assert env.get_ability_activations(0) == 0
    _play(env, deck, 168)
    env.step_self_play(H, 0.0, 0.0, H, 0.0, 0.0, 1, True)   # flag raised while deploying
    assert env.get_ability_activations(0) == 0
    assert _wait_ready(env) is not None
    spent = env.get_elixir_spent(0)
    env.set_elixir_for_team(0, 10.0)
    env.step_self_play(H, 0.0, 0.0, H, 0.0, 0.0, 1, True)
    assert env.get_ability_activations(0) == 1
    assert env.get_ability_activations(1) == 0
    assert env.get_ability_elixir_spent(0) == pytest.approx(3.0)
    assert env.get_elixir_spent(0) == pytest.approx(spent)   # cards only
    env.set_elixir_for_team(0, 10.0)
    env.step_self_play(H, 0.0, 0.0, H, 0.0, 0.0, 1, True)   # the charge is spent
    assert env.get_ability_activations(0) == 1
