#pragma once
#include "ArenaLayout.h"
#include "Board.h"
#include "PlayerState.h"
#include "Tower.h"
#include "CombatEntity.h"
#include "MatchRules.h"
#include "TimeoutRules.h"
#include "MatchStatistics.h"
#include "TowerTroops.h"
#include "RoyalChefBuffEffect.h"
#include "CardFactories.h"
#include "SpiritEmpressForms.h"
#include <algorithm>
#include <vector>
#include <string>
#include <stdexcept>
#include <random>
// std::as_const, for the non-const findTower. Included explicitly; reaching it
// transitively broke the tools/audit builds.
#include <utility>
// std::array, for the per-card cycle tracking.
#include <array>

class GameManager {
public:
    // Reserved cardIds for towers, which are built here rather than registered.
    // Negative, so they never collide with a registry id.
    static constexpr int TOWER_KING_ID = -2;
    static constexpr int TOWER_PRINCESS_ID = -3;
    // Mirror's registered id, and its Evolution's (patch 0016); playCard
    // special-cases both.
    static constexpr int MIRROR_CARD_ID = 164;
    static constexpr int EVO_MIRROR_CARD_ID = 180;
    // Spirit Empress's registered id; see SpiritEmpressForms.h.
    static constexpr int SPIRIT_EMPRESS_CARD_ID = 165;

private:
    Board board;
    MatchStatistics stats;
    int currentTick;
    bool gameOver;
    int loserTeam;
    using CycleTable = std::array<std::array<int, CARD_ID_COUNT>, 2>;

    // Every entry -1 ("not played"). A zero-filled table would claim both sides
    // played every card on tick 0.
    static constexpr CycleTable neverPlayed() {
        CycleTable t{};
        for (auto& perTeam : t) for (auto& v : perTeam) v = -1;
        return t;
    }

    // [team][cardId] -> the tick that team last played that card, -1 for never.
    // Written only in playCard, read by ClashEnv for the opponent-cycle
    // observation. A plain value, so snapshot() copies it and a rollout
    // inherits the cycle.
    CycleTable lastPlayedTick = neverPlayed();
public:
    // Every real play and ability activation, per team, in order: what an
    // observer could have watched happen. Read by ClashEnv, which passes it
    // through a simulated detector into OpponentStateEstimator (item 46).
    // Written in playCard and activateChampionAbility, cleared by reset(); a
    // plain value, so snapshot() copies it.
    struct ObservedEvent {
        int tick;
        int kind;        // OpponentStateEstimator::KIND_PLAY / KIND_ABILITY
        int cardId;      // what left the hand (an Evolution's own id even when played plain)
        float cost;      // elixir actually paid (Mirror +1, Spirit Empress 3/6, ability cost)
        bool evolved;    // the evolved form was spawned
        bool isSpell;
    };
private:
    std::array<std::vector<ObservedEvent>, 2> observedEvents;
    // Curriculum hook: scales the opponent's regen. 1.0 is a normal opponent.
    float oppElixirMultiplier = 1.0f;
    // setTeamLevel; written into each fresh Board by reset().
    int teamCardLevel[2] = { CardLevels::TOURNAMENT_LEVEL, CardLevels::TOURNAMENT_LEVEL };
    int teamTowerLevel[2] = { CardLevels::TOURNAMENT_LEVEL, CardLevels::TOURNAMENT_LEVEL };

    // How far short of the river a non-spell placement must stay on its own
    // side; Board enforces the river only during movement.
    static constexpr float OWN_HALF_RIVER_BUFFER = 0.5f;
    // Seeded once, not per reset(), so each episode deals a fresh opening hand.
    std::mt19937 rng;

    std::vector<int> aiDeckConfig = { 0, 1, 2, 3, 4, 5, 6, 7 };
    std::vector<int> oppDeckConfig = { 0, 1, 2, 3, 4, 5, 6, 7 };
    // Per-match configuration (TowerTroops.h). None is the original Princess
    // Tower.
    TowerTroopType aiTowerTroop = TowerTroopType::None;
    TowerTroopType oppTowerTroop = TowerTroopType::None;
    // Royal Chef's pancake, per team: the fraction cooked (1 = ready, held
    // until a troop qualifies). See cookRoyalChef.
    float chefCookProgress[2] = { 0.0f, 0.0f };

    // Royal Chef (TowerTroops.h RoyalChef): one pancake cooks per team, at the
    // rate of how many of that team's Princess Towers are busy (attacking, or
    // destroyed); with both destroyed it stops.
    void cookRoyalChef(int team) {
        if ((team == 0 ? aiTowerTroop : oppTowerTroop) != TowerTroopType::RoyalChef) return;
        if (currentTick < RoyalChef::COOK_START_TICKS) return;
        int standing = 0, attacking = 0;
        for (const auto& e : board.getEntities()) {
            if (e->team != team || !e->isAlive() || !e->isTower() || e->symbol == 'R') continue;
            standing++;
            if (auto t = std::dynamic_pointer_cast<CombatEntity>(e); t && t->isAttacking()) attacking++;
        }
        if (standing == 0) return;
        const int busy = std::min(2, attacking + (2 - standing));
        float& progress = chefCookProgress[team];
        if (progress < 1.0f) progress += 1.0f / static_cast<float>(RoyalChef::COOK_TICKS[busy]);
        if (progress >= 1.0f && RoyalChefBuffEffect::serve(board, team)) progress = 0.0f;
    }

