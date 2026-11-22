#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "Board.h"
#include "GameManager.h"
#include "TowerTroops.h"
#include "ArenaLayout.h"
#include "CardLevels.h"
#include "CombatEntity.h"
#include "Tower.h"
#include <memory>
#include <vector>

// Patch 0019: the four Tower Troops with their wiki level-11 stats, per side
// (GameManager's aiTowerTroop / oppTowerTroop), Dagger Duchess's magazine,
// the first-hit load times, and Royal Chef's pancake as a real +1 level.

namespace {

std::vector<int> deck() { return { 0, 1, 2, 3, 4, 5, 6, 7 }; }

// An enemy (team 1) target 4 tiles in front of team 0's left Princess Tower,
// out of every other tower's range. Tracks the ticks its hp dropped and by how
// much.
struct TowerShots {
    GameManager game;
    std::shared_ptr<StationaryCombatant> foe;
    std::vector<std::pair<int, int>> hits;   // (tick, damage)

    // `warmUp`: ticks stepped before the target appears. A tower built from
    // CardStats deploys for the first second of a match like any card.
    explicit TowerShots(TowerTroopType ours, int warmUp = 0, float tilesInFront = 4.0f)
        : game(deck(), deck(), ours, TowerTroopType::None) {
        game.seed(1);
        game.reset();
        for (int i = 0; i < warmUp; ++i) game.step();
        foe = std::make_shared<StationaryCombatant>(game.getBoard().allocateId(),
            ArenaLayout::LEFT_LANE_X, ArenaLayout::princessY(0) + tilesInFront, 1000000, 1, 0.5f, 0, 1000);
        spawn(game.getBoard(), foe);
    }
    void run(int ticks) {
        for (int t = 1; t <= ticks; ++t) {
            const int before = foe->hp;
            game.step();
            if (foe->hp < before) hits.emplace_back(t, before - foe->hp);
        }
    }
    std::vector<int> gaps() const {
        std::vector<int> g;
        for (size_t i = 1; i < hits.size(); ++i) g.push_back(hits[i].first - hits[i - 1].first);
        return g;
    }
};

// The first hit of a tower with no first-hit wind-up and the default 2
// tiles/tick shot on the same setup: the tower's deploy second at the start
// of a match, the shot loosed on the next tick, 2 ticks of flight. It used to
// be measured on the plain tower, which since patches 0035 and 0036 has a
// wind-up and a slower arrow of its own.
int plainFirstHit() {
    return DEPLOY_TIME_TICKS + 1 + 2;
}

std::shared_ptr<Entity> leftPrincess(GameManager& game, int team) {
    for (const auto& e : game.getBoard().getEntities())
        if (e->isTower() && e->symbol != 'R' && e->team == team && e->position.x < ArenaLayout::CENTER_X) return e;
    return nullptr;
}

}  // namespace

TEST_CASE("each side's Tower Troop is its own: hp per team", "[tower_troops][patch0019]") {
    const std::vector<std::pair<TowerTroopType, int>> rows = {
        { TowerTroopType::None, 3052 }, { TowerTroopType::TowerPrincess, 3052 }, { TowerTroopType::Cannoneer, 2616 },
        { TowerTroopType::DaggerDuchess, 2768 }, { TowerTroopType::RoyalChef, 2703 } };
    for (const auto& [ai, aiHp] : rows) {
        for (const auto& [opp, oppHp] : rows) {
            GameManager game(deck(), deck(), ai, opp);
            for (const auto& e : game.getBoard().getEntities()) {
                if (!e->isTower() || e->symbol == 'R') continue;
                REQUIRE(e->hp == (e->team == 0 ? aiHp : oppHp));
            }
        }
    }
}

