"""Champion / Hero abilities: when one can be pressed, how often, at what cost.

Patch 0080. Three rules, each checked against two independent sources:

* ONE CHARGE PER DEPLOYMENT, NO COOLDOWN, for every Champion and Hero but Boss
  Bandit (two charges, 3 s apart). Published: card_specs.json of release
  15.535.13 (4 Aug 2026) gives every ability `MaxCharges: 1` and no `Cooldown`,
  Boss Bandit `MaxCharges: 2, Cooldown: 3000`. Recorded: on 60,000 human sides
  holding one such card, no press is repeated on one deployment (Golden Knight 0
  with 7,247 press / redeploy / press sequences, Mighty Miner 0 / 3,778, Hero
  Berserker 0 / 11,299), Boss Bandit 599 repeats, never closer than 3.9 s.
  The engine used to give the seven older Champions a 12-30 s cooldown and
  unlimited uses, carried over to the next body and frozen while it was dead.
* A FRESH CHARGE ON EVERY DEPLOYMENT. Recorded: 251 Golden Knight presses come
  less than 12 s after the previous one, each on a new body (Mighty Miner 221
  under 13 s, Archer Queen 229 under 17 s).
* NOTHING DURING THE DEPLOY. The ability belongs to the deployed character
  (published: every carrier has `DeployTime: 1000`). Recorded: of 33,848 presses
  the 1st percentile is 1.2 s after the card or later for every card.

Published costs are the same file's `ManaCost` of each ability.
Hero Goblins is left out: its Banner is pressed after the Goblins die (its own
tests), and Hero Ice Wizard / Hero Electro Wizard have no published ability
record in that release, so only the rules above are checked for them.
"""
import pytest

import clash_royale_env as E

CE = E.ClashRoyaleEnv
H = CE.HAND_SIZE
FILL = [3, 6, 40, 24, 72, 33, 7, 0, 15, 25]
OPP = [3, 6, 40, 24, 72, 33, 7, 0]

SPOTS = [(9.0, 8.0), (4.0, 8.0), (14.0, 8.0), (9.0, 11.0), (5.0, 11.0), (13.0, 11.0), (9.0, 5.0)]

HERO_GOBLINS = 172
BOSS_BANDIT = 122
SLOT_CARDS = sorted(c for c in E.get_all_card_ids()
                    if E.get_card_info(c)["is_champion"] or E.get_card_info(c)["is_hero"])
NAMES = {c: E.get_card_info(c)["name"] for c in SLOT_CARDS}
PRESSABLE = [c for c in SLOT_CARDS if c != HERO_GOBLINS]

# card_specs.json, release nr_15.535.13: the ability's ManaCost.
PUBLISHED_COST = {
    "Mighty Miner": 1, "Golden Knight": 1, "Skeleton King": 2, "Archer Queen": 1,
    "Monk": 1, "Little Prince": 3, "Goblinstein": 2, "Boss Bandit": 1,
    "Hero Knight": 2, "Hero Wizard": 1, "Hero Musketeer": 3, "Hero Giant": 2,
    "Hero Mini P.E.K.K.A.": 1, "Hero Magic Archer": 2, "Hero Mega Minion": 2,
    "Hero Barbarian Barrel": 1, "Hero Ice Golem": 2, "Hero Balloon": 2,
    "Hero Valkyrie": 3, "Hero Dark Prince": 3, "Hero Bowler": 2,
    "Hero Berserker": 3, "Hero Tombstone": 5,
}

# Ticks from the card to the first tick the ability can be pressed. 10 is the
# body's deploy time. Hero Mega Minion is locked for a further 1.5 s after
# landing; Hero Barbarian Barrel's Barbarian lands 17 ticks after the card.
FIRST_READY = {"Hero Mega Minion": 25, "Hero Barbarian Barrel": 27}


def _ids(cards):
    return [NAMES[c] for c in cards]


def _game(cid):
    rest = [f for f in FILL if f != cid]
    deck = rest[:1] + [cid] + rest[1:7]
    assert not E.validate_deck_slots(deck)
    env = CE(deck, OPP, 3600)
    env.seed(1)
    return env, deck


def _play(env, deck, cid):
    """Play `cid` from team 0's hand on the first free spot; one tick passes."""
    assert env.set_hand_for_team(0, [cid] + [c for c in deck if c != cid][:3])
    env.set_elixir_for_team(0, 10.0)
    # A building (the card itself, or what an ability left) holds its tiles.
    spots = [(9.0, 14.0)] if E.get_card_info(cid)["is_spell"] else SPOTS
    x, y = next((x, y) for x, y in spots if env.is_valid_placement(cid, x, y, 0))
    env.step_self_play(0, x, y, H, 0.0, 0.0, 1)


def _tick(env, n=1):
    for _ in range(n):
        env.step_self_play(H, 0.0, 0.0, H, 0.0, 0.0, 1)
        env.set_elixir_for_team(0, 10.0)


def _bodies(env, cid):
    # By name too: Hero Barbarian Barrel's Barbarian is a helper of the spell.
    return [u for u in env.get_units()
            if u["team"] == 0 and (u["card_id"] == cid or u["name"] == NAMES[cid])]


