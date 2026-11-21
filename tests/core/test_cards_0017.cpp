#include <catch_amalgamated.hpp>
#include "test_helpers.h"
#include "Board.h"
#include "GameManager.h"
#include "CardRegistry.h"
#include "CombatEntity.h"
#include "Tower.h"
#include "StatsEventBus.h"
#include "HeroAbilityEffects.h"
#include <memory>
#include <set>
#include <vector>

// Patch 0017: Heroes 181-188 (Balloon, Valkyrie, Ice Wizard, Dark Prince,
// Bowler, Electro Wizard, Berserker, Tombstone) and CombatEntity's ability
// window. Each case plays the Hero from deck slot 1 through the real
// GameManager path and activates it with activateChampionAbility.

namespace {

struct Recorder : IStatsObserver {
    std::vector<DamageDealtEvent> damage;
    void onDamageDealt(const DamageDealtEvent& e) override { damage.push_back(e); }
};

struct HeroMatch {
    GameManager game;
    std::shared_ptr<Recorder> rec = std::make_shared<Recorder>();
    int hero;

    explicit HeroMatch(int heroId)
        : game(std::vector<int>{ 0, heroId, 2, 3, 4, 5, 6, 7 }, std::vector<int>{ 0, 1, 2, 3, 4, 5, 6, 7 }),
          hero(heroId) {
        game.seed(1);
        game.reset();
        game.getBoard().statsEvents.subscribe(rec);
        for (const auto& e : game.getBoard().getEntities())
            if (auto t = std::dynamic_pointer_cast<Tower>(e)) t->sleep();
    }
    Board& board() { return game.getBoard(); }
    void step(int n = 1) { for (int i = 0; i < n; ++i) game.step(); }

    // Plays the Hero at (x, y) and steps through its deploy; returns its body.
    std::shared_ptr<CombatEntity> deploy(float x, float y) {
        REQUIRE(game.setHand(0, { hero, 2, 3, 4 }));
        game.setElixir(0, 10.0f);
        REQUIRE(game.playCard(0, hero, x, y));
        step(DEPLOY_TIME_TICKS + 1);
        game.setElixir(0, 10.0f);
        return byName(CardRegistry::getInstance().getCard(hero)->name, 0);
    }
    std::shared_ptr<CombatEntity> byName(const std::string& name, int team) {
        for (const auto& e : board().getEntities())
            if (e->isAlive() && e->team == team && e->name == name)
                if (auto c = std::dynamic_pointer_cast<CombatEntity>(e)) return c;
        return nullptr;
    }
    std::shared_ptr<StationaryCombatant> dummy(float x, float y, int hp, bool flying = false) {
        auto d = std::make_shared<StationaryCombatant>(board().allocateId(), x, y, hp, 1, 0.5f, 0, 1000);
        d->isFlying = flying;
        spawn(board(), d);
        return d;
    }
    int damageTo(int targetId, int amount) const {
        int n = 0;
        for (const auto& d : rec->damage)
            if (d.targetId == targetId && d.attackerCardId == hero && d.amount == amount) n++;
        return n;
    }
};

struct HeroRow { int id; const char* name; int base; float abilityCost; };
const std::vector<HeroRow>& heroes() {
    static const std::vector<HeroRow> rows = {
        { 181, "Hero Balloon", 45, 2.0f }, { 182, "Hero Valkyrie", 10, 3.0f },
        { 183, "Hero Ice Wizard", 34, 2.0f }, { 184, "Hero Dark Prince", 46, 3.0f },
        { 185, "Hero Bowler", 22, 2.0f }, { 186, "Hero Electro Wizard", 35, 2.0f },
        { 187, "Hero Berserker", 51, 3.0f }, { 188, "Hero Tombstone", 96, 5.0f } };
    return rows;
}

}  // namespace

TEST_CASE("Heroes 181-188: base card's stats, a single-use ability, folded to the base card",
        "[hero][patch0017]") {
    for (const auto& r : heroes()) {
        INFO(r.name);
        const CardDefinition* def = CardRegistry::getInstance().getCard(r.id);
        const CardDefinition* base = CardRegistry::getInstance().getCard(r.base);
        REQUIRE(def != nullptr);
        REQUIRE(def->name == r.name);
        REQUIRE(def->isHero);
        REQUIRE_FALSE(def->isChampion);
        REQUIRE(def->cost == Catch::Approx(base->cost));
        REQUIRE(def->hp == base->hp);
        REQUIRE(def->abilityElixirCost == Catch::Approx(r.abilityCost));
        REQUIRE(faceCardIdOf(r.id) == r.base);

        Board board;
        def->spawnEntity(9.0f, 8.0f, 0, board);
        board.commitPendingEntities();
        auto body = std::dynamic_pointer_cast<CombatEntity>(board.getEntities()[0]);
        REQUIRE(body != nullptr);
        REQUIRE(body->isHero);
        REQUIRE(body->abilityUsesRemaining == 1);
        REQUIRE(body->abilityCooldownTicks == 0);
    }
}

