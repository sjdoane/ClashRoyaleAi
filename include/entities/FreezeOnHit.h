#pragma once
#include "OnHitEffect.h"
#include "CombatEntity.h"

// Kept out of OnHitEffect.h: calling applyFreeze needs CombatEntity's full
// definition, and CombatEntity.h includes OnHitEffect.h.
class FreezeOnHit : public IOnHitEffect {
    int ticks;
    float slowFactor;

public:
    FreezeOnHit(int ticks, float slowFactor) : ticks(ticks), slowFactor(slowFactor) {}
    // Read-only, for get_card_stats (card-stats audit, patch 0003).
    int getTicks() const { return ticks; }
    float getSlowFactor() const { return slowFactor; }

    void apply(std::shared_ptr<CombatEntity> target) const override {
        target->applyFreeze(ticks, slowFactor);
    }
};
