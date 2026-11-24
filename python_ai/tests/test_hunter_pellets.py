"""The Hunter fires ten pellets in a fan (CombatEntity::pelletsOnBody).

Published: ten projectiles a shot, 84 each at level 11, pellet radius 0.07,
range 4. NOT published: the fan's angle. HUNTER_FAN_DEGREES 45 puts all ten on
a Hog Rider in contact (9-10 measured in the live recordings of 2026-11-23:
the Hog lost 760-840 a volley). The share at range is not measured.
"""
import math

import python_ai  # noqa: F401
import clash_royale_env as E
import pytest

ID = {E.get_card_info(c)["name"]: c for c in E.get_all_card_ids()}
DECK = [15, 6, 25, 40, 24, 72, 33, 7]
NOOP = 4


def _first_volley(target, dist, also=None):
    g = E.ClashRoyaleEnv(list(DECK), list(DECK), 3600)
    g.seed(1)
    g.step_self_play(NOOP, 0., 0., NOOP, 0., 0., 2)
    g.inject(ID["Hunter"], 8.5, 13.0, 1, -1.0, 0)
    g.inject(ID[target], 8.5, 13.0 - dist, 0, -1.0, 0)
    if also:
        g.inject(ID[also[0]], 8.5 + also[1], 13.0 - dist, 0, -1.0, 0)
    prev = {u["id"]: u for u in g.get_units() if not u["tower"]}
    for _ in range(60):
        g.step_self_play(NOOP, 0., 0., NOOP, 0., 0., 1)
        now = {u["id"]: u for u in g.get_units() if not u["tower"]}
        hunter = next((u for u in prev.values() if u["name"] == "Hunter"), None)
        lost = {}
        for i, u in prev.items():
            if hunter is None or u["name"] == "Hunter":   # injected bodies appear after a step
                continue
            d = u["hp"] - (now[i]["hp"] if i in now else 0)
            if d > 0:
                lost[u["name"]] = (d, math.hypot(u["x"] - hunter["x"], u["y"] - hunter["y"]))
        if lost:
            return lost
        prev = now
    return {}


def test_published_per_pellet_damage_and_count():
    base = E.get_card_stats(ID["Hunter"])["base"]
    assert base["damage"] == 84


@pytest.mark.parametrize("target", ["Hog Rider", "Giant", "Ice Golem"])
def test_all_ten_pellets_land_in_contact(target):
    lost = _first_volley(target, 1.3)
    assert lost[target][0] == 840


def test_fewer_pellets_land_from_further_away():
    seen = []
    for dist in (1.3, 4.0, 5.5):
        dmg, d = _first_volley("Giant", dist)["Giant"]
        seen.append((d, dmg))
    assert seen[0][1] > seen[1][1] > seen[2][1]
    far_d, far_dmg = seen[2]
    assert far_d > 4.0
    assert 3 * 84 <= far_dmg <= 5 * 84     # 3-5 pellets on a 0.75 body near full range


def test_a_small_body_takes_at_least_one_pellet_and_no_more_than_it_has():
    lost = _first_volley("Skeletons", 2.0)
    assert lost and all(d <= 84 for d, _ in lost.values())


def test_the_volley_is_shared_never_more_than_ten():
    lost = _first_volley("Hog Rider", 1.5, also=("Ice Golem", 1.4))
    assert sum(d for d, _ in lost.values()) <= 840 + 84


def test_a_princess_tower_takes_about_four_pellets_at_his_range():
    g = E.ClashRoyaleEnv(list(DECK), list(DECK), 3600)
    g.seed(1)
    g.set_tower_hit_log(True)
    g.inject(ID["Hunter"], 3.0, 15.0, 1, -1.0, 0)
    for _ in range(120):
        g.step_self_play(NOOP, 0., 0., NOOP, 0., 0., 1)
    hits = [h[6] for h in g.get_tower_hits() if h[1] == 1 and h[4] == 0]
    assert hits and all(3 * 84 <= h <= 6 * 84 for h in hits)
