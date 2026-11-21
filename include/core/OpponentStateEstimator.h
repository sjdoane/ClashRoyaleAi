#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "CardRegistry.h"
#include "GameManager.h"
#include "PlayerState.h"

// What one player can know about the OTHER player's hidden state from watching
// the screen: elixir, Evolution charge and Champion/Hero ability cooldowns
// (perception/UPSTREAM_REQUESTS.md item 46).
//
// ONE implementation for training and play. ClashEnv feeds it the engine's play
// and ability events through a simulated detector (DetectionNoise, below);
// live perception feeds it its own detections through the bindings
// (clash_royale_env.OpponentStateEstimator). The policy therefore learns how
// much to trust the estimate under the errors it will meet live, instead of
// training on truth and playing on a noisy number.
//
// ELIXIR is a small filter around an upper bound, not a point:
//
//   hi    the bar if every report is right and nothing was missed. Missed plays
//         only LOWER the truth, so hi stays an upper bound under misses.
//   est   hi minus the expected unseen spend (bias): each reported play stands
//         for p/(1-p) unreported ones (p = assumedMissRate) at the mean cost of
//         this opponent's identified plays.
//   sigma uncertainty of est, in elixir; [lower, upper] = est -/+ 2 sigma,
//         upper capped by hi. confidence = exp(-sigma / 1.5), lowered for a
//         while after a contradiction. There is no hard floor: a missed play
//         can have spent anything (an earlier hard floor pinned est to hi and
//         erased the miss prior: OPP_STATE_REPORT.md).
//
// Three things re-anchor it:
//   - a play of cost c proves the bar was >= c: an est below c is raised to c
//     and sigma shrunk to match (the unseen spend was smaller than assumed);
//   - the cap: while hi sits at 10 the truth keeps regenerating underneath, so
//     every discarded drop of hi absorbs a drop of bias and sigma;
//   - a contradiction: a play costing more than hi proves the model short
//     (a false or double report, a misread cost, unmodelled income). Instead of
//     clamping at 0 the bar is re-anchored at the cost just paid and hi is
//     widened by the size of the surprise.
//
// Reports may arrive late and back-dated (live emits a troop 1.5 s after its
// birth, an Archer Queen ~4.5 s): a report older than the last advance is
// inserted in tick order and the recent window is replayed, so a late report
// gives the same answer as a timely one.
//
// EVOLUTIONS: an opponent's card is known to be an Evolution only once an
// evolved deployment of it is seen (the real screen shows nothing before); from
// then on the PlayerState rule (evolutionCycleThreshold un-evolved plays, then
// evolvedUsesGranted evolved ones) predicts the next deployment. A play the
// count says is evolved but was reported plain is taken as evolved (a missed
// recognition), since a missed play makes the count lag, never lead.
//
// ABILITIES: a seen activation starts that card's registry cooldown and debits
// its abilityElixirCost.
class OpponentStateEstimator {
public:
    static constexpr int MAX_TRACKED_ABILITY_CARDS = 2;
    static constexpr int ABILITY_FEATURES = 4;
    static constexpr int ELIXIR_FEATURES = 4;
    // Recency time constant of a seen activation, the cycle blocks' 200 ticks.
    static constexpr float ABILITY_RECENCY_TAU_TICKS = 200.0f;
    // How far back a late report may land and still be replayed exactly.
    static constexpr int REPLAY_WINDOW_TICKS = 150;
    // A cost this far above hi counts as a contradiction rather than rounding.
    static constexpr float CONTRADICTION_TOLERANCE = 0.15f;
    static constexpr float CONFIDENCE_SIGMA_SCALE = 1.5f;
    static constexpr float CONTRADICTION_PENALTY = 0.5f;
    static constexpr float CONTRADICTION_RECOVERY_TICKS = 300.0f;
    static constexpr float INTERVAL_SIGMAS = 2.0f;

