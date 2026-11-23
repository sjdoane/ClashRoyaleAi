#pragma once
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

    // Target selection uses the collision radius above, like every other
    // building (Entity::getTargetingRadius): the nearest footprint wins. Until
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
            if (hp < maxHp) awake = true;

            // Trigger 2: a friendly Princess Tower destroyed. The count is
            // recorded on the first update rather than compared with 2, since a
            // King on a bare board (every unit test) starts with none.
            int living = 0;
            for (const auto& e : board.getEntities()) {
                if (e->isAlive() && e->isTower() && e->team == team && e->symbol != 'R') ++living;
            }
            if (initialFriendlyPrincesses < 0) initialFriendlyPrincesses = living;
            else if (living < initialFriendlyPrincesses) awake = true;
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
};