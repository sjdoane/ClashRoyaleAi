#pragma once

#include <cmath>
#include <cstring>
#include <memory>
#include "CardStats.h"

// Published reduced damage to Crown Towers (patch 0047).
//
// The game's data tables give some damage sources a `CrownTowerDamagePercent`
// (a negative percentage: -80 means a Crown Tower takes 20% of the damage).
// This is every row of release 15.535.13 (FirstLight-Zero card_specs.json and
// projectiles.json) whose card the engine has, applied by ONE rule in
// CardRegistry::add / addEvolution. Nothing else in the registry may carry a
// literal for these cards.
//
//   record (table)                 percent   engine card, what it scales
//   Miner (character)                -80     Miner: his hits
//   FireballSpell (projectile)       -75     Fireball
//   ArrowsSpell (projectile)         -80     Arrows, per wave
//   RocketSpell (projectile)         -77     Rocket
//   LighningSpell (projectile)       -75     Lightning, per strike
//   Zap (area effect)                -75     Zap
//   SnowballSpell (projectile)       -75     Giant Snowball
//   LogProjectileRolling             -87     The Log
//   ElectroWizardZap (area effect)  -100     Electro Wizard's deploy zap
//   GoblinMachineRocketProjectile    -50     Goblin Machine's rocket (unit -24)
//
// Before this table the spells carried hand-entered absolute values, several
// of them at older percentages (Fireball 159 = 23%, Arrows 31 = 25%, Rocket
// 371 = 25%, Lightning 286 = 27%, the Zap and Giant Snowball Evolutions at
// 30%), and the Miner had none: he hit Crown Towers for his full 194.
//
// NOT in the release table, so left as the registry has them: Poison,
// Earthquake, Tornado, Goblin Curse, Vines, Void, Barbarian Barrel, Royal
// Delivery (their reductions live in the buff table, which the release
// extract does not include). Freeze has a row (-75) but no damage in the
// engine. The Evolution forms (Zap_EV1, SnowballSpell_EV1) are not resolved in
// the extract; an Evolution inherits its base card's row, as in SightClips.h.
//
// Rounding: nearest, on the level-11 value (269 x 13% = 34.97 -> 35, the
// figure the registry already had for The Log from the wiki). A unit's hits go
// through CombatEntity::crownTowerHitFactor, which truncates (194 -> 38); a
// ranged unit's factor travels on its Projectile and scales the direct hit.
namespace CrownTowerDamage {

enum class Kind { Hit, Spell, SpawnEffect };

struct Row {
    const char* name;   // registry NAME, so Evolutions inherit the row
    Kind kind;
    int percent;        // published CrownTowerDamagePercent
    int onlyUnitId;     // 0 = every unit of that name; else that CardStats id
    // An amount an official balance note gives outright, where the rounded
    // percentage would be one off or the percentage is not published. 0 = none.
    int amount = 0;
};

inline const Row* table(int& count) {
    static const Row rows[] = {
        { "Miner",          Kind::Hit,         -80,   0 },
        // Fireball: -75% (172) in release 15.535.13; "Crown Tower Damage 172 ->
        // 159 (-8%)" in the official 8 Sep 2026 notes, i.e. -77% of 689.
        { "Fireball",       Kind::Spell,       -77,   0, 159 },
        { "Arrows",         Kind::Spell,       -80,   0 },
        { "Rocket",         Kind::Spell,       -77,   0 },
        { "Lightning",      Kind::Spell,       -75,   0 },
        { "Zap",            Kind::Spell,       -75,   0 },
        { "Giant Snowball", Kind::Spell,       -75,   0 },
        { "The Log",        Kind::Spell,       -87,   0 },
        { "Electro Wizard", Kind::SpawnEffect, -100,  0 },
        // The Hero is the same card record with a hero form; it shares the
        // deploy zap (one ElectroWizardZap area effect in the release).
        { "Hero Electro Wizard", Kind::SpawnEffect, -100, 0 },
        { "Goblin Machine", Kind::Hit,         -50, -24 },
        // Goblin Drill surfacing: "Crown Tower Spawn Damage 0 -> 20" (official
        // 6 Oct 2026 notes; removed on 4 Aug) against 84 on everything else.
        { "Goblin Drill",   Kind::SpawnEffect, -76,   0, 20 },
    };
    count = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
    return rows;
}

inline int scaled(int damage, int percent) {
    return static_cast<int>(std::lround(damage * (100 + percent) / 100.0));
}

// Applies every matching row to `stats` and to its secondary unit.
inline void apply(CardStats& stats) {
    int count = 0;
    const Row* rows = table(count);
    for (int i = 0; i < count; ++i) {
        const Row& row = rows[i];
        if (stats.name != row.name) continue;
        if (row.onlyUnitId != 0 && stats.id != row.onlyUnitId) continue;
        const bool isSpell = stats.archetype == Archetype::Spell;
        switch (row.kind) {
            case Kind::Hit:
                if (!isSpell) stats.crownTowerHitMultiplier = (100 + row.percent) / 100.0f;
                break;
            case Kind::Spell:
                if (isSpell) stats.spellCrownTowerDamage = row.amount > 0 ? row.amount : scaled(stats.damage, row.percent);
                break;
            case Kind::SpawnEffect:
                if (stats.spawnEffectDamage > 0) {
                    stats.spawnEffectCrownTowerDamage = row.amount > 0 ? row.amount : scaled(stats.spawnEffectDamage, row.percent);
                }
                break;
        }
    }
    if (stats.secondaryUnit) {
        auto unit = std::make_shared<CardStats>(*stats.secondaryUnit);
        apply(*unit);
        stats.secondaryUnit = std::move(unit);
    }
}

inline CardStats applied(CardStats stats) {
    apply(stats);
    return stats;
}

} // namespace CrownTowerDamage
