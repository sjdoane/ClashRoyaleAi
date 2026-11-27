"""Cards real opponents beat us with, against the live game: patch 0091, priority 1
(switches `cloakboard`, `bushboard`, `bushpop`, `evogb`; all default off, exact names).

Sources (cited in runs/sim_0091/REPORT.md of the outer repository):

* cloakboard: the real screen draws a cloaked Royal Ghost (the r5 detector boxes it
  while cloaked; the bot's own reader sees it). The engine dropped it from the board
  planes in `CombatEntity::isSeenOnBoard` (untargetable -> not seen), so the policy
  never saw it in training. The switch draws it like any visible unit (plain and
  evolved: both forms are card 47). Targeting is unchanged: troops cannot pick it,
  spells and splash still hit it (patch 0041).
* bushboard: the same for the walking Suspicious Bush (card 85; live detector 0.58-0.96
  while it walks).
* bushpop: royalesim cards-15535.1 SuspiciousBush (Damage 0, Kamikaze, Range 1600,
  HitSpeed 300 = LoadTime 300), SuspiciousBush_DummyAEO (goblins out 625 / 675 ms after
  the death, then their own 1 s deploy), BushGoblin Speed 60 (Medium, 1.2 tiles/s).
  Live: goblins boxed 0.77-1.03 s after the bush stopped (3 pops).
* evogb: royalesim evolutions GoblinBarrel_EV1 (evo_mirror), GoblinBarrelSpell_EV1_Decoy,
  units GoblinDummy (81 hp, 66 damage); live: one fused sprite splits into two barrels at
  mirrored targets, goblins at both Princess Towers (runs/live/20261124-094533
  f010151-f010179, two more throws in runs/live_1127/barrel/REPORT.md).

The switches are read once per process from CR_P0088, so every case runs in its own
subprocess.  Run from the build folder:

    cd build/gen2_l/engine && nice -n 10 ../../../.venv/bin/python -m pytest \
        python_ai/tests/test_sim_real_0091.py -q
"""
import json
import math
import os
import subprocess
import sys

import pytest

ENGINE = os.environ.get("CR_ENGINE_UNDER_TEST") or \
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASE = ("tornado,jump,log,turret,king,speed,bankrows,stomp,jumpcol,pocketcells,kingpath,"
        "jumpedge,walklegs,hogspeed")
NEW = ("cloakboard", "bushboard", "bushpop", "evogb", "gangair", "ghostsplash", "ghostcloak")
DECK0 = [15, 6, 25, 7, 33, 72, 40, 24]          # Hog 2.6
GHOST, EVO_GHOST, BUSH, ZAP, BARREL, EVO_BARREL = 47, 136, 85, 29, 109, 132
H, W = 34, 18


def _env(E, deck1):
    env = E.ClashRoyaleEnv(list(DECK0), list(deck1), 3600)
    env.seed(1)
    for _ in range(3):
        env.step_self_play(4, 0, 0, 4, 0, 0, 10)
    return env


def _cell(obs, x, y, team):
    """the 8 unit channels at a unit's cell in `team`'s observation (ClashEnv.h encoding)."""
    cx = int(x)
    cy = int(y) if team == 0 else int(math.floor(H - 1 - y))
    return [round(float(obs[ch * H * W + cy * W + cx]), 4) for ch in range(8)]


def _units(env):
    return [(u["id"], u["card_id"], u["team"], round(u["x"], 4), round(u["y"], 4), u["hp"],
             u["targetable"], u["deploy_ticks"]) for u in env.get_units()]


def _cloak(E, card, team, x, y, zap_at):
    """one cloaking unit alone; per tick its state and its cell in both observations.
    A Zap by the other side at tick `zap_at`, on where the unit then stands."""
    env = _env(E, [BUSH, 0, 1, 10, 3, 29, 2, 24])
    before = {u["id"] for u in env.get_units()}
    env.inject(card, x, y, team)
    rows, uid = [], None
    for k in range(60):
        if k == zap_at and uid is not None:
            u = next(u for u in env.get_units() if u["id"] == uid)
            env.inject(ZAP, u["x"], u["y"], 1 - team)
        env.step_self_play(4, 0, 0, 4, 0, 0, 1)
        U = [u for u in env.get_units() if u["id"] not in before]
        if uid is None and U:
            uid = U[0]["id"]
        u = next((u for u in U if u["id"] == uid), None)
        if u is None:
            rows.append(None)
            continue
        o0, o1 = env.get_observation_for_team(0), env.get_observation_for_team(1)
        rows.append({"card_id": u["card_id"], "x": u["x"], "y": u["y"], "hp": u["hp"],
                     "targetable": u["targetable"], "deploy": u["deploy_ticks"],
                     "obs0": _cell(o0, u["x"], u["y"], 0), "obs1": _cell(o1, u["x"], u["y"], 1)})
    return {"rows": rows, "units": _units(env)}


