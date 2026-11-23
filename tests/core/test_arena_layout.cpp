#include <catch_amalgamated.hpp>
#include "ArenaLayout.h"
#include "Board.h"
#include "GameManager.h"
#include "ClashEnv.h"
#include <string>
#include <cmath>

// The arena is symmetric about the cell-index centre (WIDTH-1)/2 = 8.5, the
// x-analogue of the observation's y -> 33 - y. Every pair below must mirror
// under 17 - x, or one lane plays differently from the other.

TEST_CASE("ArenaLayout is mirror-symmetric about the board centre", "[arena][geometry]") {
    REQUIRE(ArenaLayout::CENTER_X == Catch::Approx(8.5f));
    REQUIRE(ArenaLayout::mirrorX(ArenaLayout::LEFT_LANE_X) == Catch::Approx(ArenaLayout::RIGHT_LANE_X));
    REQUIRE(ArenaLayout::mirrorX(ArenaLayout::LEFT_BRIDGE_X) == Catch::Approx(ArenaLayout::RIGHT_BRIDGE_X));
    REQUIRE(ArenaLayout::mirrorX(ArenaLayout::CENTER_X) == Catch::Approx(ArenaLayout::CENTER_X));

    // Rows mirror too: team 0's King at 2.5 <-> team 1's at 30.5.
    REQUIRE(ArenaLayout::mirrorY(ArenaLayout::kingY(0)) == Catch::Approx(ArenaLayout::kingY(1)));
    REQUIRE(ArenaLayout::mirrorY(ArenaLayout::princessY(0)) == Catch::Approx(ArenaLayout::princessY(1)));
}

// The real river row:
//
//     column  012345678901234567
//             WWBBWWWWWWWWWWBBWW      (W water, B bridge)
//
// Bridges occupy columns 2-3 and 14-15, so each centre sits on the seam between
// its tiles.
TEST_CASE("bridges are two tiles wide, centred on the seam", "[arena][geometry]") {
    REQUIRE(ArenaLayout::LEFT_BRIDGE_X == Catch::Approx(2.5f));
    REQUIRE(ArenaLayout::RIGHT_BRIDGE_X == Catch::Approx(14.5f));

    Board board;
    const float midRiver = 16.5f;
    for (int x = 0; x < ArenaLayout::WIDTH; ++x) {
        Vector2D probe{ static_cast<float>(x), midRiver };
        // "Can stand here" is "the clamp leaves it here". Until patch 0060 this
        // read only y, which encoded a clamp that moves a unit to a bank and
        // never sideways; a unit half a tile off a deck is now put on its edge.
        const Vector2D settled = board.clampToBoard(probe, false);
        bool walkable = settled.y == Catch::Approx(midRiver) && settled.x == Catch::Approx(probe.x);
        bool expected = (x == 2 || x == 3 || x == 14 || x == 15);
        INFO("column " << x << " of WWBBWWWWWWWWWWBBWW");
        REQUIRE(walkable == expected);
    }
}

// GameManager must not keep its own copy. Asserted by property (every King on
// centre, every Princess on its lane column), not by spawn order.
TEST_CASE("GameManager spawns its towers exactly where ArenaLayout says", "[arena][geometry]") {
    GameManager game({ 0,1,2,3,4,5,6,7 }, { 0,1,2,3,4,5,6,7 });
    int kingsSeen = 0, princessSeen = 0;

    for (const auto& e : game.getBoard().getEntities()) {
        if (!e->isTower()) continue;
        if (e->symbol == 'R') {
            kingsSeen++;
            REQUIRE(e->position.x == Catch::Approx(ArenaLayout::CENTER_X));
            REQUIRE(e->position.y == Catch::Approx(ArenaLayout::kingY(e->team)));
        } else {
            princessSeen++;
            bool left = e->position.x < ArenaLayout::CENTER_X;
            REQUIRE(e->position.x == Catch::Approx(left ? ArenaLayout::LEFT_LANE_X
                                                        : ArenaLayout::RIGHT_LANE_X));
            REQUIRE(e->position.y == Catch::Approx(ArenaLayout::princessY(e->team)));
        }
    }
    REQUIRE(kingsSeen == 2);
    REQUIRE(princessSeen == 4);
}

// Board's bridge members come from ArenaLayout, not a second literal.
TEST_CASE("Board's bridges are ArenaLayout's bridges", "[arena][geometry]") {
    Board board;
    REQUIRE(board.getLeftBridge().x == Catch::Approx(ArenaLayout::LEFT_BRIDGE_X));
    REQUIRE(board.getRightBridge().x == Catch::Approx(ArenaLayout::RIGHT_BRIDGE_X));
    REQUIRE(board.getLeftBridge().y == Catch::Approx(ArenaLayout::BRIDGE_Y));
    REQUIRE(board.getRightBridge().y == Catch::Approx(ArenaLayout::BRIDGE_Y));
}

// A Princess Tower is 3 wide and its bridge 2 wide, so the bridge covers the
// tower's two outer columns and the tower centre sits half a tile inboard.
// LanePath relies on this.
TEST_CASE("each bridge sits half a tile outboard of its own Princess Tower",
          "[arena][geometry]") {
    REQUIRE(ArenaLayout::LEFT_LANE_X - ArenaLayout::LEFT_BRIDGE_X == Catch::Approx(0.5f));
    REQUIRE(ArenaLayout::RIGHT_BRIDGE_X - ArenaLayout::RIGHT_LANE_X == Catch::Approx(0.5f));
}

