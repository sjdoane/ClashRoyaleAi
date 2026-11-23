#include <catch_amalgamated.hpp>
#include "ClashEnv.h"
#include <vector>
#include <algorithm>
#include <random>

// The observation size, asserted both ways:
//
//   18*34*21 + 1 + 4 + 4*195 + 10 + 2*195 + 4 + 418 = 14459
//
//   * the formula catches observationSize() disagreeing with the constants it is built from, which would split the flat vector at the wrong offset and misread every scalar silently;
//   * the literal catches the size changing at all. A resize invalidates every checkpoint, so it must never happen by accident; when intended, updating the literal is the acknowledgement.
//
// Champion support does not grow the vector; abilities are exposed through
// accessors.

TEST_CASE("ClashEnv::observationSize matches its declared layout", "[clash_env]") {
    std::vector<int> deck = { 0, 1, 2, 3, 4, 5, 6, 7 };
    ClashEnv env(deck, deck, 100);

    const int expected =
        ClashEnv::BOARD_WIDTH * ClashEnv::BOARD_HEIGHT * ClashEnv::NUM_CHANNELS
        + 1                                              // own elixir
        + ClashEnv::HAND_SIZE                            // hand costs
        + ClashEnv::HAND_SIZE * ClashEnv::NUM_CARD_IDS   // hand identity one-hots
        + ClashEnv::NUM_EXTRA_SCALARS                    // time, elixir spent, tower HP
        + ClashEnv::CYCLE_BLOCK_SIZE                     // opponent seen[] + recency[]
        + ClashEnv::NUM_LEVEL_SCALARS                    // card/tower levels (item 44)
        + ClashEnv::OPP_STATE_SIZE;                      // opponent state (item 46)

    REQUIRE(env.observationSize() == expected);
    REQUIRE(env.observationSize() == 14459);
    REQUIRE(ClashEnv::LEVEL_START == ClashEnv::CYCLE_START + ClashEnv::CYCLE_BLOCK_SIZE);
    REQUIRE(ClashEnv::OPP_STATE_START == ClashEnv::LEVEL_START + ClashEnv::NUM_LEVEL_SCALARS);
    REQUIRE(ClashEnv::OPP_STATE_START == 14041);
    REQUIRE(ClashEnv::OPP_STATE_SIZE == 418);

    // The CYCLE_START literal is a tripwire: +1 means a scalar was appended to
    // the extra block; a larger jump means something was inserted earlier. The
    // relationship below is the invariant.
    REQUIRE(ClashEnv::CYCLE_START == 13647);
    REQUIRE(ClashEnv::EXTRA_SCALARS_START + ClashEnv::NUM_EXTRA_SCALARS
            == ClashEnv::CYCLE_START);

    auto obs = env.reset();
    REQUIRE(obs.size() == static_cast<size_t>(expected));

    // Guards the split point the network uses: a disagreement shifts every
    // scalar it reads, silently.
    const int spatial = ClashEnv::BOARD_WIDTH * ClashEnv::BOARD_HEIGHT * ClashEnv::NUM_CHANNELS;
    REQUIRE(env.observationSize() - spatial
            == 1 + ClashEnv::HAND_SIZE + ClashEnv::HAND_SIZE * ClashEnv::NUM_CARD_IDS
               + ClashEnv::NUM_EXTRA_SCALARS + ClashEnv::CYCLE_BLOCK_SIZE
               + ClashEnv::NUM_LEVEL_SCALARS + ClashEnv::OPP_STATE_SIZE);
    // ...and the spatial block ends exactly where EXTRA_SCALARS_START says.
    REQUIRE(ClashEnv::EXTRA_SCALARS_START - spatial
            == 1 + ClashEnv::HAND_SIZE + ClashEnv::HAND_SIZE * ClashEnv::NUM_CARD_IDS);
}

TEST_CASE("isChampionAbilityReady/activateChampionAbility return false with nothing deployed", "[clash_env][champion]") {
    std::vector<int> deck = { 0, 1, 2, 3, 4, 5, 6, 7 }; // no Champion in this deck
    ClashEnv env(deck, deck, 100);
    env.reset();

    REQUIRE_FALSE(env.isChampionAbilityReady(0));
    REQUIRE_FALSE(env.activateChampionAbility(0));
}