def _bush(E):
    """a Bush walking at our left Princess Tower (3, 6); per tick every team-1 unit."""
    env = _env(E, [BUSH, 0, 1, 10, 3, 29, 2, 24])
    env.set_tower_hit_log(True)
    t0 = env.get_current_tick()
    env.inject(BUSH, 3.0, 22.0, 1)
    rows = []
    for _ in range(140):
        env.step_self_play(4, 0, 0, 4, 0, 0, 1)
        us = [u for u in env.get_units() if u["team"] == 1 and not u["tower"]]
        o0 = env.get_observation_for_team(0)
        rows.append([{"id": u["id"], "card_id": u["card_id"], "name": u["name"], "x": u["x"],
                      "y": u["y"], "hp": u["hp"], "targetable": u["targetable"],
                      "deploy": u["deploy_ticks"], "body": u["body_radius"],
                      "seen0": max(_cell(o0, u["x"], u["y"], 0)[4:8])} for u in us])
    hits = [h for h in env.get_tower_hits() if h[1] == 1]
    return {"t0": t0, "rows": rows, "hits": [[h[0] - t0] + list(h[1:]) for h in hits]}


def _evogb(E):
    """team 1 throws card 132 three times at our left Princess Tower (board (3, 6));
    the third cast is the evolved one (cycles 2, uses 1)."""
    env = _env(E, [EVO_BARREL, 0, 1, 10, 3, 29, 2, 24])
    env.set_tower_hit_log(True)
    casts = []
    for n in range(3):
        env.set_elixir_for_team(1, 10.0)
        env.set_hand_for_team(1, [EVO_BARREL, 0, 1, 10])
        state = env.get_evolution_state(1) if hasattr(env, "get_evolution_state") else {}
        seen = {u["id"] for u in env.get_units()}
        t_cast = env.get_current_tick()
        env.step_self_play(4, 0, 0, 0, 3, 27, 1)      # team-1 frame (3, 27) = board (3, 6)
        pend = [(p["team"], p["card_id"], round(p["x"], 3), round(p["y"], 3), p["ticks_to_impact"])
                for p in env.get_pending_spells()]
        landed = []
        for _ in range(200):
            env.step_self_play(4, 0, 0, 4, 0, 0, 1)
            for u in env.get_units():
                if u["team"] == 1 and not u["tower"] and u["id"] not in seen:
                    seen.add(u["id"])
                    landed.append({"tick": env.get_current_tick() - t_cast, "card_id": u["card_id"],
                                   "name": u["name"], "x": u["x"], "y": u["y"], "hp": u["hp"]})
        casts.append({"state": {str(k): v for k, v in state.items()}, "pending": pend,
                      "landed": landed, "t_cast": t_cast})
    hits = [list(h) for h in env.get_tower_hits() if h[1] == 1]
    return {"casts": casts, "hits": hits}


def _gang(E):
    """team 1 Goblin Gang (112) in the middle, three team-0 Minions (41, air) just below it.
    Per tick: the Minions' HP and whether a Gang Spear Goblin (unit -27) targets one."""
    env = _env(E, [112, 0, 1, 10, 3, 29, 2, 24])
    env.inject(112, 8.5, 19.0, 1)
    env.inject(41, 8.5, 16.0, 0)
    rows = []
    for _ in range(120):
        env.step_self_play(4, 0, 0, 4, 0, 0, 1)
        us = env.get_units()
        air = {u["id"]: u["hp"] for u in us if u["team"] == 0 and u["card_id"] == 41}
        spear = [u for u in us if u["team"] == 1 and u["card_id"] == -27]
        rows.append({"air_hp": sum(air.values()), "n_air": len(air), "n_spear": len(spear),
                     "spear_on_air": sum(u["target_id"] in air for u in spear)})
    return {"rows": rows}


