#pragma once

// The arena's fixed geometry, in one place; bound to Python as ARENA_*.
//
// x is a cell index clamped to [0, WIDTH-1], so the board centre, the fixed
// point of mirrorX, is (WIDTH-1)/2 = 8.5, not 9.0: the x-analogue of the
// observation's y -> (HEIGHT-1) - y.
namespace ArenaLayout {

inline constexpr int WIDTH = 18;
inline constexpr int HEIGHT = 34;

// Maps one team's frame onto the other, and one lane onto the other.
inline constexpr float CENTER_X = (WIDTH - 1) / 2.0f;   // 8.5
constexpr float mirrorX(float x) { return (WIDTH - 1) - x; }
constexpr float mirrorY(float y) { return (HEIGHT - 1) - y; }

// Princess Tower columns. Towers are 3 tiles wide (collision radius 1.5):
// columns 2-4 and 13-15.
inline constexpr float LEFT_LANE_X = 3.0f;
inline constexpr float RIGHT_LANE_X = mirrorX(LEFT_LANE_X);   // 14.0

// Bridge centres. A bridge is two tiles wide (river row WWBBWWWWWWWWWWBBWW), so
// each centre sits on the seam between its tiles: cell i covers [i-0.5, i+0.5],
// and 2.5 with Board::BRIDGE_HALF_WIDTH = 1.0 spans exactly cells 2 and 3.
inline constexpr float LEFT_BRIDGE_X = 2.5f;
inline constexpr float RIGHT_BRIDGE_X = mirrorX(LEFT_BRIDGE_X);   // 14.5
inline constexpr float BRIDGE_Y = 16.5f;                          // the river band's centre

// Tower rows. Team 0 defends the low-y end.
inline constexpr float KING_Y_TEAM0 = 2.5f;
inline constexpr float PRINCESS_Y_TEAM0 = 6.0f;
constexpr float kingY(int team) { return team == 0 ? KING_Y_TEAM0 : mirrorY(KING_Y_TEAM0); }
constexpr float princessY(int team) { return team == 0 ? PRINCESS_Y_TEAM0 : mirrorY(PRINCESS_Y_TEAM0); }

// The row of the real 18 x 32 arena an engine y stands for. The engine board
// has two rows the real arena lacks, 14 and 19, one on each bank of the river
// (the "tower" mapping of live_ppo/tile_map.py: identity up to row 13, engine
// 15-18 are real 14-17, engine 20+ are real 18+). Piecewise linear through
// those rows, so each extra row is squeezed out rather than cut:
// realRow(mirrorY(y)) == 31 - realRow(y). Distances a real projectile flies
// are measured in these rows (CardFactories::thrownSpellFlightTicks); the
// engine's King-to-enemy-Princess line is 25.1 tiles, the real one 23.2.
constexpr float realRow(float y) {
    const float a = (y - 13.0f) * 0.5f;
    const float b = (y - 18.0f) * 0.5f;
    return y - (a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a))
             - (b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b));
}

// Which lane a position belongs to: the nearest bridge, the same rule
// Board::getNextWaypoint uses, so a unit's lane objective and its crossing
// always agree. Evaluated per call, so nothing extra is carried through a
// snapshot.
constexpr bool isLeftLane(float x) { return x < CENTER_X; }
constexpr float bridgeXFor(float x) { return isLeftLane(x) ? LEFT_BRIDGE_X : RIGHT_BRIDGE_X; }
constexpr float laneXFor(float x) { return isLeftLane(x) ? LEFT_LANE_X : RIGHT_LANE_X; }

}  // namespace ArenaLayout