TEST_CASE("step()'s activateAbility param defaults to false and only fires when explicitly true", "[clash_env][champion]") {
    // Mighty Miner in deck slot 1 (Champions are legal only in slots 1 and 2).
    // The opening hand is random, so he is forced into hand index 1 through
    // debugGame().
    std::vector<int> deck = { 1, 115, 2, 3, 4, 5, 6, 7 };
    ClashEnv env(deck, deck, 100);
    env.reset();
    env.debugGame().playerAI.hand[1] = 115;

    env.step(1, 9.0f, 10.0f, 1); // deploy Mighty Miner; activateAbility defaults to false
    REQUIRE_FALSE(env.isChampionAbilityReady(0)); // still deploying (patch 0080)
    env.step(-1, 0.0f, 0.0f, 9); // the rest of his 1 s deploy time
    REQUIRE(env.isChampionAbilityReady(0)); // deployed, off cooldown, affordable

    env.step(-1, 0.0f, 0.0f, 1); // no card, no ability -- default stays false
    REQUIRE(env.isChampionAbilityReady(0)); // still ready: nothing consumed it

    env.step(-1, 0.0f, 0.0f, 1, true); // explicitly activate slot 1's ability now
    REQUIRE_FALSE(env.isChampionAbilityReady(0)); // the one charge is spent
}

