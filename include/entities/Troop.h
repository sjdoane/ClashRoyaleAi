#pragma once
#include "CombatEntity.h"

class Troop : public CombatEntity {
protected:
    float speed;

public:
    bool riverIgnores = false;

    // Base speed for the observation encoder. Not freeze-adjusted: the
    // observation describes the unit, and a freeze shows up as it not moving.
    float getSpeed() const { return speed; }

    Troop(int id, float x, float y, int hp, int team, char symbol,
        float speed, float attackRange, int damage, int attackCooldown)
        : CombatEntity(id, x, y, hp, team, symbol, attackRange, damage, attackCooldown),
        speed(speed) {}

    void setIgnoresRiver(bool ignore) {
        riverIgnores = ignore;
    }

protected:
    void moveTowards(Board& board, const Vector2D& dest) override {
        if (isRiding()) return; // carried (patch 0046): CombatEntity::update pins it to its carrier
        // A walker goes by the bridge (Board::getNextWaypoint). A flying unit
        // goes straight. A ground unit that jumps the river goes diagonally
        // to its target's column while the river is ahead, at the jump's
        // speed (patch 0087, Board::getJumperWaypoint).
        //
        // A jumper that lands on a waypoint part-way through its step spends
        // the rest of the step on the next leg (up to three legs a tick).
        // Without that, two jumpers abreast in the one-tile corridor (Royal
        // Hogs: bodies 0.6 + 0.6) push each other 0.02 outside it every tick,
        // each is handed the 0.03-long diagonal back onto its edge, and both
        // creep across the river at 0.02 a tick (tests/core/test_board.cpp,
        // "river jumpers from every start cross the river"). A walker and a
        // flying unit take one leg, exactly as before.
        const bool jumper = riverIgnores && !isFlying;
        const int maxLegs = jumper ? 3 : 1;
        Vector2D at = position;
        float budget = 1.0f;          // share of this tick's movement left
        bool moved = false;
        for (int leg = 0; leg < maxLegs && budget > 1e-3f; ++leg) {
            Vector2D waypoint = dest;
            float jumpScale = 1.0f;
            if (!riverIgnores) {
                waypoint = board.getNextWaypoint(at, dest);
            } else if (!isFlying) {
                const Board::JumperStep js = board.getJumperWaypoint(at, dest);
                waypoint = js.waypoint;
                jumpScale = js.speedScale;
            }
            const float dx = waypoint.x - at.x;
            const float dy = waypoint.y - at.y;
            const float distToWaypoint = at.distanceTo(waypoint);

            // Must be the same constant getNextWaypoint uses for arrival; two
            // different epsilons produce an absorbing state at the bridge mouths
            // (tests/core/test_board.cpp).
            if (distToWaypoint <= Board::WAYPOINT_ARRIVAL_EPS) break;
            moved = true;
            // The tick-start freeze (frozenThisTick / freezeSlowThisTick), not
            // freezeTicks: update() has already counted the freezes down by
            // now. A movement-only slow (Poison) competes with it; the
            // stronger applies, never both.
            const float freezeFactor = frozenThisTick ? freezeSlowThisTick : 1.0f;
            float currentSpeed = speed * std::min(freezeFactor, moveSlowThisTick);
            // A charge runs faster (Prince, Battle Ram...: 2x in the game).
            if (isCharging()) currentSpeed *= chargeSpeedMultiplier;
            // A Hero ability's dash or sprint (patch 0017).
            if (inAbilityWindow()) currentSpeed *= windowMoveMultiplier;
            // The diagonal leg of a river jump (patch 0087).
            currentSpeed *= jumpScale;
            // Land on the waypoint, never past it: an overshoot along a bank
            // line keeps the unit "below" the river, so it is handed the same
            // bridge mouth back and orbits it forever (UPSTREAM_REQUESTS.md
            // item 31; tests/core/test_board.cpp, [orbit]).
            const float step = std::min(currentSpeed * budget, distToWaypoint);
            at.x += (dx / distToWaypoint) * step;
            at.y += (dy / distToWaypoint) * step;
            if (currentSpeed <= 0.0f) break;
            budget -= step / currentSpeed;
        }
        // Flying troops pass over building footprints.
        if (moved) position = isFlying ? at : board.resolvePositionAgainstBuildings(at, id, bodyRadius);
    }

    void clampPosition(Board& board) override {
        position = board.clampToBoard(position, riverIgnores);
    }
};
