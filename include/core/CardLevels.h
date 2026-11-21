#pragma once
#include <cmath>

// Card and Crown Tower levels (perception/UPSTREAM_REQUESTS.md item 42).
//
// Every stat in CardRegistry and TowerTroops is the level-11 (tournament)
// value. A real opponent is often over-levelled -- the Training Camp trainer
// that beat us at level 15 -- so Board scales each team's bodies and spells
// to that team's level as they enter play (Board::commitPendingEntities).
//
// The curve is the game's single power-level table, the same for every rarity
// once levels are on the 1..16 "card level" scale: FirstLight Zero's published
// native_runner/data/competitive/card_logic.json, rarity_curves.Common
// .active_power_level_multipliers_percent (release 15.535.13), projected as
// base * percent / 100. Level 11 is 256, so a level-L value is the level-11
// value * percent[L] / 256.
//
// Checked against values read off our own recordings and the wiki:
//   Princess Tower 2534 (lv 9) 3052 (11) 4032 (14) 4424 (15); this curve gives
//   2527, 3052, 4041, 4435 -- within 0.3%. Hog Rider 1697 hp / 317 dmg at 11
//   give 2466 / 461 at 15, the same as 663 * 3.72 and 124 * 3.72.
namespace CardLevels {

static constexpr int MIN_LEVEL = 1;
static constexpr int MAX_LEVEL = 16;
// The level every registry stat is written at.
static constexpr int TOURNAMENT_LEVEL = 11;

static constexpr int MULTIPLIER_PERCENT[MAX_LEVEL] = {
    100, 110, 121, 133, 146, 160, 176, 193, 212, 233, 256, 281, 309, 339, 372, 409,
};

inline constexpr bool isValid(int level) { return level >= MIN_LEVEL && level <= MAX_LEVEL; }

// A level-11 hp or damage value at `level`. 0 and negative sentinels (-1 =
// "use the default") pass through unchanged.
inline int scale(int level11Value, int level) {
    if (level11Value <= 0 || level == TOURNAMENT_LEVEL || !isValid(level)) return level11Value;
    const double v = static_cast<double>(level11Value) * MULTIPLIER_PERCENT[level - 1]
                   / MULTIPLIER_PERCENT[TOURNAMENT_LEVEL - 1];
    return static_cast<int>(std::lround(v));
}

} // namespace CardLevels
