#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include "ArenaLayout.h"
#include "P0088.h"
#include "CardLevels.h"
#include "Entity.h"
#include "StatsEventBus.h"

class Board {
private:
    int width, height;
    std::vector<std::shared_ptr<Entity>> activeEntities;
    std::vector<std::shared_ptr<Entity>> pendingEntities;
    int idCounter = 1000;

    // The river band [15.5, 17.5), centred on 16.5 so it is symmetric under the
    // tower layout's y -> 33 - y mirror. Both teams' own-half placement bound
    // then sits at the same mirrored row.
    float riverY_start = 15.5f;
    float riverY_end = 17.5f;
    // Bridge centres from ArenaLayout: 2.5 and 14.5, on the seam between each
    // bridge's two tiles (see ArenaLayout.h).
    Vector2D leftBridge{ ArenaLayout::LEFT_BRIDGE_X, ArenaLayout::BRIDGE_Y };
    Vector2D rightBridge{ ArenaLayout::RIGHT_BRIDGE_X, ArenaLayout::BRIDGE_Y };

    // Scratch for resolveCollisions and the colliders() cache; members so the
    // allocation happens once per Board. Neither holds meaning between calls.
    struct Body {
        Entity* entity;
        float radius;
        bool building;
        bool troop;
        bool flying;
        bool alive;
    };
    struct Collider { size_t index; float radius; };
    mutable std::vector<Body> bodyScratch;
    mutable std::vector<Collider> colliderCache;
    mutable bool collidersDirty = true;

    // The arena is 18x34: one row behind each King is dead space except a
    // centred opening of real ground (isBackRowDeadZone).
    static constexpr float BACK_ROW_OPENING_HALF_WIDTH = 3.0f;

public:
    // Half a bridge's two-tile width; correct only with seam-centred bridges.
    // The deck a unit can stand on (isOnBridge: clampToBoard, the observation's
    // bridge columns, the pocket rule, the renderer). Where a unit AIMS to
    // cross is the narrower walk corridor below (patch 0087).
    static constexpr float BRIDGE_HALF_WIDTH = 1.0f;

    // THE WALK CORRIDOR (patch 0087). The centre of a unit crossing the river
    // stays within half a tile of its lane's centre column, the Princess
    // Tower's: x in [2.5, 3.5] and [13.5, 14.5]. Measured on the live
    // recordings of 2026-11-23/24 (5,201 boxes of ground troops over the
    // river, evals/hog_path2.json and the engine notes): the mass lies on
    // 2.25-3.75 and 13.25-14.75 with one peak at each corridor edge (units
    // arriving from outside hug 2.5 / 14.5, from the middle 3.5 / 13.5), and
    // nothing on 1.5-2.25 or 14.75-15.5, where the two-tile deck centred on
    // 2.5 / 14.5 used to let them walk. The inner edge is unchanged; the outer
    // edge moved one tile in, so a unit that comes from the arena's edge
    // crosses a tile nearer the middle and is seen by a centre building a tile
    // sooner.
    //
    // It is where a unit HEADS (getNextWaypoint, getJumperWaypoint), not a
    // wall. The deck a unit can stand on is still the drawn one (isOnBridge,
    // two tiles), so a body pushed sideways by another can be on the outer
    // tile. A first version made the corridor the wall as well: two bodies
    // wider than one tile together (a Goblin Machine and its turret, 0.75
    // each) then stood at the mouth pushing each other for ever.
    static constexpr float WALK_HALF_WIDTH = 0.5f;
    static constexpr float LEFT_WALK_X = ArenaLayout::LEFT_LANE_X;
    static constexpr float RIGHT_WALK_X = ArenaLayout::RIGHT_LANE_X;

    // Is this x inside a walk corridor?
    static constexpr bool inWalkCorridor(float x) {
        return (x >= LEFT_WALK_X - WALK_HALF_WIDTH && x <= LEFT_WALK_X + WALK_HALF_WIDTH)
            || (x >= RIGHT_WALK_X - WALK_HALF_WIDTH && x <= RIGHT_WALK_X + WALK_HALF_WIDTH);
    }
    // The corridor centre nearer to x (same split as ArenaLayout::isLeftLane).
    static constexpr float walkXFor(float x) {
        return ArenaLayout::isLeftLane(x) ? LEFT_WALK_X : RIGHT_WALK_X;
    }

    // Cell i covers [i - 0.5, i + 0.5], so the board's physical extent is x in
    // [-0.5, width - 0.5] and y likewise. isValidPlacement's footprint check
    // needs this physical edge: checking footprints against the index range
    // would reject a troop at both edge columns.
    static constexpr float CELL_HALF_EXTENT = 0.5f;

private:

public:
    // Public: every fire site already holds a Board&.
    StatsEventBus statsEvents;

    // Set once per tick by GameManager::step, read deep in the
    // update/performAttack chain to stamp DamageDealtEvent without threading a
    // tick through every override.
    int currentTick = 0;

