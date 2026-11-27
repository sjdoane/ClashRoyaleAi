#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "ArenaLayout.h"
#include "Board.h"
#include "Building.h"
#include "CardRegistry.h"
#include "GameManager.h"
#include "MeleeTroop.h"
#include "Tower.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

// --- clampToBoard ---
// Used after a troop moves and again after collision resolution.

TEST_CASE("clampToBoard clamps out-of-bounds positions to the board edges", "[board][clamp]") {
    Board board;
    REQUIRE(board.clampToBoard(Vector2D{ -5.0f, 10.0f }, false).x == Catch::Approx(0.0f));
    REQUIRE(board.clampToBoard(Vector2D{ 25.0f, 10.0f }, false).x == Catch::Approx(17.0f));
    REQUIRE(board.clampToBoard(Vector2D{ 10.0f, -5.0f }, false).y == Catch::Approx(0.0f));
    REQUIRE(board.clampToBoard(Vector2D{ 10.0f, 40.0f }, false).y == Catch::Approx(33.0f));
}

TEST_CASE("clampToBoard pushes non-bridge river-band positions to the nearest bank", "[board][clamp]") {
    Board board;
    REQUIRE(board.clampToBoard(Vector2D{ 10.0f, 16.0f }, false).y == Catch::Approx(15.5f));
    REQUIRE(board.clampToBoard(Vector2D{ 10.0f, 17.0f }, false).y == Catch::Approx(17.5f));
}

TEST_CASE("clampToBoard does not snap positions sitting on a bridge column", "[board][clamp]") {
    Board board;
    // Read off the board: a literal becomes an assertion about open water when
    // a bridge moves.
    REQUIRE(board.clampToBoard(Vector2D{ board.getLeftBridge().x, 17.0f }, false).y == Catch::Approx(17.0f));
    REQUIRE(board.clampToBoard(Vector2D{ board.getRightBridge().x, 17.0f }, false).y == Catch::Approx(17.0f));
}

TEST_CASE("clampToBoard skips the river snap entirely when ignoresRiver is true", "[board][clamp]") {
    Board board;
    Vector2D result = board.clampToBoard(Vector2D{ 10.0f, 17.0f }, true); // off-bridge, mid-river
    REQUIRE(result.y == Catch::Approx(17.0f)); // left untouched
}

TEST_CASE("Board::allocateId returns increasing, unique ids", "[board][id]") {
    Board board;
    int a = board.allocateId();
    int b = board.allocateId();
    int c = board.allocateId();
    REQUIRE(b == a + 1);
    REQUIRE(c == b + 1);
}

TEST_CASE("Board::addEntity is invisible until commitPendingEntities", "[board][lifecycle]") {
    Board board;
    auto e = std::make_shared<DummyEntity>(1, 0.0f, 0.0f, 100, 0);
    board.addEntity(e);
    REQUIRE(board.getEntities().empty());

    board.commitPendingEntities();
    REQUIRE(board.getEntities().size() == 1);
}

TEST_CASE("Board::commitPendingEntities with nothing pending is a no-op", "[board][lifecycle]") {
    Board board;
    board.commitPendingEntities();
    REQUIRE(board.getEntities().empty());
}

TEST_CASE("Board::cleanDeadEntities removes only the dead", "[board][lifecycle]") {
    Board board;
    auto alive = std::make_shared<DummyEntity>(1, 0.0f, 0.0f, 100, 0);
    auto dead = std::make_shared<DummyEntity>(2, 0.0f, 0.0f, 100, 0);
    dead->takeDamage(100);
    spawn(board, alive);
    spawn(board, dead);

    board.cleanDeadEntities();

    REQUIRE(board.getEntities().size() == 1);
    REQUIRE(board.getEntities()[0]->id == 1);
}

TEST_CASE("Board::cleanDeadEntities fires a dying entity's death effect before removing it", "[board][lifecycle][death]") {
    Board board;
    auto dying = std::make_shared<StationaryCombatant>(1, 3.0f, 4.0f, 100, 1, 5.0f, 10, 10);
    auto effect = std::make_shared<RecordingDeathEffect>();
    dying->deathEffect = effect;
    dying->takeDamage(100);
    spawn(board, dying);

    board.cleanDeadEntities();

    REQUIRE(effect->applied);
    REQUIRE(effect->lastPosition.x == Catch::Approx(3.0f));
    REQUIRE(effect->lastTeam == 1);
    REQUIRE(board.getEntities().empty()); // still removed as normal
}

TEST_CASE("Board::cleanDeadEntities does not fire a death effect for entities that are still alive", "[board][lifecycle][death]") {
    Board board;
    auto alive = std::make_shared<StationaryCombatant>(1, 3.0f, 4.0f, 100, 1, 5.0f, 10, 10);
    auto effect = std::make_shared<RecordingDeathEffect>();
    alive->deathEffect = effect;
    spawn(board, alive);

    board.cleanDeadEntities();

    REQUIRE_FALSE(effect->applied);
    REQUIRE(board.getEntities().size() == 1);
}

// ---------------- resolvePositionAgainstBuildings ----------------

TEST_CASE("resolvePositionAgainstBuildings leaves a position untouched when far from any building", "[board][collision]") {
    Board board;
    auto building = std::make_shared<Building>(1, 10.0f, 10.0f, 1000, 1, 'C', 5.0f, 10, 10);
    spawn(board, building);

    Vector2D resolved = board.resolvePositionAgainstBuildings(Vector2D{ 0.0f, 0.0f }, 999);
    REQUIRE(resolved.x == Catch::Approx(0.0f));
    REQUIRE(resolved.y == Catch::Approx(0.0f));
}

TEST_CASE("resolvePositionAgainstBuildings pushes a position out of a building's radius", "[board][collision]") {
    Board board;
    // radius 1.0, minDist 1.4. Input 0.5 above the centre: push 1.4 - 0.5 =
    // 0.9, plus the 0.05 perpendicular slide.
    auto building = std::make_shared<Building>(1, 10.0f, 10.0f, 1000, 1, 'C', 5.0f, 10, 10);
    spawn(board, building);

    Vector2D resolved = board.resolvePositionAgainstBuildings(Vector2D{ 10.0f, 10.5f }, 999);
    REQUIRE(resolved.x == Catch::Approx(10.05f));
    REQUIRE(resolved.y == Catch::Approx(11.4f));
}

TEST_CASE("resolvePositionAgainstBuildings ignores dead buildings", "[board][collision]") {
    Board board;
    auto building = std::make_shared<Building>(1, 10.0f, 10.0f, 100, 1, 'C', 5.0f, 10, 10);
    building->takeDamage(100); // dead
    spawn(board, building);

    Vector2D resolved = board.resolvePositionAgainstBuildings(Vector2D{ 10.0f, 10.5f }, 999);
    REQUIRE(resolved.x == Catch::Approx(10.0f));
    REQUIRE(resolved.y == Catch::Approx(10.5f));
}

TEST_CASE("resolvePositionAgainstBuildings excludes the entity's own id", "[board][collision]") {
    Board board;
    auto building = std::make_shared<Building>(42, 10.0f, 10.0f, 1000, 1, 'C', 5.0f, 10, 10);
    spawn(board, building);

    // Resolving against itself must not push against its own radius.
    Vector2D resolved = board.resolvePositionAgainstBuildings(Vector2D{ 10.0f, 10.5f }, 42);
    REQUIRE(resolved.x == Catch::Approx(10.0f));
    REQUIRE(resolved.y == Catch::Approx(10.5f));
}

TEST_CASE("resolvePositionAgainstBuildings ignores non-building entities (radius 0)", "[board][collision]") {
    Board board;
    auto troop = std::make_shared<DummyEntity>(1, 10.0f, 10.0f, 100, 1); // radius 0.0 by default
    spawn(board, troop);

    Vector2D resolved = board.resolvePositionAgainstBuildings(Vector2D{ 10.0f, 10.1f }, 999);
    REQUIRE(resolved.x == Catch::Approx(10.0f));
    REQUIRE(resolved.y == Catch::Approx(10.1f));
}

// --- resolveCollisions ---

TEST_CASE("resolveCollisions pushes two overlapping troops apart symmetrically", "[board][collision]") {
    Board board;
    auto troop1 = std::make_shared<DummyEntity>(1, 10.0f, 10.0f, 100, 0);
    auto troop2 = std::make_shared<DummyEntity>(2, 10.0f, 10.3f, 100, 1); // dist 0.3, inside minRadius 0.8
    spawn(board, troop1);
    spawn(board, troop2);

    board.resolveCollisions();

    float dist = troop1->position.distanceTo(troop2->position);
    REQUIRE(dist > 0.79f);
}

TEST_CASE("resolveCollisions pushes a troop out of an overlapping building without moving the building", "[board][collision]") {
    Board board;
    auto building = std::make_shared<Building>(1, 10.0f, 10.0f, 1000, 1, 'C', 5.0f, 10, 10); // radius 1.0
    auto troop = std::make_shared<DummyEntity>(2, 10.0f, 10.3f, 100, 0); // dist 0.3, inside minDist 1.4
    spawn(board, building);
    spawn(board, troop);

    board.resolveCollisions();

    REQUIRE(building->position.x == Catch::Approx(10.0f));
    REQUIRE(building->position.y == Catch::Approx(10.0f));
    float dist = troop->position.distanceTo(building->position);
    REQUIRE(dist > 1.39f);
}

