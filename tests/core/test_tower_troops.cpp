#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "TowerTroops.h"
#include "GameManager.h"
#include "ClashEnv.h"
#include "CombatEntity.h"
#include "MeleeTroop.h"
#include "RoyalChefBuffEffect.h"
#include "Tower.h"
#include <memory>

TEST_CASE("towerTroopStats(None) is the plain level-11 Princess Tower",
        "[tower_troops]") {
    CardStats stats = towerTroopStats(TowerTroopType::None);
    REQUIRE(stats.hp == CrownTower::PRINCESS_HP);
    REQUIRE(stats.attackRange == Catch::Approx(7.5f));
    REQUIRE(stats.damage == CrownTower::PRINCESS_DAMAGE);
    REQUIRE(stats.attackCooldown == 8);
    // Patch 0035: the plain tower has the 0.8 s first hit as well.
    REQUIRE(stats.loadTicksOverride == CrownTower::PRINCESS_FIRST_HIT_TICKS);
    REQUIRE(CrownTower::PRINCESS_FIRST_HIT_TICKS == 8);
}

// Patch 0019: the wiki's level-11 values (TowerTroops.h). The old ones were
// 3204 / 153 for Tower Princess, the plain tower's 3052 / 109 / 0.8 s for
// Cannoneer, 2298 hp and an 8x burst for Dagger Duchess, and a periodic
// effect for Royal Chef.
TEST_CASE("towerTroopStats(TowerPrincess) is the plain tower's stats with its 0.8 s first hit", "[tower_troops]") {
    CardStats stats = towerTroopStats(TowerTroopType::TowerPrincess);
    REQUIRE(stats.hp == 3052);
    REQUIRE(stats.damage == 109);
    REQUIRE(stats.attackCooldown == 8);
    REQUIRE(stats.loadTicksOverride == 8);
}

TEST_CASE("towerTroopStats(Cannoneer) gives the sourced stats with no splash", "[tower_troops]") {
    CardStats stats = towerTroopStats(TowerTroopType::Cannoneer);
    REQUIRE(stats.hp == 2616);
    REQUIRE(stats.damage == 320);
    REQUIRE(stats.attackCooldown == 22);
    REQUIRE(stats.loadTicksOverride == 8);
    REQUIRE(stats.splashRadius == Catch::Approx(0.0f));
}

TEST_CASE("towerTroopStats(DaggerDuchess) carries an 8-dagger magazine", "[tower_troops]") {
    CardStats stats = towerTroopStats(TowerTroopType::DaggerDuchess);
    REQUIRE(stats.hp == 2768);
    REQUIRE(stats.damage == 107);
    REQUIRE(stats.attackCooldown == 5);
    REQUIRE(stats.magazineSize == 8);
    REQUIRE(stats.magazineReloadTicks == 9);
    REQUIRE(stats.burstEveryNAttacks == 0);
}

TEST_CASE("towerTroopStats(RoyalChef): sourced combat stats; the pancake is cooked by GameManager", "[tower_troops]") {
    CardStats stats = towerTroopStats(TowerTroopType::RoyalChef);
    REQUIRE(stats.hp == 2703);
    REQUIRE(stats.damage == 109);
    REQUIRE(stats.attackCooldown == 10);
    REQUIRE(stats.periodicEffect == nullptr);
    REQUIRE(RoyalChef::COOK_START_TICKS == 70);
    REQUIRE(RoyalChef::COOK_TICKS[0] == 230);
    REQUIRE(RoyalChef::COOK_TICKS[2] == 380);
}

TEST_CASE("GameManager defaults to TowerTroopType::None -- existing behavior byte-for-byte unaffected",
        "[game_manager][tower_troops]") {
    GameManager game({ 0,1,2,3,4,5,6,7 }, { 0,1,2,3,4,5,6,7 });

    int princessCount = 0;
    for (const auto& e : game.getBoard().getEntities()) {
        if (e->name == "Princess Tower") {
            princessCount++;
            REQUIRE(e->hp == CrownTower::PRINCESS_HP);
        }
    }
    REQUIRE(princessCount == 4);
}

