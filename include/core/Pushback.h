#pragma once

#include <string>

// Published knockback immunity. Read by CardFactories::applyCardMetadata
// (CombatEntity::pushbackImmune), honoured by pushAway (Entity.h).
//
// The game's unit records carry `IgnorePushback`; its projectile records carry
// `Pushback` (thousandths of a tile) and, on a few, `PushbackAll`. A knockback
// moves every unit it hits except one that ignores pushback, unless the
// knockback is a PushbackAll. Until patch 0053 the engine pushed every
// non-building with every knockback: a Fireball shoved a Golem a tile back.
//
// Published knockbacks (release 15.535.13):
//   FireballSpell 1000, RocketSpell 1800, SnowballSpell 1800,
//   LogProjectileRolling 700 PushbackAll, BowlerProjectile 1000,
//   MegaKnightAppear 1000, GoblinDemolisherDeathProjectile 2000,
//   MonkPushback 1800 PushbackAll, BarbLog none.
// In the engine the radial area knockbacks (AreaSpell::knockback, pushAway)
// are the ordinary kind; the rolling throw (pushAlong: The Log) is the
// PushbackAll kind and moves everything on the ground.
//
// Source: FirstLight-Zero card_specs.json, release 15.535.13, the summoned
// unit of each card. Keyed by registry NAME like LoadTimes.h: an Evolution
// shares the row (the evolved unit records of P.E.K.K.A., Royal Giant, Mega
// Knight and Goblin Giant carry the flag too) and "Hero X" falls back to X.
// What a card spawns under another name (Golemite, Elixir Golemite, Lava Pup)
// has no flag and no row. "Goblinstein" is the Monster; the Doctor shares the
// registry name and so the row, which is not published for him.
namespace Pushback {

inline const char* const* table(int& count) {
    static const char* const rows[] = {
        "Bowler",
        "Cannon Cart",       // MovingCannon
        "Dark Prince",
        "Electro Giant",
        "Elixir Golem",      // ElixirGolem1: the whole Golem only
        "Giant",
        "Giant Skeleton",
        "Goblin Giant",
        "Goblin Machine",
        "Goblinstein",
        "Golem",
        "Lava Hound",
        "Mega Knight",
        "Mighty Miner",
        "P.E.K.K.A.",
        "Prince",
        "Royal Giant",
        "Rune Giant",        // GiantBuffer
        "Skeleton King",
        "Sparky",            // ZapMachine
    };
    count = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
    return rows;
}

// Whether a card's unit ignores ordinary knockbacks.
inline bool ignores(const std::string& name) {
    int count = 0;
    const char* const* rows = table(count);
    for (int i = 0; i < count; ++i) {
        if (name == rows[i]) return true;
    }
    static const std::string heroPrefix = "Hero ";
    if (name.compare(0, heroPrefix.size(), heroPrefix) == 0) return ignores(name.substr(heroPrefix.size()));
    return false;
}

} // namespace Pushback
