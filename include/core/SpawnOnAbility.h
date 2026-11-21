#pragma once
#include "AbilityEffect.h"
#include "CombatEntity.h"
#include "CardStats.h"
#include "CardFactories.h"
#include "PeriodicSpawnEffect.h"
#include "AreaSpell.h"
#include "ArenaLayout.h"
#include "Board.h"
#include <algorithm>
#include <memory>

// A summon ability (Little Prince's Royal Rescue): spawns a fixed troop at the
// Champion's position.
//
// Card-stats audit (patch 0003): optionally `forwardTiles` ahead of the caster
// (toward the enemy), after `delayTicks`, with a landing burst of
// `landingDamage` in `landingRadius` (Hero Musketeer's Trusty Turret). The
// delayed form is an AreaSpell whose detonation spawns the child, the same
// path Royal Delivery uses. All defaults reproduce the original behaviour.
class SpawnOnAbility : public IAbilityEffect {
    CardStats childStats;
    float forwardTiles;
    int delayTicks;
    int landingDamage;
    float landingRadius;

public:
    explicit SpawnOnAbility(CardStats childStats, float forwardTiles = 0.0f, int delayTicks = 0,
            int landingDamage = 0, float landingRadius = 0.0f)
        : childStats(std::move(childStats)), forwardTiles(forwardTiles), delayTicks(delayTicks),
          landingDamage(landingDamage), landingRadius(landingRadius) {}

    const CardStats& child() const { return childStats; }
    float getForwardTiles() const { return forwardTiles; }
    int getDelayTicks() const { return delayTicks; }
    int getLandingDamage() const { return landingDamage; }
    float getLandingRadius() const { return landingRadius; }

    void apply(Board& board, CombatEntity& self) const override {
        const float x = self.position.x;
        float y = self.position.y + ((self.team == 0) ? forwardTiles : -forwardTiles);
        y = std::clamp(y, 0.0f, static_cast<float>(ArenaLayout::HEIGHT - 1));
        if (delayTicks <= 0 && landingDamage <= 0) {
            CardFactories::spawn(childStats, x, y, self.team, board);
            return;
        }
        // AreaSpell detonates on the update after its delay reaches 0.
        auto drop = std::make_shared<AreaSpell>(board.allocateId(), x, y, self.team,
            std::max(landingRadius, 0.01f), landingDamage, std::max(delayTicks - 1, 0), childStats.symbol,
            nullptr, false, 1, 0, false, 1.0f, 0, 0.0f,
            std::make_shared<PeriodicSpawnEffect>(childStats));
        drop->name = childStats.name;
        drop->cardId = self.cardId;
        board.addEntity(drop);
    }
};
