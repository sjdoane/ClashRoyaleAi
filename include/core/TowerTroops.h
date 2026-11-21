#pragma once
#include "CardStats.h"
#include <memory>

// Crown Tower stats at level 11, the level the registry's card stats are
// (Clash Royale wiki, Princess Tower and King's Tower level tables). They were
// level 9 (2534 / 4008 hp, 90 damage) against level-11 troops, so every tower
// was about 17% weaker than the troops attacking it.
// perception/UPSTREAM_REQUESTS.md item 41.
namespace CrownTower {
    constexpr int LEVEL = 11;
    constexpr int PRINCESS_HP = 3052;
    constexpr int PRINCESS_DAMAGE = 109;
    constexpr int PRINCESS_COOLDOWN_TICKS = 8;   // 0.8 s
    constexpr int KING_HP = 4824;
    constexpr int KING_DAMAGE = 109;
    constexpr int KING_COOLDOWN_TICKS = 10;      // 1.0 s
}

// The four selectable Tower Troops; one replaces both Princess Towers for the
// match (GameManager::addTower, reset). None is the default: the plain
// Princess Tower (CrownTower, 7.5 range), kept byte-for-byte as it was.
enum class TowerTroopType {
    None,
    TowerPrincess,
    Cannoneer,
    DaggerDuchess,
    RoyalChef
};

// Royal Chef's cooking (Clash Royale wiki, Royal Chef, 4 Aug 2025 values):
// none for the first 7 s, then a pancake every 23 s while neither of his
// towers attacks and 38 s while both do (a destroyed tower counts as
// attacking); 30.5 s for one is interpolated, the wiki gives only the range.
// Both towers down: no more cooking. GameManager::cookRoyalChef runs it.
namespace RoyalChef {
    constexpr int COOK_START_TICKS = 70;
    constexpr int COOK_TICKS[3] = { 230, 305, 380 };   // 0, 1, 2 towers busy
}

// Symbol stays 'P' for every variant (see GameManager::addTower). Fields are
// set directly because towers are built by GameManager, not registered cards.
//
// Level-11 stats from the Clash Royale wiki pages (current as of the 4 Aug and
// 1 Sep 2025 balance changes; the 2026 notes did not touch Tower Troops).
// "First hit" is the sourced first-attack time, as a load time (LoadTimes.h)
// the name-keyed table cannot give a tower.
inline CardStats towerTroopStats(TowerTroopType type) {
    CardStats stats;
    stats.spawnOffsets = { {0.0f, 0.0f} };
    // The Princess Tower's sourced sight range, applied to every variant.
    stats.sightRange = 7.5f;
    // Must be set explicitly: applyCardMetadata runs after the Tower
    // constructor and overwrites targetsAir with this struct's value.
    stats.targetsAir = true;

    switch (type) {
        case TowerTroopType::TowerPrincess:
            // 3052 hp / 109 dmg, 0.8 s, first hit 0.8 s, range 7.5: the plain
            // Princess Tower's numbers plus its first-hit time.
            stats.hp = CrownTower::PRINCESS_HP; stats.attackRange = 7.5f;
            stats.damage = CrownTower::PRINCESS_DAMAGE; stats.attackCooldown = CrownTower::PRINCESS_COOLDOWN_TICKS;
            stats.withLoadTicks(8);
            break;
        case TowerTroopType::Cannoneer:
            // 2616 hp / 320 dmg, 2.2 s (1 Sep 2025), first hit 0.8 s, no splash.
            stats.hp = 2616; stats.attackRange = 7.5f; stats.damage = 320; stats.attackCooldown = 22;
            stats.withLoadTicks(8);
            break;
        case TowerTroopType::DaggerDuchess:
            // 2768 hp / 107 dmg per dagger, 0.5 s, 8 daggers, 0.9 s per dagger
            // to reload: 1.4 s a dagger once empty (the wiki's 76 "empty dagger"
            // dps = 107 / 1.4).
            stats.hp = 2768; stats.attackRange = 7.5f; stats.damage = 107; stats.attackCooldown = 5;
            stats.withMagazine(8, 9);
            break;
        case TowerTroopType::RoyalChef:
            // 2703 hp / 109 dmg, 1.0 s, first hit 0.8 s. The pancake is
            // GameManager's (RoyalChef above, RoyalChefBuffEffect.h).
            stats.hp = 2703; stats.attackRange = 7.5f; stats.damage = 109; stats.attackCooldown = 10;
            stats.withLoadTicks(8);
            break;
        case TowerTroopType::None:
        default:
            stats.hp = CrownTower::PRINCESS_HP; stats.attackRange = 7.5f;
            stats.damage = CrownTower::PRINCESS_DAMAGE; stats.attackCooldown = CrownTower::PRINCESS_COOLDOWN_TICKS;
            break;
    }
    return stats;
}