TEST_CASE("resolveCollisions leaves entities untouched when far apart", "[board][collision]") {
    Board board;
    auto troop1 = std::make_shared<DummyEntity>(1, 0.0f, 0.0f, 100, 0);
    auto troop2 = std::make_shared<DummyEntity>(2, 20.0f, 20.0f, 100, 1);
    spawn(board, troop1);
    spawn(board, troop2);

    board.resolveCollisions();

    REQUIRE(troop1->position.x == Catch::Approx(0.0f));
    REQUIRE(troop1->position.y == Catch::Approx(0.0f));
    REQUIRE(troop2->position.x == Catch::Approx(20.0f));
    REQUIRE(troop2->position.y == Catch::Approx(20.0f));
}

TEST_CASE("resolveCollisions ignores dead entities without crashing", "[board][collision]") {
    Board board;
    auto troop1 = std::make_shared<DummyEntity>(1, 10.0f, 10.0f, 100, 0);
    auto troop2 = std::make_shared<DummyEntity>(2, 10.0f, 10.1f, 100, 1);
    troop2->takeDamage(100); // dead
    spawn(board, troop1);
    spawn(board, troop2);

    REQUIRE_NOTHROW(board.resolveCollisions());
    REQUIRE(troop1->position.x == Catch::Approx(10.0f));
    REQUIRE(troop1->position.y == Catch::Approx(10.0f));
}

TEST_CASE("resolveCollisions re-clamps entities to the board bounds afterward", "[board][collision]") {
    Board board;
    // DummyEntity has no clampPosition; only a real Troop shows that
    // resolveCollisions re-clamps.
    auto troop = std::make_shared<MeleeTroop>(1, 25.0f, 10.0f, 100, 0, 1.0f, 1.0f, 10, 10, 'K');
    spawn(board, troop);

    board.resolveCollisions();

    REQUIRE(troop->position.x == Catch::Approx(17.0f));
}

// --- flying ---
// Fliers pass through everything; only entities sharing a plane collide.

TEST_CASE("resolveCollisions does not push a flying troop out of an overlapping building", "[board][collision][flying]") {
    Board board;
    auto building = std::make_shared<Building>(1, 10.0f, 10.0f, 1000, 1, 'C', 5.0f, 10, 10); // radius 1.0
    auto troop = std::make_shared<DummyEntity>(2, 10.0f, 10.3f, 100, 0); // dist 0.3, would be inside minDist 1.4
    troop->isFlying = true;
    spawn(board, building);
    spawn(board, troop);

    board.resolveCollisions();

    REQUIRE(troop->position.x == Catch::Approx(10.0f));
    REQUIRE(troop->position.y == Catch::Approx(10.3f));
}

TEST_CASE("resolveCollisions does not push a flying troop away from an overlapping grounded troop", "[board][collision][flying]") {
    Board board;
    auto flying = std::make_shared<DummyEntity>(1, 10.0f, 10.0f, 100, 0);
    flying->isFlying = true;
    auto grounded = std::make_shared<DummyEntity>(2, 10.0f, 10.3f, 100, 1); // dist 0.3, would be inside minRadius 0.8
    spawn(board, flying);
    spawn(board, grounded);

    board.resolveCollisions();

    REQUIRE(flying->position.x == Catch::Approx(10.0f));
    REQUIRE(flying->position.y == Catch::Approx(10.0f));
    REQUIRE(grounded->position.x == Catch::Approx(10.0f));
    REQUIRE(grounded->position.y == Catch::Approx(10.3f));
}

TEST_CASE("resolveCollisions still pushes two overlapping flying troops apart from each other", "[board][collision][flying]") {
    Board board;
    auto troop1 = std::make_shared<DummyEntity>(1, 10.0f, 10.0f, 100, 0);
    auto troop2 = std::make_shared<DummyEntity>(2, 10.0f, 10.3f, 100, 1); // dist 0.3, inside minRadius 0.8
    troop1->isFlying = true;
    troop2->isFlying = true;
    spawn(board, troop1);
    spawn(board, troop2);

    board.resolveCollisions();

    float dist = troop1->position.distanceTo(troop2->position);
    REQUIRE(dist > 0.79f);
}

// ---------------- getNextWaypoint ----------------

TEST_CASE("getNextWaypoint returns the target directly when both points are below the river", "[board][waypoint]") {
    Board board;
    Vector2D wp = board.getNextWaypoint(Vector2D{ 5.0f, 5.0f }, Vector2D{ 5.0f, 10.0f });
    REQUIRE(wp.x == Catch::Approx(5.0f));
    REQUIRE(wp.y == Catch::Approx(10.0f));
}

TEST_CASE("getNextWaypoint returns the target directly when both points are above the river", "[board][waypoint]") {
    Board board;
    Vector2D wp = board.getNextWaypoint(Vector2D{ 5.0f, 20.0f }, Vector2D{ 5.0f, 25.0f });
    REQUIRE(wp.x == Catch::Approx(5.0f));
    REQUIRE(wp.y == Catch::Approx(25.0f));
}

TEST_CASE("getNextWaypoint returns the target directly when both points are inside the river band", "[board][waypoint]") {
    Board board;
    // River band (15.5, 17.5): strictly inside, not at an edge.
    Vector2D wp = board.getNextWaypoint(Vector2D{ 4.0f, 16.0f }, Vector2D{ 14.0f, 17.0f });
    REQUIRE(wp.x == Catch::Approx(14.0f));
    REQUIRE(wp.y == Catch::Approx(17.0f));
}

// Until patch 0060 the three cases below expected the CENTRE of the bridge's
// mouth (board.getRightBridge().x): the engine's single crossing point, not a
// recorded or published value. A walker now keeps its own line where that
// crosses the deck and otherwise heads for the nearest point of the mouth.
TEST_CASE("getNextWaypoint routes below-to-above via the nearest bridge's start edge", "[board][waypoint]") {
    Board board;
    // Closer to the right bridge than the left, and left of its deck: the
    // deck's near corner.
    Vector2D wp = board.getNextWaypoint(Vector2D{ 10.0f, 5.0f }, Vector2D{ 10.0f, 25.0f });
    REQUIRE(wp.x == Catch::Approx(Board::RIGHT_WALK_X - Board::WALK_HALF_WIDTH));
    REQUIRE(wp.x == Catch::Approx(13.5f));
    REQUIRE(wp.y == Catch::Approx(15.5f)); // riverY_start
}

TEST_CASE("getNextWaypoint routes above-to-below via the nearest bridge's end edge", "[board][waypoint]") {
    Board board;
    Vector2D wp = board.getNextWaypoint(Vector2D{ 10.0f, 25.0f }, Vector2D{ 10.0f, 5.0f });
    REQUIRE(wp.x == Catch::Approx(Board::RIGHT_WALK_X - Board::WALK_HALF_WIDTH));
    REQUIRE(wp.y == Catch::Approx(17.5f)); // riverY_end
}

TEST_CASE("getNextWaypoint picks the left bridge when it is nearer", "[board][waypoint]") {
    Board board;
    // x 2.0 is outside the left walk corridor (2.5 to 3.5, patch 0087; it was
    // on the old deck 1.5 to 3.5): the corridor's outer corner.
    Vector2D wp = board.getNextWaypoint(Vector2D{ 2.0f, 5.0f }, Vector2D{ 2.0f, 25.0f });
    REQUIRE(wp.x == Catch::Approx(2.5f));
    REQUIRE(Board::inWalkCorridor(wp.x));
    REQUIRE(std::abs(wp.x - Board::LEFT_WALK_X) <= Board::WALK_HALF_WIDTH);
    // In the corridor: straight across.
    Vector2D own = board.getNextWaypoint(Vector2D{ 2.8f, 5.0f }, Vector2D{ 2.8f, 25.0f });
    REQUIRE(own.x == Catch::Approx(2.8f));
}

// --- the bridge-mouth absorbing state ---
// getNextWaypoint's bank tests are inclusive (`y <= riverY_start`), so a unit
// standing exactly on the near bank is still "below" and would be handed the
// point it occupies, while Troop::moveTowards does not move within 0.01 of a
// waypoint: the unit never crosses. A 0.01-radius trap disc at each bridge
// mouth.

TEST_CASE("getNextWaypoint does not strand a unit standing on the near bank", "[board][waypoint][regression]") {
    Board board;
    // The measured position: on the left bridge, at riverY_start.
    Vector2D here{ board.getLeftBridge().x, 15.5f };
    Vector2D wp = board.getNextWaypoint(here, Vector2D{ board.getLeftBridge().x, 27.0f });
    // The unit still has to cross, so the waypoint must be somewhere it is not
    // standing.
    REQUIRE(here.distanceTo(wp) > Board::WAYPOINT_ARRIVAL_EPS);
    // The far bank.
    REQUIRE(wp.y == Catch::Approx(17.5f));
}

