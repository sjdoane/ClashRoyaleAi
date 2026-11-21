"""Per-team card and Crown Tower level (UPSTREAM_REQUESTS.md item 42)."""
import clash_royale_env as E

DECK = [0, 1, 2, 3, 4, 5, 6, 7]


def test_level_11_is_the_default_and_leaves_the_engine_unscaled():
    env = E.ClashRoyaleEnv(DECK, DECK, 3600)
    env.reset()
    assert env.get_team_card_level(0) == env.get_team_card_level(1) == 11
    assert env.get_tower_max_hp(1, 1) == env.get_tower_max_hp(0, 1)


def test_an_over_levelled_opponent_has_stronger_towers_from_the_next_match():
    env = E.ClashRoyaleEnv(DECK, DECK, 3600)
    env.reset()
    princess11 = env.get_tower_max_hp(1, 1)
    assert env.set_team_level(1, 15)
    assert env.get_team_tower_level(1) == 15
    env.reset()
    # 372 / 256: the published level-15 multiplier over level 11.
    assert env.get_tower_max_hp(1, 1) == round(princess11 * 372 / 256)
    assert env.get_tower_max_hp(0, 1) == princess11
    assert not env.set_team_level(1, 17)
    assert not env.set_team_level(3, 12)
    assert env.get_team_card_level(1) == 15
