#pragma once

#include <string>

// Published deploy stagger of the multi-unit cards. Read by
// CardFactories::spawn: the units of one PLAYED card do not all finish
// deploying on the same tick.
//
// The game's card records carry `SummonDeployDelay` (milliseconds between one
// unit of the card and the next) and, for the two cards whose second body is
// another character, `SummonDeployDelaySecond`. Until patch 0059 every unit of
// a card had the same deploy time, so five Barbarians or six Royal Recruits
// began to move and to wind up on one tick.
//
// Source: FirstLight-Zero card_specs.json, release 15.535.13, the card
// records (not the unit records, whose `DeployDelay` is another field and is
// not used here).
//
//   SummonDeployDelay 100: Archers, Minions, Barbarians, Spear Goblins,
//     Minion Horde, Guards (SkeletonWarriors), Three Musketeers, Goblin Gang,
//     Elite Barbarians (AngryBarbarians), Royal Recruits, Bats, Zappies
//     (MiniSparkys), Wall Breakers, Royal Hogs, Skeleton Dragons. The Evolution
//     records that are resolved in the pack (Archers, Barbarians, Minion Horde,
//     Royal Recruits, Bats, Wall Breakers, Royal Hogs) carry the same 100.
//   SummonDeployDelay 200: Goblins.
//   SummonDeployDelaySecond 100: Rascals (the two Girls), Goblinstein (the
//     Doctor).
//   No such field: Skeletons, Skeleton Army, and every one-unit card. Fire
//     Spirits carries 100 with SummonNumber 1, which changes nothing.
//
// THE READING IS INFERRED, the field names are all that is published: unit k
// of the card (0, 1, 2, ...) finishes its deploy time k delays after the first,
// and unit k of a second group (k + 1) second-delays after it (a delay
// published for Goblinstein's single Doctor has no other meaning). Goblin
// Gang publishes no second delay, so its three Spear Goblins are not
// staggered. Which unit of a formation is first is not published: the order
// is the registry's spawnOffsets order.
//
// Keyed by registry NAME like LoadTimes.h: an Evolution shares the row and
// "Hero X" with no row of its own is X (assumed: the Hero records were not
// read). Applied only to a card that is PLAYED: CardFactories::spawn takes a
// `played` flag that only the registry's own spawn functions set. The Goblins
// of a Goblin Barrel or a Goblin Hut, the Barbarians of a Battle Ram and the
// Bats of a Night Witch are spawned by something else, have no record of this
// kind and are not staggered, although they share a name with a card that is.
namespace SummonDelays {

struct Row { const char* name; int ticks; int secondTicks; };

inline constexpr Row ROWS[] = {
    { "Archers", 1, 0 }, { "Minions", 1, 0 }, { "Barbarians", 1, 0 }, { "Spear Goblins", 1, 0 },
    { "Minion Horde", 1, 0 }, { "Guards", 1, 0 }, { "Three Musketeers", 1, 0 }, { "Goblin Gang", 1, 0 },
    { "Elite Barbarians", 1, 0 }, { "Royal Recruits", 1, 0 }, { "Bats", 1, 0 }, { "Zappies", 1, 0 },
    { "Wall Breakers", 1, 0 }, { "Royal Hogs", 1, 0 }, { "Skeleton Dragons", 1, 0 },
    { "Goblins", 2, 0 },
    { "Rascals", 0, 1 }, { "Goblinstein", 0, 1 },
};

inline const Row* findExact(const std::string& name) {
    for (const Row& r : ROWS) {
        if (name == r.name) return &r;
    }
    return nullptr;
}

// The row of a card: its own, or the plain card's for a "Hero X". nullptr for
// a card with no stagger.
inline const Row* find(const std::string& name) {
    if (const Row* r = findExact(name)) return r;
    static const std::string HERO = "Hero ";
    if (name.compare(0, HERO.size(), HERO) == 0) return findExact(name.substr(HERO.size()));
    return nullptr;
}

}  // namespace SummonDelays