TEST_CASE("getNextWaypoint does not strand a unit standing on the far bank", "[board][waypoint][regression]") {
    Board board;
    // The same trap mirrored, heading south.
    Vector2D here{ board.getRightBridge().x, 17.5f };
    Vector2D wp = board.getNextWaypoint(here, Vector2D{ board.getRightBridge().x, 6.0f });
    REQUIRE(here.distanceTo(wp) > Board::WAYPOINT_ARRIVAL_EPS);
    REQUIRE(wp.y == Catch::Approx(15.5f));
}

TEST_CASE("getNextWaypoint never returns the caller's own position while it still has to cross",
          "[board][waypoint][regression]") {
    Board board;
    // Sweep the trap disc around both bridge mouths finer than the arrival
    // epsilon, both directions; any point returning itself is absorbing. Bridge
    // positions are read off the board, so the sweep follows the bridges if
    // they move.
    const float bridges[] = { board.getLeftBridge().x, board.getRightBridge().x };
    const float banks[] = { 15.5f, 17.5f };
    for (float bx : bridges) {
        for (float by : banks) {
            for (int dx = -2; dx <= 2; ++dx) {
                for (int dy = -2; dy <= 2; ++dy) {
                    Vector2D here{ bx + dx * 0.005f, by + dy * 0.005f };
                    // Target across the river from this bank.
                    Vector2D target = (by < 16.5f) ? Vector2D{ bx, 27.0f }
                                                   : Vector2D{ bx, 6.0f };
                    Vector2D wp = board.getNextWaypoint(here, target);
                    INFO("stuck at x=" << here.x << " y=" << here.y);
                    REQUIRE(here.distanceTo(wp) > Board::WAYPOINT_ARRIVAL_EPS);
                }
            }
        }
    }
}

// --- the bridge-exit absorbing state ---
// The same defect one branch over: inside the river band, within epsilon of the
// bank it is arriving at, the unit was handed that bank's edge and froze just
// short of dry land. Four exit traps mirroring the four entry traps. The entry
// sweep above pairs each bank only with the direction in which it is the entry,
// so this sweep is the full cross product: every bank, both directions.

TEST_CASE("getNextWaypoint does not strand a unit arriving at the far bank", "[board][waypoint][regression]") {
    Board board;
    // A hair short of the north bank, still crossing north: the measured
    // freeze.
    Vector2D here{ board.getLeftBridge().x, 17.4995f };
    Vector2D wp = board.getNextWaypoint(here, Vector2D{ board.getLeftBridge().x, 27.0f });
    REQUIRE(here.distanceTo(wp) > Board::WAYPOINT_ARRIVAL_EPS);
}

TEST_CASE("getNextWaypoint does not strand a unit arriving at the near bank", "[board][waypoint][regression]") {
    Board board;
    // Mirrored: a hair past the south bank, still heading south.
    Vector2D here{ board.getRightBridge().x, 15.5005f };
    Vector2D wp = board.getNextWaypoint(here, Vector2D{ board.getRightBridge().x, 6.0f });
    REQUIRE(here.distanceTo(wp) > Board::WAYPOINT_ARRIVAL_EPS);
}

TEST_CASE("getNextWaypoint never returns the caller's own position, in EITHER crossing direction",
          "[board][waypoint][regression]") {
    Board board;
    // Bridge positions are read off the board, so the sweep follows the bridges
    // if they move.
    const float bridges[] = { board.getLeftBridge().x, board.getRightBridge().x };
    const float banks[] = { 15.5f, 17.5f };
    // Both destinations at both banks.
    const float targets[] = { 27.0f, 6.0f };

    for (float bx : bridges) {
        for (float by : banks) {
            for (float ty : targets) {
                for (int dx = -2; dx <= 2; ++dx) {
                    for (int dy = -3; dy <= 3; ++dy) {
                        Vector2D here{ bx + dx * 0.005f, by + dy * 0.005f };
                        Vector2D wp = board.getNextWaypoint(here, Vector2D{ bx, ty });
                        INFO("stuck at x=" << here.x << " y=" << here.y << " target y=" << ty);
                        REQUIRE(here.distanceTo(wp) > Board::WAYPOINT_ARRIVAL_EPS);
                    }
                }
            }
        }
    }
}

TEST_CASE("getNextWaypoint from inside the river band heads to the exit edge toward the target's side", "[board][waypoint]") {
    Board board;

    SECTION("target is above -> heads to the river end edge") {
        Vector2D wp = board.getNextWaypoint(Vector2D{ board.getLeftBridge().x, 17.0f }, Vector2D{ board.getLeftBridge().x, 25.0f });
        REQUIRE(wp.y == Catch::Approx(17.5f));
    }

    SECTION("target is below -> heads to the river start edge") {
        Vector2D wp = board.getNextWaypoint(Vector2D{ board.getLeftBridge().x, 17.0f }, Vector2D{ board.getLeftBridge().x, 5.0f });
        REQUIRE(wp.y == Catch::Approx(15.5f));
    }
}

// --- the bank-line overshoot orbit ---
// A third bridge-mouth trap, and not a fixed point: an orbit. A unit knocked off
// a bridge deck is snapped onto the bank line (clampToBoard), still counts as
// "below", and is sent to the mouth sideways along the bank. A full step past
// the mouth leaves it on the same bank line, so it is handed the same mouth back
// and steps past it the other way, forever: x flips either side of the mouth
// while getNextWaypoint never returns the unit's own position, which is why the
// sweeps above cannot see it. Measured: a Fireball left a Giant vibrating for
// 224 ticks, and 18% of teacher matches had one (UPSTREAM_REQUESTS.md item 31).
//
// Real cards through the GameManager tick loop, because the orbit lives in
// Troop::moveTowards's step, not in the planner.
TEST_CASE("a troop on a bank line walks onto the bridge instead of oscillating at its mouth",
          "[board][waypoint][regression][orbit]") {
    constexpr int GIANT = 2;       // SPEED_SLOW, the measured case
    constexpr int HOG_RIDER = 15;  // SPEED_VERY_FAST, a different step size
    const std::vector<int> deck = { 15, 6, 25, 40, 24, 72, 33, 7 };
    const float bridges[] = { ArenaLayout::LEFT_BRIDGE_X, ArenaLayout::RIGHT_BRIDGE_X };

    int stuck = 0, runs = 0, missing = 0;
    std::ostringstream examples;
    for (int card : { GIANT, HOG_RIDER }) {
        for (int team : { 0, 1 }) {
            for (float bx : bridges) {
                // Every 0.01 tiles, finer than any step, for two tiles either
                // side of the mouth.
                for (int i = -200; i <= 200; ++i) {
                    GameManager game(deck, deck);
                    Board& board = game.getBoard();
                    // Each team's own bank: team 0 crosses north, team 1 south.
                    const float bankY = team == 0 ? board.getRiverStart() : board.getRiverEnd();
                    const float x = bx + i * 0.01f;
                    CardRegistry::getInstance().getCard(card)->spawnEntity(x, bankY, team, board);
                    board.commitPendingEntities();
                    std::shared_ptr<Troop> unit;
                    for (const auto& e : board.getEntities()) {
                        if (e->cardId == card) unit = std::dynamic_pointer_cast<Troop>(e);
                    }
                    if (!unit) {  // counted, not REQUIREd: 3,208 runs
                        ++missing;
                        continue;
                    }

                    // Deploy, the sideways leg to the mouth, the river, and
                    // slack.
                    const int budget = DEPLOY_TIME_TICKS
                        + static_cast<int>(std::ceil((2.0f + 2.5f) / unit->getSpeed())) + 10;
                    bool crossed = false;
                    for (int t = 0; t < budget && !crossed; ++t) {
                        game.step();
                        crossed = team == 0 ? unit->position.y > board.getRiverEnd()
                                            : unit->position.y < board.getRiverStart();
                    }
                    ++runs;
                    if (!crossed && ++stuck <= 8) {
                        examples << "\n  card " << card << " team " << team << " from x=" << x
                                 << ", now (" << unit->position.x << ", "
                                 << unit->position.y << ")";
                    }
                }
            }
        }
    }
    REQUIRE(missing == 0);
    INFO(stuck << " of " << runs << " placements never crossed; first few:" << examples.str());
    REQUIRE(stuck == 0);
}

// --- patch 0060: a bridge is crossed across its width ---
// Evidence: the recordings of 2026-11-20 (engine notes). An Ice Golem from
// tile (8, 10) walks a straight line to the near corner of the left bridge
// (x 3.5) and crosses along that edge; a Musketeer from (9, 9) does the same
// at the right bridge (13.5); an Ice Golem placed in the lane at x 13.6-14
// crosses where it stands, with no excursion to the centre line (14.5).