def _ghost_splash(E, card):
    """a team-1 Royal Ghost (or its Evolution) against two team-0 Cannons 0.8 apart
    (buildings never move, so the spacing holds); per tick both Cannons' HP."""
    env = _env(E, [card, 0, 1, 10, 3, 29, 2, 24])
    before = {u["id"] for u in env.get_units()}
    env.inject(25, 8.1, 11.0, 0, -1.0, 0)
    env.inject(25, 8.9, 11.0, 0, -1.0, 0)
    env.inject(card, 8.5, 14.0, 1)
    rows = []
    for _ in range(60):
        env.step_self_play(4, 0, 0, 4, 0, 0, 1)
        cannons = sorted((u for u in env.get_units() if u["id"] not in before and u["team"] == 0),
                         key=lambda u: u["x"])
        rows.append([[u["x"], u["hp"]] for u in cannons])
    return {"rows": rows}


def _ghost_cloak(E):
    """a team-1 Royal Ghost kills three team-0 Skeletons in the middle and is left with
    nothing in reach; per tick its targetable flag and the Skeletons' HP."""
    env = _env(E, [GHOST, 0, 1, 10, 3, 29, 2, 24])
    before = {u["id"] for u in env.get_units()}
    env.inject(24, 8.5, 15.0, 0)
    env.inject(GHOST, 8.5, 17.0, 1)
    rows = []
    for _ in range(100):
        env.step_self_play(4, 0, 0, 4, 0, 0, 1)
        us = [u for u in env.get_units() if u["id"] not in before]
        g = [u for u in us if u["team"] == 1 and u["card_id"] == GHOST]
        sk = [u["hp"] for u in us if u["team"] == 0 and u["card_id"] == 24]
        rows.append({"ghost": g[0]["targetable"] if g else None, "sk_hp": sum(sk), "n_sk": len(sk),
                     "y": g[0]["y"] if g else None})
    return {"rows": rows}


def _child(name):
    sys.path.insert(0, ENGINE)
    import python_ai  # noqa: F401
    import clash_royale_env as E
    out = {"active": {k: v for k, v in E.p0088_active().items() if k in NEW}}
    if name == "cloak":
        out["ghost1"] = _cloak(E, GHOST, 1, 8.5, 24.0, 35)
        out["ghost0"] = _cloak(E, GHOST, 0, 8.5, 9.0, 35)
        out["evo1"] = _cloak(E, EVO_GHOST, 1, 8.5, 24.0, 35)
        out["bush1"] = _cloak(E, BUSH, 1, 8.5, 24.0, 35)
    elif name == "bush":
        out.update(_bush(E))
    elif name == "evogb":
        out.update(_evogb(E))
    elif name == "gang":
        out.update(_gang(E))
    elif name == "ghostsplash":
        out["ghost"] = _ghost_splash(E, GHOST)
        out["evo"] = _ghost_splash(E, EVO_GHOST)
    elif name == "ghostcloak":
        out.update(_ghost_cloak(E))
    return out


_CACHE = {}


def run(switches, name):
    key = (switches, name)
    if key not in _CACHE:
        env = dict(os.environ, CR_P0088=switches, CR_POCKET_TABLE="verified", PYTHONDONTWRITEBYTECODE="1")
        r = subprocess.run([sys.executable, os.path.abspath(__file__), name], env=env,
                           capture_output=True, text=True, check=True)
        _CACHE[key] = json.loads(r.stdout.strip().splitlines()[-1])
    return _CACHE[key]


def sw(*extra):
    return ",".join((BASE,) + extra)


# ---------------------------------------------------------------- switches

def test_switches_are_exact_names_and_default_off():
    a = run(sw("all"), "cloak")["active"]
    assert a == {k: False for k in NEW}, a
    b = run(sw(*NEW), "cloak")["active"]
    assert b == {k: True for k in NEW}, b


# ---------------------------------------------------------------- cloakboard / bushboard

def _cloaked_rows(res):
    return [r for r in res["rows"] if r is not None and not r["targetable"]]


@pytest.mark.parametrize("case,team,ch_own,ch_other", [("ghost1", 1, 4, 0), ("ghost0", 0, 0, 4),
                                                       ("evo1", 1, 4, 0)])
