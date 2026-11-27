#pragma once
#include "CombatEntity.h"
#include "P0088.h"
#include "ArenaLayout.h"
#include <cmath>

class Troop : public CombatEntity {
protected:
    float speed;

public:
    bool riverIgnores = false;
    // Patch 0088 (CR_P0088=stomp): the walk-pause clock, set by applyCardMetadata.
    int stompStopMs = 0, stompWaitMs = 0, stompClockMs = 0;
    // Patch 0088 (jumpcol): Board::getJumperWaypoint's step-onto-the-column
    // decision for the current target column (-1 undecided).
    int jumpColLatch = -1;
    float jumpColTargetX = -1000.0f;
    float stompWalkScale = 1.0f;   // floor(S (stop+wait)/stop) / S
    // Patch 0090 (hogspeed): speed factor on land (not over the water), set by
    // applyCardMetadata for the Hog Rider.
    float landSpeedScale = 1.0f;

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
        // Patch 0088 (CR_P0088=stomp): share of this tick spent walking, two
        // royalesim 50 ms ticks of its stomp clock. The clock does not run
        // while the unit is held by a full stop.
        float stompFactor = 1.0f;
        if (P0088::stomp() && stompStopMs > 0 && !(frozenThisTick && freezeSlowThisTick <= 0.0f)) {
            int walked = 0;
            for (int sub = 0; sub < 2; ++sub) {
                stompClockMs += 50;
                if (stompClockMs <= stompStopMs) { ++walked; continue; }
                const int rem = stompClockMs - (stompStopMs + stompWaitMs);
                if (rem >= 0) { stompClockMs = rem; ++walked; }
            }
            stompFactor = stompWalkScale * walked / 2.0f;
        }
        // Patch 0090 (walklegs): a walker too spends the rest of its step on
        // the next leg after reaching a waypoint, but only on a leg that goes
        // on in nearly the same direction (cos >= WALK_LEG_MIN_COS). Turning a
        // corner with the rest of the step took two Goblin Machine bodies
        // (0.75 + 0.75) at a bridge mouth into the water each tick, where the
        // push and the clamp put both back for ever (tests/core/test_board.cpp,
        // "pairs of wide bodies cross the river").
        const bool walkLegs = !jumper && !isFlying && P0088::walkLegs();
        const int maxLegs = (jumper || walkLegs) ? 3 : 1;
        Vector2D at = position;
        float budget = 1.0f;          // share of this tick's movement left
        bool moved = false;
        float lastUx = 0.0f, lastUy = 0.0f;
        for (int leg = 0; leg < maxLegs && budget > 1e-3f; ++leg) {
            Vector2D waypoint = dest;
            float jumpScale = 1.0f;
            if (!riverIgnores) {
                waypoint = board.getNextWaypoint(at, dest);
            } else if (!isFlying) {
                if (P0088::jumpCol() && std::abs(dest.x - jumpColTargetX) > 1e-3f) {
                    jumpColTargetX = dest.x; jumpColLatch = -1;   // new target column: decide again
                }
                const Board::JumperStep js = board.getJumperWaypoint(at, dest, P0088::jumpCol() ? &jumpColLatch : nullptr);
                waypoint = js.waypoint;
                jumpScale = js.speedScale;
            }
            const float dx = waypoint.x - at.x;
            const float dy = waypoint.y - at.y;
            const float distToWaypoint = at.engineDistanceTo(waypoint);

            // Must be the same constant getNextWaypoint uses for arrival; two
            // different epsilons produce an absorbing state at the bridge mouths
            // (tests/core/test_board.cpp).
            if (distToWaypoint <= Board::WAYPOINT_ARRIVAL_EPS) break;
            if (walkLegs) {
                const float ux = dx / distToWaypoint, uy = dy / distToWaypoint;
                if (leg > 0 && ux * lastUx + uy * lastUy < WALK_LEG_MIN_COS) break;
                lastUx = ux; lastUy = uy;
            }
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
            // Patch 0090 (CR_P0088=hogspeed): the Hog Rider's measured land speed;
            // over the water a jumper keeps its own (the crossing jumpedge fits).
            if (landSpeedScale != 1.0f && !(jumper && overWater(board, at, dest))) currentSpeed *= landSpeedScale;
            // Patch 0088 (CR_P0088=jumpspeed): over the water a jumper moves
            // at its JumpSpeed (Hog 160 / Speed 120), on top of the diagonal.
            if (jumper && P0088::jumpSpeed() && overWater(board, at, dest)) {
                currentSpeed *= P0088::JUMP_SPEED_RATIO;
            }
            currentSpeed *= stompFactor;
            // Patch 0088 (CR_P0088=bankrows): speed is in REAL tiles on the
            // squeezed rows: an engine step (ux, uy) costs sqrt(ux^2 + (k uy)^2)
            // real tiles, k = d realRow / dy (0.5 on y 13-15 and 18-20).
            if (P0088::bankRows() && distToWaypoint > 0.0f) currentSpeed *= bankRowFactor(at.y, dx / distToWaypoint, dy / distToWaypoint);
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
        // Patch 0088 (CR_P0088=jump): in the air while over the water on its
        // way across (crforge RouteFollower JUMPING: from the last land node
        // to the first one beyond). CombatEntity::update clears it each tick
        // before it moves, so a unit that stops is no longer jumping.
        if (P0088::jump() && jumper && moved && overWater(board, position, dest)) jumpingNow = true;
    }

    // Patch 0090 walklegs: a further leg in the same tick only within about
    // 18 degrees of the last one.
    static constexpr float WALK_LEG_MIN_COS = 0.95f;

    static float bankRowFactor(float y, float ux, float uy) {
        const float k = ((y >= 13.0f && y < 15.0f) || (y >= 18.0f && y < 20.0f)) ? 0.5f : 1.0f;
        if (k == 1.0f) return 1.0f;
        const float c = std::sqrt(ux * ux + k * k * uy * uy);
        return c > 1e-6f ? 1.0f / c : 1.0f;
    }

    // Over the river (bank to bank) and heading across it to dest.
    static bool overWater(const Board& board, const Vector2D& p, const Vector2D& dest) {
        constexpr float e = 1e-3f;
        if (dest.y > p.y) return p.y >= board.getRiverStart() - e && p.y < board.getRiverEnd() - e && dest.y >= board.getRiverEnd();
        if (dest.y < p.y) return p.y <= board.getRiverEnd() + e && p.y > board.getRiverStart() + e && dest.y <= board.getRiverStart();
        return false;
    }

    void clampPosition(Board& board) override {
        position = board.clampToBoard(position, riverIgnores);
    }
};
