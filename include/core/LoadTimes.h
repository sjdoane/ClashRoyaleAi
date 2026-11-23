#pragma once

#include <string>

// Attack load time, in ticks (10 = 1 s), and the first-hit wind-up that
// follows from it. Read by CardFactories::applyCardMetadata, used in
// CombatEntity::update.
//
// The published "LoadTime" is the part of the attack cycle a unit can do in
// advance, with no target: a unit walks around already loaded. What is left
// once it has a target in range is
//
//     first hit = hit speed - load time            (firstHitTicks below)
//
// and later hits follow every hit speed. A Hog Rider (hit speed 1.6 s, load
// time 1.0 s) lands its first hit 0.6 s after reaching a building; a Princess
// (3.0 s, 2.7 s) 0.3 s after stopping; a Cannon (1.0 s, 0.1 s) 0.9 s after
// seeing something. Sources: RoyaleAPI lists "First Hit" as exactly this
// difference; a live recording (runs/live/20261120-185026, patch 0034) has the
// Hog's first hit 0.58-0.75 s after it reaches the tower.
//
// History: before patch 0006 every unit hit the tick it came into range.
// Patch 0006 added the wind-up but used the load time itself as its length
// (Hog 1.0 s, Princess 2.7 s, Cannon 0.1 s), which is the complement of the
// real number. Patch 0034 corrected it to the difference.
//
// Keyed by the registry NAME, not the id, because Evolutions reuse the base
// card's name verbatim and their load time is the base card's. A name not in
// the table has no load time (the old behaviour): buildings that only spawn,
// spawned bodies with their own names, and the Crown Towers.
//
// Sources, as the comment on each row says:
//   FLZ   - FirstLight-Zero's published card_specs.json (release 15.535.13),
//           "LoadTime" in ms. Preferred: it is the current release.
//   CRAPI - RoyaleAPI cr-api-data cards_stats.json "load_time" in ms, where
//           FLZ has no row. Older game files.
//   RECORDING - the two published values disagree and a recording decides.
//   NOTES - an official balance note dated after release 15.535.13 (4 Aug
//           2026) gives the first-hit time outright; the row is hit speed
//           minus that.
// Rounded to the nearest tick.
//
// The Goblin (Goblins, Goblin Gang, Hero Goblins, and the Goblin Barrel's and
// Goblin Drill's, which are named "Goblins"): FLZ says 500 ms, the older
// CRAPI file 700 ms. With hit speed 1.1 s that is a first stab 0.6 or 0.4 s
// after deploying. A Goblin Barrel's Goblins in the recording of 2026-11-20
// first stab 1.43-1.53 s after landing and their fourth stab
// comes 0.23 s before the arrow that kills the last one; 700 ms gives 1.5 s
// and that margin here, 500 ms gives 1.7 s and a fourth stab on the same tick
// as the arrow. 700 ms it is (patch 0038).
namespace LoadTimes {

enum class Source { FLZ, CRAPI, RECORDING, NOTES };

struct Row {
    const char* name;
    int ticks;
    Source source;
    // The published hit speed in ticks, for the few cards whose engine
    // attackCooldown is not it (a spirit's cooldown is never used again; the
    // Executioner's covers his axe's flight out and back). 0 = the card's
    // attackCooldown is the published hit speed.
    int hitSpeedTicks = 0;
    // Published LoadFirstHit: the unit lands UNLOADED and has to load before
    // its first hit like before any other. Every other unit lands loaded.
    // One unit in release 15.535.13 has it (ZapMachine); RoyaleAPI's "secret
    // stats" article says the same: troops enter the arena preloaded, Sparky
    // is the only one that does not.
    bool loadFirstHit = false;
};

inline const Row* table(int& count) {
    static const Row rows[] = {
        { "Archer Queen", 9, Source::FLZ }, // ArcherQueen 900 ms
        { "Archers", 4, Source::CRAPI }, // Archer 400 ms
        { "Baby Dragon", 12, Source::FLZ }, // BabyDragon 1200 ms
        { "Balloon", 18, Source::FLZ }, // Balloon 1800 ms
        { "Bandit", 6, Source::CRAPI }, // Assassin 600 ms
        { "Barbarians", 10, Source::FLZ }, // Barbarians 1000 ms
        { "Bats", 6, Source::FLZ }, // Bats 600 ms
        { "Battle Healer", 16, Source::FLZ }, // BattleHealer 1600 ms
        { "Battle Ram", 3, Source::FLZ, 4 }, // BattleRam 350 ms of a 400 ms hit speed: first hit 50 ms, one tick here (4 would read as "not shorter than the hit speed")
        { "Bomb Tower", 13, Source::FLZ }, // BombTower 1300 ms
        { "Bomber", 16, Source::FLZ }, // Bomber 1600 ms
        { "Bowler", 20, Source::FLZ }, // Bowler 2000 ms
        { "Cannon", 1, Source::FLZ }, // Cannon 100 ms
        { "Cannon Cart", 4, Source::CRAPI }, // BrokenCannon 400 ms
        { "Dark Prince", 10, Source::FLZ }, // DarkPrince 1000 ms
        { "Dart Goblin", 4, Source::CRAPI }, // BlowdartGoblin 350 ms
        { "Electro Dragon", 16, Source::NOTES }, // first hit 0.7 -> 0.5 s (19 Oct 2026 notes) of a 2.1 s hit speed; FLZ ElectroDragon 1400 ms
        { "Electro Giant", 8, Source::FLZ }, // ElectroGiant 800 ms
        { "Electro Spirit", 1, Source::FLZ, 3 }, // ElectroSpirit 100 ms, hit speed 300 ms
        { "Electro Wizard", 12, Source::FLZ }, // ElectroWizard 1200 ms
        { "Elite Barbarians", 9, Source::CRAPI }, // AngryBarbarian 900 ms
        { "Elixir Golem", 3, Source::FLZ }, // ElixirGolem 300 ms
        { "Executioner", 4, Source::CRAPI, 9 }, // AxeMan 400 ms, hit speed 900 ms
        { "Fire Spirit", 1, Source::CRAPI, 3 }, // FireSpirits 100 ms, hit speed 300 ms
        { "Firecracker", 24, Source::FLZ }, // Firecracker 2350 ms
        { "Fisherman", 12, Source::FLZ }, // Fisherman 1200 ms
        { "Flying Machine", 6, Source::CRAPI }, // DartBarrell 600 ms
        { "Giant", 10, Source::FLZ }, // Giant 1000 ms
        { "Giant Skeleton", 10, Source::FLZ }, // GiantSkeleton 1000 ms
        { "Goblin Drill", 2, Source::FLZ, 11 }, // GoblinDrill 200 ms, hit speed 1100 ms
        { "Goblin Gang", 7, Source::RECORDING }, // Goblin: FLZ 500 ms, CRAPI 700 ms; see below
        { "Goblin Giant", 7, Source::FLZ }, // GoblinGiant 700 ms
        { "Goblins", 7, Source::RECORDING }, // Goblin: FLZ 500 ms, CRAPI 700 ms; see below
        { "Golden Knight", 7, Source::FLZ }, // GoldenKnight 700 ms
        { "Golem", 15, Source::FLZ }, // Golem 1500 ms
        { "Guards", 5, Source::FLZ }, // SkeletonWarriors 500 ms (CRAPI 600)
        { "Heal Spirit", 1, Source::CRAPI, 3 }, // HealSpirit 100 ms, hit speed 300 ms
        { "Hero Giant", 10, Source::FLZ }, // Giant 1000 ms
        { "Hero Goblins", 7, Source::RECORDING }, // Goblin: FLZ 500 ms, CRAPI 700 ms; see below
        { "Hero Ice Golem", 15, Source::CRAPI }, // IceGolemite 1500 ms
        { "Hero Knight", 7, Source::FLZ }, // Knight 700 ms
        { "Hero Magic Archer", 4, Source::FLZ }, // EliteArcher 400 ms (CRAPI 600)
        { "Hero Mega Minion", 11, Source::FLZ }, // MegaMinion 1100 ms
        { "Hero Mini P.E.K.K.A.", 11, Source::FLZ }, // MiniPekka 1100 ms
        { "Hero Musketeer", 3, Source::FLZ }, // Musketeer 300 ms
        { "Hero Wizard", 9, Source::NOTES }, // as Wizard
        { "Hog Rider", 10, Source::FLZ }, // HogRider 1000 ms
        { "Hunter", 15, Source::FLZ }, // Hunter 1500 ms
        { "Ice Golem", 15, Source::CRAPI }, // IceGolemite 1500 ms
        { "Ice Spirit", 1, Source::FLZ, 3 }, // IceSpirits 100 ms, hit speed 300 ms
        { "Ice Wizard", 12, Source::FLZ }, // IceWizard 1200 ms
        { "Inferno Dragon", 12, Source::FLZ }, // InfernoDragon 1200 ms
        { "Inferno Tower", 12, Source::FLZ }, // InfernoTower 1200 ms
        { "Knight", 7, Source::FLZ }, // Knight 700 ms
        { "Lava Hound", 3, Source::FLZ }, // LavaHound 300 ms
        { "Lumberjack", 4, Source::CRAPI }, // RageBarbarian 400 ms
        { "Magic Archer", 4, Source::FLZ }, // EliteArcher 400 ms (CRAPI 600)
        { "Mega Knight", 12, Source::FLZ }, // MegaKnight 1200 ms
        { "Mega Minion", 11, Source::FLZ }, // MegaMinion 1100 ms
        { "Mighty Miner", 7, Source::FLZ }, // MightyMiner 700 ms
        { "Miner", 8, Source::FLZ }, // Miner 800 ms
        { "Mini PEKKA", 11, Source::FLZ }, // MiniPekka 1100 ms
        { "Minion Horde", 7, Source::FLZ }, // MinionHorde 700 ms
        { "Minions", 7, Source::FLZ }, // Minions 700 ms
        { "Monk", 6, Source::FLZ }, // Monk 600 ms
        { "Mortar", 37, Source::FLZ }, // Mortar 3700 ms
        { "Mother Witch", 7, Source::CRAPI }, // WitchMother 700 ms
        { "Musketeer", 3, Source::FLZ }, // Musketeer 300 ms
        { "Night Witch", 6, Source::CRAPI }, // DarkWitch 550 ms
        { "P.E.K.K.A.", 13, Source::FLZ }, // Pekka 1300 ms
        { "Phoenix", 4, Source::FLZ }, // Phoenix 400 ms
        { "Prince", 9, Source::FLZ }, // Prince 900 ms
        { "Princess", 27, Source::FLZ }, // Princess 2700 ms
        { "Ram Rider", 11, Source::FLZ }, // RamRider 1100 ms
        { "Rascals", 11, Source::FLZ }, // Rascals 1100 ms
        { "Royal Ghost", 12, Source::CRAPI }, // Ghost 1200 ms
        { "Royal Giant", 9, Source::FLZ }, // RoyalGiant 900 ms
        { "Royal Hogs", 8, Source::FLZ }, // RoyalHogs 800 ms
        { "Skeleton Army", 6, Source::FLZ }, // SkeletonArmy 600 ms
        { "Skeleton Dragons", 16, Source::FLZ }, // SkeletonDragons 1600 ms
        { "Skeleton King", 13, Source::FLZ }, // SkeletonKing 1300 ms
        { "Skeletons", 6, Source::FLZ }, // Skeletons 600 ms
        { "Sparky", 30, Source::CRAPI, 0, true }, // ZapMachine 3000 ms, LoadFirstHit
        { "Spear Goblins", 11, Source::FLZ }, // SpearGoblins 1100 ms
        { "Tesla", 7, Source::FLZ }, // Tesla 700 ms
        { "Three Musketeers", 7, Source::FLZ }, // ThreeMusketeers 700 ms
        { "Valkyrie", 14, Source::FLZ }, // Valkyrie 1400 ms
        { "Wall Breakers", 10, Source::FLZ }, // Wallbreakers 1000 ms
        { "Witch", 4, Source::FLZ }, // Witch 400 ms
        { "Wizard", 9, Source::NOTES }, // first attack 0.4 -> 0.5 s (8 Sep 2026 notes) of a 1.4 s hit speed; FLZ Wizard 1000 ms
        // Added by the 15.535.13 audit (patch 0048): cards that had no row.
        { "Berserker", 4, Source::FLZ }, // Berserker 400 ms
        { "Boss Bandit", 7, Source::FLZ }, // BossBandit 700 ms
        { "Furnace", 12, Source::FLZ }, // FirespiritHut (the walking Furnace) 1200 ms
        { "Goblin Demolisher", 6, Source::FLZ }, // GoblinDemolisher 600 ms
        { "Goblin Machine", 7, Source::FLZ }, // GoblinMachine 700 ms (the body; the rocket turret overrides)
        { "Goblinstein", 7, Source::FLZ }, // goblinstein 700 ms (the Monster; the Doctor overrides: 1300 of 1800 ms)
        { "Little Prince", 8, Source::FLZ }, // LittlePrince 800 ms
        { "Ronin", 10, Source::FLZ }, // Ronin 1000 ms
        { "Royal Recruits", 8, Source::FLZ }, // RoyalRecruit 800 ms
        { "Rune Giant", 10, Source::FLZ }, // GiantBuffer 1000 ms
        { "Skeleton Barrel", 2, Source::FLZ, 3 }, // SkeletonBalloon 200 ms, hit speed 300 ms
        { "Zappies", 14, Source::FLZ }, // MiniSparkys 1400 ms
    };
    count = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
    return rows;
}

inline const Row* find(const std::string& name) {
    int n = 0;
    const Row* rows = table(n);
    for (int i = 0; i < n; ++i) {
        if (name == rows[i].name) return &rows[i];
    }
    // A Hero is its base card's character with an ability: "Hero X" with no
    // row of its own loads like X.
    static const std::string heroPrefix = "Hero ";
    if (name.compare(0, heroPrefix.size(), heroPrefix) == 0) return find(name.substr(heroPrefix.size()));
    return nullptr;
}

// Whether a card lands unloaded (published LoadFirstHit).
inline bool landsUnloaded(const std::string& name) {
    const Row* row = find(name);
    return row && row->loadFirstHit;
}

// The published load time of a card, 0 if it has no row.
inline int forName(const std::string& name) {
    const Row* row = find(name);
    return row ? row->ticks : 0;
}

// The first-hit wind-up for a load time and a hit speed, both in ticks: how
// long a unit that already walks loaded still needs once a target is in range.
// No load time: no wind-up, the unit hits at once. A load time no shorter than
// the hit speed (the Inferno beams: 1.2 s against 0.4 s) cannot be done in
// advance within the cycle, so it stays the whole wind-up.
inline int firstHitTicksFor(int loadTicks, int hitSpeedTicks) {
    if (loadTicks <= 0) return 0;
    if (loadTicks >= hitSpeedTicks) return loadTicks;
    return hitSpeedTicks - loadTicks;
}

// The first-hit wind-up of a card whose attackCooldown is `attackCooldownTicks`
// (taken as its hit speed unless the row carries the published one).
inline int firstHitTicks(const std::string& name, int attackCooldownTicks) {
    const Row* row = find(name);
    if (!row) return 0;
    return firstHitTicksFor(row->ticks, row->hitSpeedTicks > 0 ? row->hitSpeedTicks : attackCooldownTicks);
}

} // namespace LoadTimes