    // Elixir grants (Elixir Collector) dropped here from deep inside update();
    // GameManager::step drains them into the players' elixir each tick.
    float pendingElixirGrant[2] = { 0.0f, 0.0f };

    Board(int w = 18, int h = 34) : width(w), height(h) {}

    int allocateId() { return idCounter++; }

    // A fully independent copy for decision-time search: every entity is
    // duplicated via Entity::snapshot(), so the copy can be stepped without
    // touching the original. Copy-constructing a Board is still a shallow copy
    // that shares entities.
    //
    // Not carried across:
    //   * `statsEvents` starts empty. The collectors are stateful, and a rollout's hits would land in the live match's statistics, which feed the reward. GameManager::snapshot copies them properly.
    //   * GameManager's own state (tick, game over, elixir), which GameManager::snapshot handles.
    Board deepCopy() const {
        Board copy(width, height);

        // Geometry copied explicitly rather than trusting the in-class
        // initialisers to still agree.
        copy.riverY_start = riverY_start;
        copy.riverY_end = riverY_end;
        copy.leftBridge = leftBridge;
        copy.rightBridge = rightBridge;

        // idCounter carries across, or rollout spawns would reuse ids and
        // id-keyed lookups would join the wrong entity.
        copy.idCounter = idCounter;
        copy.currentTick = currentTick;
        copy.pendingElixirGrant[0] = pendingElixirGrant[0];
        copy.pendingElixirGrant[1] = pendingElixirGrant[1];
        for (int t = 0; t < 2; ++t) {
            copy.cardLevel[t] = cardLevel[t];
            copy.towerLevel[t] = towerLevel[t];
        }

        std::unordered_map<int, std::shared_ptr<Entity>> byOldId;
        byOldId.reserve(activeEntities.size() + pendingEntities.size());

        // Pending entities too: a card played this tick lives there until the
        // next commit.
        copy.activeEntities.reserve(activeEntities.size());
        for (const auto& e : activeEntities) {
            auto c = e->snapshot();
            byOldId[e->id] = c;
            copy.activeEntities.push_back(std::move(c));
        }
        copy.pendingEntities.reserve(pendingEntities.size());
        for (const auto& e : pendingEntities) {
            auto c = e->snapshot();
            byOldId[e->id] = c;
            copy.pendingEntities.push_back(std::move(c));
        }

        // Must be a second pass: a projectile can home on an entity copied
        // after it. A no-op except for Projectile.
        for (const auto& e : copy.activeEntities) e->remapSnapshotReferences(byOldId);
        for (const auto& e : copy.pendingEntities) e->remapSnapshotReferences(byOldId);

        return copy;
    }

    int getWidth() const { return width; }
    int getHeight() const { return height; }

    // The river band, for callers such as the own-half placement check.
    float getRiverStart() const { return riverY_start; }
    float getRiverEnd() const { return riverY_end; }
    // Is this column a bridge? The single definition, shared by the physics
    // (clampToBoard) and the observation encoder so they cannot disagree.
    //
    // Takes a float to serve both: continuous positions, and integer cell
    // centres (cell i covers [i-0.5, i+0.5]).
    bool isOnBridge(float x) const {
        return (x >= leftBridge.x - BRIDGE_HALF_WIDTH && x <= leftBridge.x + BRIDGE_HALF_WIDTH)
            || (x >= rightBridge.x - BRIDGE_HALF_WIDTH && x <= rightBridge.x + BRIDGE_HALF_WIDTH);
    }

    const Vector2D& getLeftBridge() const { return leftBridge; }
    const Vector2D& getRightBridge() const { return rightBridge; }

    // True for the unplaceable corners of the two back rows, outside the
    // centred opening; false everywhere else.
    bool isBackRowDeadZone(float x, float y) const {
        bool inBackRow = (y < 1.0f) || (y > static_cast<float>(height) - 2.0f);
        if (!inBackRow) return false;
        float centerX = (static_cast<float>(width) - 1.0f) / 2.0f;
        return (x < centerX - BACK_ROW_OPENING_HALF_WIDTH) || (x > centerX + BACK_ROW_OPENING_HALF_WIDTH);
    }

    void addEntity(std::shared_ptr<Entity> entity) {
        pendingEntities.push_back(entity);
    }

    // Lets GameManager::playCard find the entity a deploy just spawned;
    // spawnEntity returns nothing.
    size_t pendingEntityCount() const { return pendingEntities.size(); }
    const std::shared_ptr<Entity>& getPendingEntity(size_t index) const { return pendingEntities[index]; }