TEST_CASE("a walker whose line crosses the deck keeps its line", "[board][waypoint][patch0060]") {
    Board board;
    // Patch 0087: the corridor is 2.5-3.5 and 13.5-14.5 (it was 1.5-3.5 and
    // 13.5-15.5; this list held 15.2 and 1.7, on the old deck's outer tile).
    for (float x : { 13.6f, 14.0f, 14.5f, 2.5f, 2.7f, 3.3f }) {
        INFO("x " << x);
        Vector2D up = board.getNextWaypoint(Vector2D{ x, 12.0f }, Vector2D{ x, 27.0f });
        REQUIRE(up.x == Catch::Approx(x));
        REQUIRE(up.y == Catch::Approx(15.5f));
        Vector2D down = board.getNextWaypoint(Vector2D{ x, 21.0f }, Vector2D{ x, 6.0f });
        REQUIRE(down.x == Catch::Approx(x));
        REQUIRE(down.y == Catch::Approx(17.5f));
    }
    // A slanted line through the deck: the point where it meets the bank.
    Vector2D wp = board.getNextWaypoint(Vector2D{ 14.4f, 13.5f }, Vector2D{ 14.0f, 27.0f });
    REQUIRE(wp.x == Catch::Approx(14.4f - 0.4f * 2.0f / 13.5f).margin(1e-4));
    REQUIRE(wp.y == Catch::Approx(15.5f));
    // The old outer tile is no longer deck: from x 15.2 or 1.7 the outer corner.
    REQUIRE(board.getNextWaypoint(Vector2D{ 15.2f, 12.0f }, Vector2D{ 15.2f, 27.0f }).x == Catch::Approx(14.5f));
    REQUIRE(board.getNextWaypoint(Vector2D{ 1.7f, 12.0f }, Vector2D{ 1.7f, 27.0f }).x == Catch::Approx(2.5f));
}

TEST_CASE("a walker coming from the side heads for the near corner of the deck", "[board][waypoint][patch0060]") {
    Board board;
    // The two recorded starts, to their lane's Princess Tower.
    Vector2D golem = board.getNextWaypoint(Vector2D{ 8.0f, 10.0f }, Vector2D{ 3.0f, 27.0f });
    REQUIRE(golem.x == Catch::Approx(3.5f));
    REQUIRE(golem.y == Catch::Approx(15.5f));
    Vector2D musketeer = board.getNextWaypoint(Vector2D{ 9.0f, 9.0f }, Vector2D{ 14.0f, 27.0f });
    REQUIRE(musketeer.x == Catch::Approx(13.5f));
    REQUIRE(musketeer.y == Catch::Approx(15.5f));
    // From outside the lane, the outer corner (14.5 since patch 0087, it was
    // 15.5); team 1 mirrored.
    REQUIRE(board.getNextWaypoint(Vector2D{ 17.0f, 12.0f }, Vector2D{ 14.0f, 27.0f }).x == Catch::Approx(14.5f));
    REQUIRE(board.getNextWaypoint(Vector2D{ 0.0f, 12.0f }, Vector2D{ 3.0f, 27.0f }).x == Catch::Approx(2.5f));
    Vector2D down = board.getNextWaypoint(Vector2D{ 8.0f, 23.0f }, Vector2D{ 3.0f, 6.0f });
    REQUIRE(down.x == Catch::Approx(3.5f));
    REQUIRE(down.y == Catch::Approx(17.5f));

    // On the corner, the next leg is the far bank, on the deck, on the line to
    // the target.
    Vector2D across = board.getNextWaypoint(Vector2D{ 3.5f, 15.5f }, Vector2D{ 3.0f, 27.0f });
    REQUIRE(across.y == Catch::Approx(17.5f));
    REQUIRE(across.x == Catch::Approx(3.5f - 0.5f * 2.0f / 11.5f).margin(1e-4));
    // A target on the other side of the corner: straight up the edge.
    Vector2D edge = board.getNextWaypoint(Vector2D{ 3.5f, 15.5f }, Vector2D{ 9.0f, 20.0f });
    REQUIRE(edge.x == Catch::Approx(3.5f));
    REQUIRE(edge.y == Catch::Approx(17.5f));
}

TEST_CASE("clampToBoard puts a unit beside a deck back the shorter way", "[board][clamp][patch0060]") {
    Board board;
    // A tenth of a tile off the deck in mid-river: onto the edge, not a tile
    // forward or back to a bank.
    Vector2D a = board.clampToBoard(Vector2D{ 3.6f, 16.5f }, false);
    REQUIRE(a.x == Catch::Approx(3.5f));
    REQUIRE(a.y == Catch::Approx(16.5f));
    Vector2D b = board.clampToBoard(Vector2D{ 13.2f, 16.2f }, false);
    REQUIRE(b.x == Catch::Approx(13.5f));
    REQUIRE(b.y == Catch::Approx(16.2f));
    // Nearer the bank than the deck: the bank, as before.
    Vector2D c = board.clampToBoard(Vector2D{ 4.5f, 15.8f }, false);
    REQUIRE(c.x == Catch::Approx(4.5f));
    REQUIRE(c.y == Catch::Approx(15.5f));
    // Within the edge tolerance of the deck it is the edge even right at the
    // bank: a unit sent across from a corner must not be handed back to it.
    Vector2D d = board.clampToBoard(Vector2D{ 3.5f + Board::DECK_EDGE_TOLERANCE * 0.9f, 15.505f }, false);
    REQUIRE(d.x == Catch::Approx(3.5f));
    REQUIRE(d.y == Catch::Approx(15.505f));
    // On the deck, edges included: untouched.
    Vector2D e = board.clampToBoard(Vector2D{ 3.5f, 16.0f }, false);
    REQUIRE(e.x == Catch::Approx(3.5f));
    REQUIRE(e.y == Catch::Approx(16.0f));
}

