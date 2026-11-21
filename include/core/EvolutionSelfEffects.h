#pragma once
#include "../entities/SelfPeriodicEffect.h"
#include "../entities/CombatEntity.h"
#include <algorithm>
#include <limits>

// Self-periodic Evolution abilities (patch 0015). Each runs through
// CombatEntity's self-periodic hook (CardStats::withSelfPeriodicEffect).

// Evolved Giant ("Rage Trail"): from 50% health on, a rage zone every interval
// around the Giant (allies and himself), +35% speed for the buff duration. The
// radius starts at 2 tiles and grows 0.5 per attack started after the trigger,
// up to 3.5 (three attacks). The trail is a path behind him in the game; here
// it is a disc on him each interval. Rage is the engine's damage-and-attack
// buff (applyBuff), as for the Rage spell; movement speed is not modelled.
class EvoGiantRageTrailEffect : public ISelfPeriodicEffect {
    float triggerHpFraction, baseRadius, radiusPerAttack, maxRadius, multiplier;
    int durationTicks;

public:
    EvoGiantRageTrailEffect(float triggerHpFraction, float baseRadius, float radiusPerAttack,
            float maxRadius, float multiplier, int durationTicks)
        : triggerHpFraction(triggerHpFraction), baseRadius(baseRadius), radiusPerAttack(radiusPerAttack),
          maxRadius(maxRadius), multiplier(multiplier), durationTicks(durationTicks) {}

    bool apply(Board& board, CombatEntity& self) const override {
        if (self.selfPeriodicState < 0) {
            if (self.hp > self.fullHp * triggerHpFraction) return false;
            self.selfPeriodicState = self.attacksStarted;   // attacks at the trigger
        }
        const float r = std::min(maxRadius,
            baseRadius + radiusPerAttack * static_cast<float>(self.attacksStarted - self.selfPeriodicState));
        applyAreaBuff(board, self.position, r, -1, self.team, multiplier, durationTicks, 1000000);
        return true;
    }
};

// Evolved Elite Barbarians ("Rage Spear"), per barbarian: the nearest enemy
// ground troop between minRange and maxRange tiles takes `damage`, and the
// thrower plus allies within trailRadius of him are enraged for
// durationTicks. Recharges every interval; with no target in the band it
// retries next tick. The spear's rage path toward the target is modelled as
// the disc on the thrower.
class EvoEliteBarbarianSpearEffect : public ISelfPeriodicEffect {
    float minRange, maxRange;
    int damage;
    float multiplier;
    int durationTicks;
    float trailRadius;

public:
    EvoEliteBarbarianSpearEffect(float minRange, float maxRange, int damage, float multiplier,
            int durationTicks, float trailRadius)
        : minRange(minRange), maxRange(maxRange), damage(damage), multiplier(multiplier),
          durationTicks(durationTicks), trailRadius(trailRadius) {}

    bool apply(Board& board, CombatEntity& self) const override {
        std::shared_ptr<Entity> best;
        float bestDist = std::numeric_limits<float>::max();
        for (const auto& e : board.getEntities()) {
            if (e->team == self.team || !e->isAlive() || !e->isTargetable()) continue;
            if (e->isBuilding() || e->isTower() || e->isFlying) continue;
            const float d = self.position.distanceTo(e->position);
            if (d < minRange || d > maxRange || d >= bestDist) continue;
            best = e;
            bestDist = d;
        }
        if (!best) return false;
        best->takeDamage(damage);
        applyAreaBuff(board, self.position, trailRadius, -1, self.team, multiplier, durationTicks, 1000000);
        return true;
    }
};

// Evolved Electro Giant ("Level Damper"): every interval, each enemy troop and
// non-tower building within radius loses one level (CombatEntity::loseLevel:
// about -9% hp and damage per step, never below level 1). No damage; the loss
// is permanent and stacks. Crown Towers are exempt.
class LevelDamperEffect : public ISelfPeriodicEffect {
    float radius;

public:
    explicit LevelDamperEffect(float radius) : radius(radius) {}

    bool apply(Board& board, CombatEntity& self) const override {
        for (const auto& e : board.getEntities()) {
            if (e->team == self.team || !e->isAlive() || e->isTower()) continue;
            if (self.position.distanceTo(e->position) > radius) continue;
            if (auto c = std::dynamic_pointer_cast<CombatEntity>(e)) c->loseLevel();
        }
        return true;
    }
};
