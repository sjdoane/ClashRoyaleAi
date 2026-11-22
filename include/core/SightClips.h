#pragma once

#include <string>

// Published sight clip of the building-targeting troops, in tiles. Read by
// CardFactories::applyCardMetadata, used in CombatEntity::withinSightClip.
//
// The game's unit table gives these troops two extra sight fields next to
// SightRange, "SightClip" and "SightClipSide" (1/1000 tile):
//
//                  SightRange  SightClip  SightClipSide
//   Hog Rider         9500       4000        4000
//   Royal Hog         9500       4000        4000
//   Giant             7500       2000        2000
//   Royal Giant       7500       2000        2000
//   Goblin Giant      7500       2000        2000
//   Electro Giant     7500       2000        2000
//   Ice Golem         7000       2000        2000
//   Golem             7000       2000        1900
//   Golemite          (older file only)  2000  2000
//   Balloon           7700        -          2000
//   Skeleton Barrel   7700        -          2000
//   Suspicious Bush   7500       2000        2000
//
// and none for the Battle Ram, Ram Rider's ram, Lava Hound, Wall Breakers and
// Elixir Golem. No other unit has them.
//
// READING (inferred, not published as prose): the sight disc is cut down to a
// box. A building is in sight only if it is within SightRange AND no further
// than SightRange - SightClipSide to the side (x) AND no further than
// SightRange - SightClip along the arena (y), all body to body like sight
// itself (patch 0044). That makes every clipped troop's
// sideways reach 5.0-5.7 tiles: the distance from a lane to the middle of the
// arena. It is the rule players know: a building in the middle pulls a Hog or a
// Giant from either lane, one placed towards the far lane does not, although
// both are well inside the 9.5 or 7.5 sight radius. The other reading (the
// clip itself is the half-width: 4 tiles for a Hog, 2 for a Giant) would make a
// centre building unable to pull a Giant at all, which is plainly not the game.
// Supercell's own balance note for the unclipped Battle Ram and the Golem
// ("slipping past standard building placements that other win conditions get
// pulled by") is the same geometry.
//
// Before this table the engine had the disc only, and got the "a Cannon 7-8
// tiles off-lane does not pull a Hog" observation by inflating the Crown
// Towers' targeting footprint instead (Tower::getTargetingRadius, removed in
// patch 0043).
//
// Sources: FLZ = FirstLight-Zero card_specs.json (release 15.535.13), CRAPI =
// RoyaleAPI cr-api-data cards_stats.json (older files; the two agree wherever
// both have a row). HERO rows are ASSUMED equal to the base troop: the hero
// forms are not in either file.
//
// Electro Giant and Suspicious Bush joined in patch 0045, when their registry
// sight was corrected from the default 5.5 to the published 7.5.
//
// Keyed by registry NAME, like LoadTimes.h, so Evolutions inherit the row.
namespace SightClips {

struct Row {
    const char* name;
    float clip;       // along the arena (y); 0 = not clipped
    float clipSide;   // across the arena (x); 0 = not clipped
};

inline const Row* table(int& count) {
    static const Row rows[] = {
        { "Hog Rider",       4.0f, 4.0f },   // FLZ, CRAPI
        { "Royal Hogs",      4.0f, 4.0f },   // FLZ, CRAPI
        { "Giant",           2.0f, 2.0f },   // FLZ, CRAPI
        { "Royal Giant",     2.0f, 2.0f },   // FLZ, CRAPI
        { "Goblin Giant",    2.0f, 2.0f },   // FLZ, CRAPI
        { "Electro Giant",   2.0f, 2.0f },   // FLZ
        { "Suspicious Bush", 2.0f, 2.0f },   // FLZ
        { "Ice Golem",       2.0f, 2.0f },   // FLZ, CRAPI
        { "Golem",           2.0f, 1.9f },   // FLZ, CRAPI
        { "Golemite",        2.0f, 2.0f },   // CRAPI
        { "Balloon",         0.0f, 2.0f },   // FLZ, CRAPI
        { "Skeleton Barrel", 0.0f, 2.0f },   // FLZ, CRAPI
        { "Hero Giant",      2.0f, 2.0f },   // HERO: assumed as Giant
        { "Hero Ice Golem",  2.0f, 2.0f },   // HERO: assumed as Ice Golem
        { "Hero Balloon",    0.0f, 2.0f },   // HERO: assumed as Balloon
    };
    count = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
    return rows;
}

inline const Row* find(const std::string& name) {
    int count = 0;
    const Row* rows = table(count);
    for (int i = 0; i < count; ++i) {
        if (name == rows[i].name) return &rows[i];
    }
    return nullptr;
}

} // namespace SightClips