def _wait_ready(env, limit=60):
    """Ticks stepped until the ability is ready, refusing a press on the way."""
    for waited in range(limit):
        env.set_elixir_for_team(0, 10.0)
        if env.is_champion_ability_ready(0, 1):
            return waited
        assert not env.activate_champion_ability(0, 1)
        assert env.get_elixir_for_team(0) == pytest.approx(10.0)   # nothing charged
        _tick(env)
    return None


@pytest.mark.parametrize("cid", PRESSABLE, ids=_ids(PRESSABLE))
def test_no_ability_can_be_pressed_before_its_body_has_deployed(cid):
    env, deck = _game(cid)
    assert not env.is_champion_ability_ready(0, 1)       # nothing on the field
    _play(env, deck, cid)
    waited = _wait_ready(env)
    assert waited is not None
    assert 1 + waited == FIRST_READY.get(NAMES[cid], 10)


@pytest.mark.parametrize("cid", [c for c in PRESSABLE if NAMES[c] in PUBLISHED_COST],
                         ids=[NAMES[c] for c in PRESSABLE if NAMES[c] in PUBLISHED_COST])
def test_every_ability_costs_what_the_release_publishes(cid):
    env, deck = _game(cid)
    _play(env, deck, cid)
    assert _wait_ready(env) is not None
    env.set_elixir_for_team(0, 10.0)
    assert env.activate_champion_ability(0, 1)
    assert 10.0 - env.get_elixir_for_team(0) == pytest.approx(PUBLISHED_COST[NAMES[cid]])


def test_the_cost_table_covers_every_card_the_release_publishes():
    unpublished = {"Hero Ice Wizard", "Hero Electro Wizard", "Hero Goblins"}
    assert {NAMES[c] for c in SLOT_CARDS} - set(PUBLISHED_COST) == unpublished


ONE_CHARGE = [c for c in PRESSABLE if c != BOSS_BANDIT]


@pytest.mark.parametrize("cid", ONE_CHARGE, ids=_ids(ONE_CHARGE))
def test_one_charge_per_deployment_and_nothing_to_wait_out(cid):
    env, deck = _game(cid)
    _play(env, deck, cid)
    assert _wait_ready(env) is not None
    assert env.activate_champion_ability(0, 1)
    env.set_elixir_for_team(0, 10.0)
    assert not env.is_champion_ability_ready(0, 1)
    assert not env.activate_champion_ability(0, 1)
    assert env.get_elixir_for_team(0) == pytest.approx(10.0)
    # 40 s: longer than the longest cooldown the engine ever had (30 s).
    alive = 0
    for _ in range(400):
        _tick(env)
        alive += bool(_bodies(env, cid))
        assert not env.is_champion_ability_ready(0, 1)
    assert alive >= 30          # the body was there to press with, not just gone


def test_boss_bandit_has_two_charges_three_seconds_apart():
    env, deck = _game(BOSS_BANDIT)
    _play(env, deck, BOSS_BANDIT)
    assert _wait_ready(env) is not None
    assert env.activate_champion_ability(0, 1)
    again = _wait_ready(env)
    assert again == 30                                   # published Cooldown 3000 ms
    assert _bodies(env, BOSS_BANDIT)
    assert env.activate_champion_ability(0, 1)
    for _ in range(100):
        _tick(env)
        assert not env.is_champion_ability_ready(0, 1)   # published MaxCharges 2


REDEPLOYED = [c for c in PRESSABLE if NAMES[c] not in FIRST_READY]


@pytest.mark.parametrize("cid", REDEPLOYED, ids=_ids(REDEPLOYED))
def test_a_redeployed_card_has_a_fresh_charge_as_soon_as_it_has_landed(cid):
    env, deck = _game(cid)
    _play(env, deck, cid)
    assert _wait_ready(env) is not None
    assert env.activate_champion_ability(0, 1)
    # The card again 2 s after the press: far inside every old cooldown.
    _tick(env, 20)
    _play(env, deck, cid)
    waited = _wait_ready(env)
    assert waited is not None and 1 + waited == 10
    assert env.activate_champion_ability(0, 1)
    env.set_elixir_for_team(0, 10.0)
    assert not env.is_champion_ability_ready(0, 1)       # and only one


def test_a_spent_charge_stays_spent_through_a_redeploy_that_is_still_landing():
    # The newest body owns the button: while it deploys, nothing can be pressed,
    # whatever the older body had left.
    env, deck = _game(116)                               # Golden Knight
    _play(env, deck, 116)
    assert _wait_ready(env) is not None                  # old body ready, not pressed
    _play(env, deck, 116)
    env.set_elixir_for_team(0, 10.0)
    assert not env.is_champion_ability_ready(0, 1)
    assert 1 + _wait_ready(env) == 10


def test_hero_mega_minions_lockout_is_counted_from_landing_and_survives_tracking():
    # trackSlotEntities used to overwrite the body's own 1.5 s lockout with the
    # slot's (zero) cooldown, so the ability was ready on the first tick.
    env, deck = _game(173)
    _play(env, deck, 173)
    for _ in range(24):
        env.set_elixir_for_team(0, 10.0)
        assert not env.is_champion_ability_ready(0, 1)
        _tick(env)
    assert env.is_champion_ability_ready(0, 1)