    // GameManager::step passes the real tick; tests call it without one.
    // Per-team levels (CardLevels.h, UPSTREAM_REQUESTS.md item 42), applied
    // once to every entity as it enters play: the played card, and everything
    // spawned later (death spawns, spawner buildings, Graveyard). Crown Towers
    // take towerLevel. GameManager owns the setting and writes it into each
    // fresh Board; 11 everywhere is the unscaled engine.
    int cardLevel[2] = { CardLevels::TOURNAMENT_LEVEL, CardLevels::TOURNAMENT_LEVEL };
    int towerLevel[2] = { CardLevels::TOURNAMENT_LEVEL, CardLevels::TOURNAMENT_LEVEL };

    void applyTeamLevel(Entity& e) const {
        if (e.levelApplied) return;
        e.levelApplied = true;
        if (e.team != 0 && e.team != 1) return;
        const int level = e.isTower() ? towerLevel[e.team] : cardLevel[e.team];
        if (level != CardLevels::TOURNAMENT_LEVEL) e.scaleToLevel(level);
    }

    void commitPendingEntities(int tick = 0) {
        if (!pendingEntities.empty()) {
            for (const auto& e : pendingEntities) {
                applyTeamLevel(*e);
                statsEvents.notifyEntitySpawned({ e->id, e->cardId, e->team, tick });
            }
            activeEntities.insert(activeEntities.end(), pendingEntities.begin(), pendingEntities.end());
            pendingEntities.clear();
            collidersDirty = true;   // membership changed; see colliders()
        }
    }

    const std::vector<std::shared_ptr<Entity>>& getEntities() const {
        return activeEntities;
    }

    void cleanDeadEntities(int tick = 0) {
        // Nothing died: skip the three passes below, which would all be no-ops.
        // Most ticks take this branch.
        bool anyDead = false;
        for (const auto& e : activeEntities) {
            if (!e->isAlive()) { anyDead = true; break; }
        }
        if (!anyDead) return;
        collidersDirty = true;   // membership is about to change

        // Death effects fire before the erase, via Entity::onDeath.
        for (const auto& e : activeEntities) {
            if (!e->isAlive()) {
                e->onDeath(*this);
                statsEvents.notifyEntityDied({ e->id, e->cardId, e->team, tick });
            }
        }

        // Every living entity hears of every death (Skeleton King's souls). A
        // separate pass, so it reaches only entities already on the board, not
        // anything a death effect just spawned.
        for (const auto& dying : activeEntities) {
            if (!dying->isAlive()) {
                for (const auto& other : activeEntities) {
                    if (other->isAlive()) other->onNearbyDeath(*this, dying->position, dying->team);
                }
            }
        }

        activeEntities.erase(
            std::remove_if(activeEntities.begin(), activeEntities.end(),
                [](const std::shared_ptr<Entity>& e) { return !e->isAlive(); }),
            activeEntities.end()
        );
    }

