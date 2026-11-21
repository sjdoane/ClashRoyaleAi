#pragma once
#include "PeriodicEffect.h"
#include "CombatEntity.h"
#include "Board.h"

// Royal Chef's pancake (patch 0019; Clash Royale wiki, Royal Chef): +1 Level
// for one allied troop anywhere in the arena (CombatEntity::gainLevel, past the
// level cap too). Buildings, Crown Towers and troops under 33% of their full
// hp are never fed. Troops with fewer pancakes come first, then higher hp, so
// a push's tank is fed before its support and the pancakes spread out. When
// the pancake is cooked is GameManager's (cookRoyalChef); this only serves.
//
// It was a +10% hp top-up with a permanent 1.10x damage buff, once per troop,
// fed to the nearest ally within 6 tiles of the tower every 28 s, before the
// engine had levels.
class RoyalChefBuffEffect : public IPeriodicEffect {
public:
    static constexpr float MIN_HP_FRACTION = 0.33f;

    // Feeds one troop of `team`; false if none qualifies (the pancake waits).
    static bool serve(Board& board, int team) {
        std::shared_ptr<CombatEntity> best;
        for (const auto& entity : board.getEntities()) {
            if (entity->team != team || !entity->isAlive() || entity->isTower() || entity->isBuilding()) continue;
            auto c = std::dynamic_pointer_cast<CombatEntity>(entity);
            if (!c) continue;
            const int full = c->fullHp > 0 ? c->fullHp : c->hp;
            if (static_cast<float>(c->hp) < MIN_HP_FRACTION * static_cast<float>(full)) continue;
            if (!best || c->royalChefServings < best->royalChefServings
                || (c->royalChefServings == best->royalChefServings && c->hp > best->hp)) {
                best = c;
            }
        }
        if (!best) return false;
        best->royalChefServings++;
        best->gainLevel();
        return true;
    }

    void apply(Board& board, const Vector2D&, int team) const override { serve(board, team); }
};
