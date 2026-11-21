#pragma once

#include <string>

// Attack load time ("first hit"): how long a unit winds up after it comes
// into range of a new target before its first hit lands, in ticks (10 = 1 s).
// After that it hits every CardStats::attackCooldown. Read by
// CardFactories::applyCardMetadata, used in CombatEntity::update.
//
// Before this every unit hit the instant it came into range (a Hog 0.1 s after
// reaching a tower; the real game is 1.0 s), because the cooldown runs down
// while a unit walks. perception/UPSTREAM_REQUESTS.md item 40.
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
// Rounded to the nearest tick.
namespace LoadTimes {

enum class Source { FLZ, CRAPI };

struct Row {
    const char* name;
    int ticks;
    Source source;
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
        { "Battle Ram", 4, Source::FLZ }, // BattleRam 350 ms
        { "Bomb Tower", 13, Source::FLZ }, // BombTower 1300 ms
        { "Bomber", 16, Source::FLZ }, // Bomber 1600 ms
        { "Bowler", 20, Source::FLZ }, // Bowler 2000 ms
        { "Cannon", 1, Source::FLZ }, // Cannon 100 ms
        { "Cannon Cart", 4, Source::CRAPI }, // BrokenCannon 400 ms
        { "Dark Prince", 10, Source::FLZ }, // DarkPrince 1000 ms
        { "Dart Goblin", 4, Source::CRAPI }, // BlowdartGoblin 350 ms
        { "Electro Dragon", 14, Source::FLZ }, // ElectroDragon 1400 ms
        { "Electro Giant", 8, Source::FLZ }, // ElectroGiant 800 ms
        { "Electro Spirit", 1, Source::FLZ }, // ElectroSpirit 100 ms
        { "Electro Wizard", 12, Source::FLZ }, // ElectroWizard 1200 ms
        { "Elite Barbarians", 9, Source::CRAPI }, // AngryBarbarian 900 ms
        { "Elixir Golem", 3, Source::FLZ }, // ElixirGolem 300 ms
        { "Executioner", 4, Source::CRAPI }, // AxeMan 400 ms
        { "Fire Spirit", 1, Source::CRAPI }, // FireSpirits 100 ms
        { "Firecracker", 24, Source::FLZ }, // Firecracker 2350 ms
        { "Fisherman", 12, Source::FLZ }, // Fisherman 1200 ms
        { "Flying Machine", 6, Source::CRAPI }, // DartBarrell 600 ms
        { "Giant", 10, Source::FLZ }, // Giant 1000 ms
        { "Giant Skeleton", 10, Source::FLZ }, // GiantSkeleton 1000 ms
        { "Goblin Drill", 2, Source::FLZ }, // GoblinDrill 200 ms
        { "Goblin Gang", 5, Source::FLZ }, // GoblinGang 500 ms
        { "Goblin Giant", 7, Source::FLZ }, // GoblinGiant 700 ms
        { "Goblins", 5, Source::FLZ }, // Goblins 500 ms
        { "Golden Knight", 7, Source::FLZ }, // GoldenKnight 700 ms
        { "Golem", 15, Source::FLZ }, // Golem 1500 ms
        { "Guards", 6, Source::CRAPI }, // SkeletonWarrior 600 ms
        { "Heal Spirit", 1, Source::CRAPI }, // HealSpirit 100 ms
        { "Hero Giant", 10, Source::FLZ }, // Giant 1000 ms
        { "Hero Goblins", 5, Source::FLZ }, // Goblins 500 ms
        { "Hero Ice Golem", 15, Source::CRAPI }, // IceGolemite 1500 ms
        { "Hero Knight", 7, Source::FLZ }, // Knight 700 ms
        { "Hero Magic Archer", 6, Source::CRAPI }, // EliteArcher 600 ms
        { "Hero Mega Minion", 11, Source::FLZ }, // MegaMinion 1100 ms
        { "Hero Mini P.E.K.K.A.", 11, Source::FLZ }, // MiniPekka 1100 ms
        { "Hero Musketeer", 3, Source::FLZ }, // Musketeer 300 ms
        { "Hero Wizard", 10, Source::FLZ }, // Wizard 1000 ms
        { "Hog Rider", 10, Source::FLZ }, // HogRider 1000 ms
        { "Hunter", 15, Source::FLZ }, // Hunter 1500 ms
        { "Ice Golem", 15, Source::CRAPI }, // IceGolemite 1500 ms
        { "Ice Spirit", 1, Source::FLZ }, // IceSpirits 100 ms
        { "Ice Wizard", 12, Source::FLZ }, // IceWizard 1200 ms
        { "Inferno Dragon", 12, Source::FLZ }, // InfernoDragon 1200 ms
        { "Inferno Tower", 12, Source::FLZ }, // InfernoTower 1200 ms
        { "Knight", 7, Source::FLZ }, // Knight 700 ms
        { "Lava Hound", 3, Source::FLZ }, // LavaHound 300 ms
        { "Lumberjack", 4, Source::CRAPI }, // RageBarbarian 400 ms
        { "Magic Archer", 6, Source::CRAPI }, // EliteArcher 600 ms
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
        { "Sparky", 30, Source::CRAPI }, // ZapMachine 3000 ms
        { "Spear Goblins", 11, Source::FLZ }, // SpearGoblins 1100 ms
        { "Tesla", 7, Source::FLZ }, // Tesla 700 ms
        { "Three Musketeers", 7, Source::FLZ }, // ThreeMusketeers 700 ms
        { "Valkyrie", 14, Source::FLZ }, // Valkyrie 1400 ms
        { "Wall Breakers", 10, Source::FLZ }, // Wallbreakers 1000 ms
        { "Witch", 4, Source::FLZ }, // Witch 400 ms
        { "Wizard", 10, Source::FLZ }, // Wizard 1000 ms
    };
    count = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
    return rows;
}

inline int forName(const std::string& name) {
    int n = 0;
    const Row* rows = table(n);
    for (int i = 0; i < n; ++i) {
        if (name == rows[i].name) return rows[i].ticks;
    }
    return 0;
}

} // namespace LoadTimes
