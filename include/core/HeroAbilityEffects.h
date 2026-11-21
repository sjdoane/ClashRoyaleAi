#pragma once
#include "AbilityEffect.h"
#include "../entities/SelfPeriodicEffect.h"
#include "CombatEntity.h"
#include "AreaSpell.h"
#include "CardStats.h"
#include "CardFactories.h"
#include "PeriodicSpawnEffect.h"
#include "Board.h"
#include <limits>
#include <memory>

// Hero abilities added in patch 0017 (Heroes 181-188). Every one is single
// use (withHeroAbility(..., 1)), as all Hero abilities since the official
// 4 Aug 2026 notes. The timed ones open CombatEntity's ability window.

// The window a timed ability opens on its caster (CombatEntity's window*
// fields; endAbilityWindow resets them).
struct HeroAbilityWindow {
    int ticks = 0;
    bool rooted = false;
    bool suppressAttack = false;
    float damageTakenMultiplier = 1.0f;
    float cooldownMultiplier = 1.0f;
    float moveMultiplier = 1.0f;
    float crownTowerMultiplier = 1.0f;
    int minHp = 0;
};

// Opens `window` on the caster and, if given, runs `periodic` on the
// self-periodic hook for the window's length: first after firstTicks, then
// every intervalTicks, at most fires times (-1 = until the window ends).
class HeroWindowAbilityEffect : public IAbilityEffect {
    HeroAbilityWindow window;
    std::shared_ptr<ISelfPeriodicEffect> periodic;
    int firstTicks, intervalTicks, fires;

public:
    explicit HeroWindowAbilityEffect(HeroAbilityWindow window,
            std::shared_ptr<ISelfPeriodicEffect> periodic = nullptr,
            int firstTicks = 1, int intervalTicks = 1, int fires = -1)
        : window(window), periodic(std::move(periodic)), firstTicks(firstTicks),
          intervalTicks(intervalTicks), fires(fires) {}

    const HeroAbilityWindow& getWindow() const { return window; }

    void apply(Board&, CombatEntity& self) const override {
        self.abilityWindowTicksRemaining = window.ticks;
        self.windowRooted = window.rooted;
        self.windowSuppressAttack = window.suppressAttack;
        self.windowDamageTakenMultiplier = window.damageTakenMultiplier;
        self.windowCooldownMultiplier = window.cooldownMultiplier;
        self.windowMoveMultiplier = window.moveMultiplier;
        self.windowCrownTowerMultiplier = window.crownTowerMultiplier;
        self.windowMinHp = window.minHp;
        if (periodic) {
            self.selfPeriodicEffect = periodic;
            self.selfPeriodicIntervalTicks = intervalTicks;
            self.selfPeriodicTicksUntilNext = firstTicks;
            self.selfPeriodicFiresLeft = fires;
            self.selfPeriodicState = -1;
            self.selfPeriodicEndsWithWindow = true;
        }
    }
};

namespace hero_detail {

// A one-hit AreaSpell burst at `at` from `self` (detonates on its next update).
inline void burst(Board& board, const CombatEntity& self, const Vector2D& at, float radius, int damage,
        float towerMultiplier, std::shared_ptr<IPeriodicEffect> spawnOnDetonate = nullptr, int delayTicks = 0,
        bool groundOnly = false) {
    auto spell = std::make_shared<AreaSpell>(board.allocateId(), at.x, at.y, self.team,
        std::max(radius, 0.01f), damage, delayTicks, '*', nullptr, groundOnly, 1, 0, false, 1.0f, 0, 0.0f,
        std::move(spawnOnDetonate), false, 0, false, 0, 0, 0, towerMultiplier);
    spell->name = self.name;
    spell->cardId = self.cardId;
    board.addEntity(spell);
}

struct EnemyFilter {
    bool troops = true, buildings = true, towers = true, ground = true, air = true;
};

inline bool passes(const Entity& e, const EnemyFilter& f) {
    if (e.isTower()) return f.towers;
    if (e.isBuilding()) return f.buildings && f.ground;
    if (!f.troops) return false;
    return e.isFlying ? f.air : f.ground;
}

// Enemies of `self` within `range`, nearest first, at most `count`.
inline std::vector<std::shared_ptr<Entity>> nearestEnemies(Board& board, const CombatEntity& self,
        const Vector2D& from, float range, int count, const EnemyFilter& f) {
    std::vector<std::pair<float, std::shared_ptr<Entity>>> found;
    for (const auto& e : board.getEntities()) {
        if (e->team == self.team || e->team < 0 || !e->isAlive() || !e->isTargetable()) continue;
        if (!passes(*e, f)) continue;
        const float d = from.distanceTo(e->position);
        if (d <= range) found.emplace_back(d, e);
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::shared_ptr<Entity>> out;
    for (size_t i = 0; i < found.size() && static_cast<int>(i) < count; ++i) out.push_back(found[i].second);
    return out;
}

inline void hit(Board& board, const CombatEntity& self, Entity& target, int damage) {
    target.takeDamage(damage);
    board.statsEvents.notifyDamageDealt(
        { self.id, self.team, self.cardId, target.id, target.cardId, target.team, damage, board.currentTick, target.isTower() });
}

}  // namespace hero_detail

// Hero Valkyrie's "Whirlwind" pulses: every 0.25 s (alternating 2 and 3
// ticks: 14 pulses in 3.5 s) an area hit around her. Runs every tick on the
// hook; per-body tick count in selfPeriodicState.
class ValkyrieWhirlwindPulse : public ISelfPeriodicEffect {
    float radius;
    int damage;
    float towerMultiplier;

public:
    ValkyrieWhirlwindPulse(float radius, int damage, float towerMultiplier)
        : radius(radius), damage(damage), towerMultiplier(towerMultiplier) {}

