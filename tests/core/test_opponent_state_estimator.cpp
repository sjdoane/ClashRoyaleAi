#include <catch_amalgamated.hpp>
#include "ClashEnv.h"
#include "OpponentStateEstimator.h"
#include <cmath>
#include <random>
#include <vector>

// The opponent-state section (perception/UPSTREAM_REQUESTS.md item 46) and the
// estimator behind it. These pin:
//   - with the detector exact, the estimate IS the opponent's bar, a whole
//     match long (any drift means the estimator's elixir rules differ from
//     GameManager's);
//   - a cost the model says is unaffordable re-anchors instead of clamping;
//   - a late, back-dated report gives the same answer as a timely one;
//   - the cap washes out the miss prior;
//   - the evolution counter follows PlayerState's own rule;
//   - the section sits at OPP_STATE_START and a snapshot keeps it.

namespace {
const std::vector<int> DECK = { 125, 168, 129, 15, 40, 72, 33, 7 };  // live_ppo TRAIN_DECK
using OSE = OpponentStateEstimator;
}

TEST_CASE("Opponent-state estimate equals the true bar when the detector is exact",
          "[opp_state]") {
    ClashEnv env(DECK, DECK, 3000);
    ClashEnv::DetectionNoise exact;
    exact.enabled = false;
    env.setOppDetectionNoise(exact);
    env.seed(5);
    env.reset();
    std::mt19937 rng(9);
    int plays = 0;
    float worst = 0.0f;
    for (int t = 0; t < 300; ++t) {
        const int idx = (rng() % 3 == 0) ? static_cast<int>(rng() % 4) : -1;
        auto r = env.step(idx, 3.0f + static_cast<float>(rng() % 12), 3.0f + static_cast<float>(rng() % 10),
                          10, env.isChampionAbilityReady(0, 1), false);
        const auto& est = env.getOppStateEstimator(0);
        worst = std::max(worst, std::fabs(est.estimate() - env.getElixirForTeam(1)));
        plays = est.plays();
        if (r.done) break;
    }
    REQUIRE(plays > 10);
    REQUIRE(worst < 1e-3f);
}

TEST_CASE("A play the model cannot afford re-anchors the estimate, then confidence recovers",
          "[opp_state]") {
    OSE est;
    est.reportPlay(0, 26, 5.0f);          // spends the opening 5
    REQUIRE(est.estimate() == Catch::Approx(0.0f).margin(1e-4));
    est.advanceTo(10, 1.0f);              // +0.36
    est.reportPlay(10, -1, 6.0f);         // 6 > hi: a missed income or a false report
    REQUIRE(est.contradictions() == 1);
    REQUIRE(est.estimate() >= 0.0f);
    REQUIRE(est.upper() > 0.0f);          // hi widened by the surprise, not clamped to 0
    const float c0 = est.confidence();
    REQUIRE(c0 < 0.6f);
    est.advanceTo(1500, 1.0f);            // long idle: the cap re-anchors
    REQUIRE(est.estimate() == Catch::Approx(PlayerState::MAX_ELIXIR));
    REQUIRE(est.confidence() > 0.95f);
}

TEST_CASE("A play costing more than the estimate raises it to the cost (a lower bound)",
          "[opp_state]") {
    OSE::Config cfg;
    cfg.assumedMissRate = 0.3f;           // a heavy miss prior drags est below hi
    OSE est(cfg);
    for (int k = 0; k < 4; ++k) {
        est.advanceTo(30 * (k + 1), 1.0f);
        est.reportPlay(30 * (k + 1), 26, 1.0f);
    }
    REQUIRE(est.estimate() < est.hardUpper() - 0.5f);
    const float before = est.estimate();
    est.advanceTo(150, 1.0f);
    const float bar = est.estimate();
    const float cost = std::min(est.hardUpper(), bar + 1.0f);
    est.reportPlay(150, -1, cost);
    REQUIRE(est.contradictions() == 0);
    // It had at least `cost`, so after paying it the bar is >= 0 and the
    // unseen spend was smaller than assumed: est is above where the prior had
    // it net of the cost.
    REQUIRE(est.estimate() >= std::max(0.0f, bar - cost) - 1e-4f);
    REQUIRE(before < bar + 1e-4f);
}

TEST_CASE("A back-dated report replays to the same state as a timely one", "[opp_state]") {
    OSE a, b;
    a.advanceWithSchedule(100);
    a.reportPlay(100, 26, 3.0f);
    a.advanceWithSchedule(160);
    b.advanceWithSchedule(160);           // the detector emitted it 6 s late
    b.reportPlay(100, 26, 3.0f);
    REQUIRE(a.estimate() == Catch::Approx(b.estimate()).margin(1e-4));
    REQUIRE(a.sigma() == Catch::Approx(b.sigma()).margin(1e-4));
    // Back-dating matters near the cap: at 9.5 a play reported late but
    // dated 30 ticks back still debits the regen it would have missed.
    OSE c, d;
    c.advanceWithSchedule(130); c.reportPlay(130, -1, 2.0f); c.advanceWithSchedule(200);
    d.advanceWithSchedule(200); d.reportPlay(130, -1, 2.0f);
    REQUIRE(c.estimate() == Catch::Approx(d.estimate()).margin(1e-4));
}