    // Elixir rules, from the engine, never restated: PlayerState owns the cap
    // and the opening bar, GameManager the regen and its schedule.
    struct Config {
        float regenPerTick = GameManager::ELIXIR_REGEN_RATE;
        // Expected fraction of the opponent's plays the detector never reports.
        float assumedMissRate = 0.04f;
        // Mean cost of a missed play before any play is seen; after that the
        // mean of the opponent's identified plays, shrunk toward this by
        // missedCostPriorWeight plays.
        float missedPlayMeanCost = 3.0f;
        float missedCostPriorWeight = 4.0f;
        // Cost range assumed for a play whose card could not be identified.
        float unknownCostMin = 1.0f;
        float unknownCostMean = 3.5f;
        float unknownCostMax = 6.0f;
    };

    // One detector report. tick is when the play happened (back-dated where
    // the detector knows the birth), not when it was emitted.
    struct Report {
        int tick = 0;
        int kind = 0;          // KIND_PLAY or KIND_ABILITY
        int cardId = -1;       // as recognised: an evolution id if seen evolved, else the base id; -1 unknown
        float cost = -1.0f;    // < 0: unknown (taken from the card, or the unknown-cost prior)
        int evolved = -1;      // 1 seen evolved, 0 seen plain, -1 not judged
        long seq = 0;          // arrival order, tie-break inside a tick
    };
    static constexpr int KIND_PLAY = 0;
    static constexpr int KIND_ABILITY = 1;

    struct EvoState {
        int evoId = -1;
        int baseId = -1;
        int threshold = 0;
        int granted = 1;
        int cyclesUntilEvolved = 0;
        int evolvedUsesRemaining = 1;
    };

    struct AbilityState {
        int cardId = -1;
        int lastActivationTick = -1;
        int cooldownTicks = 0;
        float cost = 0.0f;
    };

    struct Core {
        int tick = 0;
        float hi = PlayerState::STARTING_ELIXIR;
        float bias = 0.0f;
        float knownCostSum = 0.0f;
        int knownCostPlays = 0;
        float sigma = 0.0f;
        int contradictions = 0;
        int lastContradictionTick = -1;
        float lastSurprise = 0.0f;
        int plays = 0;
        int unknownPlays = 0;
        std::vector<EvoState> evos;
        std::array<AbilityState, MAX_TRACKED_ABILITY_CARDS> abilities{};
    };

    OpponentStateEstimator() { reset(); }
    explicit OpponentStateEstimator(const Config& c) : cfg(c) { reset(); }

    // A match starting now with the opening bar known (the usual case).
    void reset(int tick = 0) {
        cur = Core{};
        cur.tick = tick;
        checkpoint = cur;
        segments.clear();
        events.clear();
        nextSeq = 0;
    }

    // Joined mid-match with the bar unknown: hi 10, est 5, wide sigma.
    void resetUnknown(int tick) {
        reset(tick);
        cur.hi = PlayerState::MAX_ELIXIR;
        cur.bias = PlayerState::MAX_ELIXIR - PlayerState::STARTING_ELIXIR;
        cur.sigma = 3.0f;
        checkpoint = cur;
    }

    // Seeds an Evolution the observer knows from the start (its OWN deck), at
    // the opening count PlayerState::seedSlotState uses.
    void declareEvolution(int evoId) {
        const CardDefinition* def = CardRegistry::getInstance().getCard(evoId);
        if (!def || !def->isEvolution) return;
        if (findEvo(cur, evoId)) return;
        cur.evos.push_back(makeEvo(*def));
        // Part of the state from the start, so a replay keeps it.
        if (!findEvo(checkpoint, evoId)) checkpoint.evos.push_back(makeEvo(*def));
    }

    // Regenerates to `tick` at one multiplier for the whole interval. The
    // multiplier for the step INTO tick t is the schedule's value at t, as in
    // GameManager::step; callers split at phase boundaries (ClashEnv does).
    void advanceTo(int tick, float multiplier) {
        if (tick <= cur.tick) return;
        segments.push_back({ cur.tick, tick, multiplier });
        regen(cur, tick - cur.tick, multiplier);
        cur.tick = tick;
        trim();
    }