TEST_CASE("The plain Princess Tower: 109 every 0.8 s, the first 0.8 s after engaging", "[tower_troops][patch0035]") {
    // A target that appears 4 tiles in front of a tower with nothing in range,
    // two seconds into the match. Tick 1 is the first tick the tower sees it:
    // 8 wind-up ticks, the arrow loosed on the 9th and landing 4 ticks later
    // (4 tiles at 1.25 a tick, patch 0036). Before patch 0035 the plain tower
    // loosed on tick 1.
    TowerShots s(TowerTroopType::None, 20);
    s.run(60);
    REQUIRE(s.hits.size() >= 5);
    REQUIRE(s.hits[0].first == 8 + 1 + 4);
    for (const auto& [tick, dmg] : s.hits) REQUIRE(dmg == 109);
    for (int g : s.gaps()) REQUIRE(g == 8);

    // And it is the Tower Princess exactly.
    TowerShots p(TowerTroopType::TowerPrincess, 20);
    p.run(60);
    REQUIRE(p.hits == s.hits);
}

TEST_CASE("The King Tower's first shot comes 0.5 s after it has a target, then every 1.0 s", "[tower_troops][patch0035]") {
    GameManager game(deck(), deck());
    game.seed(1);
    game.reset();
    std::shared_ptr<Tower> king;
    for (const auto& e : game.getBoard().getEntities())
        if (e->isTower() && e->symbol == 'R' && e->team == 0) king = std::dynamic_pointer_cast<Tower>(e);
    REQUIRE(king != nullptr);
    REQUIRE(king->loadTicks == CrownTower::KING_FIRST_HIT_TICKS);
    REQUIRE(CrownTower::KING_FIRST_HIT_TICKS == 5);
    // Nowhere in the King's reach is out of both Princess Towers', so they
    // are taken off the board; losing one also wakes the King.
    game.step();
    REQUIRE_FALSE(king->isAwake());
    for (const auto& e : game.getBoard().getEntities())
        if (e->isTower() && e->symbol != 'R' && e->team == 0) e->hp = 0;
    for (int i = 0; i < 5; ++i) game.step();
    REQUIRE(king->isAwake());
    auto foe = std::make_shared<StationaryCombatant>(game.getBoard().allocateId(),
        ArenaLayout::CENTER_X, ArenaLayout::kingY(0) + 5.0f, 1000000, 1, 0.5f, 0, 1000);
    spawn(game.getBoard(), foe);
    std::vector<int> hits;
    for (int t = 1; t <= 40; ++t) {
        const int before = foe->hp;
        game.step();
        if (foe->hp < before) {
            REQUIRE(before - foe->hp == CrownTower::KING_DAMAGE);
            hits.push_back(t);
        }
    }
    REQUIRE(hits.size() >= 3);
    // 5 wind-up ticks, loosed on the 6th, 2-3 ticks of flight.
    REQUIRE(hits[0] >= 8);
    REQUIRE(hits[0] <= 9);
    for (size_t i = 1; i < hits.size(); ++i) REQUIRE(hits[i] - hits[i - 1] == CrownTower::KING_COOLDOWN_TICKS);
}

TEST_CASE("A Princess Tower's arrow flies 12.5 tiles a second; the other towers' shots 20", "[tower_troops][patch0036]") {
    // Published projectile speed 600 against 1000, at 48 per tile per second.
    REQUIRE(CrownTower::PRINCESS_ARROW_TILES_PER_TICK == Catch::Approx(1.25f));
    REQUIRE(towerTroopStats(TowerTroopType::None).towerProjectileTilesPerTick == Catch::Approx(1.25f));
    REQUIRE(towerTroopStats(TowerTroopType::TowerPrincess).towerProjectileTilesPerTick == Catch::Approx(1.25f));
    for (TowerTroopType fast : { TowerTroopType::Cannoneer, TowerTroopType::DaggerDuchess, TowerTroopType::RoyalChef }) {
        REQUIRE(towerTroopStats(fast).towerProjectileTilesPerTick == Catch::Approx(0.0f));   // the Tower's default
    }
    GameManager game(deck(), deck());
    for (const auto& e : game.getBoard().getEntities()) {
        auto tower = std::dynamic_pointer_cast<Tower>(e);
        if (!tower) continue;
        REQUIRE(tower->projectileTilesPerTick == Catch::Approx(tower->symbol == 'R' ? 2.0f : 1.25f));
    }

    // Flight grows with the distance: loosed on tick 9 either way, the arrow
    // lands 4 ticks later on a target 4 tiles out and 7 ticks later 8 tiles
    // out (it was 2 and 4).
    TowerShots near(TowerTroopType::None, 20, 4.0f);
    near.run(30);
    TowerShots far(TowerTroopType::None, 20, 8.0f);
    far.run(30);
    REQUIRE(near.hits[0].first == 9 + 4);
    REQUIRE(far.hits[0].first == 9 + 7);
    // The rhythm at the target is still the hit speed.
    for (int g : far.gaps()) REQUIRE(g == 8);
}

