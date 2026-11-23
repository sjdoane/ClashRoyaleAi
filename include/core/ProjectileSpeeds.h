#pragma once

#include <string>

// Published projectile speed of the ranged troops. Read by
// CardFactories::applyCardMetadata, used where RangedTroop and
// RangedBuildingTargeter fire (CombatEntity::shotTilesPerTick).
//
// Until patch 0049 every ranged troop's shot flew 1.5 tiles a tick, whatever
// it was: a Musketeer's bullet, a Bomber's bomb and the Bowler's boulder all
// arrived together. The game's projectile table gives each one a Speed, from
// 170 (the boulder) to 1400 (Sparky).
//
// UNITS. A projectile covers published speed / 48 tiles per second: the
// divisor measured on thrown spells (patch 0030: eight Goblin Barrels, seven
// Fireballs frame by frame) and already used for the Princess Tower's arrow
// (patch 0036). At 10 ticks a second that is speed / 480 tiles a tick. NOT
// measured for a troop's shot: over a troop's 4-6 tiles the old and the new
// flight differ by one or two frames of an 11 fps recording. It rests on the
// published ratios and the thrown-spell divisor.
//
// Source: FirstLight-Zero card_specs.json / projectiles.json, release
// 15.535.13 (the projectile record each unit names). REC rows are projectile
// records named after the unit that no unit record links (the Ram Rider's
// bola, the Goblin Machine's rocket).
//
// Keyed by registry NAME, like LoadTimes.h: an Evolution inherits the row (the
// evolved projectile records carry the same Speed wherever they have one) and
// "Hero X" with no row of its own flies like X. A row belongs to the card's
// RANGED form: "Rascals" is the Rascal Girl, "Goblinstein" the Doctor; their
// melee partners never fire.
//
// No row = the old 1.5 tiles a tick (720): Zappies and the Minion Giant. The
// table has no projectile for the Zappies (their bolt is instant in the game)
// and no record at all for the Minion Giant.
//
// NOT covered: the buildings. Cannon (1000), X-Bow (1600), Bomb Tower (500)
// and the others still hit on the attack tick (Building::performAttack); the
// Mortar's shell has its own delay.
namespace ProjectileSpeeds {

// Tiles a tick of a unit without a row (the engine's old constant).
constexpr float DEFAULT_TILES_PER_TICK = 1.5f;
// Published speed units per tile a tick (48 per tile a second, 10 ticks).
constexpr float PUBLISHED_PER_TILE_PER_TICK = 480.0f;

struct Row {
    const char* name;
    int speed;   // published units
};

inline const Row* table(int& count) {
    static const Row rows[] = {
        { "Archers",           600 },
        { "Archer Queen",      800 },
        { "Baby Dragon",       500 },
        { "Bomber",            400 },
        { "Bowler",            170 },
        { "Cannon Cart",      1000 },
        { "Dart Goblin",       800 },
        { "Electro Spirit",   1000 },
        { "Executioner",       550 },
        { "Fire Spirit",       400 },
        { "Firecracker",       500 },
        { "Flying Machine",    800 },
        { "Furnace",           600 },
        { "Goblin Demolisher", 400 },
        { "Goblin Machine",    350 },   // REC: GoblinMachineRocketProjectile
        { "Goblinstein",       500 },   // the Doctor
        { "Heal Spirit",       400 },
        { "Hunter",            550 },
        { "Ice Spirit",        400 },
        { "Ice Wizard",        700 },
        { "Lava Hound",        400 },
        { "Little Prince",     800 },
        { "Magic Archer",     1000 },
        { "Mother Witch",      600 },
        { "Musketeer",        1000 },
        { "Princess",          600 },
        { "Ram Rider",         600 },   // REC: RamRiderBola
        { "Rascals",           800 },   // the Rascal Girl
        { "Royal Giant",      1000 },
        { "Skeleton Dragons",  500 },
        { "Sparky",           1400 },
        { "Spear Goblins",     500 },   // also the Goblin Gang's and the Goblin Giant's
        { "Three Musketeers", 1000 },
        { "Witch",             600 },
        { "Wizard",            600 },
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
    static const std::string heroPrefix = "Hero ";
    if (name.compare(0, heroPrefix.size(), heroPrefix) == 0) return find(name.substr(heroPrefix.size()));
    return nullptr;
}

// The published speed of a card's shot, 0 if it has no row.
inline int forName(const std::string& name) {
    const Row* row = find(name);
    return row ? row->speed : 0;
}

// Tiles a tick of a card's shot.
inline float tilesPerTick(const std::string& name) {
    const Row* row = find(name);
    return row ? static_cast<float>(row->speed) / PUBLISHED_PER_TILE_PER_TICK : DEFAULT_TILES_PER_TICK;
}

} // namespace ProjectileSpeeds