    void addTower(float x, float y, int hp, int team, float attackRange, int damage, int attackCooldown,
        char symbol, const std::string& towerName) {
        auto tower = std::make_shared<Tower>(board.allocateId(), x, y, hp, team, attackRange, damage, attackCooldown, symbol);
        tower->name = towerName;
        tower->cardId = (symbol == 'R') ? TOWER_KING_ID : TOWER_PRINCESS_ID;
        // This overload skips applyCardMetadata, so sightRange is set directly.
        // It only builds the King (sourced 7.0); Tower Troops go through the
        // overload below.
        if (symbol == 'R') {
            tower->sightRange = 7.0f;
            tower->loadTicks = CrownTower::KING_FIRST_HIT_TICKS;
            // The King starts dormant (Tower::isAwake); this is the only path
            // that builds one.
            tower->sleep();
        }
        board.addEntity(tower);
    }

    // Tower Troops: builds a Princess Tower from CardStats (TowerTroops.h) and
    // reuses applyCardMetadata for extra fields such as Dagger Duchess's burst.
    // The symbol stays 'P': the viewer sizes footprints by symbol.
    void addTower(float x, float y, int team, const std::string& towerName, const CardStats& stats) {
        auto tower = std::make_shared<Tower>(board.allocateId(), x, y, stats.hp, team, stats.attackRange, stats.damage, stats.attackCooldown, 'P');
        // After applyCardMetadata, which would otherwise overwrite name and
        // cardId with the CardStats defaults.
        CardFactories::applyCardMetadata(tower, stats);
        tower->name = towerName;
        tower->cardId = TOWER_PRINCESS_ID;
        if (stats.towerProjectileTilesPerTick > 0.0f) tower->projectileTilesPerTick = stats.towerProjectileTilesPerTick;
        board.addEntity(tower);
    }

    // The live entity tracked for a Champion slot (1 = Heroic, 2 = Wild Card),
    // or nullptr. Resolving strictly through trackedEntityId is what keeps a
    // Clone-spell copy from ever activating the ability and makes the newest
    // deploy the one that answers.
    std::shared_ptr<CombatEntity> findChampionInSlot(int team, int slot) const {
        const PlayerState& player = (team == 0) ? playerAI : playerOpponent;
        auto it = player.championSlots.find(slot);
        if (it == player.championSlots.end() || it->second.trackedEntityId == -1) return nullptr;
        for (const auto& entity : board.getEntities()) {
            if (entity->id == it->second.trackedEntityId) {
                if (!entity->isAlive()) return nullptr;
                return std::dynamic_pointer_cast<CombatEntity>(entity);
            }
        }
        return nullptr;
    }

    // Once per tick per team: copies the tracked Champion's live cooldown into
    // persistedCooldownRemaining. After it dies the last value stays, so a
    // redeploy continues the timer.
    void syncChampionCooldowns(int team) {
        PlayerState& player = (team == 0) ? playerAI : playerOpponent;
        const std::vector<int>& deckConfig = (team == 0) ? aiDeckConfig : oppDeckConfig;
        for (auto& entry : player.championSlots) {
            int slot = entry.first;
            PlayerState::ChampionSlotState& state = entry.second;
            if (state.trackedEntityId == -1) continue;
            for (const auto& entity : board.getEntities()) {
                if (entity->id != state.trackedEntityId) continue;
                if (entity->isAlive()) {
                    if (auto ce = std::dynamic_pointer_cast<CombatEntity>(entity)) {
                        state.persistedCooldownRemaining = ce->abilityCooldownRemaining;
                    }
                }
                break;
            }

            // Post-death squad reactivation (Hero Goblins' "Banner Brigade"):
            // opens the window once every unit sharing the slot's cardId and
            // team is gone. Runs before cleanDeadEntities, so units that died
            // this tick are still present with their death position.
            const CardDefinition* slotDef = CardRegistry::getInstance().getCard(deckConfig[slot]);
            if (!slotDef || slotDef->abilityUsableAfterDeathTicks <= 0) continue;
            bool anyFound = false; // a unit of this slot's card seen this tick
            bool anyAlive = false;
            // A body that already used its alive-path ability (Hero
            // Tombstone, patch 0017) spends the post-death one too: one use
            // per deploy.
            bool spent = false;
            Vector2D deathPosition = state.lastSquadWipePosition;
            for (const auto& entity : board.getEntities()) {
                if (entity->cardId != deckConfig[slot] || entity->team != team) continue;
                anyFound = true;
                if (entity->isAlive()) { anyAlive = true; break; }
                deathPosition = entity->position;
                if (auto ce = std::dynamic_pointer_cast<CombatEntity>(entity))
                    if (ce->abilityEffect && ce->abilityUsesRemaining == 0) spent = true;
            }
            if (anyAlive) {
                state.lastSquadWipeTick = -1; // a fresh deploy invalidates an unconsumed window
            } else if (anyFound && spent) {
                state.lastSquadWipeTick = -1;
            } else if (anyFound && state.lastSquadWipeTick < 0) {
                // anyFound stops the window re-arming every tick after the dead
                // squad is erased, which would let the ability fire repeatedly
                // instead of once per wipe.
                state.lastSquadWipeTick = currentTick; // first tick nothing is left alive
                state.lastSquadWipePosition = deathPosition;
            }
        }
    }