TEST_CASE("The Evolution counter follows PlayerState's rule once an evolved play is seen",
          "[opp_state][evolution]") {
    const CardDefinition* evo = CardRegistry::getInstance().getCard(125);
    REQUIRE(evo);
    REQUIRE(evo->isEvolution);
    const int base = OSE::baseIdOf(125);
    REQUIRE(base != 125);
    REQUIRE(OSE::evolutionIdOf(base) == 125);

    OSE est;
    est.reportPlay(0, base, -1.0f, 0);    // plain, evolution not yet known
    REQUIRE(est.evoChargeFor(125) == Catch::Approx(-1.0f));
    est.advanceTo(100, 1.0f);
    est.reportPlay(100, 125, -1.0f, 1);   // seen evolved: known from now on
    REQUIRE(est.cyclesUntilEvolvedFor(125) == evo->evolutionCycleThreshold);
    REQUIRE(est.evoChargeFor(125) == Catch::Approx(0.0f));
    for (int k = 0; k < evo->evolutionCycleThreshold; ++k) {
        est.advanceTo(200 + 100 * k, 1.0f);
        est.reportPlay(200 + 100 * k, base, -1.0f, 0);
    }
    REQUIRE(est.evoChargeFor(125) == Catch::Approx(1.0f));  // next deployment evolved
}

TEST_CASE("The opponent's Evolution charge in the observation matches the opponent's own",
          "[opp_state][evolution]") {
    ClashEnv env(DECK, DECK, 3000);
    ClashEnv::DetectionNoise exact;
    exact.enabled = false;
    env.setOppDetectionNoise(exact);
    env.seed(2);
    env.reset();
    const int S = ClashEnv::OPP_STATE_START;
    int checked = 0;
    for (int t = 0; t < 600 && checked < 20; ++t) {
        // Team 1 plays an Evolution whenever one is in hand (else cycles slot
        // 0); team 0 idles.
        int idx = 0;
        const auto handNow = env.getHandForTeam(1);
        for (int i = 0; i < ClashEnv::HAND_SIZE; ++i)
            if (CardRegistry::getInstance().getCard(handNow[i])->isEvolution) { idx = i; break; }
        env.setElixirForTeam(1, PlayerState::MAX_ELIXIR);
        auto r = env.stepSelfPlay(-1, 0, 0, idx, 9.0f, 10.0f, 10, false, false, false, false);
        const auto o0 = env.getObservationForTeam(0);
        const auto o1 = env.getObservationForTeam(1);
        const auto& hand1 = env.getHandForTeam(1);
        for (int i = 0; i < ClashEnv::HAND_SIZE; ++i) {
            if (o1[S + ClashEnv::OWN_EVO_OFFSET + 2 * i] != 1.0f) continue;
            const int c = hand1[i];
            if (o0[S + ClashEnv::OPP_EVO_OFFSET + c] != 1.0f) continue;   // not yet seen evolved
            REQUIRE(o0[S + ClashEnv::OPP_EVO_OFFSET + ClashEnv::NUM_CARD_IDS + c]
                    == Catch::Approx(o1[S + ClashEnv::OWN_EVO_OFFSET + 2 * i + 1]));
            ++checked;
        }
        if (r.done) break;
    }
    REQUIRE(checked > 0);
}

TEST_CASE("The opponent-state section is appended last and a snapshot keeps it", "[opp_state]") {
    ClashEnv env(DECK, DECK, 3000);
    env.seed(4);
    env.reset();
    for (int t = 0; t < 60; ++t) env.stepSelfPlay(t % 4, 9.0f, 5.0f, t % 4, 9.0f, 5.0f, 10, false, false, false, false);
    const auto obs = env.getObservationForTeam(0);
    REQUIRE(static_cast<int>(obs.size()) == ClashEnv::OPP_STATE_START + ClashEnv::OPP_STATE_SIZE);
    const int S = ClashEnv::OPP_STATE_START;
    // own hand Evolution flags read the hand
    const auto& hand = env.getHandForTeam(0);
    for (int i = 0; i < ClashEnv::HAND_SIZE; ++i) {
        const bool isEvo = CardRegistry::getInstance().getCard(hand[i])->isEvolution;
        REQUIRE(obs[S + ClashEnv::OWN_EVO_OFFSET + 2 * i] == (isEvo ? 1.0f : 0.0f));
    }
    // Hero Musketeer is deck slot 1: the own-ability slot says so
    REQUIRE(obs[S + ClashEnv::OWN_ABILITY_OFFSET] == 1.0f);
    // the elixir estimate is a fraction of the cap with a confidence in [0, 1]
    REQUIRE(obs[S + ClashEnv::OPP_ELIXIR_OFFSET] >= 0.0f);
    REQUIRE(obs[S + ClashEnv::OPP_ELIXIR_OFFSET] <= 1.0f);
    REQUIRE(obs[S + ClashEnv::OPP_ELIXIR_OFFSET + 3] >= 0.0f);
    REQUIRE(obs[S + ClashEnv::OPP_ELIXIR_OFFSET + 3] <= 1.0f);

    ClashEnv copy = env.snapshot();
    REQUIRE(copy.getObservationForTeam(0) == obs);
    // and the copy's detector continues identically
    env.stepSelfPlay(0, 9.0f, 5.0f, 0, 9.0f, 5.0f, 10, false, false, false, false);
    copy.stepSelfPlay(0, 9.0f, 5.0f, 0, 9.0f, 5.0f, 10, false, false, false, false);
    REQUIRE(copy.getObservationForTeam(0) == env.getObservationForTeam(0));
}