TEST_CASE("Tower Princess: 109 every 0.8 s, the first 0.8 s after engaging", "[tower_troops][patch0019]") {
    // Its arrow takes 2 ticks longer over these 4 tiles than the 2 tiles/tick
    // shot plainFirstHit() describes (patch 0036).
    const int plain = plainFirstHit() + 2;
    TowerShots s(TowerTroopType::TowerPrincess);
    s.run(60);
    REQUIRE(s.hits.size() >= 5);
    REQUIRE(s.hits[0].first == plain + 8);
    for (const auto& [tick, dmg] : s.hits) REQUIRE(dmg == 109);
    for (int g : s.gaps()) REQUIRE(g == 8);
}

TEST_CASE("Cannoneer: 320 every 2.2 s, the first 0.8 s after engaging", "[tower_troops][patch0019]") {
    const int plain = plainFirstHit();
    TowerShots s(TowerTroopType::Cannoneer);
    s.run(plain + 8 + 3 * 22);
    REQUIRE(s.hits.size() == 4);
    REQUIRE(s.hits[0].first == plain + 8);
    for (const auto& [tick, dmg] : s.hits) REQUIRE(dmg == 320);
    for (int g : s.gaps()) REQUIRE(g == 22);
}

TEST_CASE("Dagger Duchess: 8 daggers of 107 at 0.5 s, then one per 1.4 s", "[tower_troops][patch0019]") {
    const int plain = plainFirstHit();
    TowerShots s(TowerTroopType::DaggerDuchess);
    s.run(120);
    REQUIRE(s.hits[0].first == plain);   // no load time
    for (const auto& [tick, dmg] : s.hits) REQUIRE(dmg == 107);
    const auto g = s.gaps();
    REQUIRE(g.size() >= 10);
    for (int i = 0; i < 7; ++i) REQUIRE(g[i] == 5);    // the full magazine
    for (size_t i = 7; i < g.size(); ++i) REQUIRE(g[i] == 14);   // 0.5 s + 0.9 s reload
}

TEST_CASE("Dagger Duchess reloads while idle, one dagger per 0.9 s", "[tower_troops][patch0019]") {
    TowerShots s(TowerTroopType::DaggerDuchess);
    s.run(60);                     // empties the magazine
    auto duchess = std::dynamic_pointer_cast<CombatEntity>(leftPrincess(s.game, 0));
    REQUIRE(duchess != nullptr);
    s.foe->hp = 0;
    for (int i = 0; i < 100; ++i) s.game.step();
    REQUIRE(duchess->magazineRounds == 8);
}

TEST_CASE("CombatEntity::gainLevel is loseLevel's inverse, and goes past the cap", "[level][patch0019]") {
    StationaryCombatant a(1, 0.0f, 0.0f, 1000, 0, 1.0f, 100, 10);
    a.gainLevel();
    REQUIRE(a.currentLevel == 12);
    REQUIRE(a.hp == 1098);
    REQUIRE(a.getDamagePerTick() == Catch::Approx(11.0f / 1.0f).epsilon(0.01));   // 110 / 10
    a.loseLevel();
    REQUIRE(a.currentLevel == 11);
    REQUIRE(a.hp == 1000);

    StationaryCombatant capped(2, 0.0f, 0.0f, 4090, 0, 1.0f, 409, 10);
    capped.currentLevel = CardLevels::MAX_LEVEL;
    capped.gainLevel();
    REQUIRE(capped.currentLevel == CardLevels::MAX_LEVEL + 1);
    REQUIRE(capped.hp == 4497);   // 4090 * 409 / 372
}

