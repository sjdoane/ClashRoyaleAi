#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "CardFactories.h"
#include "CardRegistry.h"
#include "ArenaLayout.h"
#include "CardStats.h"
#include "MeleeTroop.h"
#include "RangedTroop.h"
#include "BuildingTargeter.h"
#include "RangedBuildingTargeter.h"
#include "Building.h"
#include "AreaSpell.h"
#include "FreezeOnHit.h"
#include "SpawnOnDeath.h"
#include "EnemyElixirGrantOnDeath.h"
#include "SpawnOnDeathForEnemyTeam.h"
#include "ProximityGatedPeriodicSpawnEffect.h"
#include "CursedHogOnHit.h"

// These hand-build a CardStats and call the factory functions directly,
// exercising the mechanism in isolation from any single registered card's
// specific data (see test_card_registry.cpp for the end-to-end wiring).

namespace {
    CardStats makeTroopStats() {
        CardStats stats;
        stats.name = "TestFlier";
        stats.hp = 100;
        stats.speed = 1.0f;
        stats.attackRange = 1.0f;
        stats.damage = 10;
        stats.attackCooldown = 10;
        stats.symbol = 'K';
        return stats;
    }
}

TEST_CASE("applyCardMetadata copies isFlying/targetsAir onto the spawned entity", "[card_factories][flying]") {
    Board board;
    CardStats stats = makeTroopStats();
    stats.isFlying = true;
    stats.targetsAir = true;

    SECTION("spawnMeleeSquad") {
        CardFactories::spawnMeleeSquad(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        auto troop = std::dynamic_pointer_cast<MeleeTroop>(board.getEntities().back());
        REQUIRE(troop != nullptr);
        REQUIRE(troop->isFlying);
        REQUIRE(troop->targetsAir);
    }

    SECTION("spawnRangedSquad") {
        CardFactories::spawnRangedSquad(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        auto troop = std::dynamic_pointer_cast<RangedTroop>(board.getEntities().back());
        REQUIRE(troop != nullptr);
        REQUIRE(troop->isFlying);
        REQUIRE(troop->targetsAir);
    }

    SECTION("spawnMeleeBuildingTargeter") {
        CardFactories::spawnMeleeBuildingTargeter(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        auto troop = std::dynamic_pointer_cast<BuildingTargeter>(board.getEntities().back());
        REQUIRE(troop != nullptr);
        REQUIRE(troop->isFlying);
        REQUIRE(troop->targetsAir);
    }

    SECTION("spawnRangedBuildingTargeter") {
        CardFactories::spawnRangedBuildingTargeter(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        auto troop = std::dynamic_pointer_cast<RangedBuildingTargeter>(board.getEntities().back());
        REQUIRE(troop != nullptr);
        REQUIRE(troop->isFlying);
        REQUIRE(troop->targetsAir);
    }

    SECTION("spawnDefensiveBuilding") {
        CardFactories::spawnDefensiveBuilding(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        auto building = std::dynamic_pointer_cast<Building>(board.getEntities().back());
        REQUIRE(building != nullptr);
        REQUIRE(building->isFlying);
        REQUIRE(building->targetsAir);
    }
}

TEST_CASE("applyCardMetadata copies cardId (stats.id) onto the spawned entity", "[card_factories][cardId]") {
    Board board;
    CardStats stats = makeTroopStats();
    stats.id = 42;

    SECTION("spawnMeleeSquad") {
        CardFactories::spawnMeleeSquad(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        REQUIRE(board.getEntities().back()->cardId == 42);
    }

    SECTION("spawnRangedSquad") {
        CardFactories::spawnRangedSquad(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        REQUIRE(board.getEntities().back()->cardId == 42);
    }

    SECTION("spawnMeleeBuildingTargeter") {
        CardFactories::spawnMeleeBuildingTargeter(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        REQUIRE(board.getEntities().back()->cardId == 42);
    }

    SECTION("spawnRangedBuildingTargeter") {
        CardFactories::spawnRangedBuildingTargeter(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        REQUIRE(board.getEntities().back()->cardId == 42);
    }

    SECTION("spawnDefensiveBuilding") {
        CardFactories::spawnDefensiveBuilding(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        REQUIRE(board.getEntities().back()->cardId == 42);
    }
}

TEST_CASE("spawnSpell and spawnDeployEffect also set cardId, even though they bypass applyCardMetadata", "[card_factories][cardId]") {
    Board board;

    SECTION("spawnSpell") {
        CardStats stats;
        stats.id = 7;
        stats.name = "TestSpell";
        stats.archetype = Archetype::Spell;
        stats.spellRadius = 3.0f;
        stats.damage = 100;
        stats.spellDelayTicks = 0;
        stats.symbol = '*';

        CardFactories::spawnSpell(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        REQUIRE(board.getEntities().back()->cardId == 7);
    }

    SECTION("spawnDeployEffect (via spawnMeleeSquad's trailing call)") {
        CardStats stats = makeTroopStats();
        stats.id = 35;
        stats.withSpawnEffect(3.0f, 50);

        CardFactories::spawnMeleeSquad(stats, 5.0f, 5.0f, 0, board); // troop + deploy-effect AreaSpell
        board.commitPendingEntities();
        REQUIRE(board.getEntities().back()->cardId == 35); // the deploy-effect entity, spawned last
    }
}

TEST_CASE("Troop-shaped factories derive ignoresRiver from isFlying, even when ignoresRiver itself is false", "[card_factories][flying][river]") {
    Board board;
    CardStats stats = makeTroopStats();
    stats.isFlying = true;
    REQUIRE_FALSE(stats.ignoresRiver);

    SECTION("spawnMeleeSquad") {
        CardFactories::spawnMeleeSquad(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        auto troop = std::dynamic_pointer_cast<MeleeTroop>(board.getEntities().back());
        REQUIRE(troop != nullptr);
        REQUIRE(troop->riverIgnores);
    }

    SECTION("spawnRangedSquad") {
        CardFactories::spawnRangedSquad(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        auto troop = std::dynamic_pointer_cast<RangedTroop>(board.getEntities().back());
        REQUIRE(troop != nullptr);
        REQUIRE(troop->riverIgnores);
    }

    SECTION("spawnMeleeBuildingTargeter") {
        CardFactories::spawnMeleeBuildingTargeter(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        auto troop = std::dynamic_pointer_cast<BuildingTargeter>(board.getEntities().back());
        REQUIRE(troop != nullptr);
        REQUIRE(troop->riverIgnores);
    }

    SECTION("spawnRangedBuildingTargeter") {
        CardFactories::spawnRangedBuildingTargeter(stats, 5.0f, 5.0f, 0, board);
        board.commitPendingEntities();
        auto troop = std::dynamic_pointer_cast<RangedBuildingTargeter>(board.getEntities().back());
        REQUIRE(troop != nullptr);
        REQUIRE(troop->riverIgnores);
    }
}

TEST_CASE("CardFactories::placementRadius matches each archetype's real spawned footprint", "[card_factories][placement]") {
    REQUIRE(CardFactories::placementRadius(Archetype::MeleeSquad) == Catch::Approx(Entity::IMPLICIT_TROOP_RADIUS));
    REQUIRE(CardFactories::placementRadius(Archetype::RangedSquad) == Catch::Approx(Entity::IMPLICIT_TROOP_RADIUS));
    REQUIRE(CardFactories::placementRadius(Archetype::MeleeBuildingTargeter) == Catch::Approx(Entity::IMPLICIT_TROOP_RADIUS));
    REQUIRE(CardFactories::placementRadius(Archetype::RangedBuildingTargeter) == Catch::Approx(Entity::IMPLICIT_TROOP_RADIUS));
    REQUIRE(CardFactories::placementRadius(Archetype::DefensiveBuilding) == Catch::Approx(Building::COLLISION_RADIUS));
}

TEST_CASE("spawnDeployEffect is a no-op when spawnEffectRadius is unset (default)", "[card_factories][spawn_effect]") {
    Board board;
    CardStats stats = makeTroopStats(); // spawnEffectRadius defaults to 0

    CardFactories::spawnMeleeSquad(stats, 5.0f, 5.0f, 0, board);
    board.commitPendingEntities();

    REQUIRE(board.getEntities().size() == 1); // just the troop, no extra deploy-effect entity
}

TEST_CASE("spawnDeployEffect adds exactly one deploy-effect entity per card, regardless of squad size", "[card_factories][spawn_effect]") {
    Board board;
    CardStats stats = makeTroopStats();
    stats.spawnOffsets = { {0.0f, 0.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f} }; // squad of 3
    stats.withSpawnEffect(3.0f, 50);

    CardFactories::spawnMeleeSquad(stats, 5.0f, 5.0f, 0, board);
    board.commitPendingEntities();

    REQUIRE(board.getEntities().size() == 4); // 3 troops + exactly 1 deploy-effect
    auto effect = std::dynamic_pointer_cast<AreaSpell>(board.getEntities().back());
    REQUIRE(effect != nullptr);
}

TEST_CASE("spawnDeployEffect's AreaSpell carries the configured damage and on-hit effect", "[card_factories][spawn_effect]") {
    Board board;
    auto enemy = std::make_shared<StationaryCombatant>(1, 6.0f, 5.0f, 1000, 1, 5.0f, 10, 10); // dist 1.0 from (5,5)
    spawn(board, enemy);

    CardStats stats = makeTroopStats();
    stats.withSpawnEffect(3.0f, 50, std::make_shared<FreezeOnHit>(5, 0.0f));

    CardFactories::spawnMeleeSquad(stats, 5.0f, 5.0f, 0, board);
    board.commitPendingEntities();

    auto effect = std::dynamic_pointer_cast<AreaSpell>(board.getEntities().back());
    REQUIRE(effect != nullptr);
    effect->update(board); // zero delay: detonates immediately

    REQUIRE(enemy->hp == 950);
    REQUIRE(enemy->freezeTicks == 5);
    REQUIRE(enemy->freezeSlow == Catch::Approx(0.0f));
}

TEST_CASE("applyCardMetadata copies deathEffect onto the spawned entity", "[card_factories][death]") {
    Board board;
    CardStats stats = makeTroopStats();
    stats.withDeathEffect(std::make_shared<SpawnOnDeath>(makeTroopStats()));

    CardFactories::spawnMeleeSquad(stats, 5.0f, 5.0f, 0, board);
    board.commitPendingEntities();

    auto troop = std::dynamic_pointer_cast<MeleeTroop>(board.getEntities().back());
    REQUIRE(troop != nullptr);
    REQUIRE(troop->deathEffect != nullptr);
}

TEST_CASE("SpawnOnDeath spawns its child CardStats at the dying entity's position and team", "[card_factories][death]") {
    Board board;
    CardStats childStats = makeTroopStats();
    childStats.name = "Child";
    SpawnOnDeath effect(childStats);

    effect.apply(board, Vector2D{ 7.0f, 8.0f }, 1);
    board.commitPendingEntities();

    REQUIRE(board.getEntities().size() == 1);
    auto child = std::dynamic_pointer_cast<MeleeTroop>(board.getEntities()[0]);
    REQUIRE(child != nullptr);
    REQUIRE(child->name == "Child");
    REQUIRE(child->team == 1);
    REQUIRE(child->position.x == Catch::Approx(7.0f));
    REQUIRE(child->position.y == Catch::Approx(8.0f));
}

TEST_CASE("spawnSpell wires spellGroundOnly through to the spawned AreaSpell", "[card_factories][flying]") {
    Board board;
    auto flyingEnemy = std::make_shared<DummyEntity>(99, 5.0f, 5.5f, 1000, 1);
    flyingEnemy->isFlying = true;
    spawn(board, flyingEnemy);

    CardStats stats;
    stats.name = "TestGroundSpell";
    stats.archetype = Archetype::Spell;
    stats.spellRadius = 3.0f;
    stats.damage = 500;
    stats.spellDelayTicks = 0;
    stats.symbol = '*';
    stats.withGroundOnly();

    CardFactories::spawnSpell(stats, 5.0f, 5.0f, 0, board);
    board.commitPendingEntities();

    auto spell = std::dynamic_pointer_cast<AreaSpell>(board.getEntities().back());
    REQUIRE(spell != nullptr);
    spell->update(board); // zero delay: detonates immediately

    REQUIRE(flyingEnemy->hp == 1000); // untouched: ground-only spell, flying target
}

TEST_CASE("A non-flying card leaves riverIgnores false unless set explicitly", "[card_factories][river]") {
    Board board;
    CardStats stats = makeTroopStats();
    // isFlying stays false, ignoresRiver stays false (default).

    CardFactories::spawnMeleeSquad(stats, 5.0f, 5.0f, 0, board);
    board.commitPendingEntities();
    auto troop = std::dynamic_pointer_cast<MeleeTroop>(board.getEntities().back());
    REQUIRE(troop != nullptr);
    REQUIRE_FALSE(troop->riverIgnores);
}

// ---------------- Elixir Golem split-chain effects ----------------

TEST_CASE("EnemyElixirGrantOnDeath credits the OPPOSING team's pendingElixirGrant", "[card_factories][elixir]") {
    Board board;
    EnemyElixirGrantOnDeath effect(0.5f);

    effect.apply(board, Vector2D{ 0.0f, 0.0f }, 0); // team 0 died

    REQUIRE(board.pendingElixirGrant[1] == Catch::Approx(0.5f)); // team 1 (the opponent) benefits
    REQUIRE(board.pendingElixirGrant[0] == Catch::Approx(0.0f)); // not its own team
}

TEST_CASE("SpawnOnDeathForEnemyTeam spawns its child for the OPPOSING team", "[card_factories][death]") {
    Board board;
    CardStats childStats = makeTroopStats();
    childStats.name = "CursedHogChild";
    SpawnOnDeathForEnemyTeam effect(childStats);

    effect.apply(board, Vector2D{ 3.0f, 4.0f }, 0); // team 0 died
    board.commitPendingEntities();

    REQUIRE(board.getEntities().size() == 1);
    auto child = board.getEntities()[0];
    REQUIRE(child->team == 1); // spawned fighting for team 1, not team 0
    REQUIRE(child->position.x == Catch::Approx(3.0f));
}

TEST_CASE("CursedHogOnHit applies the curse and arms a same-tick death spawn for the enemy team", "[card_factories][curse]") {
    Board board;
    auto target = std::make_shared<StationaryCombatant>(1, 0.0f, 0.0f, 100, 1, 1.0f, 10, 10);

    CardStats hogStats = makeTroopStats();
    hogStats.name = "Cursed Hog";
    CursedHogOnHit effect(1.5f, 60, hogStats);

    effect.apply(target);

    REQUIRE(target->curseDamageTakenMultiplier == Catch::Approx(1.5f));
    REQUIRE(target->curseTicksRemaining == 60);
    REQUIRE(target->deathEffect != nullptr);

    // The armed deathEffect spawns for team 0 (opposite the cursed
    // target's own team 1) when the target actually dies.
    board.currentTick = 0;
    target->deathEffect->apply(board, target->position, target->team);
    board.commitPendingEntities();
    REQUIRE(board.getEntities().size() == 1);
    REQUIRE(board.getEntities()[0]->team == 0);
}

TEST_CASE("CursedHogOnHit composes with an existing deathEffect instead of overwriting it", "[card_factories][curse]") {
    Board board;
    auto target = std::make_shared<StationaryCombatant>(1, 0.0f, 0.0f, 100, 1, 1.0f, 10, 10);
    auto original = std::make_shared<RecordingDeathEffect>();
    target->deathEffect = original;

    CardStats hogStats = makeTroopStats();
    CursedHogOnHit effect(1.5f, 60, hogStats);
    effect.apply(target);

    target->deathEffect->apply(board, target->position, target->team);
    board.commitPendingEntities();

    REQUIRE(original->applied); // the unit's own original death behavior still ran
    REQUIRE(board.getEntities().size() == 1); // plus the Cursed Hog spawn
}

// ---------------- proximity-gated periodic spawn (Goblin Hut) ----------------

TEST_CASE("ProximityGatedPeriodicSpawnEffect spawns when an enemy is within range", "[card_factories][periodic]") {
    Board board;
    auto enemy = std::make_shared<DummyEntity>(1, 1.0f, 0.0f, 100, 1); // dist 1.0
    spawn(board, enemy);

    ProximityGatedPeriodicSpawnEffect effect(makeTroopStats(), 6.0f);
    effect.apply(board, Vector2D{ 0.0f, 0.0f }, 0);
    board.commitPendingEntities();

    REQUIRE(board.getEntities().size() == 2); // the enemy, plus the spawned child
}

TEST_CASE("ProximityGatedPeriodicSpawnEffect stays quiet with no enemy in range", "[card_factories][periodic]") {
    Board board;
    auto farEnemy = std::make_shared<DummyEntity>(1, 100.0f, 0.0f, 100, 1); // way outside 6.0
    spawn(board, farEnemy);

    ProximityGatedPeriodicSpawnEffect effect(makeTroopStats(), 6.0f);
    effect.apply(board, Vector2D{ 0.0f, 0.0f }, 0);
    board.commitPendingEntities();

    REQUIRE(board.getEntities().size() == 1); // no spawn -- only the original enemy
}

TEST_CASE("ProximityGatedPeriodicSpawnEffect ignores allies when checking for a nearby enemy", "[card_factories][periodic]") {
    Board board;
    auto ally = std::make_shared<DummyEntity>(1, 1.0f, 0.0f, 100, 0); // same team, dist 1.0
    spawn(board, ally);

    ProximityGatedPeriodicSpawnEffect effect(makeTroopStats(), 6.0f);
    effect.apply(board, Vector2D{ 0.0f, 0.0f }, 0);
    board.commitPendingEntities();

    REQUIRE(board.getEntities().size() == 1); // no spawn -- the nearby unit is an ally, not an enemy
}

// --- thrown spells: the flight from the King Tower ---

namespace {
    CardStats makeThrownSpellStats(int publishedSpeed) {
        CardStats stats;
        stats.name = "TestThrownSpell";
        stats.archetype = Archetype::Spell;
        stats.spellRadius = 2.5f;
        stats.damage = 500;
        stats.spellDelayTicks = 10;
        stats.symbol = 'O';
        stats.withThrownFromKingTower(publishedSpeed);
        return stats;
    }
}

TEST_CASE("A spell that is not thrown keeps its fixed delay wherever it is cast", "[card_factories][flight]") {
    CardStats stats = makeThrownSpellStats(0);
    REQUIRE(CardFactories::thrownSpellFlightTicks(stats, 3.0f, 6.0f, 0) == 10);
    REQUIRE(CardFactories::thrownSpellFlightTicks(stats, 14.0f, 27.0f, 0) == 10);
    REQUIRE(CardFactories::thrownSpellFlightTicks(stats, 14.0f, 27.0f, 1) == 10);
}

TEST_CASE("A thrown spell's flight is the real-arena distance from the caster's King Tower over speed / 48", "[card_factories][flight]") {
    // Goblin Barrel, published speed 400 = 8.33 tiles/s. King Tower to an
    // enemy Princess Tower is 23.2 real tiles (5.5 across, 22.5 along): 2.8 s,
    // against 2.76 s measured over 8 flights. Onto the enemy King Tower (26
    // tiles) 3.1 s, against 3.28 s measured once.
    CardStats barrel = makeThrownSpellStats(400);
    REQUIRE(CardFactories::thrownSpellFlightTicks(barrel, ArenaLayout::LEFT_LANE_X, ArenaLayout::princessY(1), 0) == 28);
    REQUIRE(CardFactories::thrownSpellFlightTicks(barrel, ArenaLayout::RIGHT_LANE_X, ArenaLayout::princessY(1), 0) == 28);
    REQUIRE(CardFactories::thrownSpellFlightTicks(barrel, ArenaLayout::CENTER_X, ArenaLayout::kingY(1), 0) == 31);

    // Fireball, published 600 = 12.5 tiles/s: 1.9 s onto that tower, under a
    // second in defence.
    CardStats fireball = makeThrownSpellStats(600);
    REQUIRE(CardFactories::thrownSpellFlightTicks(fireball, ArenaLayout::LEFT_LANE_X, ArenaLayout::princessY(1), 0) == 19);
    REQUIRE(CardFactories::thrownSpellFlightTicks(fireball, ArenaLayout::LEFT_LANE_X, 10.0f, 0) == 7);

    // Never under one tick, even cast on the King Tower itself.
    REQUIRE(CardFactories::thrownSpellFlightTicks(fireball, ArenaLayout::CENTER_X, ArenaLayout::kingY(0), 0) == 1);
}

TEST_CASE("A thrown spell's flight is the same for both teams and never shrinks with distance", "[card_factories][flight]") {
    CardStats stats = makeThrownSpellStats(400);
    int previous = 0;
    for (float y = ArenaLayout::kingY(0); y <= 33.0f; y += 0.5f) {
        const int ticks = CardFactories::thrownSpellFlightTicks(stats, ArenaLayout::CENTER_X, y, 0);
        REQUIRE(ticks >= previous);
        previous = ticks;
        for (float x : { 0.0f, 3.0f, 8.5f, 14.0f, 17.0f }) {
            REQUIRE(CardFactories::thrownSpellFlightTicks(stats, x, y, 0)
                    == CardFactories::thrownSpellFlightTicks(stats, ArenaLayout::mirrorX(x), ArenaLayout::mirrorY(y), 1));
        }
    }
}

TEST_CASE("spawnSpell gives a thrown spell its flight as the fuse and reports it until it lands", "[card_factories][flight]") {
    Board board;
    auto enemy = std::make_shared<DummyEntity>(99, ArenaLayout::LEFT_LANE_X, 27.0f, 1000, 1);
    spawn(board, enemy);

    CardStats stats = makeThrownSpellStats(600);
    const int flight = CardFactories::thrownSpellFlightTicks(stats, ArenaLayout::LEFT_LANE_X, 27.0f, 0);
    REQUIRE(flight == 19);

    CardFactories::spawnSpell(stats, ArenaLayout::LEFT_LANE_X, 27.0f, 0, board);
    board.commitPendingEntities();
    auto spell = std::dynamic_pointer_cast<AreaSpell>(board.getEntities().back());
    REQUIRE(spell != nullptr);
    REQUIRE(spell->thrown);

    // ticksToImpact n: the n-th update from now applies the spell.
    for (int remaining = flight + 1; remaining > 1; --remaining) {
        REQUIRE_FALSE(spell->landed);
        REQUIRE(spell->ticksToImpact() == remaining);
        spell->update(board);
        REQUIRE(enemy->hp == 1000);   // still in the air
    }
    REQUIRE(spell->ticksToImpact() == 1);
    spell->update(board);
    REQUIRE(spell->landed);
    REQUIRE(spell->ticksToImpact() == -1);
    REQUIRE(enemy->hp == 500);

    // A copy made mid-flight (Board::deepCopy uses snapshot) resumes the fuse.
    CardFactories::spawnSpell(stats, ArenaLayout::LEFT_LANE_X, 27.0f, 0, board);
    board.commitPendingEntities();
    auto second = std::dynamic_pointer_cast<AreaSpell>(board.getEntities().back());
    for (int i = 0; i < 5; ++i) second->update(board);
    auto copy = std::dynamic_pointer_cast<AreaSpell>(second->snapshot());
    REQUIRE(copy->thrown);
    REQUIRE(copy->ticksToImpact() == second->ticksToImpact());
}

TEST_CASE("The registry's thrown spells carry their published projectile speeds", "[card_factories][flight][registry]") {
    // Fireball 600, Goblin Barrel 400, Rocket 350, Arrows 1100, Giant
    // Snowball 800 (game data); nothing else is thrown from the King Tower.
    const auto& registry = CardRegistry::getInstance();
    REQUIRE(registry.getCard(7)->stats->spellProjectileSpeed == 600);
    REQUIRE(registry.getCard(109)->stats->spellProjectileSpeed == 400);
    REQUIRE(registry.getCard(30)->stats->spellProjectileSpeed == 350);
    REQUIRE(registry.getCard(3)->stats->spellProjectileSpeed == 1100);
    REQUIRE(registry.getCard(100)->stats->spellProjectileSpeed == 800);
    // Evolved forms fly like their base card.
    REQUIRE(registry.getCard(132)->evolvedStats->spellProjectileSpeed == 400);
    REQUIRE(registry.getCard(158)->evolvedStats->spellProjectileSpeed == 800);
    for (int id : { 29, 31, 32, 33, 101, 103, 106, 107, 110, 111 }) {
        REQUIRE(registry.getCard(id)->stats->spellProjectileSpeed == 0);
    }
}