    // advanceTo on the engine's own schedule (GameManager::elixirMultiplierAtTick),
    // split at each phase boundary so a long gap is exact. ClashEnv and live
    // perception both advance with this, from the match clock.
    void advanceWithSchedule(int tick) {
        while (cur.tick < tick) {
            const int from = cur.tick;
            int end = tick;
            for (int b : { GameManager::DOUBLE_ELIXIR_TICK, GameManager::TRIPLE_ELIXIR_TICK }) {
                if (from + 1 < b && b <= end) end = b - 1;
            }
            advanceTo(end, GameManager::elixirMultiplierAtTick(from + 1));
        }
    }

    void report(Report r) {
        r.seq = nextSeq++;
        // From the future: regenerate to it first.
        if (r.tick > cur.tick) advanceWithSchedule(r.tick);
        if (r.tick < checkpoint.tick) r.tick = checkpoint.tick;   // older than the window: as old as it can be
        const bool late = r.tick < cur.tick;
        // Kept in (tick, seq) order.
        auto it = std::upper_bound(events.begin(), events.end(), r,
            [](const Report& a, const Report& b) { return a.tick < b.tick || (a.tick == b.tick && a.seq < b.seq); });
        events.insert(it, r);
        if (!late) {
            apply(cur, r);
        } else {
            replay();
        }
    }

    // Convenience wrappers for the bindings.
    void reportPlay(int tick, int cardId, float cost = -1.0f, int evolved = -1) {
        Report r; r.tick = tick; r.kind = KIND_PLAY; r.cardId = cardId; r.cost = cost; r.evolved = evolved;
        report(r);
    }
    void reportAbility(int tick, int cardId, float cost = -1.0f) {
        Report r; r.tick = tick; r.kind = KIND_ABILITY; r.cardId = cardId; r.cost = cost;
        report(r);
    }

    // --- read-out ---
    int tick() const { return cur.tick; }
    float estimate() const { return std::clamp(cur.hi - cur.bias, 0.0f, cur.hi); }
    // A band of INTERVAL_SIGMAS sigma around the estimate. The top is also
    // capped by hi, the bar if every unreported play cost nothing: a hard
    // bound unless a report was false or over-costed (a contradiction). There
    // is no hard floor: a missed play can have spent anything.
    float lower() const { return std::clamp(estimate() - INTERVAL_SIGMAS * cur.sigma, 0.0f, estimate()); }
    float upper() const { return std::clamp(estimate() + INTERVAL_SIGMAS * cur.sigma, estimate(), cur.hi); }
    float hardUpper() const { return cur.hi; }
    float sigma() const { return cur.sigma; }
    int contradictions() const { return cur.contradictions; }
    int lastContradictionTick() const { return cur.lastContradictionTick; }
    float lastSurprise() const { return cur.lastSurprise; }
    int plays() const { return cur.plays; }
    float confidence() const {
        float c = std::exp(-cur.sigma / CONFIDENCE_SIGMA_SCALE);
        if (cur.lastContradictionTick >= 0) {
            const float age = static_cast<float>(std::max(0, cur.tick - cur.lastContradictionTick));
            c *= 1.0f - CONTRADICTION_PENALTY * std::exp(-age / CONTRADICTION_RECOVERY_TICKS);
        }
        return std::clamp(c, 0.0f, 1.0f);
    }

    // est/10, lower/10, upper/10, confidence.
    std::array<float, ELIXIR_FEATURES> elixirFeatures() const {
        const float m = PlayerState::MAX_ELIXIR;
        return { estimate() / m, lower() / m, upper() / m, confidence() };
    }

    // 1.0 when the card's next deployment is evolved, else
    // 1 - cyclesUntilEvolved / threshold. The SAME encoding ClashEnv writes for
    // the observer's own hand from PlayerState, so own and opponent read alike.
    static float evoCharge(int cyclesUntilEvolved, int threshold) {
        if (cyclesUntilEvolved <= 0) return 1.0f;
        if (threshold <= 0) return 1.0f;
        return std::clamp(1.0f - static_cast<float>(cyclesUntilEvolved) / static_cast<float>(threshold), 0.0f, 1.0f);
    }

