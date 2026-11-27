#pragma once
#include <cstdlib>
#include <string>

// Patch 0088: each change is a separate switch, read once from the environment
// variable CR_P0088 (a comma list), so each can be measured on its own. Unset
// or empty = the engine of patch 0087, bit for bit.
//
//   tornado    Tornado drags (attract) for its 1.05 s life instead of a 1.5-tile
//              pull on landing
//   jump       a river jumper (Hog Rider, ...) is mid-jump while it crosses the
//              water: no retarget, no attack, no collision, no push or pull,
//              not hit by ground-only area damage
//   jumpspeed  the jump runs at the published JumpSpeed (Hog 160 / Speed 120)
//   log        The Log starts rolling 3 ticks sooner
//   turret     Hero Musketeer's Trusty Turret hits for 148
//   king       the King Tower wakes 3.1 s after its trigger (first shot 3.6 s)
//   evocannon  Evolved Cannon barrage on a Crown Tower 53
//   speed      every troop walks at S/50 tiles/s (royalesim) instead of
//              S x 0.02209 tiles/s: 9.5 % slower (runs/speed_check_1125/VERDICT.txt);
//              applied where a troop is built, get_card_stats still reports
//              the old figure; the board keeps its 34 rows
//   stomp      Giant, Royal Giant, Goblin Giant, Golem, Ice Golem walk in
//              royalesim's stop/wait pulses (StopMovementAfterMS / WaitMS)
//   bankrows   a troop on the two extra engine rows each side of the river
//              (ArenaLayout::realRow squeezes y 13-15 and 18-20 into one real
//              row each) moves at its speed in REAL tiles, so the extra rows
//              cost no extra travel time
//   physdist   every distance the game rules measure (targeting, attack range,
//              sight, spell and splash radii, projectile flight) is taken in
//              REAL 32-row tiles: y goes through ArenaLayout::realRow first, so
//              the two extra bank rows add no length (y 13-15 and 18-20 count
//              as one row each). Coordinates, observations and
//              placement stay in the 34-row engine frame. Movement legs and
//              collisions stay in engine tiles (bankrows makes the walk real).
//   jumpcol    a river jumper steps onto its target's column at the bank only
//              where its diagonal jump would start (placed beside the bridge);
//              one walking in from further back crosses on the corridor edge
//   all        every switch above
//
// The registry reads the switches when it is built, so set the variable before
// the module is imported.
namespace P0088 {
inline const std::string& raw() {
    static const std::string v = [] {
        const char* e = std::getenv("CR_P0088");
        return std::string(e ? e : "");
    }();
    return v;
}
inline bool on(const char* name) {
    const std::string& s = raw();
    if (s.empty()) return false;
    const std::string n(name);
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        const std::string tok = s.substr(i, j - i);
        if (tok == n || tok == "all") return true;
        i = j + 1;
    }
    return false;
}
inline bool tornado() { static const bool b = on("tornado"); return b; }
inline bool jump() { static const bool b = on("jump"); return b; }
inline bool jumpSpeed() { static const bool b = on("jumpspeed"); return b; }
inline bool log() { static const bool b = on("log"); return b; }
inline bool turret() { static const bool b = on("turret"); return b; }
inline bool king() { static const bool b = on("king"); return b; }
inline bool evoCannon() { static const bool b = on("evocannon"); return b; }
inline bool speed() { static const bool b = on("speed"); return b; }
// REAL_TILES_PER_MIN_TO_ENGINE 0.011045 -> 0.010 (royalesim S/50 tiles/s, which
// the speed check measured on 300+ live tracks within 1 %).
inline bool stomp() { static const bool b = on("stomp"); return b; }
inline bool bankRows() { static const bool b = on("bankrows"); return b; }
inline bool physDist() { static const bool b = on("physdist"); return b; }
inline bool jumpCol() { static const bool b = on("jumpcol"); return b; }
inline float speedScale() { return speed() ? 0.010f / 0.011045f : 1.0f; }

// Patch 0089 switches, read from the same CR_P0088 list but NOT turned on by
// "all" (so an "all" list keeps meaning the 0088 engine):
//   pocketcells  the pocket opens only the lane's inner bridge column (cells
//                3 / 14), no building on a bridge cell, no card on the four
//                bank corner cells (0|17, 15|18), no deploy-anywhere card on
//                river water off the bridge (human replays, royalesim)
//   kingpath     a King-bound unit already past the river walks straight at
//                the King when that line passes no standing tower, instead of
//                the lane waypoint W1 (LanePath::approachPoint; royalesim
//                path2026: no waypoint, the pocket Hog from the centre column
//                hits the King ~2 s sooner)
inline bool onExact(const char* name) {
    const std::string& s = raw();
    const std::string n(name);
    size_t i = 0;
    while (!s.empty() && i <= s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        if (s.substr(i, j - i) == n) return true;
        i = j + 1;
    }
    return false;
}
inline bool pocketCells() { static const bool b = onExact("pocketcells"); return b; }
inline bool kingPath() { static const bool b = onExact("kingpath"); return b; }

