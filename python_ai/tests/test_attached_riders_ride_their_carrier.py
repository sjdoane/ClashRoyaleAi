"""Attached secondaries ride their carrier (patch 0046).

Published game data (RoyaleAPI cr-api-data cards_stats.json; FirstLight-Zero
card_specs.json, release 15.535.13): the Ram and the Goblin Giant carry
`SpawnAttach: true` with `SpawnCharacter` RamRider (1) and SpearGoblinGiant
(2). The RamRider character has `TargetOnlyTroops: true` and no death spawn;
SpearGoblinGiant has `DeathSpawnCharacter: SpearGoblin`.

Before this patch both were ordinary second units dropped on the same tile: a
lone Ram Rider was two 1766-HP bodies, its rider stood at range shooting the
Princess Tower and then walked on to the King Tower (3884 tower damage where
the ram alone deals 1750), and the Goblin Giant's Spear Goblins ran ahead of it
and died first.
"""
import python_ai  # noqa: F401
import clash_royale_env as E

NOOP = 4
RAM_RIDER, GOBLIN_GIANT, KNIGHT, CANNON = 87, 88, 0, 129
OURS = [129, 168, 125, 15, 40, 72, 33, 7]
FILL = [4, 12, 112, 61, 20, 14, 26]


def _start(attacker, defender_deck=None):
    env = E.ClashRoyaleEnv(list(defender_deck or OURS), [attacker] + FILL, 3600)
    env.seed(1)
    env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 20)
    env.set_elixir_for_team(0, 10.0)
    env.set_elixir_for_team(1, 10.0)
    assert env.set_hand_for_team(1, [attacker] + FILL[:3])
    return env


def _towers(env):
    return {u["id"]: u for u in env.get_units() if u["tower"] and u["team"] == 0}


def _attackers(env):
    return [u for u in env.get_units() if u["team"] == 1 and not u["tower"]]


def test_a_lone_ram_rider_is_one_body_and_only_the_ram_hits_the_tower():
    env = _start(RAM_RIDER)
    before = {i: u["hp"] for i, u in _towers(env).items()}
    env.step_self_play(NOOP, 0.0, 0.0, 0, 3.0, 15.0, 1)
    rode = orphaned = 0
    for _ in range(320):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        units = _attackers(env)
        bodies = [u for u in units if u["targetable"]]
        riders = [u for u in units if not u["targetable"]]
        assert len(bodies) <= 1 and len(riders) <= 1
        if riders and not bodies:
            orphaned += 1   # the tick the ram fell on; the rider goes on its next update
            continue
        for r in riders:
            rode += 1
            assert r["hp"] == 1766
            assert abs(r["x"] - bodies[0]["x"]) < 0.25 and abs(r["y"] - bodies[0]["y"]) < 0.25
            assert all(t["target_id"] != r["id"] for t in _towers(env).values())
            assert r["target_id"] not in before, "the rider targets troops only"
    assert rode > 100
    assert orphaned <= 1, "the rider outlived its ram"
    assert not _attackers(env)
    after = {i: u["hp"] for i, u in _towers(env).items()}
    lost = sorted(before[i] - after.get(i, 0) for i in before)
    # one Princess Tower, hit by the ram alone: a 542 charge and then 271s
    # (damage 250 -> 271, official 6 Oct 2026 notes; patch 0048)
    assert lost[0] == 0 and lost[1] == 0
    assert 1000 <= lost[2] <= 2500 and (lost[2] - 542) % 271 == 0


def test_the_rider_shoots_a_troop_beside_the_road_and_leaves_a_building_alone():
    deck = [CANNON, KNIGHT, 125, 15, 40, 72, 33, 7]
    env = _start(RAM_RIDER, deck)
    assert env.set_hand_for_team(0, deck[:4])
    env.step_self_play(1, 5.0, 11.0, 0, 3.0, 15.0, 1)   # Knight beside the lane
    env.set_elixir_for_team(0, 10.0)
    slot = list(env.get_hand_for_team(0)).index(CANNON)
    env.step_self_play(slot, 3.0, 11.0, NOOP, 0.0, 0.0, 1)   # Cannon in the ram's way
    knight_lost = 0
    last = None
    shot_at_knight = False
    for _ in range(120):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        units = env.get_units()
        structures = {u["id"] for u in units if u["team"] == 0 and (u["tower"] or u["building"])}
        knights = [u for u in units if u["team"] == 0 and u["name"] == "Knight"]
        for r in (u for u in units if u["team"] == 1 and not u["tower"] and not u["targetable"]):
            assert r["target_id"] not in structures
            shot_at_knight |= bool(knights) and r["target_id"] == knights[0]["id"]
        if knights:
            # only while the card is on the board: afterwards the Knight walks
            # on into the other side's Princess Tower
            if last is not None and knights[0]["hp"] < last and any(u["team"] == 1 and not u["tower"] for u in units):
                knight_lost += last - knights[0]["hp"]
            last = knights[0]["hp"]
    assert shot_at_knight
    # nothing else on the board attacks troops: every loss is a 104-damage bolt
    assert knight_lost > 0 and knight_lost % 104 == 0


def test_the_goblin_giant_carries_its_spear_goblins_until_it_dies():
    env = _start(GOBLIN_GIANT)
    env.step_self_play(NOOP, 0.0, 0.0, 0, 3.0, 15.0, 1)
    carried = dropped = 0
    for _ in range(600):
        env.step_self_play(NOOP, 0.0, 0.0, NOOP, 0.0, 0.0, 1)
        units = _attackers(env)
        giant = [u for u in units if u["name"] == "Goblin Giant"]
        spears = [u for u in units if u["name"] == "Spear Goblins"]
        if giant:
            assert len(spears) == 2
            for s in spears:
                carried += 1
                assert not s["targetable"] and s["hp"] == 133
                assert abs(s["x"] - giant[0]["x"]) < 0.75 and abs(s["y"] - giant[0]["y"]) < 0.75
        else:
            # they drop on their next update, one tick after the Giant falls
            dropped = max(dropped, sum(1 for s in spears if s["targetable"]))
    assert carried > 200
    assert dropped == 2
