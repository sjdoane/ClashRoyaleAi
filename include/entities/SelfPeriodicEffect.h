#pragma once
#include "Entity.h"

class CombatEntity;

// A periodic hook that sees the entity it runs on (patch 0015): Evolved
// Giant's rage trail, Evolved Elite Barbarians' spear, Evolved Electro Giant's
// Level Damper. IPeriodicEffect only gets a position and a team.
//
// apply returns whether it fired. A fire restarts the full interval; a
// decline (no target yet, hp still above a threshold) retries next tick.
// Effect objects are shared by every body of a card, so per-body state lives
// on the entity (CombatEntity::selfPeriodicState), never in the effect.
class ISelfPeriodicEffect {
public:
    virtual ~ISelfPeriodicEffect() = default;
    virtual bool apply(Board& board, CombatEntity& self) const = 0;
};
