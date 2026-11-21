#pragma once
#include "PeriodicEffect.h"
#include "CardStats.h"
#include "CardFactories.h"
#include "Board.h"
#include "CombatEntity.h"
#include <memory>

// Evolved Skeletons' "Never-ending Horde": spawns another copy on each landed
// attack, unless maxAlive with that id are already alive on the team. Has
// IPeriodicEffect's shape because CombatEntity::onHitSpawnEffect fires it from
// the same call site.
class CappedSpawnOnHitEffect : public IPeriodicEffect,
        public std::enable_shared_from_this<CappedSpawnOnHitEffect> {
    CardStats childStats;
    int maxAlive;
    // Card-stats audit (patch 0003): children get this same effect, so every
    // Evolved Skeleton multiplies (the population cap still applies).
    bool childrenMultiply;

public:
    CappedSpawnOnHitEffect(CardStats childStats, int maxAlive, bool childrenMultiply = false)
        : childStats(std::move(childStats)), maxAlive(maxAlive), childrenMultiply(childrenMultiply) {}
    // Read-only, for get_card_stats (card-stats audit, patch 0003).
    const CardStats& child() const { return childStats; }
    int getMaxAlive() const { return maxAlive; }

    void apply(Board& board, const Vector2D& position, int team) const override {
        int aliveCount = 0;
        for (const auto& e : board.getEntities()) {
            if (e->isAlive() && e->team == team && e->cardId == childStats.id) aliveCount++;
        }
        if (aliveCount >= maxAlive) return;
        const size_t before = board.pendingEntityCount();
        CardFactories::spawn(childStats, position.x, position.y, team, board);
        if (!childrenMultiply) return;
        auto self = std::const_pointer_cast<CappedSpawnOnHitEffect>(shared_from_this());
        for (size_t i = before; i < board.pendingEntityCount(); ++i) {
            if (auto c = std::dynamic_pointer_cast<CombatEntity>(board.getPendingEntity(i))) {
                c->onHitSpawnEffect = self;
            }
        }
    }
};