    // -1 when the evolution is not known (no evolved deployment seen yet).
    float evoChargeFor(int evoId) const {
        const EvoState* e = findEvo(cur, evoId);
        return e ? evoCharge(e->cyclesUntilEvolved, e->threshold) : -1.0f;
    }
    int cyclesUntilEvolvedFor(int evoId) const {
        const EvoState* e = findEvo(cur, evoId);
        return e ? e->cyclesUntilEvolved : -1;
    }
    std::vector<int> knownEvolutions() const {
        std::vector<int> out;
        for (const auto& e : cur.evos) out.push_back(e.evoId);
        return out;
    }

    // Writes known[c] at out[c] and charge[c] at out[numIds + c] (2 * numIds floats).
    void writeEvoBlocks(float* out, int numIds) const {
        for (const auto& e : cur.evos) {
            if (e.evoId < 0 || e.evoId >= numIds) continue;
            out[e.evoId] = 1.0f;
            out[numIds + e.evoId] = evoCharge(e.cyclesUntilEvolved, e.threshold);
        }
    }

    // Per tracked card k: seen, cooldown remaining fraction, cost/10, recency.
    void writeAbilityFeatures(float* out) const {
        for (int k = 0; k < MAX_TRACKED_ABILITY_CARDS; ++k) {
            const AbilityState& a = cur.abilities[k];
            float* f = out + k * ABILITY_FEATURES;
            if (a.cardId < 0) { f[0] = f[1] = f[2] = f[3] = 0.0f; continue; }
            f[0] = 1.0f;
            f[2] = a.cost / PlayerState::MAX_ELIXIR;
            if (a.lastActivationTick < 0) { f[1] = 0.0f; f[3] = 0.0f; continue; }
            const int age = std::max(0, cur.tick - a.lastActivationTick);
            f[1] = a.cooldownTicks > 0
                ? std::clamp(static_cast<float>(a.cooldownTicks - age) / static_cast<float>(a.cooldownTicks), 0.0f, 1.0f)
                : 0.0f;
            f[3] = std::exp(-static_cast<float>(age) / ABILITY_RECENCY_TAU_TICKS);
        }
    }
    const std::array<AbilityState, MAX_TRACKED_ABILITY_CARDS>& abilityStates() const { return cur.abilities; }

    const Config& config() const { return cfg; }
    void setConfig(const Config& c) { cfg = c; }

    // --- card identity helpers (registry-derived, shared with ClashEnv) ---
    // Evolutions reuse the base card's name verbatim (CardRegistry::addEvolution).
    static int baseIdOf(int cardId) {
        const auto& t = tables();
        auto it = t.evoToBase.find(cardId);
        return it == t.evoToBase.end() ? cardId : it->second;
    }
    static int evolutionIdOf(int baseId) {
        const auto& t = tables();
        auto it = t.baseToEvo.find(baseId);
        return it == t.baseToEvo.end() ? -1 : it->second;
    }

private:
    struct Segment { int from; int to; float multiplier; };

    Config cfg;
    Core cur;
    Core checkpoint;
    std::deque<Segment> segments;
    std::deque<Report> events;
    long nextSeq = 0;

    struct Tables {
        std::unordered_map<int, int> evoToBase;
        std::unordered_map<int, int> baseToEvo;
    };
    static const Tables& tables() {
        static const Tables t = [] {
            Tables out;
            const auto& reg = CardRegistry::getInstance();
            std::unordered_map<std::string, int> baseByName;
            for (int id = 0; id < CARD_ID_COUNT; ++id) {
                const CardDefinition* d = reg.getCard(id);
                if (d && !d->isEvolution) baseByName.emplace(d->name, id);
            }
            for (int id = 0; id < CARD_ID_COUNT; ++id) {
                const CardDefinition* d = reg.getCard(id);
                if (!d || !d->isEvolution) continue;
                auto it = baseByName.find(d->name);
                if (it == baseByName.end()) continue;
                out.evoToBase[id] = it->second;
                out.baseToEvo.emplace(it->second, id);
            }
            return out;
        }();
        return t;
    }