    // Post-death ability readiness, the fallback when nothing in the slot is
    // alive.
    bool isPostDeathAbilityReady(int team, int slot) const {
        const PlayerState& player = (team == 0) ? playerAI : playerOpponent;
        auto it = player.championSlots.find(slot);
        if (it == player.championSlots.end() || it->second.lastSquadWipeTick < 0) return false;
        const std::vector<int>& deckConfig = (team == 0) ? aiDeckConfig : oppDeckConfig;
        const CardDefinition* slotDef = CardRegistry::getInstance().getCard(deckConfig[slot]);
        if (!slotDef || slotDef->abilityUsableAfterDeathTicks <= 0 || !slotDef->postDeathAbilityEffect) return false;
        if (currentTick - it->second.lastSquadWipeTick > slotDef->abilityUsableAfterDeathTicks) return false;
        return player.elixir >= slotDef->abilityElixirCost;
    }

public:
    // --- elixir phases ---
    // The real schedule (FirstLight-Zero battle_env.py; IL_Replay match ends):
    // 3:00 of regulation, double elixir from 2:00, then 2:00 of overtime whose
    // first minute stays double and whose last minute is triple.
    //
    //   0:00 - 2:00   1x   one elixir per 2.8 s
    //   2:00 - 4:00   2x   one per 1.4 s
    //   4:00 - 5:00   3x   one per 0.933 s
    //
    // In ticks (10 ticks = 1 s, perception/timebase.py). Public because
    // ClashEnv, the bindings and GameLogger all need it.
    // One elixir per 2.8 s at 1x (28 ticks). Measured: in 2,085 Ranked/Ladder
    // replays that ran to ~5:00, spend + leaked never exceeds 5 + 197.86 (the
    // 2.8 s schedule's ceiling) and 667 sides exceed 194.0, which 2.857 s
    // (the old 0.035) cannot produce (perception/UPSTREAM_REQUESTS.md item 38).
    // Static and bound, so Python reads it instead of restating it.
    static constexpr float ELIXIR_REGEN_RATE = 1.0f / 28.0f;
    static constexpr int DOUBLE_ELIXIR_TICK = 1200;   // 2:00
    static constexpr int TRIPLE_ELIXIR_TICK = 2400;   // 4:00, one minute into overtime
    // Triple elixir is the last minute of overtime, which MatchRules owns.
    // Asserted rather than derived, since MatchRules knows nothing about
    // elixir.
    static_assert(TRIPLE_ELIXIR_TICK > MatchRules::REGULATION_END_TICK &&
                  TRIPLE_ELIXIR_TICK < MatchRules::MATCH_END_TICK,
                  "triple elixir starts inside overtime");

    // The largest multiplier, and the normaliser of the observation scalar;
    // bound so perception's encoder divides by the same number.
    static constexpr float MAX_ELIXIR_MULTIPLIER = 3.0f;

    // A pure function of the tick, static so ClashEnv, GameLogger and the
    // viewer share one schedule.
    static constexpr float elixirMultiplierAtTick(int tick) {
        if (tick >= TRIPLE_ELIXIR_TICK) return MAX_ELIXIR_MULTIPLIER;
        if (tick >= DOUBLE_ELIXIR_TICK) return 2.0f;
        return 1.0f;
    }

    // The shared phase for both players; oppElixirMultiplier is a separate
    // per-opponent handicap that composes with it (see step()).
    float getElixirMultiplier() const { return elixirMultiplierAtTick(currentTick); }

    PlayerState playerAI;
    PlayerState playerOpponent;

    GameManager(const std::vector<int>& aiDeck, const std::vector<int>& opponentDeck,
            TowerTroopType aiTowerTroopType = TowerTroopType::None,
            TowerTroopType oppTowerTroopType = TowerTroopType::None)
        : gameOver(false), loserTeam(-1), rng(std::random_device{}()) {
        aiDeckConfig = aiDeck;
        oppDeckConfig = opponentDeck;
        aiTowerTroop = aiTowerTroopType;
        oppTowerTroop = oppTowerTroopType;
        reset();
    }

    // Seeds the generator that deals the opening hands and deck order. Not
    // ClashEnv::rng, which only feeds HeuristicOpponent. Takes effect on the
    // next reset(); ClashEnv::seed calls one.
    void seed(unsigned int s) { rng.seed(s); }

    void setOpponentDeck(const std::vector<int>& deck) {
        std::string err = validateDeckSlots(deck);
        if (!err.empty()) throw std::invalid_argument("GameManager::setOpponentDeck: invalid deck -- " + err);
        oppDeckConfig = deck;
    }

    void setOpponentElixirMultiplier(float multiplier) {
        oppElixirMultiplier = std::max(0.0f, multiplier);
    }

    // Per-team card and Crown Tower level, 1..16 (CardLevels.h, UPSTREAM item
    // 41). Cards take effect from the next entity to enter play; towers are
    // built at reset(), so a tower level waits for the next match.
    // towerLevel < 0 follows cardLevel (a real account's King level is near
    // its card level). Returns false and changes nothing on a bad value.
    bool setTeamLevel(int team, int cardLevel, int towerLevel = -1) {
        if (team != 0 && team != 1) return false;
        if (towerLevel < 0) towerLevel = cardLevel;
        if (!CardLevels::isValid(cardLevel) || !CardLevels::isValid(towerLevel)) return false;
        teamCardLevel[team] = cardLevel;
        teamTowerLevel[team] = towerLevel;
        board.cardLevel[team] = cardLevel;
        board.towerLevel[team] = towerLevel;
        return true;
    }
    int getTeamCardLevel(int team) const { return teamCardLevel[team == 1 ? 1 : 0]; }
    int getTeamTowerLevel(int team) const { return teamTowerLevel[team == 1 ? 1 : 0]; }