    // Push a point out of a circular obstacle, with a small perpendicular slide
    // so it travels around instead of sticking. Shared by movement and
    // collision resolution.
    static Vector2D pushAwayFrom(Vector2D pos, const Vector2D& obstacleCenter, float minDist) {
        float dx = pos.x - obstacleCenter.x;
        float dy = pos.y - obstacleCenter.y;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < minDist) {
            // Direction and distance are computed separately: a point exactly
            // on the centre is pushed the full minDist along +x. The ordinary
            // case is unchanged.
            float ux, uy;
            if (dist < 0.001f) {
                ux = 1.0f; uy = 0.0f;   // coincident: any direction
            } else {
                ux = dx / dist; uy = dy / dist;
            }
            float push = minDist - dist;
            pos.x += ux * push + uy * 0.05f;
            pos.y += uy * push - ux * 0.05f;
        }
        return pos;
    }

    // Indices of everything with a real collision radius (buildings and towers,
    // a handful of entities), so resolvePositionAgainstBuildings does not scan
    // the whole list per moving troop.
    //
    // Radii never change, so only membership invalidates the cache, and it
    // changes only in commitPendingEntities and cleanDeadEntities, which set
    // the flag. Liveness is checked live.
    const std::vector<Collider>& colliders() const {
        if (collidersDirty) {
            colliderCache.clear();
            for (size_t i = 0; i < activeEntities.size(); ++i) {
                const float r = activeEntities[i]->getCollisionRadius();
                if (r > 0.0f) colliderCache.push_back(Collider{ i, r });
            }
            collidersDirty = false;
        }
        return colliderCache;
    }

    Vector2D resolvePositionAgainstBuildings(const Vector2D& pos, int entityId,
                                             float bodyRadius = Entity::IMPLICIT_TROOP_RADIUS) const {
        Vector2D resolved = pos;
        // Ascending index order: the pushes compose, so order is part of the
        // answer.
        for (const Collider& c : colliders()) {
            const auto& entity = activeEntities[c.index];
            if (entity->id == entityId || !entity->isAlive()) continue;

            resolved = pushAwayFrom(resolved, entity->position, c.radius + bodyRadius);
        }
        return resolved;
    }

    // Separates every overlapping pair for one tick, then re-clamps everyone to
    // the board and river.
    void resolveCollisions() {
        // Per-entity facts read once per entity instead of per pair; within
        // this call radius, targetability and liveness are constant. Positions
        // are not hoisted, since they are what this mutates.
        const size_t count = activeEntities.size();
        bodyScratch.clear();
        bodyScratch.reserve(count);
        for (const auto& e : activeEntities) {
            const float r = e->getCollisionRadius();
            bodyScratch.push_back(Body{
                e.get(), r, r > 0.0f, (r <= 0.0f && e->isTargetable()), e->isFlying, e->isAlive() });
        }

        for (size_t i = 0; i < count; ++i) {
            const Body& b1 = bodyScratch[i];
            if (!b1.alive) continue;
            for (size_t j = i + 1; j < count; ++j) {
                const Body& b2 = bodyScratch[j];

                if (!b2.alive) continue;

                Entity* e1 = b1.entity;
                Entity* e2 = b2.entity;

                const float r1 = b1.radius;
                const float r2 = b2.radius;

                const bool isBuilding1 = b1.building;
                const bool isBuilding2 = b2.building;
                const bool isTroop1 = b1.troop;
                const bool isTroop2 = b2.troop;

                // Only entities in the same plane collide; fliers pass through
                // everything.
                // A river jumper mid-jump touches nothing (patch 0088,
                // crforge ContactRule; never set with the switch off).
                if (e1->jumpingNow || e2->jumpingNow) continue;
                if (isTroop1 && isTroop2 && b1.flying == b2.flying) {
                    float dx = e1->position.x - e2->position.x;
                    float dy = e1->position.y - e2->position.y;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    // Two troops touch at the sum of their published bodies.
                    float minRadius = e1->bodyRadius + e2->bodyRadius;

                    if (dist < minRadius) {
                        if (dist < 0.001f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
                        float overlap = minRadius - dist;
                        // Each gives way by the OTHER's share of the two
                        // masses (published Mass, BodySizes.h): a Hog Rider
                        // (4) against an Ice Golem (6) takes 0.6 of the
                        // overlap and the Golem 0.4. Equal masses halve it.
                        const float massSum = e1->bodyMass + e2->bodyMass;
                        const float share1 = massSum > 0.0f ? e2->bodyMass / massSum : 0.5f;
                        const float share2 = 1.0f - share1;
                        const float nx = dx / dist;
                        const float ny = dy / dist;

                        // Tiny orthogonal nudge to prevent jitter lock.
                        const float noise = 0.01f;

                        e1->position.x += nx * overlap * share1 + ny * noise;
                        e1->position.y += ny * overlap * share1 - nx * noise;
                        e2->position.x -= nx * overlap * share2 + ny * noise;
                        e2->position.y -= ny * overlap * share2 - nx * noise;
                    }
                }

                if (isTroop1 && isBuilding2 && !b1.flying) {
                    e1->position = pushAwayFrom(e1->position, e2->position, r2 + e1->bodyRadius);
                }
                if (isTroop2 && isBuilding1 && !b2.flying) {
                    e2->position = pushAwayFrom(e2->position, e1->position, r1 + e2->bodyRadius);
                }
            }
        }

        // Re-clamp: collisions can push a troop into the river or off the
        // board. Each entity's clampPosition respects riverIgnores.
        for (auto& entity : activeEntities) {
            if (entity->isAlive()) {
                entity->clampPosition(*this);
            }
        }
    }

    // Keeps a position on the board and out of the river unless allowed in it.
    //
    // A ground unit in the river band beside a bridge deck is put back the
    // SHORTER way: sideways onto the deck's edge, or onto the nearer bank
    // (patch 0060). Until then it was always the bank, which was harmless
    // while every walker crossed along the deck's centre line; units now cross
    // anywhere across the deck, the edges included, and a nudge of a hundredth
    // of a tile there must not throw a unit a tile forward or back. Within
    // DECK_EDGE_TOLERANCE of the deck it is always the edge: a unit standing
    // within the arrival tolerance of a deck corner is sent across from there
    // (getNextWaypoint), and must not be handed back to the bank by its own
    // first step.
    Vector2D clampToBoard(Vector2D pos, bool ignoresRiver) const {
        pos.x = std::max(0.0f, std::min(pos.x, static_cast<float>(width - 1)));
        pos.y = std::max(0.0f, std::min(pos.y, static_cast<float>(height - 1)));

        if (!ignoresRiver && pos.y > riverY_start && pos.y < riverY_end) {
            // isOnBridge(), so the observation encoder gets the same answer.
            if (!isOnBridge(pos.x)) {
                const float edgeX = nearestDeckEdgeX(pos.x);
                const float toEdge = std::abs(pos.x - edgeX);
                const float toBank = std::min(pos.y - riverY_start, riverY_end - pos.y);
                if (toEdge <= toBank || toEdge <= DECK_EDGE_TOLERANCE) {
                    pos.x = edgeX;
                } else {
                    float riverMid = (riverY_start + riverY_end) / 2.0f;
                    pos.y = (pos.y < riverMid) ? riverY_start : riverY_end;
                }
            }
        }
        return pos;
    }

    // The single "standing on the waypoint" tolerance, shared with
    // Troop::moveTowards, which does not move closer than this. If
    // getNextWaypoint hands back a point the mover is already within this of,
    // the unit freezes for the rest of the match (tests/core/test_board.cpp).
    static constexpr float WAYPOINT_ARRIVAL_EPS = 0.01f;

    // Twice the arrival tolerance: see clampToBoard.
    static constexpr float DECK_EDGE_TOLERANCE = 2.0f * WAYPOINT_ARRIVAL_EPS;

    // The x of the deck edge (either bridge) nearest to `x`.
    float nearestDeckEdgeX(float x) const {
        const float edges[] = { leftBridge.x - BRIDGE_HALF_WIDTH, leftBridge.x + BRIDGE_HALF_WIDTH,
                                rightBridge.x - BRIDGE_HALF_WIDTH, rightBridge.x + BRIDGE_HALF_WIDTH };
        float best = edges[0];
        for (float e : edges) {
            if (std::abs(x - e) < std::abs(x - best)) best = e;
        }
        return best;
    }

    // Where a walker going from `from` to `to` meets the bank line y = bankY of
    // the bridge centred on bridgeX: where its straight line crosses that
    // line, kept on the deck. A line that does not reach the bank between the
    // two points (the walker is already past it, or `to` is on the deck) takes
    // `to`'s x.
    float crossingX(const Vector2D& from, const Vector2D& to, float bankY, float bridgeX) const {
        float x = to.x;
        const float dy = to.y - from.y;
        if (std::abs(dy) > 1e-6f) {
            const float t = (bankY - from.y) / dy;
            if (t >= 0.0f && t <= 1.0f) x = from.x + (to.x - from.x) * t;
        }
        return std::max(bridgeX - WALK_HALF_WIDTH, std::min(x, bridgeX + WALK_HALF_WIDTH));
    }

    // The next point a ground walker at currentPos heads for on its way to
    // targetPos: targetPos itself unless the river is between them.
    //
    // A BRIDGE IS CROSSED ACROSS ITS WIDTH (patch 0060). Until then every
    // walker was sent through the one point at the centre of the nearer
    // bridge's mouth, so a squad placed abreast crossed in single file and a
    // unit walking in the lane a tile off the centre line swerved to it and
    // back. Now the walker keeps its straight line to the target wherever
    // that line crosses the deck, and otherwise heads for the nearest point of
    // the deck's mouth, which is its corner. Measured on the recordings of
    // 2026-11-20 (engine notes, patch 0060): an Ice Golem from tile (8, 10)
    // and a Musketeer from (9, 9) walk a straight line to the near corner of
    // the bridge and cross along that edge; an Ice Golem placed in the lane
    // at x 13.6 crosses at 13.6.
    //
    // The bridge is still the nearer one, by its centre.
    //
    // ARRIVAL: Troop::moveTowards does not move closer than
    // WAYPOINT_ARRIVAL_EPS to a waypoint, so no branch may hand a walker a
    // point it is already that close to while it still has to cross: on the
    // near bank it is handed the far bank, at the far bank the target
    // (tests/core/test_board.cpp sweeps every branch, both directions, the
    // corners included).
    Vector2D getNextWaypoint(const Vector2D& currentPos, const Vector2D& targetPos) const {
        bool isCurrentBelow = currentPos.y <= riverY_start;
        bool isTargetBelow = targetPos.y <= riverY_start;
        bool isCurrentAbove = currentPos.y >= riverY_end;
        bool isTargetAbove = targetPos.y >= riverY_end;

        if ((isCurrentBelow && isTargetBelow) || (isCurrentAbove && isTargetAbove) ||
            (!isCurrentBelow && !isCurrentAbove && !isTargetBelow && !isTargetAbove)) {
            return targetPos;
        }

        // The nearer corridor, by its centre (patch 0087: the lane column,
        // until then the deck's seam half a tile further out).
        float distToLeft = currentPos.engineDistanceTo(Vector2D{ LEFT_WALK_X, leftBridge.y });
        float distToRight = currentPos.engineDistanceTo(Vector2D{ RIGHT_WALK_X, rightBridge.y });
        float bridgeX = (distToLeft < distToRight) ? LEFT_WALK_X : RIGHT_WALK_X;

        if (isCurrentBelow || isCurrentAbove) {
            // On dry land with the river ahead. The bank tests are inclusive,
            // so a unit already on the near bank is still "below" ("above");
            // hand it the far bank, or it is routed to where it stands.
            const float nearY = isCurrentBelow ? riverY_start : riverY_end;
            const float farY = isCurrentBelow ? riverY_end : riverY_start;
            Vector2D nearBank{ crossingX(currentPos, targetPos, nearY, bridgeX), nearY };
            const Vector2D farBank{ crossingX(currentPos, targetPos, farY, bridgeX), farY };
            if (currentPos.engineDistanceTo(nearBank) <= WAYPOINT_ARRIVAL_EPS) {
                return farBank;
            }
            // Pushed along the bank onto the deck's outer tile (patch 0087):
            // it would have to walk nearly along the bank line to reach the
            // corridor's mouth, and whatever pushed it out stands there. It
            // goes across from where it is, towards the corridor. A unit
            // walking in from its placement tile never meets this: the
            // shallowest line to a mouth, from the arena's edge on the row
            // beside the river, is 11 degrees.
            const float toWater = std::abs(nearY - currentPos.y);
            const float lateral = std::abs(nearBank.x - currentPos.x);
            if (isOnBridge(currentPos.x) && toWater < MOUTH_FUNNEL_SLOPE * lateral) {
                return farBank;
            }
            return nearBank;
        }
        // Inside the river band, heading for the exit bank. Within
        // WAYPOINT_ARRIVAL_EPS of it this leg is done: return targetPos,
        // which the first branch returns anyway once y reaches the bank.
        // Handing back the edge itself would freeze the unit just short of
        // dry land (tests/core/test_board.cpp, the bridge-exit block).
        if (isTargetAbove || isTargetBelow) {
            const float exitY = isTargetAbove ? riverY_end : riverY_start;
            Vector2D exitBank{ crossingX(currentPos, targetPos, exitY, bridgeX), exitY };
            if (currentPos.engineDistanceTo(exitBank) <= WAYPOINT_ARRIVAL_EPS) {
                return targetPos;
            }
            return exitBank;
        }
        return targetPos;
    }

    // See getNextWaypoint: tan of the angle to the bank line below which a
    // walker on the deck's outer tile stops heading for the corridor's mouth
    // and crosses from where it is (10 degrees).
    static constexpr float MOUTH_FUNNEL_SLOPE = 0.18f;

    // RIVER JUMPERS (patch 0087). Where a ground unit that jumps the river
    // (Hog Rider, Royal Hogs, Ram Rider, ...) heads next on its way to
    // targetPos, and how fast.
    //
    // Until this patch a jumper walked a straight line to its target from
    // wherever it stood. The recorded Hog Rider does not (145 own plays in
    // the live runs of 2026-11-23/24, evals/tools/hog_path2.py):
    //
    //   placed on    lands at (engine row 18)   straight line
    //   ( 1, 15)     2.52  [2.50, 2.58]  n 51   1.5
    //   (16, 15)    14.54 [14.49, 14.58] n 55   15.5
    //   ( 6, 15)     3.6                 n 1    5.2
    //   ( 8, 15)     5.0                 n 1    6.8
    //
    // and then goes straight up that column to about row 21. With a building
    // in sight from the start (a collector on (10, 23), an Inferno Tower on
    // (10.5, 22.5), Hog on (16, 15)) it lands on 13.0 and 13.1, not on the
    // straight line's 13.6 and 13.5. Every one of these is the same path:
    // sideways and forward at the same rate (45 degrees over the real rows)
    // until it is within half a tile of its target's column, then straight
    // ahead. It does not slow down for the diagonal: it reaches the far bank
    // when a Hog going straight across would (2.0-2.4 tiles/s sideways on top
    // of its 2.3-2.7 forward), which is the game's faster jump.
    //
    // So, while the river is still ahead of it or under it:
    //   - within half a tile of the target's column: straight across;
    //   - else, far enough back to reach that column before the water at 45
    //     degrees or steeper: the straight line to it on the near bank (what a
    //     walker does; a Hog from behind the King tower reaches the river on
    //     13.55, the corridor's corner);
    //   - else 45 degrees towards the column, at sqrt(2) x speed from the bank
    //     row on (JUMP_ZONE), so its forward speed is unchanged.
    // Past the far bank it walks the straight line to its target as before
    // (measured after landing: the straight line, 55 degrees where 45 would
    // have been the other reading).
    //
    // One more recorded case: a Hog placed a tile or less off its target's
    // column, on the tile beside the bridge, does not jump. It steps onto the
    // column and walks up it: placed on (2, 15) it is on 3.0 over the river
    // (4 of 4 plays with no building in sight), from (15, 15) on 14.0 (2 of
    // 2), from (3, 15) on 3.05-3.2 (2), and it reaches the far bank 0.1-0.2 s
    // after a jumping Hog would. So, short of the water, within
    // BRIDGE_WALK_REACH of the column and on its outer side (further from the
    // middle of the arena than the column), it heads for the column's point on
    // the near bank at walking speed (0.24 s longer than straight from one
    // tile off). The recorded sidestep is spread over about a row; here it is
    // done by the bank. Outer side only, because the three Hogs recorded
    // coming from the middle (placed behind the King Tower) reach the river on
    // 13.55, the corridor's inner corner, not on 14.0. Nothing is recorded
    // from one tile inside (4, 15) / (13, 15): it takes the half-tile rule.
    //
    // For a tower the column is the walk corridor, so jumpers and walkers
    // cross in the same place. Flying units do not come here.
    struct JumperStep { Vector2D waypoint; float speedScale; };
    static constexpr float JUMP_ZONE = 1.0f;
    static constexpr float JUMP_DIAGONAL_SPEED = 1.41421356f;
    static constexpr float BRIDGE_WALK_REACH = 1.0f;
    // Patch 0090 jumpedge: where the corner jump lands, outside the target
    // column (real deck edges 2.0 / 15.0 by the bridge posts; the Hog lands
    // 0.2 inside them), and how far past the near bank (real row 16.0).
    static constexpr float JUMP_EDGE_INSET = 0.8f;
    static constexpr float JUMP_EDGE_ROWS = 1.5f;
    // ... and the straight run after it: to JUMP_EDGE_RUN engine rows past the
    // far bank (engine 20.0 = real 18.0), only on the edge line (within
    // JUMP_EDGE_RUN_TOL of its x) and only with the target at least
    // JUMP_EDGE_RUN_CLEAR rows beyond the run's end.
    static constexpr float JUMP_EDGE_RUN = 2.5f;
    static constexpr float JUMP_EDGE_RUN_TOL = 0.05f;
    static constexpr float JUMP_EDGE_RUN_CLEAR = 2.0f;
    // The run's next point, or y = -1e9 when there is none.
    Vector2D jumpEdgeRun(const Vector2D& at, const Vector2D& targetPos, float farY, float dir) const {
        const Vector2D none{ at.x, -1.0e9f };
        const float laneX = (std::abs(at.x - LEFT_WALK_X) <= std::abs(at.x - RIGHT_WALK_X)) ? LEFT_WALK_X : RIGHT_WALK_X;
        if (std::abs(targetPos.x - laneX) > BRIDGE_HALF_WIDTH + 1e-3f) return none;
        const float out = (targetPos.x < ArenaLayout::CENTER_X) ? -1.0f : 1.0f;
        if (std::abs(at.x - (targetPos.x + out * JUMP_EDGE_INSET)) > JUMP_EDGE_RUN_TOL) return none;
        const float runEnd = farY + dir * JUMP_EDGE_RUN;
        if ((runEnd - at.y) * dir <= WAYPOINT_ARRIVAL_EPS) return none;
        if ((targetPos.y - runEnd) * dir < JUMP_EDGE_RUN_CLEAR) return none;
        return Vector2D{ at.x, runEnd };
    }
    // colLatch (jumpcol only): the unit's own record of the step-onto-the-column
    // decision, -1 undecided, 0 no, 1 yes; null = decide on the spot.
    JumperStep getJumperWaypoint(const Vector2D& currentPos, const Vector2D& targetPos,
                                 int* colLatch = nullptr) const {
        const bool up = targetPos.y > currentPos.y;
        const float nearY = up ? riverY_start : riverY_end;
        const float farY = up ? riverY_end : riverY_start;
        const float dir = up ? 1.0f : -1.0f;
        const bool targetBeyond = up ? (targetPos.y >= farY) : (targetPos.y <= farY);
        const bool beforeFarBank = up ? (currentPos.y < farY) : (currentPos.y > farY);
        // Patch 0090 jumpedge: past the far bank, a unit that landed on the
        // edge line runs straight on to JUMP_EDGE_RUN rows past the bank before
        // it turns to its target (recorded corner Hog: x 2.24 / 2.26 / 2.31 on
        // real rows 17 / 18 / 19, then 2.48 / 2.54 on 20 / 21).
        if (P0088::jumpEdge() && !beforeFarBank) {
            const Vector2D run = jumpEdgeRun(currentPos, targetPos, farY, dir);
            if (run.y > -1000.0f) return { run, 1.0f };
        }
        if (!targetBeyond || !beforeFarBank) return { targetPos, 1.0f };

        const float toWater = (nearY - currentPos.y) * dir;      // > 0 while short of the water
        const float off = std::abs(targetPos.x - currentPos.x);
        const bool outerSide = (currentPos.x - targetPos.x) * (targetPos.x - ArenaLayout::CENTER_X) > 0.0f;
        const float gx = std::max(targetPos.x - WALK_HALF_WIDTH,
                                  std::min(currentPos.x, targetPos.x + WALK_HALF_WIDTH));
        const float lateral = std::abs(gx - currentPos.x);
        // Patch 0090 (CR_P0088=jumpedge, P0088.h). Only for a target in the
        // lane: its column within the deck of the bridge nearest the unit.
        // Recorded (runs/bridge_1126, true positions = box centre less the
        // hammer's 0.3): the corner Hog (1, 15) goes in a straight line to
        // (2.2, real row 16.0) and lands there (n 140; (16, 15) on 14.8, n 130;
        // the opponents' the same, n 13), forward at its walking speed (real
        // rows 17 / 18 / 19 at 1.10 / 1.58 / 2.05 s); the Hog beside the bridge
        // (2, 15) walks onto it (2.24 / 2.46 / 2.71 on real 14.5 / 15 / 16,
        // n 12; (15, 15) the mirror, n 8).
        if (P0088::jumpEdge()) {
            const float laneX = (std::abs(currentPos.x - LEFT_WALK_X) <= std::abs(currentPos.x - RIGHT_WALK_X))
                ? LEFT_WALK_X : RIGHT_WALK_X;
            if (std::abs(targetPos.x - laneX) <= BRIDGE_HALF_WIDTH + 1e-3f) {
                const bool inBand = toWater <= WAYPOINT_ARRIVAL_EPS;      // on the near bank or past it
                // Arrival tolerance: a unit that stopped within WAYPOINT_ARRIVAL_EPS
                // of the bridge mouth (the corridor's edge) has arrived there; a
                // tighter test sent a beside-the-bridge Hog 0.004 short of it into
                // the corner jump instead (a 2.5 % slower step was enough).
                const bool inCorridor = std::abs(currentPos.x - laneX) <= WALK_HALF_WIDTH + WAYPOINT_ARRIVAL_EPS;
                const float out = (targetPos.x < ArenaLayout::CENTER_X) ? -1.0f : 1.0f;
                const Vector2D edge{ targetPos.x + out * JUMP_EDGE_INSET, nearY + dir * JUMP_EDGE_ROWS };
                const float toEdge = (edge.y - currentPos.y) * dir;
                const bool walk = inBand ? inCorridor : (off <= BRIDGE_WALK_REACH + 1e-3f);
                if (walk) return { getNextWaypoint(currentPos, targetPos), 1.0f };
                if (outerSide) {
                    if (toEdge <= WAYPOINT_ARRIVAL_EPS || currentPos.engineDistanceTo(edge) <= WAYPOINT_ARRIVAL_EPS) {
                        const Vector2D run = jumpEdgeRun(currentPos, targetPos, farY, dir);
                        return { run.y > -1000.0f ? run : targetPos, 1.0f };
                    }
                    float scale = 1.0f;
                    if (toWater <= JUMP_ZONE) {
                        scale = std::min(JUMP_DIAGONAL_SPEED, currentPos.engineDistanceTo(edge) / toEdge);
                    }
                    return { edge, scale };
                }
            }
        }
        // Patch 0088 (CR_P0088=jumpcol): the step onto the column replaces the
        // diagonal jump only, i.e. only where the jump would otherwise start
        // (toWater <= lateral: it cannot walk into the corridor before the
        // water). The recorded basis is Hogs placed ON the bank tile beside
        // the bridge (2,15) / (15,15) / (3,15). A Hog that walks from further
        // back walks into the corridor and crosses on its edge; recorded
        // enemy Hogs from behind the bridge never cross on the column
        // (runs/hoggap_1125/colB). Off: any Hog within a tile outside the
        // column short of the water heads for the column (patch 0087).
        // Decided once, the first time the unit is beside its crossing: the step
        // itself changes the geometry (it heads for the column, not the
        // corridor edge), so the test is not re-run along the way.
        const bool beside = outerSide && toWater > WAYPOINT_ARRIVAL_EPS && off > WAYPOINT_ARRIVAL_EPS
            && off <= BRIDGE_WALK_REACH + 1e-3f;
        bool jumpWouldStart = true;
        if (P0088::jumpCol() && beside) {
            if (colLatch == nullptr) {
                jumpWouldStart = toWater <= lateral * 1.001f;
            } else {
                if (*colLatch < 0) *colLatch = (toWater <= lateral * 1.001f) ? 1 : 0;
                jumpWouldStart = *colLatch == 1;
            }
        }
        if (beside && jumpWouldStart) {
            // Beside its crossing: onto the column at the bank, no jump.
            return { Vector2D{ targetPos.x, nearY }, 1.0f };
        }

        if (lateral <= WAYPOINT_ARRIVAL_EPS) {
            // In its column: straight across. Never a point it already stands
            // on (Troop::moveTowards would not move).
            const Vector2D across{ currentPos.x, farY };
            if (currentPos.engineDistanceTo(across) <= WAYPOINT_ARRIVAL_EPS) return { targetPos, 1.0f };
            return { across, 1.0f };
        }
        if (toWater >= lateral) return { Vector2D{ gx, nearY }, 1.0f };
        const bool jumping = toWater <= JUMP_ZONE;
        return { Vector2D{ gx, currentPos.y + dir * lateral }, jumping ? JUMP_DIAGONAL_SPEED : 1.0f };
    }
};