    static EvoState makeEvo(const CardDefinition& def) {
        EvoState e;
        e.evoId = def.id;
        e.baseId = baseIdOf(def.id);
        e.threshold = def.evolutionCycleThreshold;
        e.granted = std::max(1, def.evolvedUsesGranted);
        e.cyclesUntilEvolved = def.evolutionCycleThreshold;
        e.evolvedUsesRemaining = e.granted;
        return e;
    }
    static EvoState* findEvo(Core& s, int evoId) {
        for (auto& e : s.evos) if (e.evoId == evoId) return &e;
        return nullptr;
    }
    static const EvoState* findEvo(const Core& s, int evoId) {
        for (const auto& e : s.evos) if (e.evoId == evoId) return &e;
        return nullptr;
    }
    static EvoState* findEvoByBase(Core& s, int baseId) {
        for (auto& e : s.evos) if (e.baseId == baseId) return &e;
        return nullptr;
    }

    // One tick at a time, as GameManager::step adds it: the float sum is then
    // the engine's own, and independent of how callers chunk their advances.
    void regen(Core& s, int dt, float multiplier) const {
        const float g = cfg.regenPerTick * multiplier;
        for (int i = 0; i < dt; ++i) {
            const float raw = s.hi + g;
            s.hi = std::min(raw, PlayerState::MAX_ELIXIR);
            const float over = raw - s.hi;
            if (over > 0.0f) {
                // The truth sits below hi by the unseen spend and keeps filling
                // while hi is pinned: the cap absorbs it.
                s.bias = std::max(0.0f, s.bias - over);
                s.sigma = std::max(0.0f, s.sigma - over);
            }
        }
    }

    // Debits a play whose cost lies in [cMin, cMax] (mean cMean); one number
    // for an identified card. missPrior: a PLAY stands for the unreported plays
    // around it; an ability activation does not.
    void spend(Core& s, float cMin, float cMean, float cMax, int tick, bool missPrior = true) const {
        if (cMin > s.hi + CONTRADICTION_TOLERANCE) {
            // Impossible under the model: re-anchor on the play itself. The
            // bar was at least cMin; the model was short by `surprise`, so allow
            // that much more above it.
            const float surprise = cMin - s.hi;
            s.contradictions++;
            s.lastContradictionTick = tick;
            s.lastSurprise = surprise;
            s.hi = std::min(PlayerState::MAX_ELIXIR, cMin + surprise);
            s.bias = s.hi - cMin;
            s.sigma = std::max(s.sigma, surprise);
        } else {
            const float est = s.hi - s.bias;
            if (cMin > est) {
                // Possible, but only if the unseen spend was smaller than assumed.
                s.bias = std::max(0.0f, s.hi - cMin);
                s.sigma = std::min(s.sigma, std::max(0.0f, s.hi - cMin));
            }
        }
        s.hi = std::max(0.0f, s.hi - cMin);
        // An unidentified card: its cost above the minimum is expected spend.
        s.bias += (cMean - cMin);
        const float spread = cMax - cMin;
        float var = s.sigma * s.sigma + spread * spread / 12.0f;
        // The plays this one stands for that were never reported.
        const float p = missPrior ? std::clamp(cfg.assumedMissRate, 0.0f, 0.9f) : 0.0f;
        const float perPlay = p / (1.0f - p);
        const float w = cfg.missedCostPriorWeight;
        const float missCost = (cfg.missedPlayMeanCost * w + s.knownCostSum) / (w + static_cast<float>(s.knownCostPlays));
        s.bias += perPlay * missCost;
        var += perPlay * missCost * missCost;
        s.sigma = std::sqrt(var);
        s.bias = std::clamp(s.bias, 0.0f, s.hi);
        s.sigma = std::min(s.sigma, PlayerState::MAX_ELIXIR);
    }

