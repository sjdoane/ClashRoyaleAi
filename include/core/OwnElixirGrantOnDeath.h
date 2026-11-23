#pragma once
#include "DeathEffect.h"
#include "Board.h"

// Grants elixir to the unit's OWN team when it dies, however it dies (the
// Elixir Collector's published ManaOnDeath 1: one last elixir when it is
// destroyed or its lifetime runs out). Via Board::pendingElixirGrant, like
// EnemyElixirGrantOnDeath, which pays the other side.
class OwnElixirGrantOnDeath : public IDeathEffect {
    float amount;

public:
    explicit OwnElixirGrantOnDeath(float amount) : amount(amount) {}

    void apply(Board& board, const Vector2D& /*position*/, int team) const override {
        board.pendingElixirGrant[team] += amount;
    }
};