    Board& getBoard() { return board; }
    const Board& getBoard() const { return board; }
    const MatchStatistics& getStatistics() const { return stats; }

    // A fully independent copy of the match for decision-time search. Every
    // member is a value type and copies correctly (including rng, so two
    // snapshots of one position roll out identically, and lastPlayedTick, so a
    // rollout keeps the opponent's cycle), except `board` and `stats`, which
    // hold shared_ptrs and are replaced with deep copies.
    //
    // Copy-then-replace, so a new member joins the snapshot automatically.
    GameManager snapshot() const {
        GameManager copy(*this);
        copy.board = board.deepCopy();
        copy.stats = stats.snapshotFor(copy.board);
        return copy;
    }

    // The exact bounds isValidPlacement enforces, exposed so the bindings query
    // them instead of re-deriving them.
    float getMaxPlacementX() const {
        return static_cast<float>(board.getWidth() - 1);
    }

    // How far into its own half a non-spell, non-deploy-anywhere card can go;
    // the same for both teams in their own mirrored frame.
    float getOwnHalfMaxY() const {
        return board.getRiverStart() - OWN_HALF_RIVER_BUFFER;
    }

    float getElixirAI() const { return playerAI.elixir; }
    float getElixirOpp() const { return playerOpponent.elixir; }

    bool isGameOver() const { return gameOver; }
    int getLoserTeam() const { return loserTeam; }

    const std::vector<int>& getHand(int team) const {
        return (team == 0) ? playerAI.hand : playerOpponent.hand;
    }

    // Records an observed play without simulating one (see
    // ClashEnv::notePlayedCard for why this is separate from inject()).
    // getLastPlayedTick returns the tick of `team`'s last play of `cardId`, or
    // -1. Both ignore out-of-range ids: callers sweep the whole id range.
    // Both fold the id to faceCardIdOf (patch 0014): an Evolution or Hero play
    // is recorded under the base card the screen shows, which is all live
    // perception can report, so training and live write the same index.
    void notePlayedCard(int team, int cardId) {
        if (team != 0 && team != 1) return;
        if (cardId < 0 || cardId >= CARD_ID_COUNT) return;
        lastPlayedTick[team][faceCardIdOf(cardId)] = currentTick;
    }

    const std::vector<ObservedEvent>& getObservedEvents(int team) const {
        return observedEvents[(team == 1) ? 1 : 0];
    }
    const std::vector<int>& getDeckConfig(int team) const {
        return (team == 0) ? aiDeckConfig : oppDeckConfig;
    }

    int getLastPlayedTick(int team, int cardId) const {
        if (team != 0 && team != 1) return -1;
        if (cardId < 0 || cardId >= CARD_ID_COUNT) return -1;
        return lastPlayedTick[team][faceCardIdOf(cardId)];
    }

    float getElixir(int team) const {
        return (team == 0) ? playerAI.elixir : playerOpponent.elixir;
    }

    // --- state-estimator setters ---
    // Write a reconstructed live state into the simulator, so search evaluates
    // the real position rather than a reset one (reset() sets elixir to 5.0 and
    // deals a random hand). On GameManager rather than PlayerState because the
    // hand and the deck queue are one invariant: together a permutation of the
    // deck.
    void setElixir(int team, float value) {
        PlayerState& player = (team == 0) ? playerAI : playerOpponent;
        // The same [0, 10] clamp step() enforces.
        player.elixir = std::max(0.0f, std::min(value, PlayerState::MAX_ELIXIR));
    }

    // Returns false and changes nothing unless `cards` is a valid hand for this
    // team's deck: right size, no duplicates, every card in the deck. A
    // silently accepted misread would put the policy in a position the real
    // game is not in.
    bool setHand(int team, const std::vector<int>& cards) {
        PlayerState& player = (team == 0) ? playerAI : playerOpponent;
        if (cards.size() != player.hand.size()) return false;

        // The deck is the current hand plus the queue, so this stays correct
        // mid-match.
        std::vector<int> pool(player.hand.begin(), player.hand.end());
        pool.insert(pool.end(), player.deckQueue.begin(), player.deckQueue.end());

        std::vector<int> remaining = pool;
        for (int card : cards) {
            auto it = std::find(remaining.begin(), remaining.end(), card);
            if (it == remaining.end()) return false;   // not in the deck, or a duplicate
            remaining.erase(it);
        }

        player.hand.assign(cards.begin(), cards.end());
        // The queue keeps the remaining cards' order, so re-setting the current
        // hand is a no-op.
        player.deckQueue.assign(remaining.begin(), remaining.end());
        // Cooldowns are cleared: perception cannot see a slot's post-cycle
        // delay, and assuming none risks a play ~2 s early, while guessing one
        // would refuse a legal play.
        player.handCooldownTicks.assign(player.hand.size(), 0);
        return true;
    }

    // --- towers and clock (perception/UPSTREAM_REQUESTS.md item 22) ---
    // Slots are board coordinates, never team-relative: 0 = King, 1 = left
    // Princess, 2 = right, left/right judged against ArenaLayout's centre.
    const Tower* findTower(int team, int slot) const {
        for (const auto& e : board.getEntities()) {
            const Tower* t = dynamic_cast<const Tower*>(e.get());
            if (t == nullptr || t->team != team) continue;
            const bool isKing = (t->symbol == 'R');
            if (slot == 0 && isKing) return t;
            if (slot == 1 && !isKing && t->position.x < ArenaLayout::CENTER_X) return t;
            if (slot == 2 && !isKing && t->position.x > ArenaLayout::CENTER_X) return t;
        }
        return nullptr;
    }