// Patch 0090 switches (exact names only, like 0089; "all" does not enable them).
// Measured on the recorded live runs (runs/bridge_1126: 2,018 real crossings,
// round-5 detector; own Hog 270 corner plays, 20 beside the bridge):
//   jumpedge  a river jumper whose target stands in its lane (target column
//             within the bridge's deck) crosses like the game's Hog:
//             - from the tile beside the bridge or on it (within a tile of the
//               target column): the walker's route onto the bridge
//               (Board::getNextWaypoint), no step onto the column
//               (real x 2.24 / 2.46 / 2.71 on real rows 14.5 / 15 / 16 from (2,15));
//             - from further out (the corner, x 1 / 16): a straight line to
//               (column -/+ 0.8, 1.5 rows past the near bank) = real (2.2 | 14.8,
//               row 16.0), forward at walking speed, then at its target
//               (0087's 45-degree diagonal landed it on 2.57 | 14.43);
//             a target off the lane keeps 0087's rule.
//   walklegs  a walker keeps the rest of its step after reaching a waypoint
//             (up to three legs a tick, as jumpers already do): no lost time
//             at the bridge's mouths (real walkers cross at their own speed;
//             the one-leg walker lost ~4 %).
//   hogspeed  the Hog Rider walks on land at 2.34 tiles/s instead of S/50 =
//             2.40 (the jump over the water keeps its speed, so jumpedge's
//             crossing is unchanged). Real: 174 clean tracks on real rows
//             18.6 -> 21.6 (both screen halves), median 2.340 tiles/s along
//             the path, 95 % interval of the median 2.325-2.352; on the same
//             rows and mapping Ice Golem, Skeletons and Knight match S/50
//             within 1-2 %, so it is the card, not the screen mapping
//             (runs/bridge_1126/speed2.py).
inline bool jumpEdge() { static const bool b = onExact("jumpedge"); return b; }
inline bool walkLegs() { static const bool b = onExact("walklegs"); return b; }
inline bool hogSpeed() { static const bool b = onExact("hogspeed"); return b; }
// hogspeed: land speed 2.34 tiles/s whether or not "speed" is on (S/50 = 2.40;
// speedScale() 1 builds it at S x 0.02209 = 2.65).
constexpr float HOG_LAND_SPEED_OF_S50 = 2.34f / 2.40f;
inline float hogLandScale() { return HOG_LAND_SPEED_OF_S50 * (0.010f / 0.011045f) / speedScale(); }

// Patch 0091 switches (exact names only, like 0089/0090; "all" does not enable
// them). Each one makes the sim match real play for a card real opponents beat
// us with (runs/sim_0091/REPORT.md has the sources):
//   cloakboard  a cloaked Royal Ghost (card 47, plain and Evolution) is written
//               into both players' board planes like a visible unit. Real
//               players see the faint ghost; the live detector boxes it at
//               ~0.9 while cloaked and live_ppo/cloaked_rows.py encodes it.
//               Targeting is unchanged: troops and buildings still cannot pick
//               it, spells and splash still hit it (Entity::takesAreaDamage).
//   bushboard   the walking (cloaked) Suspicious Bush (card 85) is written into
//               the board planes the same way (live detector 0.58-0.96 while it
//               walks); untargetable as before.
//   bushpop     the Suspicious Bush never attacks: on the tick it comes into
//               reach of its building it dies (no damage) and its death
//               spawns the 2 Bush Goblins (royalesim cards-15535.1:
//               SuspiciousBush Damage 0, Kamikaze, HitSpeed 300 = LoadTime
//               300 so no wind-up, calibration KAMIKAZE_DEATH at_fire).
//               Range 1.6 (Range 1600). The goblins come out 625 / 675 ms
//               after the death (SuspiciousBush_DummyAEO schedule, use_deploy)
//               and then deploy their 1 s: here they are hidden and
//               untargetable for 6 / 7 ticks and deploy 10 + 6 / 10 + 7.
//               Bush Goblin speed Medium, 1.2 tiles/s (BushGoblin Speed 60).
//               Live: goblins first boxed 0.77-1.03 s after the bush stopped
//               (3 pops, runs/sim_0091/REPORT.md).
//   evogb       the Evolved Goblin Barrel (card 132, evolved cast) is the
//               plain barrel (3 Goblins, plain formation) PLUS a decoy barrel
//               thrown at the mirrored point (2 x CENTER_X - x, y), same
//               flight, landing 3 GoblinDummy (81 hp, 66 damage, hit 1.1 s,
//               load 0.7 s, range 0.5, speed 2.4) at the mirrored members
//               (royalesim evolutions.GoblinBarrel_EV1.evo_mirror,
//               GoblinBarrelSpell_EV1_Decoy, units.GoblinDummy, calibration
//               EVO_BARREL_DECOY_POINTS). Live: two barrels from one fused
//               sprite to mirrored targets, goblins at both Princess Towers
//               (runs/live/20261124-094533 f010151-f010179). Without it the
//               evolved cast is 6 Goblins at one point (not sourced).
inline bool cloakBoard() { static const bool b = onExact("cloakboard"); return b; }
inline bool bushBoard() { static const bool b = onExact("bushboard"); return b; }
inline bool bushPop() { static const bool b = onExact("bushpop"); return b; }
inline bool evoGb() { static const bool b = onExact("evogb"); return b; }
constexpr int ROYAL_GHOST_CARD_ID = 47;
constexpr int SUSPICIOUS_BUSH_CARD_ID = 85;
constexpr int BUSH_GOBLIN_STATS_ID = -30;
// SuspiciousBush_DummyAEO schedule: delay_ms 675 (x -1) and 625 (x +1),
// rounded to engine ticks, in spawn-offset order {-0.4, 0}, {+0.4, 0}.
constexpr int BUSH_GOBLIN_DELAY_TICKS[2] = {7, 6};
constexpr float BUSH_RANGE = 1.6f;