// Lane selection is nearest-bridge, as in Board::getNextWaypoint, so a unit's
// lane objective and its crossing agree.
TEST_CASE("lane selection splits at the board centre and matches the bridges",
          "[arena][geometry]") {
    REQUIRE(ArenaLayout::isLeftLane(0.0f));
    REQUIRE(ArenaLayout::isLeftLane(8.49f));
    REQUIRE_FALSE(ArenaLayout::isLeftLane(ArenaLayout::CENTER_X));
    REQUIRE_FALSE(ArenaLayout::isLeftLane(17.0f));

    REQUIRE(ArenaLayout::bridgeXFor(1.0f) == Catch::Approx(ArenaLayout::LEFT_BRIDGE_X));
    REQUIRE(ArenaLayout::bridgeXFor(16.0f) == Catch::Approx(ArenaLayout::RIGHT_BRIDGE_X));
    REQUIRE(ArenaLayout::laneXFor(1.0f) == Catch::Approx(ArenaLayout::LEFT_LANE_X));
    REQUIRE(ArenaLayout::laneXFor(16.0f) == Catch::Approx(ArenaLayout::RIGHT_LANE_X));
}

// --- the observation must describe the physics ---
// Channel 8 is the river/bridge mask, the network's only map of where it can
// cross. These compare it against movement, not a literal, so they cannot go
// stale on the next arena change.

TEST_CASE("the river mask marks a column passable iff a unit can actually stand there",
          "[arena][observation][regression]") {
    Board board;
    ClashEnv env({ 15, 6, 25, 40, 24, 72, 33, 7 }, { 15, 6, 25, 40, 24, 72, 33, 7 });
    env.reset();

    const int W = ClashEnv::BOARD_WIDTH;
    const int plane = W * ClashEnv::BOARD_HEIGHT;
    constexpr int riverRow = 17;
    const float midRiver = 16.5f;

    for (int team = 0; team < 2; ++team) {
        std::vector<float> obs = env.getObservationForTeam(team);
        for (int x = 0; x < W; ++x) {
            // Ground truth: can a river-respecting unit hold this position
            // inside the river band (clampToBoard's rule)?
            Vector2D probe{ static_cast<float>(x), midRiver };
            // Both coordinates since patch 0060: the clamp may also move a unit
            // sideways onto a deck's edge, which is not "can stand here".
            const Vector2D settled = board.clampToBoard(probe, false);
            const bool passable = std::abs(settled.y - midRiver) < 1e-4f && std::abs(settled.x - probe.x) < 1e-4f;
            const float marked = obs[8 * plane + riverRow * W + x];

            INFO("team " << team << " column " << x
                 << ": physics says " << (passable ? "BRIDGE" : "water")
                 << ", observation says " << (marked > 0.0f ? "BRIDGE" : "water"));
            REQUIRE((marked > 0.0f) == passable);
        }
    }
}

TEST_CASE("both teams see the bridge mask on the same row and columns",
          "[arena][observation]") {
    ClashEnv env({ 15, 6, 25, 40, 24, 72, 33, 7 }, { 15, 6, 25, 40, 24, 72, 33, 7 });
    env.reset();
    const int W = ClashEnv::BOARD_WIDTH;
    const int plane = W * ClashEnv::BOARD_HEIGHT;
    constexpr int riverRow = 17;

    std::vector<float> a = env.getObservationForTeam(0);
    std::vector<float> b = env.getObservationForTeam(1);
    for (int x = 0; x < W; ++x) {
        INFO("column " << x);
        REQUIRE(a[8 * plane + riverRow * W + x] == b[8 * plane + riverRow * W + x]);
    }
}

// Board::isOnBridge is the shared definition; it must select exactly the real
// river row.
TEST_CASE("isOnBridge reproduces the real river row WWBBWWWWWWWWWWBBWW",
          "[arena][geometry]") {
    Board board;
    std::string row;
    for (int x = 0; x < ArenaLayout::WIDTH; ++x)
        row += board.isOnBridge(static_cast<float>(x)) ? 'B' : 'W';
    INFO("engine row: " << row);
    REQUIRE(row == "WWBBWWWWWWWWWWBBWW");
}

TEST_CASE("realRow squeezes out the two rows the real 32-row arena lacks", "[arena_layout][real_row]") {
    // Identity on a team's own half up to row 13, engine 15-18 are real 14-17,
    // engine 20 and up are real 18 and up (live_ppo/tile_map.py, "tower").
    REQUIRE(ArenaLayout::realRow(0.0f) == Catch::Approx(0.0f));
    REQUIRE(ArenaLayout::realRow(13.0f) == Catch::Approx(13.0f));
    REQUIRE(ArenaLayout::realRow(15.0f) == Catch::Approx(14.0f));
    REQUIRE(ArenaLayout::realRow(18.0f) == Catch::Approx(17.0f));
    REQUIRE(ArenaLayout::realRow(20.0f) == Catch::Approx(18.0f));
    REQUIRE(ArenaLayout::realRow(33.0f) == Catch::Approx(31.0f));
    // Continuous and never decreasing, and the same from either end.
    float previous = ArenaLayout::realRow(0.0f);
    for (float y = 0.25f; y <= 33.0f; y += 0.25f) {
        const float row = ArenaLayout::realRow(y);
        REQUIRE(row >= previous);
        REQUIRE(row - previous <= 0.25f + 1e-4f);
        REQUIRE(ArenaLayout::realRow(ArenaLayout::mirrorY(y)) == Catch::Approx(31.0f - row).margin(1e-4));
        previous = row;
    }
    // The real King-to-enemy-Princess line: 5.5 across, 22.5 along.
    REQUIRE(ArenaLayout::realRow(ArenaLayout::princessY(1)) - ArenaLayout::realRow(ArenaLayout::kingY(0))
            == Catch::Approx(22.5f));
}
