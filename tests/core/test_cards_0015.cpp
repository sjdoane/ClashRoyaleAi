#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "Board.h"
#include "GameManager.h"
#include "CardRegistry.h"
#include "CardLevels.h"
#include "CombatEntity.h"
#include "Tower.h"
#include "EvolutionSelfEffects.h"
#include <cmath>
#include <memory>
#include <set>
#include <vector>

// Patch 0015: Evolved Giant (177), Evolved Elite Barbarians (178), Evolved
// Electro Giant (179), on CombatEntity's self-periodic hook; Electro Giant's
// damage (26 Aug 2026 notes) and Evolved Barbarians' rage length (4 Aug).

namespace {

constexpr int GIANT = 2, EVO_GIANT = 177;
constexpr int ELITE_BARBARIANS = 17, EVO_ELITE_BARBARIANS = 178;
constexpr int ELECTRO_GIANT = 84, EVO_ELECTRO_GIANT = 179;

struct Match {
    GameManager game;
    Match() : game(std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7 }, std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7 }) {
        game.seed(1);
        game.reset();
        for (const auto& e : game.getBoard().getEntities())
            if (auto t = std::dynamic_pointer_cast<Tower>(e)) t->sleep();
    }
    Board& board() { return game.getBoard(); }
    void step(int n = 1) { for (int i = 0; i < n; ++i) game.step(); }

    std::vector<std::shared_ptr<CombatEntity>> spawnCard(int cardId, float x, float y, int team, bool evolved) {
        std::set<int> before;
        for (const auto& e : board().getEntities()) before.insert(e->id);
        const CardDefinition* def = CardRegistry::getInstance().getCard(cardId);
        if (evolved) def->spawnEvolvedEntity(x, y, team, board());
        else def->spawnEntity(x, y, team, board());
        board().commitPendingEntities();
        std::vector<std::shared_ptr<CombatEntity>> out;
        for (const auto& e : board().getEntities())
            if (!before.count(e->id))
                if (auto c = std::dynamic_pointer_cast<CombatEntity>(e)) out.push_back(c);
        return out;
    }
    std::shared_ptr<StationaryCombatant> dummy(float x, float y, int team, int hp, bool flying = false) {
        auto d = std::make_shared<StationaryCombatant>(board().allocateId(), x, y, hp, team, 0.5f, 0, 1000);
        d->isFlying = flying;
        spawn(board(), d);
        return d;
    }
};

// Exposes the current damage, which CombatEntity keeps protected.
struct DamageProbe : StationaryCombatant {
    using StationaryCombatant::StationaryCombatant;
    int currentDamage() const { return getCurrentDamage(); }
};

}  // namespace

TEST_CASE("Evolutions 177-179 are registered against their base cards", "[evolution][patch0015]") {
    auto& reg = CardRegistry::getInstance();
    const std::vector<std::pair<int, int>> pairs = {
        { EVO_GIANT, GIANT }, { EVO_ELITE_BARBARIANS, ELITE_BARBARIANS }, { EVO_ELECTRO_GIANT, ELECTRO_GIANT } };
    for (const auto& [evo, base] : pairs) {
        const CardDefinition* e = reg.getCard(evo);
        const CardDefinition* b = reg.getCard(base);
        REQUIRE(e != nullptr);
        REQUIRE(b != nullptr);
        REQUIRE(e->isEvolution);
        REQUIRE(e->name == b->name);
        REQUIRE(e->cost == Catch::Approx(b->cost));
        REQUIRE(e->hp == b->hp);
        REQUIRE(e->evolutionCycleThreshold == 1);
        REQUIRE(e->evolvedUsesGranted == 1);
        REQUIRE(faceCardIdOf(evo) == base);
    }
}

TEST_CASE("Electro Giant deals 184 (26 Aug 2026 notes), base and Evolution", "[evolution][patch0015]") {
    REQUIRE(CardRegistry::getInstance().getCard(ELECTRO_GIANT)->stats->damage == 184);
    REQUIRE(CardRegistry::getInstance().getCard(EVO_ELECTRO_GIANT)->stats->damage == 184);
}