    Tower* findTower(int team, int slot) {
        return const_cast<Tower*>(std::as_const(*this).findTower(team, slot));
    }

    // Returns false and changes nothing on a value the engine cannot hold. hp
    // <= 0 is refused rather than clamped: a 0-hp tower that still stands is
    // impossible, and killing one has side effects that belong to
    // destroyTower().
    //
    // Takes engine-absolute hp. Real accounts' tower levels differ per player,
    // so perception reports a fraction and multiplies by getTowerMaxHp().
    bool setTowerHp(int team, int slot, float hp) {
        Tower* t = findTower(team, slot);
        if (t == nullptr || !t->isAlive()) return false;
        if (hp <= 0.0f) return false;
        int want = static_cast<int>(hp + 0.5f);
        const int ceiling = t->getMaxHp();
        if (want > ceiling) want = ceiling;
        if (want < 1) want = 1;
        t->hp = want;
        // `awake` is not set here: Tower::update latches it on hp < maxHp, the
        // same path real damage takes.
        return true;
    }

    // The only way to express a fallen tower, since setTowerHp refuses hp <= 0.
    bool destroyTower(int team, int slot) {
        Tower* t = findTower(team, slot);
        if (t == nullptr || !t->isAlive()) return false;   // idempotent
        t->takeDamage(t->hp);
        // takeDamage, as a real killing blow, but CombatEntity's override can
        // absorb (shield, parry, dash). No tower has those today; the
        // postcondition keeps this a destruction if one ever does.
        if (t->isAlive()) t->hp = 0;
        return true;
    }

    // -1 for a slot that does not exist, never mistaken for a tower at zero.
    int getTowerHp(int team, int slot) const {
        const Tower* t = findTower(team, slot);
        return t == nullptr ? -1 : t->hp;
    }

    int getTowerMaxHp(int team, int slot) const {
        const Tower* t = findTower(team, slot);
        return t == nullptr ? -1 : t->getMaxHp();
    }

    // Clamped at zero only; ClashEnv::setCurrentTick applies the maxTicks
    // bound. Board's copy moves with it.
    void setCurrentTick(int tick) {
        currentTick = std::max(0, tick);
        board.currentTick = currentTick;
    }

    int getCurrentTick() const { return currentTick; }

    // `isRollingSpell` narrows the spell exemption for The Log and Barbarian
    // Barrel, and `ownSideOnly` narrows it further, to a troop's own side, for
    // Barbarian Barrel (see the end of this function).
    bool isValidPlacement(int team, float x, float y, bool isSpell, float placedRadius,
                          bool deployAnywhere = false, bool isRollingSpell = false,
                          bool ownSideOnly = false) const {
        float maxX = static_cast<float>(board.getWidth() - 1);
        float maxY = static_cast<float>(board.getHeight() - 1);
        if (x < 0.0f || x > maxX || y < 0.0f || y > maxY) return false;
        if (board.isBackRowDeadZone(x, y)) return false;

        if (!isSpell) {
            // Footprint against the board's physical edge
            // (Board::CELL_HALF_EXTENT): a body legal by its centre can still
            // hang off the arena. Spells are exempt; a spell's radius is an
            // area of effect, not a body.
            const float minEdge = -Board::CELL_HALF_EXTENT;
            const float maxEdgeX = maxX + Board::CELL_HALF_EXTENT;
            const float maxEdgeY = maxY + Board::CELL_HALF_EXTENT;
            if (x - placedRadius < minEdge || x + placedRadius > maxEdgeX ||
                y - placedRadius < minEdge || y + placedRadius > maxEdgeY) {
                return false;
            }

            // Deploy-anywhere cards (Miner, Goblin Drill) skip the own-half
            // rule but not the building check below.
            if (!deployAnywhere) {
                if (team == 0 && y > board.getRiverStart() - OWN_HALF_RIVER_BUFFER) return false;
                if (team == 1 && y < board.getRiverEnd() + OWN_HALF_RIVER_BUFFER) return false;
            }

            // Nothing may be placed on an existing building: the gap is its
            // radius plus the new card's footprint.
            for (const auto& entity : board.getEntities()) {
                float r = entity->getCollisionRadius();
                if (entity->isAlive() && r > 0.0f) {
                    float dx = x - entity->position.x;
                    float dy = y - entity->position.y;
                    float requiredDist = placedRadius + r;
                    if (dx*dx + dy*dy < requiredDist*requiredDist) return false;
                }
            }
        }

        // Rolling spells may be cast on their own half or the river band, no
        // further. From the bridge row The Log's 10.1 range reaches the enemy
        // Princess Tower's near edge, 9.00 away; from the own-half limit it
        // does not, which is why the bound is the river's end. A roller cast
        // deep in enemy territory rolls off the board. Mirrored for team 1.
        if (isSpell && isRollingSpell) {
            if (team == 0 && y > board.getRiverEnd()) return false;
            if (team == 1 && y < board.getRiverStart()) return false;
        }
        // Barbarian Barrel is cast on its own side only, the troop rule above,
        // with no river allowance (UPSTREAM_REQUESTS.md item 32n).
        if (isSpell && ownSideOnly) {
            if (team == 0 && y > board.getRiverStart() - OWN_HALF_RIVER_BUFFER) return false;
            if (team == 1 && y < board.getRiverEnd() + OWN_HALF_RIVER_BUFFER) return false;
        }
        return true;
    }