    bool apply(Board& board, CombatEntity& self) const override {
        if (self.selfPeriodicState < 0) self.selfPeriodicState = 0;
        const int k = self.selfPeriodicState++;
        if ((k * 2) % 5 < 2) hero_detail::burst(board, self, self.position, radius, damage, towerMultiplier, nullptr, 0, true);
        return true;
    }
};

// Hero Bowler's "Stone Swish" shot: a lob at the nearest ground enemy
// (troop, building or Crown Tower) within range. No target: retry next tick.
class BowlerLobShot : public ISelfPeriodicEffect {
    float range, radius;
    int damage;
    float towerMultiplier;

public:
    BowlerLobShot(float range, float radius, int damage, float towerMultiplier)
        : range(range), radius(radius), damage(damage), towerMultiplier(towerMultiplier) {}

    bool apply(Board& board, CombatEntity& self) const override {
        hero_detail::EnemyFilter f;
        f.air = false;
        auto t = hero_detail::nearestEnemies(board, self, self.position, range, 1, f);
        if (t.empty()) return false;
        hero_detail::burst(board, self, t[0]->position, radius, damage, towerMultiplier, nullptr, 0, true);
        return true;
    }
};

// Hero Electro Wizard's "Surging Strikes": the `count` nearest enemies within
// range take `damage` and are stunned for stunTicks.
class HeroElectroWizardStrikesEffect : public IAbilityEffect {
    float range;
    int count, damage, stunTicks;

public:
    HeroElectroWizardStrikesEffect(float range, int count, int damage, int stunTicks)
        : range(range), count(count), damage(damage), stunTicks(stunTicks) {}

    void apply(Board& board, CombatEntity& self) const override {
        for (const auto& e : hero_detail::nearestEnemies(board, self, self.position, range, count, {})) {
            hero_detail::hit(board, self, *e, damage);
            if (auto c = std::dynamic_pointer_cast<CombatEntity>(e)) c->applyFreeze(stunTicks, 0.0f);
        }
    }
};

// Hero Ice Wizard's "Frosty Fella": a snowman at the nearest enemy troop
// within range (else at the Ice Wizard); every enemy troop within radius of
// it takes `damage` and is frozen for freezeTicks.
class HeroIceWizardSnowmanEffect : public IAbilityEffect {
    CardStats snowman;
    float range, radius;
    int damage, freezeTicks;

public:
    HeroIceWizardSnowmanEffect(CardStats snowman, float range, float radius, int damage, int freezeTicks)
        : snowman(std::move(snowman)), range(range), radius(radius), damage(damage), freezeTicks(freezeTicks) {}

    void apply(Board& board, CombatEntity& self) const override {
        hero_detail::EnemyFilter troopsOnly;
        troopsOnly.buildings = troopsOnly.towers = false;
        auto t = hero_detail::nearestEnemies(board, self, self.position, range, 1, troopsOnly);
        const Vector2D at = t.empty() ? self.position : t[0]->position;
        for (const auto& e : hero_detail::nearestEnemies(board, self, at, radius,
                 std::numeric_limits<int>::max(), troopsOnly)) {
            hero_detail::hit(board, self, *e, damage);
            if (auto c = std::dynamic_pointer_cast<CombatEntity>(e)) c->applyFreeze(freezeTicks, 0.0f);
        }
        CardFactories::spawn(snowman, at.x, at.y, self.team, board);
    }
};

// Hero Balloon's "Coffin Cadets": a Skeletrooper drops on the nearest enemy
// ground unit (troop or building) within range, else under the Balloon:
// landing damage in a radius after delayTicks, then the trooper fights.
class HeroBalloonCadetEffect : public IAbilityEffect {
    CardStats trooper;
    float range, landingRadius;
    int landingDamage, delayTicks;
    float landingTowerMultiplier;

public:
    HeroBalloonCadetEffect(CardStats trooper, float range, float landingRadius, int landingDamage,
            float landingTowerMultiplier, int delayTicks)
        : trooper(std::move(trooper)), range(range), landingRadius(landingRadius), landingDamage(landingDamage),
          delayTicks(delayTicks), landingTowerMultiplier(landingTowerMultiplier) {}

    void apply(Board& board, CombatEntity& self) const override {
        hero_detail::EnemyFilter ground;
        ground.air = false;
        ground.towers = false;
        auto t = hero_detail::nearestEnemies(board, self, self.position, range, 1, ground);
        const Vector2D at = t.empty() ? self.position : t[0]->position;
        hero_detail::burst(board, self, at, landingRadius, landingDamage, landingTowerMultiplier,
            std::make_shared<PeriodicSpawnEffect>(trooper), delayTicks, true);
    }
};

// Hero Dark Prince's "Destructive Dismount": the Rhino lands under him after
// delayTicks with spawn damage (none to Crown Towers since 1 Jun 2026) and
// heads for buildings; the Prince loses his charge for good.
class HeroDarkPrinceDismountEffect : public IAbilityEffect {
    CardStats rhino;
    int delayTicks, landingDamage;
    float landingRadius;

public:
    HeroDarkPrinceDismountEffect(CardStats rhino, int delayTicks, int landingDamage, float landingRadius)
        : rhino(std::move(rhino)), delayTicks(delayTicks), landingDamage(landingDamage), landingRadius(landingRadius) {}

    void apply(Board& board, CombatEntity& self) const override {
        self.chargeThreshold = 0.0f;
        self.chargeProgress = 0.0f;
        hero_detail::burst(board, self, self.position, landingRadius, landingDamage, 0.0f,
            std::make_shared<PeriodicSpawnEffect>(rhino), delayTicks, true);
    }
};