TEST_CASE("GameManager wires the selected Tower Troop onto both of that team's Princess Towers",
        "[game_manager][tower_troops]") {
    GameManager game({ 0,1,2,3,4,5,6,7 }, { 0,1,2,3,4,5,6,7 },
        TowerTroopType::Cannoneer, TowerTroopType::TowerPrincess);

    int aiPrincessCount = 0, oppPrincessCount = 0;
    for (const auto& e : game.getBoard().getEntities()) {
        if (e->name != "Princess Tower") continue;
        if (e->team == 0) { aiPrincessCount++; REQUIRE(e->hp == 2616); }       // Cannoneer
        if (e->team == 1) { oppPrincessCount++; REQUIRE(e->hp == 3052); }     // Tower Princess
    }
    REQUIRE(aiPrincessCount == 2);
    REQUIRE(oppPrincessCount == 2);
}

TEST_CASE("RoyalChefBuffEffect feeds an allied troop +1 level, never a building or tower", "[tower_troops][royal_chef]") {
    // Patch 0019: +1 Level (wiki), was a +10% hp top-up with a damage buff.
    Board board;
    auto combatAlly = std::make_shared<StationaryCombatant>(2, 3.0f, 0.0f, 1000, 0, 1.0f, 100, 100);
    spawn(board, combatAlly);
    auto tower = std::make_shared<Tower>(3, 0.0f, 0.0f, 3000, 0, 7.5f, 109, 8, 'P');
    spawn(board, tower);

    RoyalChefBuffEffect effect;
    effect.apply(board, Vector2D{ 0.0f, 0.0f }, 0); // fires from the tower's own position

    REQUIRE(combatAlly->currentLevel == 12);
    REQUIRE(combatAlly->hp == 1098);   // 1000 * 281 / 256
    REQUIRE(combatAlly->buffTicksRemaining == 0);
    REQUIRE(tower->hp == 3000);
    REQUIRE(tower->royalChefServings == 0);
}


TEST_CASE("the Royal Chef feeds an ally once, not compounding forever",
          "[tower_troops][royal_chef][regression]") {
    // The original concern: a tank parked beside the tower fed every time
    // the effect fired. Since patch 0019 one pancake is one level, the Chef
    // spreads them (fewest pancakes first), and GameManager cooks at most one
    // per 23 s (test_cards_0019.cpp). A repeat feeding of the same troop
    // happens only when no unfed troop qualifies, as in the game (wiki: "It's
    // possible to level up a single troop multiple times").
    Board board;
    auto ally = std::make_shared<MeleeTroop>(1, 5.0f, 6.0f, 1000, 0, 0.1f, 1.0f, 100, 10, 'a');
    spawn(board, ally);
    auto other = std::make_shared<MeleeTroop>(2, 6.0f, 6.0f, 500, 0, 0.1f, 1.0f, 100, 10, 'b');
    spawn(board, other);

    RoyalChefBuffEffect chef;
    chef.apply(board, Vector2D{ 5.0f, 5.0f }, 0);
    const int afterFirst = ally->hp;

    // Control: the first serving did something, and went to the bigger troop.
    REQUIRE(afterFirst == 1098);
    REQUIRE(ally->currentLevel == 12);

    // The second goes to the troop not yet fed, not the tank again.
    chef.apply(board, Vector2D{ 5.0f, 5.0f }, 0);
    INFO("hp after one serving = " << afterFirst << ", after two = " << ally->hp);
    REQUIRE(ally->hp == afterFirst);
    REQUIRE(other->currentLevel == 12);
}

TEST_CASE("Crown Towers are level 11, the level of the registry's card stats", "[tower_troops][level]") {
    // Clash Royale wiki level tables (UPSTREAM item 41). Level 9 was 2534 / 4008 / 90.
    REQUIRE(CrownTower::LEVEL == 11);
    REQUIRE(CrownTower::PRINCESS_HP == 3052);
    REQUIRE(CrownTower::PRINCESS_DAMAGE == 109);
    REQUIRE(CrownTower::KING_HP == 4824);
    REQUIRE(CrownTower::KING_DAMAGE == 109);
    // The building normaliser follows the King, so a full King still reads 1.0.
    REQUIRE(ClashEnv::MAX_BUILDING_HP == Catch::Approx(static_cast<float>(CrownTower::KING_HP)));
}