TEST_CASE("getNextWaypoint never returns the caller's own position at a deck corner or along a mouth",
          "[board][waypoint][regression][patch0060]") {
    Board board;
    // Every 0.25 across both mouths and a tile to each side, finer than the
    // arrival epsilon around each such point, both banks, targets across the
    // river in both directions at five x. A point that returns itself (within
    // the tolerance Troop::moveTowards refuses to move in) is absorbing unless
    // it is the target.
    const float bridges[] = { board.getLeftBridge().x, board.getRightBridge().x };
    const float banks[] = { 15.5f, 17.5f };
    const float targetYs[] = { 27.0f, 6.0f, 20.0f, 13.0f };
    const float targetXs[] = { 0.0f, 3.0f, 8.5f, 14.0f, 17.0f };
    int checked = 0;
    for (float bx : bridges) {
        for (int ix = -8; ix <= 8; ++ix) {
            for (float by : banks) {
                for (float ty : targetYs) {
                    for (float tx : targetXs) {
                        for (int dx = -3; dx <= 3; ++dx) {
                            for (int dy = -3; dy <= 3; ++dy) {
                                Vector2D here{ bx + ix * 0.25f + dx * 0.004f, by + dy * 0.004f };
                                Vector2D target{ tx, ty };
                                const bool sameSide = (here.y <= 15.5f && ty <= 15.5f) || (here.y >= 17.5f && ty >= 17.5f);
                                if (sameSide) continue;
                                Vector2D wp = board.getNextWaypoint(here, target);
                                ++checked;
                                INFO("stuck at x=" << here.x << " y=" << here.y << " target (" << tx << ", " << ty << ")");
                                REQUIRE(here.distanceTo(wp) > Board::WAYPOINT_ARRIVAL_EPS);
                                // Whatever it is handed across the river is on a deck.
                                if (wp.y == Catch::Approx(15.5f) || wp.y == Catch::Approx(17.5f)) {
                                    if (!(wp.x == target.x && wp.y == target.y)) REQUIRE(Board::inWalkCorridor(wp.x));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    REQUIRE(checked > 40000);
}

// Real cards through the GameManager tick loop: nobody stalls at a mouth, a
// corner or an edge, alone or in a squad, and a squad placed abreast crosses
// abreast.
TEST_CASE("walkers from every start cross the bridge, and a squad crosses abreast",
          "[board][waypoint][regression][patch0060]") {
    const std::vector<int> deck = { 15, 6, 25, 40, 24, 72, 33, 7 };
    constexpr int KNIGHT = 0, GIANT = 2, BARBARIANS = 8, SKELETON_ARMY = 12, ROYAL_RECRUITS = 77, GOLEM = 19;

    SECTION("single walkers, every 0.1 across the arena, three rows, both teams") {
        int stuck = 0, runs = 0;
        std::ostringstream examples;
        for (int card : { KNIGHT, GIANT, GOLEM }) {
            for (int team : { 0, 1 }) {
                for (float rowsBack : { 0.0f, 0.3f, 3.0f }) {
                    for (int i = 0; i <= 170; ++i) {
                        GameManager game(deck, deck);
                        Board& board = game.getBoard();
                        const float x = i * 0.1f;
                        const float y = team == 0 ? board.getRiverStart() - rowsBack : board.getRiverEnd() + rowsBack;
                        CardRegistry::getInstance().getCard(card)->spawnEntity(x, y, team, board);
                        board.commitPendingEntities();
                        std::shared_ptr<Troop> unit;
                        for (const auto& e : board.getEntities()) {
                            if (e->cardId == card) unit = std::dynamic_pointer_cast<Troop>(e);
                        }
                        REQUIRE(unit);
                        // Deploy, at most 7.5 tiles to a mouth, the river, slack.
                        const int budget = DEPLOY_TIME_TICKS
                            + static_cast<int>(std::ceil((8.0f + rowsBack + 2.5f) / unit->getSpeed())) + 10;
                        bool crossed = false;
                        for (int t = 0; t < budget && !crossed; ++t) {
                            game.step();
                            crossed = team == 0 ? unit->position.y > board.getRiverEnd()
                                                : unit->position.y < board.getRiverStart();
                        }
                        ++runs;
                        if (!crossed && ++stuck <= 8) {
                            examples << "\n  card " << card << " team " << team << " from (" << x << ", " << y
                                     << "), now (" << unit->position.x << ", " << unit->position.y << ")";
                        }
                    }
                }
            }
        }
        INFO(stuck << " of " << runs << " never crossed; first few:" << examples.str());
        REQUIRE(stuck == 0);
        REQUIRE(runs == 3 * 2 * 3 * 171);
    }

    SECTION("squads: every unit crosses, at more than one x") {
        for (int card : { BARBARIANS, SKELETON_ARMY, ROYAL_RECRUITS }) {
            for (int team : { 0, 1 }) {
                for (float x : { 2.5f, 3.5f, 8.5f, 13.0f, 14.5f, 16.5f }) {
                    GameManager game(deck, deck);
                    Board& board = game.getBoard();
                    // Towers asleep: this is about walking, not about who survives.
                    for (const auto& e : board.getEntities())
                        if (auto t = std::dynamic_pointer_cast<Tower>(e)) t->sleep();
                    const float y = team == 0 ? 12.0f : 21.0f;
                    CardRegistry::getInstance().getCard(card)->spawnEntity(x, y, team, board);
                    board.commitPendingEntities();
                    std::vector<std::shared_ptr<Troop>> squad;
                    for (const auto& e : board.getEntities()) {
                        if (e->cardId == card) squad.push_back(std::dynamic_pointer_cast<Troop>(e));
                    }
                    REQUIRE(squad.size() >= 5);
                    std::vector<float> crossedAtX(squad.size(), -1.0f);
                    for (int t = 0; t < 260; ++t) {
                        game.step();
                        for (size_t k = 0; k < squad.size(); ++k) {
                            const bool over = team == 0 ? squad[k]->position.y > board.getRiverEnd()
                                                        : squad[k]->position.y < board.getRiverStart();
                            if (over && crossedAtX[k] < 0.0f) crossedAtX[k] = squad[k]->position.x;
                        }
                    }
                    INFO("card " << card << " team " << team << " x " << x);
                    float lo = 99.0f, hi = -99.0f;
                    for (float cx : crossedAtX) {
                        REQUIRE(cx >= 0.0f);
                        // Read one step after leaving the deck, so a step's
                        // sideways drift is allowed.
                        // The deck. Since patch 0087 a unit HEADS for the
                        // walk corridor (the inner tile and a half), but a
                        // squad's bodies push each other and the outer ones
                        // cross on the deck's outer tile.
                        const float offCentre = std::min(std::abs(cx - ArenaLayout::LEFT_BRIDGE_X),
                                                         std::abs(cx - ArenaLayout::RIGHT_BRIDGE_X));
                        REQUIRE(offCentre <= Board::BRIDGE_HALF_WIDTH + 0.3f);
                        lo = std::min(lo, cx);
                        hi = std::max(hi, cx);
                    }
                    // Placed in the lane the squad is not funnelled into single
                    // file: its crossing points span some of the deck. (Coming
                    // from the side it queues along the near edge; not asserted.)
                    const bool inLane = x == 2.5f || x == 14.5f;
                    if (inLane) REQUIRE(hi - lo > 0.4f);
                }
            }
        }
    }
}

// --- isBackRowDeadZone ---
// One row behind each King, dead except a centred opening (Board.h,
// BACK_ROW_OPENING_HALF_WIDTH).

TEST_CASE("isBackRowDeadZone is false everywhere outside the two new back rows", "[board][deadzone]") {
    Board board;
    REQUIRE_FALSE(board.isBackRowDeadZone(0.0f, 5.0f));   // ordinary row, corner x
    REQUIRE_FALSE(board.isBackRowDeadZone(17.0f, 5.0f));  // ordinary row, other corner x
    REQUIRE_FALSE(board.isBackRowDeadZone(8.5f, 16.5f));  // mid-board
}

TEST_CASE("isBackRowDeadZone blocks the corners of the bottom back row but not its center gap", "[board][deadzone]") {
    Board board;
    REQUIRE(board.isBackRowDeadZone(0.0f, 0.0f));
    REQUIRE(board.isBackRowDeadZone(17.0f, 0.0f));
    REQUIRE_FALSE(board.isBackRowDeadZone(8.5f, 0.0f));   // board's own horizontal center
    REQUIRE_FALSE(board.isBackRowDeadZone(5.5f, 0.0f));   // left edge of the opening
    REQUIRE_FALSE(board.isBackRowDeadZone(11.5f, 0.0f));  // right edge of the opening
    REQUIRE(board.isBackRowDeadZone(5.4f, 0.0f));         // just outside the opening
    REQUIRE(board.isBackRowDeadZone(11.6f, 0.0f));        // just outside the opening
}

TEST_CASE("isBackRowDeadZone mirrors the same opening on the top back row", "[board][deadzone]") {
    Board board;
    float maxY = static_cast<float>(board.getHeight() - 1); // 33
    REQUIRE(board.isBackRowDeadZone(0.0f, maxY));
    REQUIRE_FALSE(board.isBackRowDeadZone(8.5f, maxY));
}


// --- collision: what does not participate ---

TEST_CASE("resolveCollisions ignores untargetable entities (spells, projectiles)",
          "[board][collision]") {
    // resolveCollisions treats "radius 0 and isTargetable()" as a troop.
    // AreaSpell and Projectile return false from isTargetable(), which is the
    // only thing stopping a Fireball's marker from shoving the troops it lands
    // on.
    Board board;
    auto troop = std::make_shared<DummyEntity>(1, 5.0f, 5.0f, 100, 0);
    auto ghost = std::make_shared<DummyEntity>(2, 5.0f, 5.0f, 100, 1);
    ghost->targetable = false;   // stands in for AreaSpell/Projectile
    spawn(board, troop);
    spawn(board, ghost);

    board.resolveCollisions();

    REQUIRE(troop->position.x == Catch::Approx(5.0f));
    REQUIRE(troop->position.y == Catch::Approx(5.0f));
    REQUIRE(ghost->position.x == Catch::Approx(5.0f));
    REQUIRE(ghost->position.y == Catch::Approx(5.0f));
}

TEST_CASE("resolvePositionAgainstBuildings tracks buildings appearing and disappearing",
          "[board][collision][lifecycle]") {
    // The collider set changes only through commitPendingEntities and
    // cleanDeadEntities; this walks a position through both, so any cache must
    // be invalidated by both.
    Board board;
    const Vector2D probe{ 5.0f, 5.0f };

    // 1. Empty board: untouched.
    REQUIRE(board.resolvePositionAgainstBuildings(probe, 99).x == Catch::Approx(5.0f));

    // 2. A building commits: pushed out.
    class Blocker : public Entity {
    public:
        Blocker(int id, float x, float y) : Entity(id, x, y, 100, 1, 'B') {}
        void update(Board&) override {}
        float getCollisionRadius() const override { return 1.0f; }
    };
    auto blocker = std::make_shared<Blocker>(7, 5.0f, 5.0f);
    board.addEntity(blocker);
    board.commitPendingEntities();
    const Vector2D pushed = board.resolvePositionAgainstBuildings(probe, 99);
    REQUIRE(pushed.distanceTo(blocker->position) > 1.0f);

    // 3. It dies and is erased: untouched again.
    blocker->takeDamage(100);
    board.cleanDeadEntities();
    const Vector2D afterDeath = board.resolvePositionAgainstBuildings(probe, 99);
    REQUIRE(afterDeath.x == Catch::Approx(5.0f));
    REQUIRE(afterDeath.y == Catch::Approx(5.0f));
}


TEST_CASE("pushAwayFrom clears the full radius even from dead centre",
          "[board][collision][regression]") {
    // A point exactly on the obstacle's centre is pushed the full minDist (see
    // Board::pushAwayFrom).
    const Vector2D centre{ 5.0f, 5.0f };

    SECTION("dead centre") {
        const Vector2D out = Board::pushAwayFrom(centre, centre, 1.4f);
        REQUIRE(out.distanceTo(centre) >= Catch::Approx(1.4f).margin(0.01f));
    }

    SECTION("a King Tower's larger footprint, same rule") {
        const Vector2D out = Board::pushAwayFrom(centre, centre, 2.4f);
        REQUIRE(out.distanceTo(centre) >= Catch::Approx(2.4f).margin(0.01f));
    }

    SECTION("the ordinary off-centre case is unchanged") {
        // 0.5 away, needs 1.4: lands on the boundary plus the perpendicular
        // slide.
        const Vector2D near{ 5.5f, 5.0f };
        const Vector2D out = Board::pushAwayFrom(near, centre, 1.4f);
        REQUIRE(out.x == Catch::Approx(6.4f));
        REQUIRE(out.y == Catch::Approx(4.95f));
    }
}


// --- patch 0087: the walk corridor, and the path of a river jumper ---
// Evidence (engine notes, patch 0087; all from the live recordings of
// 2026-11-23/24): ground troops cross with their centre on x 2.5-3.5 and
// 13.5-14.5; a Hog Rider placed on (1, 15) lands on 2.52 (n 51), on (16, 15)
// on 14.54 (n 55), on (6, 15) on 3.6, on the tile beside the bridge (2, 15) /
// (15, 15) it walks up 3.0 / 14.0, and it reaches the far bank when a Hog
// going straight across would.

TEST_CASE("the walk corridor is half a tile each side of the Princess Tower's column",
          "[board][clamp][patch0087]") {
    Board board;
    for (float x : { 2.5f, 3.0f, 3.5f, 13.5f, 14.0f, 14.5f }) REQUIRE(Board::inWalkCorridor(x));
    for (float x : { 1.5f, 2.0f, 2.4f, 3.6f, 13.4f, 14.6f, 15.0f, 15.5f }) REQUIRE_FALSE(Board::inWalkCorridor(x));
    REQUIRE(Board::LEFT_WALK_X == Catch::Approx(3.0f));
    REQUIRE(Board::RIGHT_WALK_X == Catch::Approx(14.0f));
    // It is where a unit heads, not a wall: the deck is still the drawn one,
    // and a unit pushed onto its outer tile in mid-river stays there.
    for (float x : { 1.5f, 2.0f, 3.5f, 13.5f, 15.0f, 15.5f }) REQUIRE(board.isOnBridge(x));
    Vector2D a = board.clampToBoard(Vector2D{ 2.2f, 16.5f }, false);
    REQUIRE(a.x == Catch::Approx(2.2f));
    REQUIRE(a.y == Catch::Approx(16.5f));
}

TEST_CASE("a walker pushed along the bank onto the outer tile crosses from where it is",
          "[board][waypoint][regression][patch0087]") {
    Board board;
    const Vector2D left{ 3.0f, 27.0f };
    // Walking in from its placement tile: the corridor's mouth.
    for (const Vector2D from : { Vector2D{ 2.0f, 15.0f }, Vector2D{ 1.0f, 15.0f }, Vector2D{ 0.0f, 15.0f },
                                 Vector2D{ 2.0f, 12.0f } }) {
        Vector2D wp = board.getNextWaypoint(from, left);
        INFO("from (" << from.x << ", " << from.y << ")");
        REQUIRE(wp.x == Catch::Approx(2.5f));
        REQUIRE(wp.y == Catch::Approx(board.getRiverStart()));
    }
    // Shoved to (2.1, 15.45), 0.05 short of the water: across, towards the
    // corridor, not sideways along the bank to the mouth.
    Vector2D wp = board.getNextWaypoint(Vector2D{ 2.1f, 15.45f }, left);
    REQUIRE(wp.x == Catch::Approx(2.5f));
    REQUIRE(wp.y == Catch::Approx(board.getRiverEnd()));
    // Team 1, right bridge.
    Vector2D down = board.getNextWaypoint(Vector2D{ 15.2f, 17.53f }, Vector2D{ 14.0f, 6.0f });
    REQUIRE(down.x == Catch::Approx(14.5f));
    REQUIRE(down.y == Catch::Approx(board.getRiverStart()));
    // Off the deck altogether there is water ahead: the mouth.
    Vector2D off = board.getNextWaypoint(Vector2D{ 1.2f, 15.45f }, left);
    REQUIRE(off.y == Catch::Approx(board.getRiverStart()));
}

// Two bodies that do not fit the corridor abreast. With the corridor as a wall
// they pushed each other at the mouth for ever (Goblin Machine: no tower hit in
// 40 s).
TEST_CASE("pairs of wide bodies cross the river", "[board][waypoint][regression][patch0087]") {
    const std::vector<int> deck = { 15, 6, 25, 40, 24, 72, 33, 7 };
    constexpr int GOBLIN_MACHINE = 55, ELITE_BARBARIANS = 17, RASCALS = 113, ROYAL_RECRUITS = 77;
    int stuck = 0, runs = 0;
    std::ostringstream examples;
    for (int card : { GOBLIN_MACHINE, ELITE_BARBARIANS, RASCALS, ROYAL_RECRUITS }) {
        if (!CardRegistry::getInstance().getCard(card)) continue;
        for (int team : { 0, 1 }) {
            for (float rowsBack : { 0.5f, 1.5f, 4.0f }) {
                for (int i = 0; i <= 34; ++i) {
                    GameManager game(deck, deck);
                    Board& board = game.getBoard();
                    const float x = i * 0.5f;
                    const float y = team == 0 ? board.getRiverStart() - rowsBack : board.getRiverEnd() + rowsBack;
                    CardRegistry::getInstance().getCard(card)->spawnEntity(x, y, team, board);
                    board.commitPendingEntities();
                    std::vector<std::shared_ptr<Troop>> units;
                    for (const auto& e : board.getEntities()) {
                        if (e->team == team && !e->isTower()) {
                            if (auto t = std::dynamic_pointer_cast<Troop>(e)) units.push_back(t);
                        }
                    }
                    REQUIRE(units.size() >= 2);
                    float slowest = units[0]->getSpeed();
                    for (const auto& u : units) slowest = std::min(slowest, u->getSpeed());
                    // Twice a straight walk: bodies queue at the mouth.
                    const int budget = 2 * (DEPLOY_TIME_TICKS + 10
                        + static_cast<int>(std::ceil((9.0f + rowsBack + 2.5f) / slowest)));
                    auto over = [&](const std::shared_ptr<Troop>& u) {
                        return !u->isAlive() || (team == 0 ? u->position.y > board.getRiverEnd()
                                                           : u->position.y < board.getRiverStart());
                    };
                    bool all = false;
                    for (int t = 0; t < budget && !all; ++t) {
                        game.step();
                        all = true;
                        for (const auto& u : units) all = all && over(u);
                    }
                    ++runs;
                    if (!all && ++stuck <= 8) {
                        examples << "\n  card " << card << " team " << team << " from (" << x << ", " << y << "):";
                        for (const auto& u : units) examples << " (" << u->position.x << ", " << u->position.y << ")";
                    }
                }
            }
        }
    }
    INFO(stuck << " of " << runs << " did not all cross; first few:" << examples.str());
    REQUIRE(stuck == 0);
    REQUIRE(runs >= 2 * 3 * 35 * 3);
}

TEST_CASE("a river jumper goes 45 degrees to within half a tile of its target's column, then straight",
          "[board][waypoint][patch0087]") {
    Board board;
    const Vector2D left{ 3.0f, 27.0f }, right{ 14.0f, 27.0f };
    const float fast = Board::JUMP_DIAGONAL_SPEED;

    // The two bridge corners and the tile a Hog is played on from the middle.
    Board::JumperStep a = board.getJumperWaypoint(Vector2D{ 1.0f, 15.0f }, left);
    Board::JumperStep b = board.getJumperWaypoint(Vector2D{ 16.0f, 15.0f }, right);
    // Patch 0090 (CR_P0088=jumpedge): the corner jump lands on the bridge's
    // outer edge line, 0.8 outside the column, 1.5 rows past the near bank,
    // and keeps its forward speed: scale = length / forward length.
    const float edgeScale = std::hypot(1.2f, 2.0f) / 2.0f;
    if (P0088::jumpEdge()) {
        REQUIRE(a.waypoint.x == Catch::Approx(2.2f));
        REQUIRE(a.waypoint.y == Catch::Approx(17.0f));
        REQUIRE(a.speedScale == Catch::Approx(edgeScale));
        REQUIRE(b.waypoint.x == Catch::Approx(14.8f));
        REQUIRE(b.waypoint.y == Catch::Approx(17.0f));
        REQUIRE(b.speedScale == Catch::Approx(edgeScale));
    } else {
    REQUIRE(a.waypoint.x == Catch::Approx(2.5f));
    REQUIRE(a.waypoint.y == Catch::Approx(16.5f));
    REQUIRE(a.speedScale == Catch::Approx(fast));
    REQUIRE(b.waypoint.x == Catch::Approx(14.5f));
    REQUIRE(b.waypoint.y == Catch::Approx(16.5f));
    REQUIRE(b.speedScale == Catch::Approx(fast));
    }
    Board::JumperStep c = board.getJumperWaypoint(Vector2D{ 6.0f, 15.0f }, left);
    REQUIRE(c.waypoint.x == Catch::Approx(3.5f));
    REQUIRE(c.waypoint.y == Catch::Approx(17.5f));
    REQUIRE(c.speedScale == Catch::Approx(fast));

    // In its column over the water: straight across; on the far bank: the target.
    Board::JumperStep d = board.getJumperWaypoint(Vector2D{ 2.5f, 16.5f }, left);
    if (P0088::jumpEdge()) {
        // Patch 0090: inside the walk corridor a jumper takes the walker's
        // waypoint, its own line to the target where that crosses the bank.
        const Vector2D walk = board.getNextWaypoint(Vector2D{ 2.5f, 16.5f }, left);
        REQUIRE(d.waypoint.x == Catch::Approx(walk.x));
        REQUIRE(d.waypoint.x == Catch::Approx(2.5f + 0.5f / 10.5f));
    } else {
    REQUIRE(d.waypoint.x == Catch::Approx(2.5f));
    }
    REQUIRE(d.waypoint.y == Catch::Approx(17.5f));
    REQUIRE(d.speedScale == Catch::Approx(1.0f));
    Board::JumperStep e = board.getJumperWaypoint(Vector2D{ 2.5f, 17.5f }, left);
    REQUIRE(e.waypoint.x == Catch::Approx(3.0f));
    REQUIRE(e.waypoint.y == Catch::Approx(27.0f));
    REQUIRE(e.speedScale == Catch::Approx(1.0f));

    // Far enough back to reach the column before the water: the straight line
    // to it on the near bank, at walking speed.
    Board::JumperStep f = board.getJumperWaypoint(Vector2D{ 1.0f, 10.0f }, left);
    if (P0088::jumpEdge()) {   // patch 0090: the edge point from anywhere on the outer side
        REQUIRE(f.waypoint.x == Catch::Approx(2.2f));
        REQUIRE(f.waypoint.y == Catch::Approx(17.0f));
    } else {
    REQUIRE(f.waypoint.x == Catch::Approx(2.5f));
    REQUIRE(f.waypoint.y == Catch::Approx(15.5f));
    }
    REQUIRE(f.speedScale == Catch::Approx(1.0f));
    Board::JumperStep fromKing = board.getJumperWaypoint(Vector2D{ 9.0f, 0.0f }, right);
    REQUIRE(fromKing.waypoint.x == Catch::Approx(13.5f));   // recorded: 13.55 at the river
    REQUIRE(fromKing.waypoint.y == Catch::Approx(15.5f));
    // Not far enough back, and more than a tile from the water: 45 degrees at
    // walking speed.
    Board::JumperStep g = board.getJumperWaypoint(Vector2D{ 1.0f, 14.2f }, left);
    if (P0088::jumpEdge()) {   // patch 0090; more than JUMP_ZONE from the water: walking speed
        REQUIRE(g.waypoint.x == Catch::Approx(2.2f));
        REQUIRE(g.waypoint.y == Catch::Approx(17.0f));
    } else {
    REQUIRE(g.waypoint.x == Catch::Approx(2.5f));
    REQUIRE(g.waypoint.y == Catch::Approx(15.7f));
    }
    REQUIRE(g.speedScale == Catch::Approx(1.0f));

    // A building in the middle is its target: the same 45 degrees, towards
    // the building's column.
    Board::JumperStep h = board.getJumperWaypoint(Vector2D{ 16.0f, 15.0f }, Vector2D{ 10.0f, 23.0f });
    REQUIRE(h.waypoint.x == Catch::Approx(10.5f));
    REQUIRE(h.waypoint.y - 15.0f == Catch::Approx(16.0f - 10.5f));
    REQUIRE(h.speedScale == Catch::Approx(fast));

    // Team 1 is the mirror.
    Board::JumperStep i = board.getJumperWaypoint(Vector2D{ 16.0f, 18.0f }, Vector2D{ 14.0f, 6.0f });
    if (P0088::jumpEdge()) {   // patch 0090
        REQUIRE(i.waypoint.x == Catch::Approx(14.8f));
        REQUIRE(i.waypoint.y == Catch::Approx(16.0f));
        REQUIRE(i.speedScale == Catch::Approx(edgeScale));
    } else {
    REQUIRE(i.waypoint.x == Catch::Approx(14.5f));
    REQUIRE(i.waypoint.y == Catch::Approx(16.5f));
    REQUIRE(i.speedScale == Catch::Approx(fast));
    }

    // A target on its own side of the river: the target.
    Board::JumperStep k = board.getJumperWaypoint(Vector2D{ 1.0f, 10.0f }, Vector2D{ 9.0f, 14.0f });
    REQUIRE(k.waypoint.x == Catch::Approx(9.0f));
    REQUIRE(k.waypoint.y == Catch::Approx(14.0f));
    REQUIRE(k.speedScale == Catch::Approx(1.0f));
}

TEST_CASE("a jumper beside its crossing, on the outer side, walks onto the column instead of jumping",
          "[board][waypoint][patch0087]") {
    Board board;
    const Vector2D left{ 3.0f, 27.0f }, right{ 14.0f, 27.0f };
    if (P0088::jumpEdge()) {
        // Patch 0090: beside the bridge (within BRIDGE_WALK_REACH of the
        // column, outer side) a jumper takes the walker's route; recorded
        // (2, 15): 2.24 / 2.46 / 2.71 on real rows 14.5 / 15 / 16, not the
        // column at the bank.
        for (const auto& [from, to] : { std::pair<Vector2D, Vector2D>{ { 2.0f, 15.0f }, left },
                                        { { 15.0f, 15.0f }, right },
                                        { { 15.0f, 18.0f }, { 14.0f, 6.0f } },
                                        { { 2.0f, 18.0f }, { 3.0f, 6.0f } } }) {
            const Board::JumperStep js = board.getJumperWaypoint(from, to);
            const Vector2D walk = board.getNextWaypoint(from, to);
            REQUIRE(js.waypoint.x == Catch::Approx(walk.x));
            REQUIRE(js.waypoint.y == Catch::Approx(walk.y));
            REQUIRE(js.speedScale == Catch::Approx(1.0f));
            REQUIRE(std::abs(js.waypoint.x - (from.x < 8.5f ? 3.0f : 14.0f)) >= 0.5f - 1e-4f);
        }
        return;
    }
    Board::JumperStep a = board.getJumperWaypoint(Vector2D{ 2.0f, 15.0f }, left);
    REQUIRE(a.waypoint.x == Catch::Approx(3.0f));
    REQUIRE(a.waypoint.y == Catch::Approx(15.5f));
    REQUIRE(a.speedScale == Catch::Approx(1.0f));
    Board::JumperStep b = board.getJumperWaypoint(Vector2D{ 15.0f, 15.0f }, right);
    REQUIRE(b.waypoint.x == Catch::Approx(14.0f));
    REQUIRE(b.waypoint.y == Catch::Approx(15.5f));
    Board::JumperStep down = board.getJumperWaypoint(Vector2D{ 15.0f, 18.0f }, Vector2D{ 14.0f, 6.0f });
    REQUIRE(down.waypoint.x == Catch::Approx(14.0f));
    REQUIRE(down.waypoint.y == Catch::Approx(17.5f));
    // On the column: straight across.
    Board::JumperStep c = board.getJumperWaypoint(Vector2D{ 3.0f, 15.0f }, left);
    REQUIRE(c.waypoint.x == Catch::Approx(3.0f));
    REQUIRE(c.waypoint.y == Catch::Approx(17.5f));
    // One tile on the INNER side: nothing recorded, the half-tile rule.
    Board::JumperStep d = board.getJumperWaypoint(Vector2D{ 4.0f, 15.0f }, left);
    REQUIRE(d.waypoint.x == Catch::Approx(3.5f));
    REQUIRE(d.waypoint.y == Catch::Approx(15.5f));
    // Two tiles out: the jump.
    Board::JumperStep e = board.getJumperWaypoint(Vector2D{ 1.0f, 15.0f }, left);
    REQUIRE(e.speedScale == Catch::Approx(Board::JUMP_DIAGONAL_SPEED));
}

TEST_CASE("getJumperWaypoint never returns the caller's own position",
          "[board][waypoint][regression][patch0087]") {
    Board board;
    const Vector2D targets[] = { { 3.0f, 27.0f }, { 14.0f, 27.0f }, { 8.5f, 30.5f }, { 10.0f, 23.0f },
                                 { 9.0f, 18.0f }, { 3.0f, 6.0f }, { 14.0f, 6.0f }, { 8.0f, 11.0f },
                                 { 8.5f, 16.5f }, { 0.0f, 20.0f }, { 17.0f, 13.0f } };
    int checked = 0;
    for (const Vector2D& target : targets) {
        for (int ix = 0; ix <= 68; ++ix) {
            for (int iy = 0; iy <= 40; ++iy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    for (int dy = -2; dy <= 2; ++dy) {
                        const Vector2D here{ ix * 0.25f + dx * 0.004f, 11.5f + iy * 0.25f + dy * 0.004f };
                        if (here.distanceTo(target) <= Board::WAYPOINT_ARRIVAL_EPS) continue;
                        const Board::JumperStep js = board.getJumperWaypoint(here, target);
                        ++checked;
                        INFO("stuck at (" << here.x << ", " << here.y << ") target (" << target.x << ", " << target.y << ")");
                        REQUIRE(here.distanceTo(js.waypoint) > Board::WAYPOINT_ARRIVAL_EPS);
                        const bool plain = js.speedScale == Catch::Approx(1.0f);
                        const bool fast = js.speedScale == Catch::Approx(Board::JUMP_DIAGONAL_SPEED);
                        // Patch 0090 jumpedge: the jump to the edge point keeps the forward
                        // speed, so its scale lies between the two.
                        const bool edge = P0088::jumpEdge() && js.speedScale >= 1.0f - 1e-4f
                            && js.speedScale <= Board::JUMP_DIAGONAL_SPEED + 1e-4f;
                        REQUIRE((plain || fast || edge));
                    }
                }
            }
        }
    }
    REQUIRE(checked > 700000);
}

// Real cards through the GameManager tick loop.
TEST_CASE("a Hog Rider lands where the recorded one does, when a straight one would",
          "[board][waypoint][patch0087]") {
    const std::vector<int> deck = { 15, 6, 25, 40, 24, 72, 33, 7 };
    constexpr int HOG = 15;
    struct Landing { float x; int tick; };
    auto land = [&](float x, int team) {
        GameManager game(deck, deck);
        Board& board = game.getBoard();
        const float y = team == 0 ? 15.0f : 18.0f;
        CardRegistry::getInstance().getCard(HOG)->spawnEntity(x, y, team, board);
        board.commitPendingEntities();
        std::shared_ptr<Troop> hog;
        for (const auto& e : board.getEntities()) {
            if (e->cardId == HOG) hog = std::dynamic_pointer_cast<Troop>(e);
        }
        REQUIRE(hog);
        for (int t = 1; t <= 80; ++t) {
            game.step();
            const bool over = team == 0 ? hog->position.y >= board.getRiverEnd()
                                        : hog->position.y <= board.getRiverStart();
            if (over) return Landing{ hog->position.x, t };
        }
        return Landing{ -1.0f, -1 };
    };
    struct Case { float from, lands; };
    // placed on -> far-bank x. Recorded: 2.52, 14.54, 3.6, 3.0, 14.0, 3.05-3.2.
    // Patch 0090 (jumpedge): 2.52 / 14.54 were box centres; the box carries
    // the Hog's hammer, +0.3 outward while it faces up the left lane
    // (runs/bridge_1126): the corner Hog lands on 2.2 / 14.8 (n 136 / 130),
    // and the one beside the bridge walks, 2.6 / 14.4 at the far bank.
    const bool edge = P0088::jumpEdge();
    const Case cases[] = { { 1.0f, edge ? 2.2f : 2.5f }, { 16.0f, edge ? 14.8f : 14.5f }, { 6.0f, 3.5f }, { 11.0f, 13.5f },
                           { 0.0f, edge ? 2.2f : 2.5f }, { 17.0f, edge ? 14.8f : 14.5f },
                           { 2.0f, edge ? 2.59f : 3.0f }, { 15.0f, edge ? 14.41f : 14.0f }, { 3.0f, 3.0f }, { 14.0f, 14.0f } };
    for (int team : { 0, 1 }) {
        const Landing straight = land(3.0f, team);
        REQUIRE(straight.tick > 0);
        for (const Case& c : cases) {
            const Landing got = land(c.from, team);
            INFO("team " << team << " placed on x " << c.from << ": far bank at x " << got.x << " on tick " << got.tick
                 << " (straight: tick " << straight.tick << ")");
            REQUIRE(got.tick > 0);
            REQUIRE(got.x == Catch::Approx(c.lands).margin(0.08));
            // The jump costs no time; the step onto the column from the tile
            // beside the bridge costs 0.24 s (recorded 0.1-0.2 s).
            const bool besideBridge = c.from == 2.0f || c.from == 15.0f;
            REQUIRE(got.tick - straight.tick >= 0);
            REQUIRE(got.tick - straight.tick <= (besideBridge ? 3 : 1));
        }
    }
}

TEST_CASE("river jumpers from every start cross the river", "[board][waypoint][regression][patch0087]") {
    const std::vector<int> deck = { 15, 6, 25, 40, 24, 72, 33, 7 };
    constexpr int HOG = 15, PRINCE = 14, BANDIT = 50, BATTLE_RAM = 81, ROYAL_HOGS = 82;
    int stuck = 0, runs = 0;
    std::ostringstream examples;
    for (int card : { HOG, PRINCE, BANDIT, BATTLE_RAM, ROYAL_HOGS }) {
        for (int team : { 0, 1 }) {
            for (float rowsBack : { 0.0f, 0.3f, 3.0f }) {
                for (int i = 0; i <= 170; ++i) {
                    GameManager game(deck, deck);
                    Board& board = game.getBoard();
                    const float x = i * 0.1f;
                    const float y = team == 0 ? board.getRiverStart() - rowsBack : board.getRiverEnd() + rowsBack;
                    CardRegistry::getInstance().getCard(card)->spawnEntity(x, y, team, board);
                    board.commitPendingEntities();
                    std::shared_ptr<Troop> unit;
                    for (const auto& e : board.getEntities()) {
                        if (e->cardId == card) unit = std::dynamic_pointer_cast<Troop>(e);
                    }
                    REQUIRE(unit);
                    const int budget = DEPLOY_TIME_TICKS + 5
                        + static_cast<int>(std::ceil((8.0f + rowsBack + 2.5f) / unit->getSpeed())) + 10;
                    bool crossed = false;
                    for (int t = 0; t < budget && !crossed; ++t) {
                        game.step();
                        crossed = team == 0 ? unit->position.y > board.getRiverEnd()
                                            : unit->position.y < board.getRiverStart();
                    }
                    ++runs;
                    if (!crossed && ++stuck <= 8) {
                        examples << "\n  card " << card << " team " << team << " from (" << x << ", " << y
                                 << "), now (" << unit->position.x << ", " << unit->position.y << ")";
                    }
                }
            }
        }
    }
    INFO(stuck << " of " << runs << " never crossed; first few:" << examples.str());
    REQUIRE(stuck == 0);
    REQUIRE(runs == 5 * 2 * 3 * 171);
}

// Patch 0090 (CR_P0088=hogspeed): on 174 clean real tracks past the river
// (runs/bridge_1126/speed2.py, real rows 18.6-21.6, both screen halves) the Hog
// runs 2.34 tiles/s, not S/50 = 2.40, while Ice Golem, Skeletons and Knight
// match S/50 on the same rows. Over the water it keeps its speed, so the
// crossing jumpedge fits is unchanged.
TEST_CASE("hogspeed: the Hog Rider walks 2.5 % slower on land and keeps its speed over the water",
          "[board][patch0090]") {
    REQUIRE(P0088::HOG_LAND_SPEED_OF_S50 * 2.40f == Catch::Approx(2.34f));
    REQUIRE(P0088::hogLandScale() * P0088::speedScale() == Catch::Approx(0.975f * 0.010f / 0.011045f));
    const std::vector<int> deck = { 15, 6, 25, 40, 24, 72, 33, 7 };
    constexpr int HOG = 15, LUMBERJACK = 21;   // same tier (120)
    const float want = P0088::hogSpeed() ? 0.975f : 1.0f;
    auto spawn = [](GameManager& game, float hx, float hy, float lx, float ly,
                    std::shared_ptr<Troop>& hog, std::shared_ptr<Troop>& lj) {
        Board& board = game.getBoard();
        CardRegistry::getInstance().getCard(HOG)->spawnEntity(hx, hy, 0, board);
        CardRegistry::getInstance().getCard(LUMBERJACK)->spawnEntity(lx, ly, 0, board);
        board.commitPendingEntities();
        for (const auto& e : board.getEntities()) {
            if (e->cardId == HOG) hog = std::dynamic_pointer_cast<Troop>(e);
            if (e->cardId == LUMBERJACK) lj = std::dynamic_pointer_cast<Troop>(e);
        }
    };
    SECTION("open ground past the river: Hog step / Lumberjack step") {
        GameManager game(deck, deck);
        std::shared_ptr<Troop> hog, lj;
        spawn(game, 3.0f, 20.5f, 14.0f, 20.5f, hog, lj);
        REQUIRE(hog); REQUIRE(lj);
        REQUIRE(hog->landSpeedScale == Catch::Approx(want));
        REQUIRE(lj->landSpeedScale == 1.0f);
        float hs = 0.0f, ls = 0.0f; int n = 0;
        for (int t = 1; t <= 40; ++t) {
            const Vector2D h0 = hog->position, l0 = lj->position;
            game.step();
            const float dh = h0.distanceTo(hog->position), dl = l0.distanceTo(lj->position);
            if (h0.y >= 20.5f && hog->position.y <= 23.0f && l0.y >= 20.5f && lj->position.y <= 23.0f
                && dh > 0.05f && dl > 0.05f) { hs += dh; ls += dl; ++n; }
        }
        INFO("ticks " << n << " hog " << hs << " lumberjack " << ls);
        REQUIRE(n >= 5);
        REQUIRE(hs / ls == Catch::Approx(want).epsilon(0.005));
    }
    SECTION("over the water: the Hog's step is the Lumberjack's on open ground") {
        if (!P0088::jumpEdge()) return;   // the column route over the bridge (0090 jumpedge)
        GameManager game(deck, deck);
        Board& board = game.getBoard();
        std::shared_ptr<Troop> hog, lj;
        spawn(game, 3.0f, 12.0f, 14.0f, 6.0f, hog, lj);
        REQUIRE(hog); REQUIRE(lj);
        float hs = 0.0f, ls = 0.0f; int nh = 0, nl = 0;
        for (int t = 1; t <= 60; ++t) {
            const Vector2D h0 = hog->position, l0 = lj->position;
            game.step();
            const float dh = h0.distanceTo(hog->position), dl = l0.distanceTo(lj->position);
            if (h0.y >= board.getRiverStart() && hog->position.y < board.getRiverEnd() && dh > 0.05f) { hs += dh; ++nh; }
            if (l0.y >= 6.5f && lj->position.y <= 12.5f && dl > 0.05f) { ls += dl; ++nl; }
        }
        INFO("water ticks " << nh << " step " << hs / std::max(nh, 1) << ", land ticks " << nl << " step " << ls / std::max(nl, 1));
        REQUIRE(nh >= 3); REQUIRE(nl >= 5);
        REQUIRE((hs / nh) / (ls / nl) == Catch::Approx(1.0f).epsilon(0.005));
    }
}
