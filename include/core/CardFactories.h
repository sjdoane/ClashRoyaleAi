#pragma once
#include "CardStats.h"
#include "LoadTimes.h"
#include "SightClips.h"
#include "ProjectileSpeeds.h"
#include "Pushback.h"
#include "Board.h"
#include "MeleeTroop.h"
#include "RangedTroop.h"
#include "BuildingTargeter.h"
#include "RangedBuildingTargeter.h"
#include "Building.h"
#include "AreaSpell.h"
#include "ArenaLayout.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

// One factory per Archetype (CardStats.h). Nothing here is specific to a card.
namespace CardFactories {

inline void applyOnHit(const std::shared_ptr<CombatEntity>& entity, const CardStats& stats) {
    if (stats.onHit) {
        entity->addOnHitEffect(stats.onHit);
    }
}

// Copies a card's per-entity configuration onto each entity it produces.
inline void applyCardMetadata(const std::shared_ptr<CombatEntity>& entity, const CardStats& stats) {
    entity->name = stats.name;
    entity->cardId = stats.id;
    // Deploy time (CardStats.h: DEPLOY_TIME_TICKS unless the card publishes
    // its own). Set here because every troop and building passes through;
    // spells and deploy effects do not, since a spell has its own delay.
    entity->deployTicksRemaining = stats.deployTicks;
    // First-hit wind-up: hit speed minus the published load time (LoadTimes.h),
    // or a first-hit time given outright (CardStats::withLoadTicks).
    entity->loadTicks = stats.loadTicksOverride >= 0
        ? stats.loadTicksOverride
        : LoadTimes::firstHitTicks(stats.name, stats.attackCooldown);
    entity->isFlying = stats.isFlying;
    entity->targetsAir = stats.targetsAir;
    entity->targetsTroopsOnly = stats.targetsTroopsOnly;
    entity->deathEffect = stats.deathEffect;
    entity->rampMidTick = stats.rampMidTick;
    entity->rampFullTick = stats.rampFullTick;
    entity->rampStartFraction = stats.rampStartFraction;
    entity->rampMidFraction = stats.rampMidFraction;
    entity->rampStage4Tick = stats.rampStage4Tick;
    entity->rampStage4Fraction = stats.rampStage4Fraction;
    entity->rampGracePeriodTicks = stats.rampGracePeriodTicks;
    entity->maxSplitTargets = stats.maxSplitTargets;
    entity->splitTargetsFullDamage = stats.splitTargetsFullDamage;
    entity->splashRadius = stats.splashRadius;
    entity->shieldHp = stats.shieldHp;
    entity->chargeThreshold = stats.chargeThreshold;
    entity->chargeMultiplier = stats.chargeMultiplier;
    entity->chargeSpeedMultiplier = stats.chargeSpeedMultiplier;
    entity->chargeIsSticky = stats.chargeIsSticky;
    entity->parryIntervalTicks = stats.parryIntervalTicks;
    entity->hookRange = stats.hookRange;
    entity->startsInvisible = stats.startsInvisible;
    entity->revealTicksAfterAttack = stats.revealTicksAfterAttack;
    entity->periodicEffect = stats.periodicEffect;
    entity->periodicIntervalTicks = stats.periodicIntervalTicks;
    entity->periodicTicksUntilNext = stats.periodicIntervalTicks; // first fire after one full interval
    entity->periodicHotIntervalTicks = stats.periodicHotIntervalTicks;
    entity->selfPeriodicEffect = stats.selfPeriodicEffect;
    entity->selfPeriodicIntervalTicks = stats.selfPeriodicIntervalTicks;
    entity->selfPeriodicTicksUntilNext = stats.selfPeriodicFirstTicks;
    entity->crownTowerHitMultiplier = stats.crownTowerHitMultiplier;
    entity->periodicSideOffset = stats.periodicSideOffset;
    entity->auraRadius = stats.auraRadius;
    entity->auraMaxTargets = stats.auraMaxTargets;
    entity->auraEveryNAttacks = stats.auraEveryNAttacks;
    entity->auraBuffMultiplier = stats.auraBuffMultiplier;
    entity->auraBuffDurationTicks = stats.auraBuffDurationTicks;
    entity->healAllyAmount = stats.healAllyAmount;
    entity->dieAfterFirstHit = stats.dieAfterFirstHit;
    entity->chargeGrantsInvulnerability = stats.chargeGrantsInvulnerability;
    entity->resetCooldownOnFreeze = stats.resetCooldownOnFreeze;
    entity->recoilDistance = stats.recoilDistance;
    entity->minAttackRange = stats.minAttackRange;
    entity->sightRange = stats.sightRange;
    // Published sight clip of the building-targeters (SightClips.h).
    if (const SightClips::Row* clipRow = SightClips::find(stats.name)) {
        entity->sightClip = clipRow->clip;
        entity->sightClipSide = clipRow->clipSide;
    }
    // Published speed of the card's shot (ProjectileSpeeds.h, patch 0049).
    entity->shotTilesPerTick = ProjectileSpeeds::tilesPerTick(stats.name);
    entity->pushbackImmune = Pushback::ignores(stats.name);
    entity->transformAtHpFraction = stats.transformAtHpFraction;
    entity->transformCheckMaxHp = stats.hp;
    entity->transformLifetimeTicks = stats.transformLifetimeTicks;
    entity->transformBecomesStationary = stats.transformBecomesStationary;
    entity->transformKillsSelf = stats.transformKillsSelf;
    entity->transformDeathEffect = stats.transformDeathEffect;
    entity->jumpMinRange = stats.jumpMinRange;
    entity->jumpMaxRange = stats.jumpMaxRange;
    entity->jumpDamageMultiplier = stats.jumpDamageMultiplier;
    entity->jumpSplashRadius = stats.jumpSplashRadius;
    entity->lineSplash = stats.lineSplash;
    entity->lineSplashRange = stats.lineSplashRange;
    entity->rangeFalloff = stats.rangeFalloff;
    entity->rangeFalloffMinFraction = stats.rangeFalloffMinFraction;
    entity->rangeBandMinDist = stats.rangeBandMinDist;
    entity->rangeBandMaxDist = stats.rangeBandMaxDist;
    entity->rangeBandDamageMultiplier = stats.rangeBandDamageMultiplier;
    entity->isChampion = stats.isChampion;
    entity->isHero = stats.isHero;
    entity->abilityElixirCost = stats.abilityElixirCost;
    entity->abilityCooldownTicks = stats.abilityCooldownTicks;
    entity->abilityEffect = stats.abilityEffect;
    entity->abilityUsesRemaining = stats.abilityUsesLimit;
    entity->abilitySlotCardId = stats.abilitySlotCardId;
    // A post-spawn ability lockout (Hero Mega Minion) seeds the cooldown
    // directly.
    if (stats.initialAbilityCooldownTicks > 0) entity->abilityCooldownRemaining = stats.initialAbilityCooldownTicks;
    entity->soulCollectionRadius = stats.soulCollectionRadius;
    entity->maxSouls = stats.maxSouls;
    entity->hitSpeedRampMidTick = stats.hitSpeedRampMidTick;
    entity->hitSpeedRampFullTick = stats.hitSpeedRampFullTick;
    entity->hitSpeedRampMidFraction = stats.hitSpeedRampMidFraction;
    entity->hitSpeedRampFullFraction = stats.hitSpeedRampFullFraction;
    entity->burstEveryNAttacks = stats.burstEveryNAttacks;
    entity->magazineSize = stats.magazineSize;
    entity->magazineReloadTicks = stats.magazineReloadTicks;
    entity->burstDamageMultiplier = stats.burstDamageMultiplier;
    entity->onDamageTakenEffect = stats.onDamageTaken;
    entity->healOnHitAmount = stats.healOnHitAmount;
    entity->healOnHitMaxHp = stats.healOnHitMaxHp;
    entity->healOnHitSecondPulseDelayTicks = stats.healOnHitSecondPulseDelayTicks;
    entity->onHitSpawnEffect = stats.onHitSpawnEffect;
    entity->splashAppliesOnHit = stats.splashAppliesOnHit;
    entity->onHitPullRadius = stats.onHitPullRadius;
    entity->onHitPullDistance = stats.onHitPullDistance;
    entity->onHitPullDamage = stats.onHitPullDamage;
    entity->selfHasteDurationTicks = stats.selfHasteDurationTicks;
    entity->selfHasteCooldownMultiplier = stats.selfHasteCooldownMultiplier;
    // Runtime state (ability cooldown, souls, invisibility, hit-speed
    // multiplier) is not copied from stats.
    applyOnHit(entity, stats);
    if (stats.initialCooldownTicks > 0) entity->seedCooldown(stats.initialCooldownTicks);
    // Published LoadFirstHit (LoadTimes.h): the unit lands unloaded, so its
    // first hit is a whole hit speed after its deploy time at the earliest.
    // Walking loads it as after any hit. CombatEntity::update starts the
    // count when the deploy time ends; the seed covers a unit without one.
    entity->landsUnloaded = LoadTimes::landsUnloaded(stats.name);
    if (entity->landsUnloaded) entity->seedCooldown(stats.attackCooldown);
    if (stats.passiveDamageReduction < 1.0f) entity->applyCurse(stats.passiveDamageReduction, 999999);
}

// Flying implies ignoring the river; ignoresRiver alone also covers ground
// units that jump it (Hog Rider).
inline bool shouldIgnoreRiver(const CardStats& stats) {
    return stats.ignoresRiver || stats.isFlying;
}

// The footprint isValidPlacement keeps clear of existing buildings: Building's
// collision radius for a building, the implicit troop radius otherwise. The
// real game forbids placing on a building outright.
inline float placementRadius(Archetype archetype) {
    return archetype == Archetype::DefensiveBuilding
        ? Building::COLLISION_RADIUS
        : Entity::IMPLICIT_TROOP_RADIUS;
}

// A card's one-time deploy burst (e.g. Electro Wizard's zap) at the placement
// point, once per card rather than per squad member. Most cards set none.
inline void spawnDeployEffect(const CardStats& stats, float x, float y, int team, Board& board) {
    if (stats.spawnEffectRadius <= 0.0f) return;
    // Card-stats audit (patch 0003): one impact per offset (Evolved Cannon's
    // barrage), with optional knockback and reduced Crown Tower damage.
    std::vector<Vector2D> offsets = stats.spawnEffectOffsets;
    if (offsets.empty()) offsets.push_back(Vector2D{ 0.0f, 0.0f });
    const float forward = (team == 0) ? 1.0f : -1.0f;
    for (const auto& o : offsets) {
        const float ex = std::clamp(x + o.x, 0.0f, static_cast<float>(ArenaLayout::WIDTH - 1));
        const float ey = std::clamp(y + forward * o.y, 0.0f, static_cast<float>(ArenaLayout::HEIGHT - 1));
        auto effect = std::make_shared<AreaSpell>(
            board.allocateId(), ex, ey, team, stats.spawnEffectRadius, stats.spawnEffectDamage,
            0, stats.symbol, stats.spawnEffectOnHit, false, 1, 0, false, 1.0f, 0, stats.spawnEffectKnockback);
        effect->name = stats.name;
        effect->cardId = stats.id;
        effect->crownTowerDamage = stats.spawnEffectCrownTowerDamage;
        board.addEntity(effect);
    }
}

// A squad's formation is written in its owner's frame: +x to the right as the
// board is stored, +y toward the enemy. Team 1 faces -y, so its formation is
// mirrored in y (patch 0037). Before this the offsets were added unmirrored and
// the two sides fielded front-to-back mirror images of each other: a Goblin
// Barrel's third goblin landed behind a team-1 tower but on the river side of a
// team-0 tower, where the other Princess Tower could reach it.
inline Vector2D formationSlot(float x, float y, const Vector2D& offset, int team) {
    const float forward = (team == 0) ? 1.0f : -1.0f;
    return Vector2D{ x + offset.x, y + forward * offset.y };
}

inline void spawnMeleeSquad(const CardStats& stats, float x, float y, int team, Board& board) {
    for (const auto& offset : stats.spawnOffsets) {
        const Vector2D slot = formationSlot(x, y, offset, team);
        auto troop = std::make_shared<MeleeTroop>(
            board.allocateId(), slot.x, slot.y, stats.hp, team,
            stats.speed, stats.attackRange, stats.damage, stats.attackCooldown, stats.symbol);
        if (shouldIgnoreRiver(stats)) troop->setIgnoresRiver(true);
        applyCardMetadata(troop, stats);
        board.addEntity(troop);
    }
    spawnDeployEffect(stats, x, y, team, board);
}

inline void spawnRangedSquad(const CardStats& stats, float x, float y, int team, Board& board) {
    for (const auto& offset : stats.spawnOffsets) {
        const Vector2D slot = formationSlot(x, y, offset, team);
        auto troop = std::make_shared<RangedTroop>(
            board.allocateId(), slot.x, slot.y, stats.hp, team,
            stats.speed, stats.attackRange, stats.damage, stats.attackCooldown, stats.symbol);
        if (shouldIgnoreRiver(stats)) troop->setIgnoresRiver(true);
        troop->boomerang = stats.boomerang;
        troop->boomerangReturnDelayTicks = stats.boomerangReturnDelayTicks;
        applyCardMetadata(troop, stats);
        board.addEntity(troop);
    }
    spawnDeployEffect(stats, x, y, team, board);
}

inline void spawnMeleeBuildingTargeter(const CardStats& stats, float x, float y, int team, Board& board) {
    // Loops over spawnOffsets for multi-unit children such as Golemites.
    for (const auto& offset : stats.spawnOffsets) {
        const Vector2D slot = formationSlot(x, y, offset, team);
        auto troop = std::make_shared<BuildingTargeter>(
            board.allocateId(), slot.x, slot.y, stats.hp, team,
            stats.speed, stats.attackRange, stats.damage, stats.attackCooldown, stats.symbol);
        if (shouldIgnoreRiver(stats)) troop->setIgnoresRiver(true);
        applyCardMetadata(troop, stats);
        board.addEntity(troop);
    }
    spawnDeployEffect(stats, x, y, team, board);
}

inline void spawnRangedBuildingTargeter(const CardStats& stats, float x, float y, int team, Board& board) {
    for (const auto& offset : stats.spawnOffsets) {
        const Vector2D slot = formationSlot(x, y, offset, team);
        auto troop = std::make_shared<RangedBuildingTargeter>(
            board.allocateId(), slot.x, slot.y, stats.hp, team,
            stats.speed, stats.attackRange, stats.damage, stats.attackCooldown, stats.symbol);
        if (shouldIgnoreRiver(stats)) troop->setIgnoresRiver(true);
        applyCardMetadata(troop, stats);
        board.addEntity(troop);
    }
    spawnDeployEffect(stats, x, y, team, board);
}

inline void spawnDefensiveBuilding(const CardStats& stats, float x, float y, int team, Board& board) {
    auto building = std::make_shared<Building>(
        board.allocateId(), x, y, stats.hp, team, stats.symbol,
        stats.attackRange, stats.damage, stats.attackCooldown, stats.lifetimeTicks);
    applyCardMetadata(building, stats);
    board.addEntity(building);
    // Card-stats audit (patch 0003): buildings never fired their deploy burst,
    // so Evolved Cannon's barrage was silently dropped.
    spawnDeployEffect(stats, x, y, team, board);
}

// Thrown spells: published projectile speed N flies N / 48 real tiles per
// second, not the N / 60 of the usual reading ("Medium 60 = 1 tile/s").
// Measured frame by frame on spells leaving a King Tower (launch = first frame
// the projectile shows on the tower, landing = first frame of the burst):
//   Goblin Barrel (400): 8 flights onto a Princess Tower, 23.2 tiles, 2.72 to
//     2.81 s (mean 2.76; /60 predicts 3.48), and one onto the King Tower,
//     26 tiles, 3.28 s.
//   Fireball (600): 7 flights of 6.9 to 27 tiles in 0.57 to 2.02 s; a line
//     through the origin fits at 12.3 tiles/s (/60 predicts 10).
//   Rocket (350): 15.3 tiles in 2.06 s and ~1.97 s (/60 predicts 2.62).
//   Giant Snowball (800): 23.2 tiles in 1.30 s (/60 predicts 1.74).
// Published / measured tiles-per-second is 47.6, 48.8, 46.5 and 45; 48 is
// within the measurement error of each. The Log's roll is not part of this:
// it does follow /60 (CardRegistry.h, The Log).
inline constexpr float THROWN_SPEED_PER_TILE_PER_SECOND = 48.0f;
inline constexpr float SPELL_TICKS_PER_SECOND = 10.0f;

// Ticks from the cast to the first application of a spell cast by `team` at
// (x, y). A thrown spell (CardStats::spellProjectileSpeed) leaves the centre of
// the caster's King Tower -- also when that tower is gone -- and its flight is
// the straight-line distance in real-arena tiles (ArenaLayout::realRow) over
// its speed, rounded to the nearest tick and never under one. Every other
// spell keeps its fixed spellDelayTicks. A pure function of the card and the
// two points: no board state, no randomness.
inline int thrownSpellFlightTicks(const CardStats& stats, float x, float y, int team) {
    if (stats.spellProjectileSpeed <= 0) return stats.spellDelayTicks;
    const float dx = x - ArenaLayout::CENTER_X;
    const float dy = ArenaLayout::realRow(y) - ArenaLayout::realRow(ArenaLayout::kingY(team));
    const float tiles = std::sqrt(dx * dx + dy * dy);
    const float tilesPerTick = static_cast<float>(stats.spellProjectileSpeed)
        / (THROWN_SPEED_PER_TILE_PER_SECOND * SPELL_TICKS_PER_SECOND);
    return std::max(1, static_cast<int>(std::lround(tiles / tilesPerTick)));
}

inline void spawnSpell(const CardStats& stats, float x, float y, int team, Board& board) {
    auto spell = std::make_shared<AreaSpell>(
        board.allocateId(), x, y, team, stats.spellRadius, stats.damage,
        thrownSpellFlightTicks(stats, x, y, team), stats.symbol,
        stats.spellOnHit, stats.spellGroundOnly, stats.spellRemainingHits, stats.spellTickInterval,
        stats.spellBuffsAllies, stats.spellBuffMultiplier, stats.spellBuffDurationTicks, stats.spellKnockback,
        stats.spellSpawnEffect, stats.spellClonesAllies, stats.spellTargetTopHpCount,
        stats.spellTieredDamage, stats.spellTierSingleDamage, stats.spellTierFewDamage, stats.spellTierManyDamage);
    spell->name = stats.name;
    spell->cardId = stats.id;
    // Set after construction for the reason configureRoll gives below: the
    // constructor is shared with call sites that have no card behind them.
    spell->crownTowerDamage = stats.spellCrownTowerDamage;
    spell->spawnRingRadius = stats.spellSpawnRingRadius;
    spell->thrown = stats.spellProjectileSpeed > 0;
    // Rolling spells (The Log, Barbarian Barrel) are configured after
    // construction: configureRoll latches the roll origin from the spawn
    // position, so it must run before the first update.
    if (stats.spellRollRange > 0.0f) {
        spell->configureRoll(stats.spellRollRange, stats.spellRollWidth,
                             stats.spellRollSpeed, stats.spellRollKnockback);
        spell->rollBodyDepth = stats.spellRollBodyDepth;
    }
    board.addEntity(spell);
}

inline void spawn(const CardStats& stats, float x, float y, int team, Board& board) {
    const size_t firstPrimary = board.pendingEntityCount();
    switch (stats.archetype) {
        case Archetype::MeleeSquad:             spawnMeleeSquad(stats, x, y, team, board); break;
        case Archetype::RangedSquad:             spawnRangedSquad(stats, x, y, team, board); break;
        case Archetype::MeleeBuildingTargeter:   spawnMeleeBuildingTargeter(stats, x, y, team, board); break;
        case Archetype::RangedBuildingTargeter:  spawnRangedBuildingTargeter(stats, x, y, team, board); break;
        case Archetype::DefensiveBuilding:       spawnDefensiveBuilding(stats, x, y, team, board); break;
        case Archetype::Spell:                   spawnSpell(stats, x, y, team, board); break;
        default: throw std::logic_error("CardFactories::spawn: unhandled archetype");
    }
    // Compound cards (Goblin Giant, Ram Rider, Goblin Machine, Goblin Gang,
    // Rascals): the second, independently targeting unit spawns at the same
    // point.
    if (stats.secondaryUnit) {
        const size_t firstSecondary = board.pendingEntityCount();
        spawn(*stats.secondaryUnit, x, y, team, board);
        // Attached secondaries (patch 0046) ride the card's first unit.
        if (stats.secondaryAttached) {
            std::shared_ptr<Entity> carrier;
            for (size_t i = firstPrimary; i < firstSecondary && !carrier; ++i) {
                if (std::dynamic_pointer_cast<Troop>(board.getPendingEntity(i))) carrier = board.getPendingEntity(i);
            }
            for (size_t i = firstSecondary; carrier && i < board.pendingEntityCount(); ++i) {
                if (auto rider = std::dynamic_pointer_cast<Troop>(board.getPendingEntity(i))) {
                    rider->rideOn(*carrier, stats.secondaryDropsOnCarrierDeath);
                }
            }
        }
    }
}

} // namespace CardFactories
