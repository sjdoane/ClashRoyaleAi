#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "CardRegistry.h"
#include "ClashEnv.h"
#include <vector>
#include <algorithm>
#include "RangedBuildingTargeter.h"

// Patch 0014: the id range grows to CARD_ID_COUNT = 195, Minion Giant (176),
// and the opponent cycle block keyed by the card the screen shows.

namespace {

int idNamed(const std::string& name, bool evolution) {
    for (const auto& [id, def] : CardRegistry::getInstance().getAllCards()) {
        if (def.name == name && def.isEvolution == evolution) return id;
    }
    return -1;
}

constexpr int cycleBase() {
    return ClashEnv::BOARD_WIDTH * ClashEnv::BOARD_HEIGHT * ClashEnv::NUM_CHANNELS
         + 1 + ClashEnv::HAND_SIZE + ClashEnv::HAND_SIZE * ClashEnv::NUM_CARD_IDS
         + ClashEnv::NUM_EXTRA_SCALARS;
}

// Plays deck slot 0 as team 1 through stepSelfPlay -> GameManager::playCard.
void playSlot0AsOpponent(ClashEnv& env, const std::vector<int>& deck) {
    REQUIRE(env.setHandForTeam(1, { deck[0], deck[3], deck[4], deck[5] }));
    env.setElixirForTeam(1, 10.0f);
    env.stepSelfPlay(4, 0.0f, 0.0f, 0, 9.0f, 8.0f, 1);
}

}  // namespace

TEST_CASE("every registered id is inside [0, CARD_ID_COUNT)", "[card_registry][ids]") {
    int highest = -1;
    for (const auto& [id, def] : CardRegistry::getInstance().getAllCards()) {
        REQUIRE(id >= 0);
        REQUIRE(id < CARD_ID_COUNT);
        highest = std::max(highest, id);
    }
    REQUIRE(CARD_ID_COUNT == 195);
    REQUIRE(ClashEnv::NUM_CARD_IDS == CARD_ID_COUNT);
    REQUIRE(highest < CARD_ID_COUNT);
}

TEST_CASE("Minion Giant (176): flying, building-targeting, ranged, current balance stats",
        "[card_registry][minion_giant]") {
    const CardDefinition* def = CardRegistry::getInstance().getCard(176);
    REQUIRE(def != nullptr);
    REQUIRE(def->name == "Minion Giant");
    REQUIRE(def->cost == Catch::Approx(4.0f));
    REQUIRE_FALSE(def->isEvolution);
    REQUIRE_FALSE(def->isHero);

    Board board;
    def->spawnEntity(9.0f, 10.0f, 0, board);
    board.commitPendingEntities();
    auto mg = std::dynamic_pointer_cast<CombatEntity>(board.getEntities()[0]);
    REQUIRE(mg != nullptr);
    REQUIRE(mg->hp == 1817);
    REQUIRE(def->stats->damage == 153);         // 19 Oct 2026: 168 -> 153
    REQUIRE(mg->getAttackCooldown() == 17);   // 6 Oct 2026: 1.5 s -> 1.7 s
    REQUIRE(mg->getAttackRange() == Catch::Approx(4.0f));
    REQUIRE(mg->isFlying);
    REQUIRE(std::dynamic_pointer_cast<RangedBuildingTargeter>(mg) != nullptr);
}

TEST_CASE("faceCardIdOf folds Evolutions and Heroes to their base card", "[card_registry][ids][cycle]") {
    const int wallBreakers = idNamed("Wall Breakers", false);
    REQUIRE(wallBreakers >= 0);
    REQUIRE(faceCardIdOf(123) == wallBreakers);  // Evolution: same name as its base
    REQUIRE(faceCardIdOf(166) == idNamed("Knight", false));  // "Hero Knight"
    REQUIRE(faceCardIdOf(170) == 5);   // "Hero Mini P.E.K.K.A." -> "Mini PEKKA": dots ignored
    REQUIRE(faceCardIdOf(174) == 101); // Hero Barbarian Barrel
    REQUIRE(faceCardIdOf(15) == 15);   // a plain card is itself
    REQUIRE(faceCardIdOf(164) == 164); // Mirror
    REQUIRE(faceCardIdOf(117) == 117); // a Champion is itself
    REQUIRE(faceCardIdOf(-3) == -3);   // out of range: unchanged
    for (const auto& [id, def] : CardRegistry::getInstance().getAllCards()) {
        INFO(def.name << " (" << id << ")");
        const CardDefinition* face = CardRegistry::getInstance().getCard(faceCardIdOf(id));
        REQUIRE(face != nullptr);
        REQUIRE_FALSE(face->isEvolution);
        if (def.isEvolution || def.isHero) REQUIRE(faceCardIdOf(id) != id);
    }
}

TEST_CASE("an Evolution play lights its base card's cycle slot, as live perception does",
        "[cycle][evolution]") {
    const std::vector<int> deck = { 123, 6, 25, 40, 24, 72, 33, 7 };
    const int base = faceCardIdOf(123);
    ClashEnv env(deck, deck, 3600);
    env.reset();
    playSlot0AsOpponent(env, deck);

    const std::vector<float> obs = env.getObservationForTeam(0);
    REQUIRE(obs[cycleBase() + base] == 1.0f);
    REQUIRE(obs[cycleBase() + 123] == 0.0f);
    REQUIRE(obs[cycleBase() + ClashEnv::NUM_CARD_IDS + 123] == 0.0f);
    // The folding getter answers for either id, so pollers keyed by deck ids work.
    REQUIRE(env.getLastPlayedTick(1, 123) >= 0);
    REQUIRE(env.getLastPlayedTick(1, 123) == env.getLastPlayedTick(1, base));
}

TEST_CASE("a Hero play lights its base card's cycle slot", "[cycle][hero]") {
    const std::vector<int> deck = { 15, 166, 25, 40, 24, 72, 33, 7 };
    const int knight = faceCardIdOf(166);
    ClashEnv env(deck, deck, 3600);
    env.reset();
    REQUIRE(env.setHandForTeam(1, { 166, 25, 40, 24 }));
    env.setElixirForTeam(1, 10.0f);
    env.stepSelfPlay(4, 0.0f, 0.0f, 0, 9.0f, 8.0f, 1);

    const std::vector<float> obs = env.getObservationForTeam(0);
    REQUIRE(obs[cycleBase() + knight] == 1.0f);
    REQUIRE(obs[cycleBase() + 166] == 0.0f);
}

TEST_CASE("notePlayedCard (the live path) folds the same way", "[cycle]") {
    const std::vector<int> deck = { 15, 6, 25, 40, 24, 72, 33, 7 };
    ClashEnv env(deck, deck, 3600);
    env.reset();
    env.notePlayedCard(1, 123);
    const std::vector<float> obs = env.getObservationForTeam(0);
    REQUIRE(obs[cycleBase() + faceCardIdOf(123)] == 1.0f);
    REQUIRE(obs[cycleBase() + 123] == 0.0f);
}

TEST_CASE("getEvolutionCardIds lists exactly the Evolution ids, ascending", "[card_registry][ids]") {
    const std::vector<int> evo = getEvolutionCardIds();
    REQUIRE_FALSE(evo.empty());
    REQUIRE(std::is_sorted(evo.begin(), evo.end()));
    for (int id : evo) REQUIRE(CardRegistry::getInstance().getCard(id)->isEvolution);
    const std::vector<int> all = getAllCardIds();
    for (int id : evo) REQUIRE(std::find(all.begin(), all.end(), id) == all.end());
}