    bool playCard(int team, int targetCardId, float x, float y) {
        if (gameOver) return false;

        PlayerState& player = (team == 0) ? playerAI : playerOpponent;

        int handIndex = -1;
        for (int i = 0; i < static_cast<int>(player.hand.size()); ++i) {
            if (player.hand[i] == targetCardId) {
                handIndex = i;
                break;
            }
        }

        if (handIndex == -1) return false;

        const CardDefinition* cardDef = CardRegistry::getInstance().getCard(targetCardId);
        if (!cardDef) return false;

        // Mirror: legality, spawn and cost come from this team's last play (+1
        // elixir), reusing that card's own rules, and the copy is one level
        // above the team's (wiki Mirror). Fails if nothing has been played.
        // Evolved Mirror (180) plays as Mirror until its evolved play, which
        // copies the last card as its Evolution where it has one.
        bool isMirror = (targetCardId == MIRROR_CARD_ID || targetCardId == EVO_MIRROR_CARD_ID);
        // Spirit Empress: the form and cost follow the current elixir at play
        // time (an approximation; see SpiritEmpressForms.h). Both forms share a
        // footprint, so legality needs no substitution.
        bool isSpiritEmpress = (targetCardId == SPIRIT_EMPRESS_CARD_ID);
        const CardDefinition* effectiveDef = cardDef;
        float costOverride = -1.0f;
        if (isMirror) {
            effectiveDef = CardRegistry::getInstance().getCard(player.lastPlayedCardId);
            if (!effectiveDef) return false;
            // Mirror can duplicate a Champion or Hero; the tracking below
            // resolves off the spawned entity's cardId, so the copy is
            // trackable too.
            costOverride = effectiveDef->cost + 1.0f;
        } else if (isSpiritEmpress) {
            costOverride = (player.elixir >= 6.0f) ? 6.0f : 3.0f;
        }

        if (!isValidPlacement(team, x, y, effectiveDef->isSpell, effectiveDef->placementRadius,
                              effectiveDef->deployAnywhere, effectiveDef->rollRange > 0.0f,
                              effectiveDef->castOwnSideOnly)) return false;

        PlayerState::PlayCardResult result = player.playCard(handIndex, costOverride);
        if (result.cardId != -1) {
            size_t pendingBefore = board.pendingEntityCount();
            if (isSpiritEmpress) {
                bool flying = (costOverride >= 6.0f);
                CardFactories::spawn(flying ? spiritEmpressFlyingStats() : spiritEmpressGroundStats(), x, y, team, board);
            } else if (isMirror && result.useEvolvedForm) {
                // Evolved Mirror: the last card's Evolution, else its base.
                const CardDefinition* evo = CardRegistry::getInstance().getCard(evolutionIdOf(player.lastPlayedCardId));
                if (evo && evo->spawnEvolvedEntity) evo->spawnEvolvedEntity(x, y, team, board);
                else effectiveDef->spawnEntity(x, y, team, board);
            } else if (result.useEvolvedForm && effectiveDef->spawnEvolvedEntity) {
                effectiveDef->spawnEvolvedEntity(x, y, team, board);
            } else {
                effectiveDef->spawnEntity(x, y, team, board);
            }
            if (isMirror) {
                // +1 level on the bodies the copy spawns now. Bodies they spawn
                // later (death spawns, a spawner's units) take the team level.
                const int level = std::min(board.cardLevel[team] + 1, CardLevels::MAX_LEVEL);
                for (size_t i = pendingBefore; i < board.pendingEntityCount(); ++i) {
                    Entity& e = *board.getPendingEntity(i);
                    if (e.levelApplied || e.team != team) continue;
                    e.levelApplied = true;
                    e.scaleToLevel(level);
                }
            }
            float reportedCost = (costOverride >= 0.0f) ? costOverride : cardDef->cost;
            board.statsEvents.notifyCardPlayed({ team, result.cardId, reportedCost, x, y, currentTick });
            observedEvents[team].push_back({ currentTick, 0, result.cardId, reportedCost,
                                             result.useEvolvedForm, effectiveDef->isSpell });

            // Card-cycle tracking (perception/UPSTREAM_REQUESTS.md item 24).
            // Recorded here because every real play passes through this
            // function, whoever the caller.
            //
            // `result.cardId` is what left the hand: for a Mirror play, Mirror
            // itself (164), which is what has to cycle back round. Folded to
            // the face card, as in notePlayedCard.
            if (result.cardId >= 0 && result.cardId < CARD_ID_COUNT) {
                lastPlayedTick[team][faceCardIdOf(result.cardId)] = currentTick;
            }
            // A second Mirror replays what preceded the first, so Mirror never
            // becomes lastPlayedCardId.
            if (!isMirror) player.lastPlayedCardId = result.cardId;

            // Champion and Hero slot tracking for bodies that spawn with the
            // play; bodies a spell spawns later are adopted by step().
            trackSlotEntities(team, pendingBefore);

            return true;
        }
        return false;
    }