TEST_CASE("Evolved Barbarians' Blade Rage lasts 5 s (4 Aug 2026 notes)", "[evolution][patch0015]") {
    const CardDefinition* def = CardRegistry::getInstance().getCard(155);
    REQUIRE(def->evolvedStats != nullptr);
    REQUIRE(def->evolvedStats->selfHasteDurationTicks == 50);
}

TEST_CASE("only the evolved forms carry the self-periodic ability", "[evolution][patch0015]") {
    for (int base : { GIANT, ELITE_BARBARIANS, ELECTRO_GIANT }) {
        Match m;
        for (const auto& c : m.spawnCard(base, 9.0f, 8.0f, 0, false)) REQUIRE(c->selfPeriodicEffect == nullptr);
    }
    for (int evo : { EVO_GIANT, EVO_ELITE_BARBARIANS, EVO_ELECTRO_GIANT }) {
        Match m;
        const auto plain = m.spawnCard(evo, 9.0f, 8.0f, 0, false);
        for (const auto& c : plain) REQUIRE(c->selfPeriodicEffect == nullptr);
        const auto evolved = m.spawnCard(evo, 9.0f, 8.0f, 0, true);
        REQUIRE_FALSE(evolved.empty());
        for (const auto& c : evolved) REQUIRE(c->selfPeriodicEffect != nullptr);
    }
}

// ---------------- Evolved Giant ----------------

TEST_CASE("Evolved Giant's rage trail starts at half health and widens with his attacks",
        "[evolution][evo_giant][patch0015]") {
    Board board;
    auto giant = std::make_shared<StationaryCombatant>(board.allocateId(), 9.0f, 10.0f, 4000, 0, 1.2f, 0, 12);
    giant->fullHp = 4000;
    spawn(board, giant);
    auto near = std::make_shared<StationaryCombatant>(board.allocateId(), 10.9f, 10.0f, 500, 0, 0.5f, 0, 1000);
    auto mid = std::make_shared<StationaryCombatant>(board.allocateId(), 11.9f, 10.0f, 500, 0, 0.5f, 0, 1000);
    auto far = std::make_shared<StationaryCombatant>(board.allocateId(), 12.6f, 10.0f, 500, 0, 0.5f, 0, 1000);
    auto foe = std::make_shared<StationaryCombatant>(board.allocateId(), 10.0f, 10.0f, 500, 1, 0.5f, 0, 1000);
    for (auto& e : { near, mid, far, foe }) spawn(board, e);

    const EvoGiantRageTrailEffect trail(0.5f, 2.0f, 0.5f, 3.5f, 1.35f, 15);
    giant->hp = 2001;
    REQUIRE_FALSE(trail.apply(board, *giant));     // above half: nothing yet
    REQUIRE(near->buffTicksRemaining == 0);

    giant->hp = 2000;
    giant->attacksStarted = 7;
    REQUIRE(trail.apply(board, *giant));
    REQUIRE(giant->selfPeriodicState == 7);
    REQUIRE(giant->buffDamageMultiplier == Catch::Approx(1.35f));  // he is in his own trail
    REQUIRE(near->buffTicksRemaining == 15);
    REQUIRE(near->buffDamageMultiplier == Catch::Approx(1.35f));
    REQUIRE(mid->buffTicksRemaining == 0);          // 2.9 tiles > 2.0
    REQUIRE(foe->buffTicksRemaining == 0);          // enemies are never enraged

    giant->attacksStarted = 9;                      // two attacks since: radius 3.0
    REQUIRE(trail.apply(board, *giant));
    REQUIRE(mid->buffTicksRemaining == 15);
    REQUIRE(far->buffTicksRemaining == 0);          // 3.6 tiles

    giant->attacksStarted = 30;                     // capped at 3.5
    giant->hp = 4000;                               // once triggered, it stays on
    REQUIRE(trail.apply(board, *giant));
    REQUIRE(far->buffTicksRemaining == 0);
}

