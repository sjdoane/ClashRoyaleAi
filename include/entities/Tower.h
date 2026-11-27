#pragma once
#include "P0088.h"
#include "Building.h"
#include "Projectile.h"

class Tower : public Building {
public:
    Tower(int id, float x, float y, int hp, int team,
        float attackRange, int damage, int attackCooldown, char symbol)
        : Building(id, x, y, hp, team, symbol, attackRange, damage, attackCooldown, -1) {
        targetsAir = true; // every tower defends against air
        // Fallback for towers built outside GameManager (tests): sight never
        // below attack range. GameManager sets the sourced values afterwards.
        sightRange = attackRange;
    }

    float getCollisionRadius() const override {
        return (symbol == 'R') ? 2.0f : 1.5f;
    }
    float getPlacementRadius() const override { return getCollisionRadius(); }

    // How far this tower's shot flies per tick. The default is the King's
    // cannonball (published speed 1000: 1000 / 48 / 10 = 2.08, see
    // CrownTower::PRINCESS_ARROW_TILES_PER_TICK for the conversion), which the
    // Cannoneer and Dagger Duchess share. GameManager::addTower sets a tower
    // troop's own from CardStats::towerProjectileTilesPerTick.
    static constexpr float DEFAULT_PROJECTILE_TILES_PER_TICK = 2.0f;
    float projectileTilesPerTick = DEFAULT_PROJECTILE_TILES_PER_TICK;

    // Target selection ranks a Crown Tower by its PUBLISHED body (1.0
    // Princess, 1.4 King), not by the engine's 1.5 / 2.0 collision radius
    // above, which still sets reach, sight and where units stop (patch 0086).
    // Measured on 84 recorded building pulls: with 1.5 a Hog Rider, a Golem
    // and an Ice Golem stayed on the tower where the real ones turned to a
    // Tesla, Cannon or Bomb Tower that was nearer by 0.3-0.5 tile centre to
    // centre; every radius from 0.4 to 1.1 fits, 1.5 does not.
    static constexpr float PUBLISHED_PRINCESS_RADIUS = 1.0f;
    static constexpr float PUBLISHED_KING_RADIUS = 1.4f;
    float getTargetingRadius() const override {
        return (symbol == 'R') ? PUBLISHED_KING_RADIUS : PUBLISHED_PRINCESS_RADIUS;
    }

    // History: the nearest footprint wins (Entity::getTargetingRadius). Until
    // patch 0043 a Crown Tower competed with an inflated footprint (3.3 / 4.4),
    // fitted so that a Cannon 7-8 tiles off-lane would not pull a Hog Rider.
    // That fact is the published sight clip (SightClips.h, patch 0042); the
    // inflated footprint also made every troop prefer a tower over a unit up
    // to 1.8 tiles nearer, so Skeletons, an Ice Golem or a Cannon played in
    // front of the tower did not distract.

    bool isTower() const override { return true; }

    // Its own override: Building's would slice a Tower to a plain Building,
    // losing isTower() and the tower fallback in findTarget. The copy carries
    // `awake`, so a snapshotted King stays dormant.
    std::shared_ptr<Entity> snapshot() const override {
        return std::make_shared<Tower>(*this);
    }

    // --- activation ---
    // The King Tower starts dormant: it cannot acquire or fire until woken,
    // permanently, by taking any damage or by losing a friendly Princess Tower.
    // Only the King is built asleep (GameManager::addTower).
    bool isAwake() const { return awake; }
    void wake() { awake = true; }
    void sleep() { awake = false; }

    void update(Board& board) override {
        if (!awake) {
            // Trigger 1: any damage. Latched on hp < maxHp rather than hooked
            // into one of the several damage entry points; the latch only sets,
            // so a heal cannot re-sleep it.
            bool trigger = hp < maxHp;

            // Trigger 2: a friendly Princess Tower destroyed. The count is
            // recorded on the first update rather than compared with 2, since a
            // King on a bare board (every unit test) starts with none.
            int living = 0;
            for (const auto& e : board.getEntities()) {
                if (e->isAlive() && e->isTower() && e->team == team && e->symbol != 'R') ++living;
            }
            if (initialFriendlyPrincesses < 0) initialFriendlyPrincesses = living;
            else if (living < initialFriendlyPrincesses) trigger = true;
            // Patch 0088 (CR_P0088=king): the King wakes KING_WAKE_DELAY_TICKS
            // after its trigger (royalesim KING_ACTIVATE_TIME_MS 3550).
            if (trigger && !P0088::king()) awake = true;
            else if (trigger && wakeDelayLeft < 0) wakeDelayLeft = P0088::KING_WAKE_DELAY_TICKS;
            if (wakeDelayLeft >= 0 && wakeDelayLeft-- == 0) awake = true;
        }
        Building::update(board);
    }

protected:
    // The single choke point for dormancy: with no acquisition nothing is ever
    // attacked. The tower stays targetable, which is what lets trigger 1 fire.
    std::shared_ptr<Entity> findTarget(Board& board) const override {
        if (!awake) return nullptr;
        return CombatEntity::findTarget(board);
    }

    void performAttack(Board& board, std::shared_ptr<Entity> target) override {
        // lineSplash/lineSplashRange forwarded: a Tower Troop can carry them.
        auto arrow = std::make_shared<Projectile>(
            board.allocateId(), position.x, position.y, team, target, projectileTilesPerTick, getCurrentDamage(), onHitEffects,
            false, 0, id, cardId, splashRadius, lineSplash, lineSplashRange);
        board.addEntity(arrow);
    }

private:
    // Princess Towers; only the King is put to sleep.
    bool awake = true;
    // -1 until the first update() records the starting count.
    int initialFriendlyPrincesses = -1;
    // Patch 0088: ticks until a triggered King wakes, -1 = not triggered.
    int wakeDelayLeft = -1;
};