    // Champion and Hero slot tracking (PlayerState::ChampionSlotState): adopts
    // a pending Champion/Hero body of `team`, from pending index `fromPending`
    // on, as the tracked entity of the slot whose deck card it belongs to.
    // Matched off the body's own abilitySlotCardId, else its cardId, rather
    // than off what left the hand, so a Mirror copy is tracked like an original
    // and the ability belongs to the newest instance.
    //
    // playCard adopts the bodies spawned with the play. step() calls it with
    // `lateSpawnsOnly`, before each commit, for bodies a spell spawns later and
    // that say so (abilitySlotCardId): Hero Barbarian Barrel's Barbarian lands
    // ~17 ticks after the play, as a -47 helper, and was never tracked -- its
    // ability could not be activated (UPSTREAM_REQUESTS.md item 32a). Only
    // declared bodies, so a Clone copy (which carries isChampion but never a
    // slot: CombatEntity::becomeCloneCopy) is still never adopted.
    void trackSlotEntities(int team, size_t fromPending, bool lateSpawnsOnly = false) {
        PlayerState& player = (team == 0) ? playerAI : playerOpponent;
        const std::vector<int>& deckConfig = (team == 0) ? aiDeckConfig : oppDeckConfig;
        for (int slot : { 1, 2 }) {
            for (size_t i = fromPending; i < board.pendingEntityCount(); ++i) {
                auto ce = std::dynamic_pointer_cast<CombatEntity>(board.getPendingEntity(i));
                if (!ce || ce->team != team || !(ce->isChampion || ce->isHero)) continue;
                if (lateSpawnsOnly && ce->abilitySlotCardId == 0) continue;
                const int slotCard = (ce->abilitySlotCardId != 0) ? ce->abilitySlotCardId : ce->cardId;
                if (slotCard != deckConfig[slot]) continue;
                PlayerState::ChampionSlotState& slotState = player.championSlots[slot];
                ce->abilityCooldownRemaining = slotState.persistedCooldownRemaining;
                slotState.trackedEntityId = ce->id;
                break;
            }
        }
    }

    // Whether `team`'s Champion/Hero in `slot` has a live body, and its
    // ability cooldown left (the live body's, else the persisted one a redeploy
    // resumes). For the observation's own-ability section (item 46).
    bool isChampionDeployed(int team, int slot) const {
        return findChampionInSlot(team, slot) != nullptr;
    }
    int championCooldownRemaining(int team, int slot) const {
        if (auto champion = findChampionInSlot(team, slot)) return champion->abilityCooldownRemaining;
        const PlayerState& player = (team == 0) ? playerAI : playerOpponent;
        auto it = player.championSlots.find(slot);
        return it == player.championSlots.end() ? 0 : it->second.persistedCooldownRemaining;
    }

    // Whether `team`'s Champion in `slot` (1 = Heroic, 2 = Wild Card) could
    // activate now: deployed, off cooldown, uses left, affordable. The two
    // slots are independent.
    bool isChampionAbilityReady(int team, int slot = 1) const {
        auto champion = findChampionInSlot(team, slot);
        if (champion && champion->abilityEffect) {
            if (champion->abilityCooldownRemaining > 0) return false;
            if (champion->abilityUsesRemaining == 0) return false;
            const PlayerState& player = (team == 0) ? playerAI : playerOpponent;
            return player.elixir >= champion->abilityElixirCost;
        }
        // Nothing alive in the slot: fall back to the post-death path (false
        // for every card without one).
        return isPostDeathAbilityReady(team, slot);
    }

    // Activates the Champion's ability in `slot` (e.g. Mighty Miner's
    // "Explosive Escape"). Checks everything before deducting elixir, so a
    // failed call spends nothing.
    bool activateChampionAbility(int team, int slot = 1) {
        if (gameOver) return false;

        auto champion = findChampionInSlot(team, slot);
        if (champion && champion->abilityEffect) {
            if (champion->abilityCooldownRemaining > 0) return false;
            // Without this, a uses-limited ability with a zero cooldown would
            // keep charging elixir for a no-op activation.
            if (champion->abilityUsesRemaining == 0) return false;

            PlayerState& player = (team == 0) ? playerAI : playerOpponent;
            if (player.elixir < champion->abilityElixirCost) return false;

            player.elixir -= champion->abilityElixirCost;
            champion->activateAbility(board);
            board.statsEvents.notifyChampionAbilityActivated({ team, champion->cardId, champion->abilityElixirCost, currentTick });
            observedEvents[team].push_back({ currentTick, 1, getDeckConfig(team)[slot], champion->abilityElixirCost, false, false });
            return true;
        }

        // Post-death squad reactivation, reachable only when nothing in the
        // slot is alive. Readiness is confirmed before anything is spent.
        if (!isPostDeathAbilityReady(team, slot)) return false;

        PlayerState& player = (team == 0) ? playerAI : playerOpponent;
        PlayerState::ChampionSlotState& slotState = player.championSlots[slot];
        const std::vector<int>& deckConfig = (team == 0) ? aiDeckConfig : oppDeckConfig;
        const CardDefinition* slotDef = CardRegistry::getInstance().getCard(deckConfig[slot]);

        player.elixir -= slotDef->abilityElixirCost;
        slotDef->postDeathAbilityEffect->apply(board, slotState.lastSquadWipePosition, team);
        slotState.lastSquadWipeTick = -1; // consumed until the squad is redeployed and wiped again
        board.statsEvents.notifyChampionAbilityActivated({ team, deckConfig[slot], slotDef->abilityElixirCost, currentTick });
        observedEvents[team].push_back({ currentTick, 1, deckConfig[slot], slotDef->abilityElixirCost, false, false });
        return true;
    }