TEST_CASE("every new Hero's ability is activatable once through the real play path", "[hero][patch0017]") {
    for (const auto& r : heroes()) {
        INFO(r.name);
        HeroMatch m(r.id);
        m.deploy(9.0f, 8.0f);
        m.dummy(9.0f, 11.0f, 5000);
        REQUIRE(m.game.isChampionAbilityReady(0, 1));
        const float before = m.game.getElixir(0);
        REQUIRE(m.game.activateChampionAbility(0, 1));
        REQUIRE(m.game.getElixir(0) == Catch::Approx(before - r.abilityCost));
        REQUIRE_FALSE(m.game.isChampionAbilityReady(0, 1));
        REQUIRE_FALSE(m.game.activateChampionAbility(0, 1));
    }
}

TEST_CASE("Hero Balloon's Coffin Cadets drops a Skeletrooper with 263 landing damage on the nearest ground unit",
        "[hero][hero_balloon][patch0017]") {
    HeroMatch m(181);
    auto balloon = m.deploy(9.0f, 8.0f);
    auto near = m.dummy(9.0f, 12.0f, 5000);
    auto air = m.dummy(9.0f, 10.0f, 5000, true);   // nearer, but air is never the drop
    REQUIRE(m.game.activateChampionAbility(0, 1));
    m.step(8);
    REQUIRE(m.damageTo(near->id, 263) == 1);
    REQUIRE(m.damageTo(air->id, 263) == 0);
    auto trooper = m.byName("Skeletrooper", 0);
    REQUIRE(trooper != nullptr);
    REQUIRE(trooper->position.distanceTo(near->position) < 1.5f);
    // -90% to Crown Towers on its own hits.
    for (const auto& e : m.board().getEntities())
        if (e->isTower()) { REQUIRE(trooper->crownTowerHitFactor(*e) == Catch::Approx(0.1f)); break; }
    (void)balloon;
}

TEST_CASE("Hero Valkyrie's Whirlwind: 14 pulses of 97 in 3.5 s, no swings, 15% less damage taken",
        "[hero][hero_valkyrie][patch0017]") {
    HeroMatch m(182);
    auto valk = m.deploy(9.0f, 8.0f);
    auto foe = m.dummy(9.0f, 9.5f, 20000);
    REQUIRE(m.game.activateChampionAbility(0, 1));
    REQUIRE(valk->inAbilityWindow());
    const int hp = valk->hp;
    valk->takeDamage(100);
    REQUIRE(valk->hp == hp - 85);
    const int swingsBefore = m.damageTo(foe->id, 266);
    m.step(35);
    REQUIRE(m.damageTo(foe->id, 97) == 14);
    REQUIRE(m.damageTo(foe->id, 266) == swingsBefore);   // no swings for the 35 updates
    REQUIRE_FALSE(valk->inAbilityWindow());
    m.step(10);
    REQUIRE(m.damageTo(foe->id, 97) == 14);
    REQUIRE(m.damageTo(foe->id, 266) > swingsBefore);    // swings resume
    REQUIRE(valk->selfPeriodicEffect == nullptr);   // the pulses end with the window
}

TEST_CASE("Hero Ice Wizard's Frosty Fella freezes and hits enemy troops at the nearest one, and leaves a Snowman",
        "[hero][hero_ice_wizard][patch0017]") {
    HeroMatch m(183);
    m.deploy(9.0f, 8.0f);
    auto a = m.dummy(9.0f, 12.0f, 5000);
    auto b = m.dummy(10.5f, 12.5f, 5000);
    auto far = m.dummy(9.0f, 15.5f, 5000);
    REQUIRE(m.game.activateChampionAbility(0, 1));
    REQUIRE(m.damageTo(a->id, 46) == 1);
    REQUIRE(m.damageTo(b->id, 46) == 1);
    REQUIRE(m.damageTo(far->id, 46) == 0);
    REQUIRE(a->freezeTicks == 50);
    REQUIRE(a->freezeSlow == Catch::Approx(0.0f));
    m.step(1);
    auto snowman = m.byName("Snowman", 0);
    REQUIRE(snowman != nullptr);
    REQUIRE(snowman->position.distanceTo(Vector2D{ 9.0f, 12.0f }) < 0.01f);   // where a stood
    m.step(60);
    REQUIRE(m.byName("Snowman", 0) == nullptr);   // ~5 s life
}