TEST_CASE("an evolved Giant in a match enrages after dropping to half health",
        "[evolution][evo_giant][patch0015]") {
    Match m;
    auto giant = m.spawnCard(EVO_GIANT, 9.0f, 8.0f, 0, true).at(0);
    m.step(15);                                     // deployed, acting
    REQUIRE(giant->fullHp > 0);
    REQUIRE(giant->buffTicksRemaining == 0);
    giant->hp = giant->fullHp / 2;
    m.step(2);
    REQUIRE(giant->buffTicksRemaining > 0);
    REQUIRE(giant->buffDamageMultiplier == Catch::Approx(1.35f));
}

// ---------------- Evolved Elite Barbarians ----------------

TEST_CASE("Evolved Elite Barbarians throw a 220 spear at a ground troop 3-4.5 tiles away and enrage",
        "[evolution][evo_elite_barbarians][patch0015]") {
    Board board;
    auto barb = std::make_shared<StationaryCombatant>(board.allocateId(), 9.0f, 10.0f, 1341, 0, 1.2f, 0, 12);
    spawn(board, barb);
    auto ally = std::make_shared<StationaryCombatant>(board.allocateId(), 9.0f, 9.0f, 500, 0, 0.5f, 0, 1000);
    auto close = std::make_shared<StationaryCombatant>(board.allocateId(), 9.0f, 12.5f, 1000, 1, 0.5f, 0, 1000);
    auto air = std::make_shared<StationaryCombatant>(board.allocateId(), 9.0f, 13.5f, 1000, 1, 0.5f, 0, 1000);
    air->isFlying = true;
    for (auto& e : { ally, close, air }) spawn(board, e);

    const EvoEliteBarbarianSpearEffect spear(3.0f, 4.5f, 220, 1.35f, 25, 1.5f);
    REQUIRE_FALSE(spear.apply(board, *barb));       // 2.5 tiles is too close; air is never a target
    REQUIRE(close->hp == 1000);
    REQUIRE(air->hp == 1000);

    auto inBand = std::make_shared<StationaryCombatant>(board.allocateId(), 9.0f, 14.0f, 1000, 1, 0.5f, 0, 1000);
    auto nearer = std::make_shared<StationaryCombatant>(board.allocateId(), 12.5f, 10.0f, 1000, 1, 0.5f, 0, 1000);
    spawn(board, inBand);
    spawn(board, nearer);
    REQUIRE(spear.apply(board, *barb));
    REQUIRE(nearer->hp == 1000 - 220);              // the nearest one in the band (3.5 < 4.0)
    REQUIRE(inBand->hp == 1000);
    REQUIRE(barb->buffTicksRemaining == 25);
    REQUIRE(ally->buffTicksRemaining == 25);
    REQUIRE(close->buffTicksRemaining == 0);
}

// Patch 0055. The official 8 September 2026 note: "Elite Barbarians Evolution:
// Spear Range 3.5-5 -> 3-4.5 tiles, Rage Duration 2.5s -> 2s". The band was
// already the new one; the Rage still lasted the August 2.5 s.
TEST_CASE("the registered Evolved Elite Barbarians are enraged for 2 s by a spear, the 8 Sep 2026 value",
        "[evolution][evo_elite_barbarians][patch0055]") {
    Match m;
    auto barbs = m.spawnCard(EVO_ELITE_BARBARIANS, 9.0f, 8.0f, 0, true);
    REQUIRE(barbs.size() == 2);
    // One ground troop in the 3-4.5 band of both, too tough to die, harmless.
    auto foe = m.dummy(9.0f, 12.0f, 1, 100000);
    int enragedUpdates = 0, hpAfterThrow = -1;
    for (int t = 1; t <= 45; ++t) {
        m.step();
        if (hpAfterThrow < 0 && foe->hp < 100000) hpAfterThrow = foe->hp;
        if (barbs[0]->buffTicksRemaining > 0) enragedUpdates++;
    }
    REQUIRE(hpAfterThrow == 100000 - 2 * 220);      // one spear each, on the same update; the next is 5 s away
    REQUIRE(enragedUpdates == 20);
}