    void reset() {
        board = Board();
        for (int t = 0; t < 2; ++t) {
            board.cardLevel[t] = teamCardLevel[t];
            board.towerLevel[t] = teamTowerLevel[t];
        }
        // Right after replacing the board, whose subscriber list died with it:
        // fresh collectors before any event fires.
        stats.attach(board);
        currentTick = 0;
        gameOver = false;
        loserTeam = -1;
        chefCookProgress[0] = chefCookProgress[1] = 0.0f;
        // Cleared per match, or the agent would see cards the opponent has not
        // shown.
        for (auto& perTeam : lastPlayedTick) perTeam.fill(-1);
        for (auto& perTeam : observedEvents) perTeam.clear();

        std::string aiErr = validateDeckSlots(aiDeckConfig);
        if (!aiErr.empty()) throw std::invalid_argument("GameManager: invalid AI deck -- " + aiErr);
        std::string oppErr = validateDeckSlots(oppDeckConfig);
        if (!oppErr.empty()) throw std::invalid_argument("GameManager: invalid opponent deck -- " + oppErr);

        playerAI.initializeDeck(aiDeckConfig, rng);
        playerOpponent.initializeDeck(oppDeckConfig, rng);

        // Tower positions from ArenaLayout.h.
        addTower(ArenaLayout::CENTER_X, ArenaLayout::kingY(0), CrownTower::KING_HP, 0, 7.0f,
                 CrownTower::KING_DAMAGE, CrownTower::KING_COOLDOWN_TICKS, 'R', "King Tower");
        addTower(ArenaLayout::CENTER_X, ArenaLayout::kingY(1), CrownTower::KING_HP, 1, 7.0f,
                 CrownTower::KING_DAMAGE, CrownTower::KING_COOLDOWN_TICKS, 'R', "King Tower");

        // Princess Towers, from the configured Tower Troop.
        addTower(ArenaLayout::LEFT_LANE_X,  ArenaLayout::princessY(0), 0, "Princess Tower", towerTroopStats(aiTowerTroop));
        addTower(ArenaLayout::RIGHT_LANE_X, ArenaLayout::princessY(0), 0, "Princess Tower", towerTroopStats(aiTowerTroop));
        addTower(ArenaLayout::LEFT_LANE_X,  ArenaLayout::princessY(1), 1, "Princess Tower", towerTroopStats(oppTowerTroop));
        addTower(ArenaLayout::RIGHT_LANE_X, ArenaLayout::princessY(1), 1, "Princess Tower", towerTroopStats(oppTowerTroop));

        board.commitPendingEntities(currentTick);
    }

    void step() {
        if (gameOver) return;

        currentTick++;
        board.currentTick = currentTick;

        // Read the phase once, after the tick advances, and use it for both
        // players. oppElixirMultiplier multiplies on top: a 1.5x opponent in
        // double elixir gets 3x, as intended.
        const float elixirPhase = getElixirMultiplier();
        playerAI.elixir = std::min(playerAI.elixir + ELIXIR_REGEN_RATE * elixirPhase, PlayerState::MAX_ELIXIR);
        playerOpponent.elixir = std::min(
            playerOpponent.elixir + ELIXIR_REGEN_RATE * elixirPhase * oppElixirMultiplier, PlayerState::MAX_ELIXIR);
        playerAI.tick();
        playerOpponent.tick();

        // Before each commit: a Champion/Hero body spawned outside playCard
        // (by a spell, or by last tick's death effects) is adopted by its slot.
        trackSlotEntities(0, 0, /*lateSpawnsOnly=*/true);
        trackSlotEntities(1, 0, /*lateSpawnsOnly=*/true);
        board.commitPendingEntities(currentTick);

        for (auto& entity : board.getEntities()) {
            if (entity->isAlive()) entity->update(board);
        }
        cookRoyalChef(0);
        cookRoyalChef(1);

        // Elixir Collector: drain this tick's grants into the players, with the
        // same cap.
        playerAI.elixir = std::min(playerAI.elixir + board.pendingElixirGrant[0], PlayerState::MAX_ELIXIR);
        playerOpponent.elixir = std::min(playerOpponent.elixir + board.pendingElixirGrant[1], PlayerState::MAX_ELIXIR);
        board.pendingElixirGrant[0] = 0.0f;
        board.pendingElixirGrant[1] = 0.0f;

        trackSlotEntities(0, 0, /*lateSpawnsOnly=*/true);
        trackSlotEntities(1, 0, /*lateSpawnsOnly=*/true);
        board.commitPendingEntities(currentTick);
        board.resolveCollisions();

        // evaluateAtTick: from 3:00 a crown lead, or the first crown in
        // overtime, ends the match.
        MatchRules::Outcome outcome = MatchRules::evaluateAtTick(board, currentTick);
        // 5:00: overtime expires and the tower tiebreak decides, whatever the
        // caller's maxTicks. loserTeam -1 is the exact-tie draw.
        if (!outcome.over && currentTick >= MatchRules::MATCH_END_TICK) {
            outcome = TimeoutRules::resolve(board);
        }
        if (outcome.over) {
            gameOver = true;
            loserTeam = outcome.loserTeam;
            board.statsEvents.notifyMatchEnded({ loserTeam, currentTick });
        }

        // After the entity updates (so this tick's cooldown decrement is
        // captured) and before dead entities are erased (so a Champion that
        // died this tick persists its final cooldown).
        syncChampionCooldowns(0);
        syncChampionCooldowns(1);

        board.cleanDeadEntities(currentTick);
    }
};