// Patch 0091, priority 2 (exact names only, default off; sources from the
// royalesim reference data, cards-15535.1 = the 15.535.29 client's asset pack):
//   gangair     Goblin Gang's three Spear Goblins (unit -27) hit air.
//               GoblinGang second_summon SpearGoblin x 3; units.SpearGoblin
//               attacks_air true (the same record as card 23, which already
//               has withTargetsAir, and Goblin Giant's -26).
//   ghostsplash the Royal Ghost's hit splashes 1.0 round its target
//               (units.Ghost and units.Ghost_EV1 area_damage_radius_milli
//               1000, self_as_aoe_center false). Card 47 and both forms of
//               136. Applied as every melee splash here
//               (MeleeTroop::performAttack -> applySplashDamage: centre to
//               centre, air included); whether the real splash reaches air
//               bodies is not in the data and is not decided here.
//   ghostcloak  the Ghost hides again 2.3 s after its hit, not 1.8 s
//               (units.Ghost / Ghost_EV1 idle_invisibility time_ms 2000,
//               counted from the END of the attack; calibration INVISIBILITY,
//               measured on client 15.535.29 and 16.402: towers drop it on
//               the hit + 46 ticks of 50 ms in 18 of 26 cases, its attack
//               ending on the hit + 5-6; the engine has no back-swing, so it
//               counts from the hit). Patch 0040's 1800 is release 15.535.13.
inline bool gangAir() { static const bool b = onExact("gangair"); return b; }
inline bool ghostSplash() { static const bool b = onExact("ghostsplash"); return b; }
inline bool ghostCloak() { static const bool b = onExact("ghostcloak"); return b; }
constexpr float GHOST_SPLASH = 1.0f;
constexpr int GHOST_RECLOAK_TICKS = 23;
inline int ghostRevealTicks() { return ghostCloak() ? GHOST_RECLOAK_TICKS : 18; }
inline float ghostSplashRadius() { return ghostSplash() ? GHOST_SPLASH : 0.0f; }

// Tornado (royalesim area_effect_objects Tornado, status ATTRACT_LAW
// base_speed): life 1050 ms, every tick a victim in the radius takes an extra
// step of attract_percentage 360 % of its own base speed toward the centre.
constexpr float TORNADO_ATTRACT = 3.6f;
constexpr int TORNADO_LIFE_TICKS = 11;     // 1.05 s, the landing tick included
// HogRider JumpSpeed 160 / Speed 120.
constexpr float JUMP_SPEED_RATIO = 160.0f / 120.0f;
// royalesim calibration KING_ACTIVATE_TIME_MS 3550 (first target and swing on
// the wake tick); our King then winds up KING_FIRST_HIT_TICKS (5), so the wake
// is held 31 ticks for a first shot 3.6 s after the trigger.
constexpr int KING_WAKE_DELAY_TICKS = 31;

// royalesim data/derived/cards-160635007-20261103.json StopMovementAfterMS /
// WaitMS; path2026::stomp_clock_step on its 50 ms tick, run twice per engine
// tick. While walking the unit moves at floor(S x (stop + wait) / stop)
// (card.rs move_speed). Names: an Evolution shares the name; "Hero X" is X.
struct Stomp { int stopMs; int waitMs; int speed; };
inline Stomp stompFor(const std::string& name) {
    std::string n = name.rfind("Hero ", 0) == 0 ? name.substr(5) : name;
    if (n == "Giant" || n == "Royal Giant") return {640, 100, 45};
    if (n == "Goblin Giant") return {640, 100, 60};
    if (n == "Golem") return {1000, 200, 45};
    if (n == "Ice Golem") return {470, 80, 45};
    return {0, 0, 0};
}
}