TEST_CASE("an evolved Elite Barbarian recharges the spear every 5 s", "[evolution][evo_elite_barbarians][patch0015]") {
    Match m;
    auto barbs = m.spawnCard(EVO_ELITE_BARBARIANS, 9.0f, 8.0f, 0, true);
    REQUIRE(barbs.size() == 2);
    for (auto& b : barbs) {
        REQUIRE(b->selfPeriodicIntervalTicks == 50);
        REQUIRE(b->selfPeriodicTicksUntilNext == 1);
    }
}

// ---------------- Evolved Electro Giant ----------------

TEST_CASE("Level Damper takes one level off nearby enemies, not Crown Towers, not below level 1",
        "[evolution][evo_electro_giant][patch0015]") {
    Board board;
    auto eg = std::make_shared<StationaryCombatant>(board.allocateId(), 9.0f, 10.0f, 3952, 0, 1.2f, 0, 12);
    spawn(board, eg);
    auto foe = std::make_shared<DamageProbe>(board.allocateId(), 9.0f, 15.5f, 2560, 1, 0.5f, 256, 1000);
    auto out = std::make_shared<StationaryCombatant>(board.allocateId(), 9.0f, 16.5f, 2560, 1, 0.5f, 256, 1000);
    auto friendly = std::make_shared<StationaryCombatant>(board.allocateId(), 9.0f, 11.0f, 2560, 0, 0.5f, 256, 1000);
    spawn(board, foe);
    for (auto& e : { out, friendly }) spawn(board, e);

    const LevelDamperEffect damper(6.0f);
    REQUIRE(damper.apply(board, *eg));
    REQUIRE(foe->currentLevel == CardLevels::TOURNAMENT_LEVEL - 1);
    // One step of CardLevels::MULTIPLIER_PERCENT: 256 -> 233 (level 11 -> 10).
    REQUIRE(foe->hp == 2330);
    REQUIRE(foe->currentDamage() == 233);
    REQUIRE(out->hp == 2560);                       // 6.5 tiles
    REQUIRE(friendly->hp == 2560);

    REQUIRE(damper.apply(board, *eg));              // cumulative
    REQUIRE(foe->currentLevel == CardLevels::TOURNAMENT_LEVEL - 2);
    REQUIRE(foe->hp == 2120);                       // 2330 * 212 / 233

    foe->currentLevel = CardLevels::MIN_LEVEL;
    const int hp = foe->hp;
    REQUIRE(damper.apply(board, *eg));
    REQUIRE(foe->hp == hp);
}

TEST_CASE("an evolved Electro Giant's first Level Damper pulse lands 2.5 s after the deploy, then every 6 s",
        "[evolution][evo_electro_giant][patch0015]") {
    Match m;
    auto eg = m.spawnCard(EVO_ELECTRO_GIANT, 9.0f, 8.0f, 0, true).at(0);
    auto foe = m.dummy(9.0f, 12.0f, 1, 10000);
    auto tower = [&]() -> std::shared_ptr<Entity> {
        for (const auto& e : m.board().getEntities()) if (e->isTower() && e->team == 1) return e;
        return nullptr;
    }();
    REQUIRE(tower != nullptr);
    const int towerHp = tower->hp;

    int firstLoss = -1;
    for (int t = 1; t <= 120 && firstLoss < 0; ++t) {
        m.step();
        if (foe->currentLevel < CardLevels::TOURNAMENT_LEVEL) firstLoss = t;
    }
    // Deploy (DEPLOY_TIME_TICKS) then 25 ticks, +-1 for the hook's ordering.
    REQUIRE(firstLoss >= DEPLOY_TIME_TICKS + 24);
    REQUIRE(firstLoss <= DEPLOY_TIME_TICKS + 27);
    m.step(59);
    REQUIRE(foe->currentLevel == CardLevels::TOURNAMENT_LEVEL - 1);
    m.step(2);
    REQUIRE(foe->currentLevel == CardLevels::TOURNAMENT_LEVEL - 2);
    REQUIRE(foe->hp < 10000);

    // Crown Towers are exempt: a pulse centred on one leaves it whole.
    eg->position = tower->position;
    REQUIRE(LevelDamperEffect(6.0f).apply(m.board(), *eg));
    REQUIRE(tower->hp == towerHp);
}