def test_cloaked_ghost_is_on_the_board_only_with_cloakboard(case, team, ch_own, ch_other):
    off, on = run(BASE, "cloak")[case], run(sw("cloakboard"), "cloak")[case]
    assert all(r["card_id"] == GHOST for r in off["rows"] if r)
    c_off, c_on = _cloaked_rows(off), _cloaked_rows(on)
    assert len(c_off) >= 20 and len(c_on) == len(c_off)
    for r in c_off:      # gen2_k: not drawn while cloaked
        assert max(r["obs0"]) == 0.0 and max(r["obs1"]) == 0.0, r
    for r in c_on:       # drawn as a melee troop: enemy channel 4 for team 0's view of a team-1 unit
        v0, v1 = r["obs0"], r["obs1"]
        enemy_view, own_view = (v0, v1) if team == 1 else (v1, v0)
        assert enemy_view[4] > 0.0 and own_view[0] > 0.0, r
        assert abs(enemy_view[4] - min(r["hp"] / 4000.0, 1.0)) < 0.02 or enemy_view[4] > 0, r


@pytest.mark.parametrize("case", ["ghost1", "ghost0", "evo1", "bush1"])
def test_board_switches_change_nothing_but_the_observation(case):
    """targeting and every unit's life are the same with the switches on: who can pick
    the cloaked unit, the Zap that still hits it (patch 0041)."""
    off = run(BASE, "cloak")[case]
    on = run(sw("cloakboard", "bushboard"), "cloak")[case]
    assert off["units"] == on["units"]
    for a, b in zip(off["rows"], on["rows"]):
        if a is None:
            assert b is None
            continue
        assert {k: a[k] for k in ("x", "y", "hp", "targetable", "deploy")} == \
               {k: b[k] for k in ("x", "y", "hp", "targetable", "deploy")}


@pytest.mark.parametrize("case", ["ghost1", "ghost0", "evo1", "bush1"])
def test_a_spell_still_hits_the_cloaked_unit(case):
    """a Zap (192 at level 11) on where the cloaked unit stood 2 ticks before landing;
    the Bush (81 hp) dies to it."""
    on = run(sw("cloakboard", "bushboard"), "cloak")[case]["rows"]
    assert on[34] is not None and not on[34]["targetable"]
    if case == "bush1":
        assert any(r is None for r in on[35:45]), [r and r["hp"] for r in on[34:45]]
        return
    hp = [r["hp"] for r in on if r]
    assert hp[34] - min(hp) == 192, (hp[34], min(hp))


def test_walking_bush_is_on_the_board_only_with_bushboard():
    off = run(BASE, "cloak")["bush1"]
    on = run(sw("bushboard"), "cloak")["bush1"]
    ghost_only = run(sw("cloakboard"), "cloak")["bush1"]
    c_off, c_on, c_g = _cloaked_rows(off), _cloaked_rows(on), _cloaked_rows(ghost_only)
    assert len(c_off) >= 20
    assert all(max(r["obs0"]) == 0.0 for r in c_off + c_g)
    assert all(r["card_id"] == BUSH and max(r["obs0"][4:8]) > 0.0 for r in c_on), c_on[:2]


# ---------------------------------------------------------------- bushpop

def _bush_track(res):
    bush = [(k, u) for k, row in enumerate(res["rows"]) for u in row if u["card_id"] == BUSH]
    last_k = bush[-1][0]
    gobs = {}
    for k, row in enumerate(res["rows"]):
        for u in row:
            if u["card_id"] != BUSH:
                gobs.setdefault(u["id"], []).append((k, u))
    return bush, last_k, gobs


def test_bushpop_dies_in_reach_and_never_attacks():
    off, on = run(BASE, "bush"), run(sw("bushpop"), "bush")
    assert any(h[2] == BUSH for h in off["hits"]), off["hits"]       # gen2_k: the bush hits
    assert not any(h[2] == BUSH for h in on["hits"]), on["hits"]
    bush, last_k, _ = _bush_track(on)
    _, u = bush[-1]
    # it dies on the tick it comes into reach: range 1.6 + its body + the tower's 1.5
    reach = 1.6 + u["body"] + 1.5
    step = abs(bush[-1][1]["y"] - bush[-2][1]["y"])
    d = math.hypot(u["x"] - 3.0, u["y"] - 6.0)
    assert reach - step - 0.02 <= d <= reach + 0.02, (d, reach, step)
    assert last_k < len(on["rows"]) - 20