    void apply(Core& s, const Report& r) const {
        const CardDefinition* def = (r.cardId >= 0) ? CardRegistry::getInstance().getCard(r.cardId) : nullptr;
        if (r.kind == KIND_ABILITY) {
            float cost = r.cost >= 0.0f ? r.cost : (def ? def->abilityElixirCost : 0.0f);
            AbilityState* a = abilitySlot(s, r.cardId, def);
            if (a) {
                a->lastActivationTick = r.tick;
                if (cost <= 0.0f) cost = a->cost;
            }
            if (cost > 0.0f) spend(s, cost, cost, cost, r.tick, /*missPrior=*/false);
            return;
        }

        s.plays++;
        const float known = r.cost >= 0.0f ? r.cost : (def ? def->cost : -1.0f);
        if (known >= 0.0f) {
            s.knownCostSum += known;
            s.knownCostPlays++;
            spend(s, known, known, known, r.tick);
        } else {
            s.unknownPlays++;
            spend(s, cfg.unknownCostMin, cfg.unknownCostMean, cfg.unknownCostMax, r.tick);
        }
        if (!def) return;

        if (def->isChampion || def->isHero) abilitySlot(s, r.cardId, def);

        // Evolution bookkeeping.
        if (def->isEvolution && r.evolved != 0) {
            EvoState* e = findEvo(s, def->id);
            if (!e) { s.evos.push_back(makeEvo(*def)); e = &s.evos.back(); }
            consumeEvolved(*e);
            return;
        }
        const int base = baseIdOf(r.cardId);
        EvoState* e = findEvoByBase(s, base);
        if (!e) return;
        if (r.evolved == 1 || e->cyclesUntilEvolved <= 0) consumeEvolved(*e);
        else e->cyclesUntilEvolved--;
    }

    static void consumeEvolved(EvoState& e) {
        e.evolvedUsesRemaining--;
        if (e.evolvedUsesRemaining <= 0) {
            e.cyclesUntilEvolved = e.threshold;
            e.evolvedUsesRemaining = e.granted;
        }
    }

    static AbilityState* abilitySlot(Core& s, int cardId, const CardDefinition* def) {
        if (cardId < 0) return nullptr;
        for (auto& a : s.abilities) if (a.cardId == cardId) return &a;
        if (!def || !(def->isChampion || def->isHero)) return nullptr;
        for (auto& a : s.abilities) {
            if (a.cardId >= 0) continue;
            a.cardId = cardId;
            a.cost = def->abilityElixirCost;
            a.cooldownTicks = def->stats ? def->stats->abilityCooldownTicks : 0;
            return &a;
        }
        return nullptr;
    }

    // Recomputes `cur` from the checkpoint, the stored segments and every
    // stored report, in tick order.
    void replay() {
        Core s = checkpoint;
        size_t ei = 0;
        auto applyUpTo = [&](int t) {
            while (ei < events.size() && events[ei].tick <= t) {
                apply(s, events[ei]);
                ++ei;
            }
        };
        applyUpTo(s.tick);
        for (const auto& seg : segments) {
            int t = seg.from;
            while (ei < events.size() && events[ei].tick <= seg.to) {
                const int et = std::max(events[ei].tick, t);
                regen(s, et - t, seg.multiplier);
                t = et;
                s.tick = t;
                apply(s, events[ei]);
                ++ei;
            }
            regen(s, seg.to - t, seg.multiplier);
            s.tick = seg.to;
        }
        while (ei < events.size()) { apply(s, events[ei]); ++ei; }
        s.tick = cur.tick;
        cur = s;
    }

    // Folds segments (and their reports) older than the replay window into the
    // checkpoint.
    void trim() {
        while (!segments.empty() && segments.front().to < cur.tick - REPLAY_WINDOW_TICKS) {
            const Segment seg = segments.front();
            int t = seg.from;
            while (!events.empty() && events.front().tick <= seg.to) {
                const int et = std::max(events.front().tick, t);
                regen(checkpoint, et - t, seg.multiplier);
                t = et;
                checkpoint.tick = t;
                apply(checkpoint, events.front());
                events.pop_front();
            }
            regen(checkpoint, seg.to - t, seg.multiplier);
            checkpoint.tick = seg.to;
            segments.pop_front();
        }
    }
};