TEST_CASE("Royal Chef: the first pancake 7 s + 23 s in with no tower busy, to the biggest unfed troop",
        "[tower_troops][royal_chef][patch0019]") {
    GameManager game(deck(), deck(), TowerTroopType::RoyalChef, TowerTroopType::RoyalChef);
    game.seed(1);
    game.reset();
    Board& board = game.getBoard();
    // Team 0's own half, beside the King, out of every enemy's reach.
    auto tank = std::make_shared<StationaryCombatant>(board.allocateId(), 6.0f, 9.0f, 3000, 0, 0.5f, 0, 1000);
    auto small = std::make_shared<StationaryCombatant>(board.allocateId(), 11.0f, 9.0f, 500, 0, 0.5f, 0, 1000);
    auto weak = std::make_shared<StationaryCombatant>(board.allocateId(), 8.0f, 10.0f, 5000, 0, 0.5f, 0, 1000);
    spawn(board, tank);
    spawn(board, small);
    spawn(board, weak);
    game.step();
    weak->hp = 1000;   // under 33% of its 5000: never fed

    int fedAt = -1;
    for (int t = 2; t <= 320 && fedAt < 0; ++t) {
        game.step();
        if (tank->currentLevel > 11) fedAt = t;
    }
    REQUIRE(fedAt >= 299);
    REQUIRE(fedAt <= 302);
    REQUIRE(tank->hp == CardLevels::scale(3000, 12));
    REQUIRE(small->currentLevel == 11);

    for (int i = 0; i < 240; ++i) game.step();   // the second goes to the unfed troop
    REQUIRE(small->currentLevel == 12);
    REQUIRE(tank->currentLevel == 12);
    REQUIRE(weak->currentLevel == 11);
}

TEST_CASE("Royal Chef cooks slower while a tower attacks and stops with both towers down",
        "[tower_troops][royal_chef][patch0019]") {
    SECTION("one tower busy: 30.5 s") {
        TowerShots s(TowerTroopType::RoyalChef);
        Board& board = s.game.getBoard();
        auto ally = std::make_shared<StationaryCombatant>(board.allocateId(), 11.0f, 9.0f, 3000, 0, 0.5f, 0, 1000);
        spawn(board, ally);
        int fedAt = -1;
        for (int t = 1; t <= 400 && fedAt < 0; ++t) {
            s.game.step();
            if (ally->currentLevel > 11) fedAt = t;
        }
        // From tick 70, about 8 ticks of 1/230 before the tower engages, then 1/305.
        REQUIRE(fedAt >= 370);
        REQUIRE(fedAt <= 378);
    }
    SECTION("both Princess Towers destroyed: no pancake") {
        GameManager game(deck(), deck(), TowerTroopType::RoyalChef, TowerTroopType::None);
        game.seed(1);
        game.reset();
        Board& board = game.getBoard();
        for (const auto& e : board.getEntities())
            if (e->isTower() && e->symbol != 'R' && e->team == 0) e->hp = 0;
        auto ally = std::make_shared<StationaryCombatant>(board.allocateId(), 9.0f, 9.0f, 3000, 0, 0.5f, 0, 1000);
        spawn(board, ally);
        for (int i = 0; i < 500; ++i) game.step();
        REQUIRE(ally->currentLevel == 11);
    }
    SECTION("an opponent without the Chef never cooks") {
        GameManager game(deck(), deck(), TowerTroopType::None, TowerTroopType::None);
        game.seed(1);
        game.reset();
        Board& board = game.getBoard();
        auto ally = std::make_shared<StationaryCombatant>(board.allocateId(), 9.0f, 9.0f, 3000, 0, 0.5f, 0, 1000);
        spawn(board, ally);
        for (int i = 0; i < 400; ++i) game.step();
        REQUIRE(ally->currentLevel == 11);
    }
}
