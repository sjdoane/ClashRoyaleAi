#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "Board.h"
#include "GameManager.h"
#include "CardRegistry.h"
#include "CombatEntity.h"
#include "Tower.h"
#include <memory>

// Patch 0018: stat drift on cards from earlier patches, against the wiki.

TEST_CASE("Hero Knight's Triumphant Taunt: a 512 shield, enemies within 7.5 tiles taunted for 5 s",
        "[hero][hero_knight][patch0018]") {
    GameManager game({ 0, 166, 2, 3, 4, 5, 6, 7 }, { 0, 1, 2, 3, 4, 5, 6, 7 });
    game.seed(1);
    game.reset();
    Board& board = game.getBoard();
    for (const auto& e : board.getEntities())
        if (auto t = std::dynamic_pointer_cast<Tower>(e)) t->sleep();

    REQUIRE(game.setHand(0, { 166, 2, 3, 4 }));
    game.setElixir(0, 10.0f);
    REQUIRE(game.playCard(0, 166, 9.0f, 8.0f));
    for (int i = 0; i <= DEPLOY_TIME_TICKS; ++i) game.step();
    std::shared_ptr<CombatEntity> knight;
    for (const auto& e : board.getEntities())
        if (e->team == 0 && e->name == "Hero Knight") knight = std::dynamic_pointer_cast<CombatEntity>(e);
    REQUIRE(knight != nullptr);

    const Vector2D at = knight->position;
    auto inside = std::make_shared<StationaryCombatant>(board.allocateId(), at.x, at.y + 7.3f, 5000, 1, 0.5f, 0, 1000);
    auto outside = std::make_shared<StationaryCombatant>(board.allocateId(), at.x, at.y - 7.8f, 5000, 1, 0.5f, 0, 1000);
    spawn(board, inside);
    spawn(board, outside);

    game.setElixir(0, 10.0f);
    REQUIRE(game.activateChampionAbility(0, 1));
    REQUIRE(knight->shieldHp == 512);
    REQUIRE(inside->forcedTargetEntityId == knight->id);
    REQUIRE(inside->forcedTargetTicksRemaining == 50);
    REQUIRE(outside->forcedTargetEntityId != knight->id);
}