def test_bushpop_two_goblins_hidden_then_deploying():
    """SuspiciousBush_DummyAEO: the goblins come out 625 / 675 ms after the death (6 / 7
    ticks, hidden and untargetable until then), then deploy their own 1 s."""
    on = run(sw("bushpop"), "bush")
    bush, last_k, gobs = _bush_track(on)
    death = last_k + 1                     # first tick without the bush
    assert len(gobs) == 2, gobs.keys()
    out = []
    for track in gobs.values():
        k0, u0 = track[0]
        assert k0 == death + 1, (k0, death)          # the death spawn is on the board next tick
        assert u0["name"] == "Bush Goblins" and u0["card_id"] == -30
        assert abs(u0["y"] - bush[-1][1]["y"]) < 0.05 and abs(abs(u0["x"] - 3.0) - 0.4) < 0.05
        k_out = next(k for k, u in track if u["targetable"])
        u_out = dict(track)[k_out]
        out.append(k_out - death)
        assert u_out["deploy"] == 10, u_out           # then the 1 s deploy
        # drawn on our board from the same tick, not before
        assert next(k for k, u in track if u["seen0"] > 0.0) == k_out
    assert sorted(out) == [6, 7], out
    # live: first boxed 0.77-1.03 s after the bush stopped (3 pops; the detector needs a
    # few frames), the engine 0.6 / 0.7 s after its last tick


def test_bush_goblins_walk_at_medium():
    """BushGoblin Speed 60: 1.2 tiles/s under the trainer's `speed` switch (S/50). The two
    walk in contact (bodies 0.5 + 0.5, 1.0 apart) and push each other, so the pair's
    centre is measured: equal masses, the pushes cancel."""
    on = run(sw("bushpop"), "bush")
    _, _, gobs = _bush_track(on)
    a, b = (dict(t) for t in gobs.values())
    ks = [k for k in sorted(set(a) & set(b)) if a[k]["deploy"] == 0 and b[k]["deploy"] == 0]
    # ticks on which both walk (each moves more than half a step), until one stops at the tower
    steps = [(a[ka]["y"] + b[ka]["y"]) / 2 - (a[kb]["y"] + b[kb]["y"]) / 2
             for ka, kb in zip(ks, ks[1:])
             if kb == ka + 1 and a[ka]["y"] - a[kb]["y"] > 0.05 and b[ka]["y"] - b[kb]["y"] > 0.05]
    assert len(steps) >= 4, steps
    v = sum(steps) / len(steps) * 10.0
    assert abs(v - 1.2) <= 0.04, (v, steps)


def test_bushpop_off_keeps_the_gen2_k_bush():
    off = run(BASE, "bush")
    bush, last_k, gobs = _bush_track(off)
    assert [h[6] for h in off["hits"] if h[2] == BUSH][:1] == [256]


# ---------------------------------------------------------------- evogb

def test_evo_barrel_cycle_and_plain_casts_unchanged():
    off, on = run(BASE, "evogb"), run(sw("evogb"), "evogb")
    assert [c["state"] for c in on["casts"]] == [{"132": [2, 1]}, {"132": [1, 1]}, {"132": [0, 1]}]
    for n in (0, 1):     # casts 1 and 2 are the plain barrel, the same either way
        a, b = off["casts"][n], on["casts"][n]
        assert a["pending"] == b["pending"] and len(a["pending"]) == 1
        assert [(u["card_id"], u["hp"]) for u in a["landed"] if u["tick"] <= 40] == \
               [(u["card_id"], u["hp"]) for u in b["landed"] if u["tick"] <= 40]


def test_evolved_cast_is_two_mirrored_barrels():
    on = run(sw("evogb"), "evogb")["casts"][2]
    p = sorted(on["pending"], key=lambda r: r[2])
    assert len(p) == 2, p
    (t0, c0, x0, y0, f0), (t1, c1, x1, y1, f1) = p
    assert t0 == t1 == 1 and c0 == c1 == BARREL
    assert abs(x0 - 3.0) < 1e-3 and abs(x1 - 14.0) < 1e-3 and y0 == y1 == 6.0
    assert f0 == f1 and 25 <= f0 <= 30, (f0, f1)       # same flight (28 ticks, patch 0030)
    land = [u for u in on["landed"] if u["tick"] <= f0 + 3]
    real = [u for u in land if u["x"] < 8.5]
    dummy = [u for u in land if u["x"] > 8.5]
    assert len(real) == 3 and all(u["hp"] == 202 and u["name"] == "Goblins" and u["card_id"] != -53
                                  for u in real), real
    assert len(dummy) == 3 and all(u["hp"] == 81 and u["card_id"] == -53 for u in dummy), dummy
    # the decoy's goblins stand at the mirrored members of the real formation
    rs = sorted((round(17.0 - u["x"], 1), round(u["y"], 1)) for u in real)
    ds = sorted((round(u["x"], 1), round(u["y"], 1)) for u in dummy)
    assert all(abs(a[0] - b[0]) <= 0.15 and abs(a[1] - b[1]) <= 0.15 for a, b in zip(rs, ds)), (rs, ds)