TEST_CASE("sampleRandomDeck always produces a deck that passes validateDeckSlots", "[clash_env][random_deck]") {
    std::mt19937 rng(2024);
    for (int trial = 0; trial < 300; ++trial) {
        std::vector<int> deck = sampleRandomDeck(rng);
        REQUIRE(deck.size() == 8);
        REQUIRE(validateDeckSlots(deck).empty());

        // A real deck cannot repeat a card; neither may sampleRandomDeck.
        std::vector<int> sorted = deck;
        std::sort(sorted.begin(), sorted.end());
        REQUIRE(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
    }
}


TEST_CASE("each hand slot's card-identity block is a true one-hot", "[clash_env][observation]") {
    // The one-hot block is resize-and-set, equivalent to per-id push_backs only
    // while it is zeroed and exactly one index is written; an out-of-range id
    // must leave it empty.
    ClashEnv env({ 15, 6, 25, 40, 24, 72, 33, 7 }, { 15, 6, 25, 40, 24, 72, 33, 7 });
    env.reset();

    const std::vector<float> obs = env.getObservationForTeam(0);
    const std::vector<int> hand = env.getHand();

    constexpr int BOARD_W = 18, BOARD_H = 34, CHANNELS = 21, HAND = 4, CARD_IDS = 195;
    const size_t oneHotBase = static_cast<size_t>(BOARD_W) * BOARD_H * CHANNELS + 1 + HAND;

    REQUIRE(hand.size() == static_cast<size_t>(HAND));
    for (int slot = 0; slot < HAND; ++slot) {
        const size_t base = oneHotBase + static_cast<size_t>(slot) * CARD_IDS;
        int hotCount = 0, hotIndex = -1;
        for (int k = 0; k < CARD_IDS; ++k) {
            if (obs[base + k] != 0.0f) { hotCount++; hotIndex = k; }
        }
        INFO("hand slot " << slot << " holds card " << hand[slot]);
        REQUIRE(hotCount == 1);
        REQUIRE(hotIndex == hand[slot]);
        REQUIRE(obs[base + hotIndex] == Catch::Approx(1.0f));
    }
}

// --- damage to a spawned body is not tower damage (UPSTREAM item 28) ---
// Spawned helper bodies carry unregistered negative ids, as towers do, so a
// tower shooting them must not be booked as tower damage. That stat feeds the
// agent's tower potential and the teacher's rollout.
namespace {
int towerHpTotal(const ClashEnv& env, int team) {
    int total = 0;
    for (int slot = 0; slot < 3; ++slot) total += static_cast<int>(env.getTowerHp(team, slot));
    return total;
}
}

TEST_CASE("a tower shooting SPAWNED bodies books no tower damage for its owner",
          "[stats][tower_damage][spawned]") {
    const std::vector<int> deck = { 109, 110, 112, 81, 24, 72, 33, 7 };
    for (int cardId : { 109, 110, 112, 81 }) {   // Goblin Barrel, Graveyard, Goblin Gang, Battle Ram
        ClashEnv env(deck, { 15, 6, 25, 40, 24, 72, 33, 7 }, 3600);
        env.seed(3);
        const int team0Before = towerHpTotal(env, 0);
        env.inject(cardId, 3.0f, 25.0f, 0, -1.0f, 0);
        const int noop = ClashEnv::HAND_SIZE;
        for (int t = 0; t < 30; ++t) env.stepSelfPlay(noop, 0, 0, noop, 0, 0, 10);
        INFO("card " << cardId);
        // Precondition: team 0's towers were never hit, so any team-1 tower
        // damage is misclassified.
        REQUIRE(towerHpTotal(env, 0) == team0Before);
        CHECK(env.getTowerDamageDealt(1) == 0);
    }
}

TEST_CASE("CONTROL: a tower hit is still booked as tower damage",
          "[stats][tower_damage][spawned]") {
    // Must fire, or the case above passes on a collector that books nothing.
    ClashEnv env({ 15, 6, 25, 40, 24, 72, 33, 7 }, { 15, 6, 25, 40, 24, 72, 33, 7 }, 3600);
    env.seed(3);
    const int team1Before = towerHpTotal(env, 1);
    env.inject(15, 3.0f, 22.0f, 0, -1.0f, 0);     // Hog Rider at the enemy left Princess
    const int noop = ClashEnv::HAND_SIZE;
    for (int t = 0; t < 30; ++t) env.stepSelfPlay(noop, 0, 0, noop, 0, 0, 10);
    REQUIRE(towerHpTotal(env, 1) < team1Before);
    CHECK(env.getTowerDamageDealt(0) > 0);
}

TEST_CASE("each tower reads hp over ITS OWN max at any level, and the levels are observed", "[clash_env][level]") {
    // UPSTREAM item 44. Under hp / MAX_BUILDING_HP a full level-15 King read
    // 1.45 and a level-11 Princess 0.63; live perception reports own-max
    // fractions, so the two disagreed by a per-level factor.
    std::vector<int> deck = { 0, 1, 2, 3, 4, 5, 6, 7 };
    for (int level : { 11, 13, 15 }) {
        ClashEnv env(deck, deck, 3000);
        REQUIRE(env.setTeamLevel(1, level));
        env.seed(3);
        auto obs = env.getObservationForTeam(0);
        const int e = ClashEnv::EXTRA_SCALARS_START;
        for (int k = 3; k < 9; ++k) REQUIRE(obs[e + k] == Catch::Approx(1.0f));

        // A quarter off every enemy tower reads 0.75, whatever its level.
        for (int slot = 0; slot < 3; ++slot) {
            REQUIRE(env.setTowerHp(1, slot, env.getTowerMaxHp(1, slot) * 3 / 4));
        }
        obs = env.getObservationForTeam(0);
        for (int k = 6; k < 9; ++k) REQUIRE(obs[e + k] == Catch::Approx(0.75f).margin(1e-3));

        // The building channel's tower cells follow the same convention: the
        // enemy King's cell (channel 7) is 0.75 too, never above 1.
        float maxCell = 0.0f;
        for (int i = 7 * ClashEnv::BOARD_WIDTH * ClashEnv::BOARD_HEIGHT;
             i < 8 * ClashEnv::BOARD_WIDTH * ClashEnv::BOARD_HEIGHT; ++i) {
            maxCell = std::max(maxCell, obs[i]);
        }
        REQUIRE(maxCell == Catch::Approx(0.75f).margin(1e-3));

        // Level section: own 11 reads 0, the opponent (level - 11) / 5.
        const int l = ClashEnv::LEVEL_START;
        REQUIRE(obs[l + 0] == Catch::Approx(0.0f));
        REQUIRE(obs[l + 1] == Catch::Approx(0.0f));
        REQUIRE(obs[l + 2] == Catch::Approx((level - 11) / 5.0f));
        REQUIRE(obs[l + 3] == Catch::Approx((level - 11) / 5.0f));

        // Team 1's view is the mirror: its own levels in slots 0-1.
        auto obs1 = env.getObservationForTeam(1);
        REQUIRE(obs1[l + 0] == Catch::Approx((level - 11) / 5.0f));
        REQUIRE(obs1[l + 2] == Catch::Approx(0.0f));
    }
}