TEST_CASE("Hero Dark Prince's Destructive Dismount: the Rhino lands after 1 s with 307, and he loses his charge",
        "[hero][hero_dark_prince][patch0017]") {
    HeroMatch m(184);
    auto dp = m.deploy(9.0f, 8.0f);
    auto foe = m.dummy(10.0f, 8.5f, 20000);
    REQUIRE(dp->chargeThreshold > 0.0f);
    REQUIRE(m.game.activateChampionAbility(0, 1));
    REQUIRE(dp->chargeThreshold == 0.0f);
    m.step(5);
    REQUIRE(m.byName("Rhino", 0) == nullptr);
    m.step(8);
    auto rhino = m.byName("Rhino", 0);
    REQUIRE(rhino != nullptr);
    REQUIRE(rhino->hp == 1356);
    REQUIRE(m.damageTo(foe->id, 307) == 1);
}

TEST_CASE("Hero Bowler's Stone Swish: rooted, three 508 lobs at 11.5 tiles after a 2.5 s cast",
        "[hero][hero_bowler][patch0017]") {
    HeroMatch m(185);
    auto bowler = m.deploy(9.0f, 6.0f);
    auto foe = m.dummy(9.0f, 15.0f, 20000);
    REQUIRE(m.game.activateChampionAbility(0, 1));
    const Vector2D at = bowler->position;
    m.step(24);
    REQUIRE(m.damageTo(foe->id, 508) == 0);
    m.step(4);
    REQUIRE(m.damageTo(foe->id, 508) == 1);
    m.step(45);
    REQUIRE(m.damageTo(foe->id, 508) == 3);
    REQUIRE(bowler->position.distanceTo(at) < 1e-4f);
    m.step(20);
    REQUIRE(m.damageTo(foe->id, 508) == 3);         // three shots, then it ends
    REQUIRE_FALSE(bowler->inAbilityWindow());
}

TEST_CASE("Hero Electro Wizard's Surging Strikes stun the two nearest enemies for 3 s",
        "[hero][hero_electro_wizard][patch0017]") {
    HeroMatch m(186);
    m.deploy(9.0f, 8.0f);
    auto a = m.dummy(9.0f, 11.0f, 5000);
    auto b = m.dummy(9.0f, 11.5f, 5000, true);
    auto c = m.dummy(9.0f, 12.5f, 5000);
    REQUIRE(m.game.activateChampionAbility(0, 1));
    REQUIRE(a->freezeTicks == 30);
    REQUIRE(b->freezeTicks == 30);
    REQUIRE(c->freezeTicks < 30);
    REQUIRE(m.damageTo(a->id, 115) >= 1);
    REQUIRE(m.damageTo(b->id, 115) >= 1);
}

TEST_CASE("Hero Berserker's Savage Survival: 3.5 s at 1 hp minimum, 0.2 s hits, -75% to Crown Towers",
        "[hero][hero_berserker][patch0017]") {
    HeroMatch m(187);
    auto berserker = m.deploy(9.0f, 8.0f);
    REQUIRE(m.game.activateChampionAbility(0, 1));
    REQUIRE(berserker->windowCooldownMultiplier == Catch::Approx(1.0f / 3.0f));
    REQUIRE(berserker->windowMoveMultiplier == Catch::Approx(1.5f));
    for (const auto& e : m.board().getEntities())
        if (e->isTower()) { REQUIRE(berserker->crownTowerHitFactor(*e) == Catch::Approx(0.25f)); break; }
    berserker->takeDamage(100000);
    REQUIRE(berserker->hp == 1);
    m.step(34);
    REQUIRE(berserker->isAlive());
    m.step(2);
    REQUIRE_FALSE(berserker->inAbilityWindow());
    berserker->takeDamage(5);
    REQUIRE_FALSE(berserker->isAlive());
}

TEST_CASE("Hero Tombstone raises the Tomb Queen once per deploy, alive or just after its death",
        "[hero][hero_tombstone][patch0017]") {
    SECTION("used while it stands: no second Queen after it dies") {
        HeroMatch m(188);
        auto tomb = m.deploy(9.0f, 8.0f);
        REQUIRE(tomb != nullptr);
        REQUIRE(m.game.activateChampionAbility(0, 1));
        m.step(2);
        auto queen = m.byName("Tomb Queen", 0);
        REQUIRE(queen != nullptr);
        REQUIRE(queen->hp == 4224);
        tomb->takeDamage(100000);
        for (int i = 0; i < 20; ++i) {
            m.step();
            m.game.setElixir(0, 10.0f);
            REQUIRE_FALSE(m.game.isChampionAbilityReady(0, 1));
        }
    }
    SECTION("unused when it dies: usable for 7 s after") {
        HeroMatch m(188);
        auto tomb = m.deploy(9.0f, 8.0f);
        tomb->takeDamage(100000);
        m.step(2);
        m.game.setElixir(0, 10.0f);
        REQUIRE(m.game.isChampionAbilityReady(0, 1));
        REQUIRE(m.game.activateChampionAbility(0, 1));
        m.step(2);
        REQUIRE(m.byName("Tomb Queen", 0) != nullptr);
        REQUIRE_FALSE(m.game.isChampionAbilityReady(0, 1));
    }
}