def test_goblin_dummy_stabs_for_66():
    on = run(sw("evogb"), "evogb")
    amounts = {h[6] for h in on["hits"] if h[2] == -53}
    assert amounts == {66}, amounts


def test_evogb_off_keeps_six_goblins_at_one_point():
    off = run(BASE, "evogb")["casts"][2]
    assert len(off["pending"]) == 1
    land = [u for u in off["landed"] if u["tick"] <= off["pending"][0][4] + 3]
    assert len(land) == 6 and all(u["x"] < 8.5 for u in land), land


# ---------------------------------------------------------------- priority 2

def test_gangair_spear_goblins_hit_minions_only_with_the_switch():
    off, on = run(BASE, "gang")["rows"], run(sw("gangair"), "gang")["rows"]
    assert max(r["n_spear"] for r in off) == 3 and max(r["n_air"] for r in off) == 3
    # gen2_k: the Gang's Spear Goblins never pick an air unit, and while any of them lives
    # nothing on team 1 hurts the Minions (melee Goblins are ground only). Once the Gang is
    # dead the Minions fly on into the Princess Tower's reach (off: from tick 84), so only
    # the ticks with a Spear Goblin alive are compared.
    alive_off = [r for r in off if r["n_spear"] > 0]
    alive_on = [r for r in on if r["n_spear"] > 0]
    assert sum(r["spear_on_air"] for r in off) == 0
    assert alive_off[-1]["air_hp"] == off[0]["air_hp"], (off[0], alive_off[-1])
    # gangair: they do (units.SpearGoblin attacks_air)
    assert sum(r["spear_on_air"] for r in on) > 0
    assert alive_on[-1]["air_hp"] < off[0]["air_hp"], (alive_on[-1], off[0])


def _first_split(off, on):
    for k, (a, b) in enumerate(zip(off, on)):
        if a != b:
            return k, a, b
    return None


@pytest.mark.parametrize("case", ["ghost", "evo"])
def test_ghostsplash_first_hit_also_lands_on_the_cannon_beside(case):
    off = run(BASE, "ghostsplash")[case]["rows"]
    on = run(sw("ghostsplash"), "ghostsplash")[case]["rows"]
    # off: the Ghost's first hit is 263 on one Cannon only (both Cannons also decay 27 a
    # second, on the same ticks in both runs; a drop over 100 is a hit)
    drops = [(k, i, off[k - 1][i][1] - off[k][i][1]) for k in range(1, len(off)) for i in range(2)
             if len(off[k]) == 2 and off[k - 1][i][1] - off[k][i][1] > 100]
    assert drops and drops[0][2] == 263, drops[:3]
    # on: the runs are identical up to that hit, then the other Cannon has 263 less
    k, a, b = _first_split(off, on)
    assert k == drops[0][0], (k, drops[0])
    hit = drops[0][1]
    assert a[hit][1] == b[hit][1]
    assert a[1 - hit][1] - b[1 - hit][1] == 263, (a, b)


def _recloak_gap(rows):
    last_hit = max(k for k in range(1, len(rows)) if rows[k]["sk_hp"] < rows[k - 1]["sk_hp"])
    assert rows[last_hit]["n_sk"] == 0 or rows[-1]["n_sk"] == 0
    assert rows[last_hit]["ghost"] is True
    hidden = next(k for k in range(last_hit, len(rows)) if rows[k]["ghost"] is False)
    return hidden - last_hit


def test_ghostcloak_hides_again_2_3_s_after_the_last_hit():
    off = _recloak_gap(run(BASE, "ghostcloak")["rows"])
    on = _recloak_gap(run(sw("ghostcloak"), "ghostcloak")["rows"])
    assert off == 18, off            # gen2_k: 1.8 s (patch 0040)
    assert on == 23, on              # calibration INVISIBILITY: dropped on the hit + 2.3 s


if __name__ == "__main__":
    print(json.dumps(_child(sys.argv[1])))
