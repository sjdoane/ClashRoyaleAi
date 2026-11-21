#include <catch_amalgamated.hpp>
#include "CardLevels.h"
#include "GameManager.h"
#include "TowerTroops.h"
#include "ArenaLayout.h"
#include <memory>

// UPSTREAM_REQUESTS.md item 42: per-team card and Crown Tower levels.

namespace {
// hp of the first non-tower entity `team` has on the board.
int firstUnitHp(const GameManager& game, int team) {
    for (const auto& e : game.getBoard().getEntities()) {
        if (e->team == team && !e->isTower()) return e->hp;
    }
    return -1;
}
} // namespace

TEST_CASE("the level curve is the published power-level table, pinned to level 11", "[card_levels]") {
    REQUIRE(CardLevels::MULTIPLIER_PERCENT[CardLevels::TOURNAMENT_LEVEL - 1] == 256);
    for (int l = CardLevels::MIN_LEVEL; l < CardLevels::MAX_LEVEL; ++l) {
        REQUIRE(CardLevels::MULTIPLIER_PERCENT[l] > CardLevels::MULTIPLIER_PERCENT[l - 1]);
    }
    // Hog Rider, level 11 -> 15: 663 * 3.72 and 124 * 3.72 in the real table.
    REQUIRE(CardLevels::scale(1697, 15) == 2466);
    REQUIRE(CardLevels::scale(317, 15) == 461);
    // Princess Tower: 4424 read live at level 15, 4032 at 14 (within 0.3%).
    REQUIRE(CardLevels::scale(CrownTower::PRINCESS_HP, 15) == Catch::Approx(4424).epsilon(0.003));
    REQUIRE(CardLevels::scale(CrownTower::PRINCESS_HP, 14) == Catch::Approx(4032).epsilon(0.003));
    // Level 11, sentinels and out-of-range levels pass through.
    REQUIRE(CardLevels::scale(1697, 11) == 1697);
    REQUIRE(CardLevels::scale(-1, 15) == -1);
    REQUIRE(CardLevels::scale(0, 15) == 0);
    REQUIRE(CardLevels::scale(1697, 0) == 1697);
    REQUIRE(CardLevels::scale(1697, 17) == 1697);
}

TEST_CASE("setTeamLevel refuses bad values and changes nothing", "[card_levels]") {
    GameManager game({ 0,1,2,3,4,5,6,7 }, { 0,1,2,3,4,5,6,7 });
    REQUIRE_FALSE(game.setTeamLevel(2, 15));
    REQUIRE_FALSE(game.setTeamLevel(1, 0));
    REQUIRE_FALSE(game.setTeamLevel(1, 17));
    REQUIRE_FALSE(game.setTeamLevel(1, 15, 17));
    REQUIRE(game.getTeamCardLevel(1) == CardLevels::TOURNAMENT_LEVEL);
    REQUIRE(game.getTeamTowerLevel(1) == CardLevels::TOURNAMENT_LEVEL);
    REQUIRE(game.setTeamLevel(1, 15));
    REQUIRE(game.getTeamTowerLevel(1) == 15);   // -1 follows the card level
    REQUIRE(game.setTeamLevel(1, 15, 13));
    REQUIRE(game.getTeamTowerLevel(1) == 13);
}

TEST_CASE("a team's Crown Towers take its tower level at reset; the other side's do not move",
        "[card_levels]") {
    GameManager game({ 0,1,2,3,4,5,6,7 }, { 0,1,2,3,4,5,6,7 });
    REQUIRE(game.getTowerMaxHp(1, 0) == CrownTower::KING_HP);
    REQUIRE(game.setTeamLevel(1, 15));
    // Towers were built at construction: unchanged until the next match.
    REQUIRE(game.getTowerMaxHp(1, 1) == CrownTower::PRINCESS_HP);
    game.reset();
    REQUIRE(game.getTowerMaxHp(1, 0) == CardLevels::scale(CrownTower::KING_HP, 15));
    REQUIRE(game.getTowerMaxHp(1, 1) == CardLevels::scale(CrownTower::PRINCESS_HP, 15));
    REQUIRE(game.getTowerHp(1, 1) == game.getTowerMaxHp(1, 1));
    REQUIRE(game.getTowerMaxHp(0, 0) == CrownTower::KING_HP);
    REQUIRE(game.getTowerMaxHp(0, 1) == CrownTower::PRINCESS_HP);
}

TEST_CASE("a played troop enters play at its team's level, once", "[card_levels]") {
    auto knightHp = [](int level) {
        GameManager game({ 0,1,2,3,4,5,6,7 }, { 0,1,2,3,4,5,6,7 });
        REQUIRE(game.setTeamLevel(0, level));
        REQUIRE(game.setHand(0, { 0, 1, 2, 3 }));
        REQUIRE(game.playCard(0, 0, 9.0f, 10.0f));   // Knight
        game.step();
        const int hp = firstUnitHp(game, 0);
        // A snapshot (rollouts) and further commits never scale it again.
        GameManager copy = game.snapshot();
        copy.step();
        REQUIRE(firstUnitHp(copy, 0) == hp);
        return hp;
    };
    const int at11 = knightHp(11);
    REQUIRE(at11 > 0);
    REQUIRE(knightHp(15) == CardLevels::scale(at11, 15));
    REQUIRE(knightHp(9) == CardLevels::scale(at11, 9));
}

TEST_CASE("a spell's crown-tower damage follows its team's level", "[card_levels]") {
    auto fireballOnTower = [](int level) {
        GameManager game({ 0,1,2,3,4,5,6,7 }, { 0,1,2,3,4,5,6,7 });
        REQUIRE(game.setTeamLevel(0, level));
        REQUIRE(game.setHand(0, { 7, 1, 2, 3 }));     // Fireball
        game.setElixir(0, 10.0f);
        const int hp0 = game.getTowerHp(1, 1);
        REQUIRE(game.playCard(0, 7, ArenaLayout::LEFT_LANE_X, ArenaLayout::princessY(1)));
        for (int i = 0; i < 40; ++i) game.step();
        return hp0 - game.getTowerHp(1, 1);
    };
    const int at11 = fireballOnTower(11);
    REQUIRE(at11 == 159);   // audit 0003: wiki Fireball crown-tower damage, level 11
    REQUIRE(fireballOnTower(15) == CardLevels::scale(159, 15));
}
