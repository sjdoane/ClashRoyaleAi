#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <optional>
#include "ArenaLayout.h"
#include "ClashEnv.h"
#include "SpawnOnAbility.h"
#include "CappedSpawnOnHitEffect.h"
#include "AreaDamageOnDeath.h"
#include "DelayedAreaDamageOnDeath.h"
#include "FreezeOnHit.h"
#include <algorithm>

namespace py = pybind11;


// --- get_card_stats (card-stats audit, patch 0003) ---
// The CardStats a registered card spawns with, in engine units, for comparing
// against the wiki. Read-only. Effect objects are described where their type
// is known (dynamic_cast); unknown effects appear by presence only.
namespace {

const char* archetypeName(Archetype a) {
    switch (a) {
        case Archetype::MeleeSquad: return "MeleeSquad";
        case Archetype::RangedSquad: return "RangedSquad";
        case Archetype::MeleeBuildingTargeter: return "MeleeBuildingTargeter";
        case Archetype::RangedBuildingTargeter: return "RangedBuildingTargeter";
        case Archetype::DefensiveBuilding: return "DefensiveBuilding";
        case Archetype::Spell: return "Spell";
    }
    return "?";
}

py::object onHitDict(const std::shared_ptr<IOnHitEffect>& e) {
    if (!e) return py::none();
    py::dict d;
    if (auto f = std::dynamic_pointer_cast<FreezeOnHit>(e)) {
        d["type"] = "freeze";
        d["ticks"] = f->getTicks();
        d["slow_factor"] = f->getSlowFactor();
    } else {
        d["type"] = "other";
    }
    return d;
}

py::dict statsDict(const CardStats& s, int depth = 0) {
    constexpr float TICKS_PER_SECOND = 10.0f;
    py::dict d;
    d["id"] = s.id;
    d["name"] = s.name;
    d["cost"] = s.cost;
    d["archetype"] = archetypeName(s.archetype);
    d["hp"] = s.hp;
    d["damage"] = s.damage;
    d["attack_cooldown_ticks"] = s.attackCooldown;
    d["load_ticks"] = LoadTimes::forName(s.name);
    d["hit_speed_s"] = s.attackCooldown / TICKS_PER_SECOND;
    d["first_hit_ticks"] = s.initialCooldownTicks;   // 0 = no seeded first-hit delay
    d["speed_tiles_per_tick"] = s.speed;
    d["speed_engine_tiles_per_s"] = s.speed * TICKS_PER_SECOND;
    // Inverse of troop()'s MOVEMENT_SPEED_SCALE and the SPEED_* tier factor:
    // the wiki's "Medium (60)" number.
    d["speed_wiki_tiles_per_min"] = s.speed / (MOVEMENT_SPEED_SCALE * REAL_TILES_PER_MIN_TO_ENGINE);
    d["attack_range"] = s.attackRange;
    d["min_attack_range"] = s.minAttackRange;
    d["sight_range"] = s.sightRange;
    d["deploy_ticks"] = s.deployTicks;
    d["count"] = static_cast<int>(s.spawnOffsets.size());
    py::list offs;
    for (const auto& o : s.spawnOffsets) offs.append(py::make_tuple(o.x, o.y));
    d["spawn_offsets"] = offs;
    d["is_flying"] = s.isFlying;
    d["targets_air"] = s.targetsAir;
    d["ground_only_spell"] = s.spellGroundOnly;
    d["splash_radius"] = s.splashRadius;
    d["shield_hp"] = s.shieldHp;
    d["charge_threshold"] = s.chargeThreshold;
    d["charge_multiplier"] = s.chargeMultiplier;
    d["die_after_first_hit"] = s.dieAfterFirstHit;
    d["on_hit"] = onHitDict(s.onHit);
    d["lifetime_ticks"] = (s.archetype == Archetype::DefensiveBuilding) ? s.lifetimeTicks : 0;
    d["transform_at_hp_fraction"] = s.transformAtHpFraction;
    d["transform_lifetime_ticks"] = s.transformLifetimeTicks;
    // Spells.
    d["spell_radius"] = s.spellRadius;
    d["spell_delay_ticks"] = s.spellDelayTicks;
    d["spell_hits"] = s.spellRemainingHits;
    d["spell_tick_interval"] = s.spellTickInterval;
    d["spell_knockback"] = s.spellKnockback;
    d["spell_crown_tower_damage"] = s.spellCrownTowerDamage;   // -1 = full damage
    d["spell_on_hit"] = onHitDict(s.spellOnHit);
    d["roll_range"] = s.spellRollRange;
    d["roll_width"] = s.spellRollWidth;
    d["roll_speed"] = s.spellRollSpeed;
    d["roll_knockback"] = s.spellRollKnockback;
    // Deploy burst.
    d["spawn_effect_radius"] = s.spawnEffectRadius;
    d["spawn_effect_damage"] = s.spawnEffectDamage;
    d["spawn_effect_count"] = s.spawnEffectRadius > 0.0f
        ? static_cast<int>(std::max<size_t>(1, s.spawnEffectOffsets.size())) : 0;
    d["spawn_effect_knockback"] = s.spawnEffectKnockback;
    d["spawn_effect_crown_tower_damage"] = s.spawnEffectCrownTowerDamage;
    // Death effect.
    py::object death = py::none();
    if (auto a = std::dynamic_pointer_cast<AreaDamageOnDeath>(s.deathEffect)) {
        py::dict dd; dd["type"] = "area_damage"; dd["radius"] = a->getRadius(); dd["damage"] = a->getDamage();
        dd["on_hit"] = onHitDict(a->getOnHit());
        death = dd;
    } else if (auto b = std::dynamic_pointer_cast<DelayedAreaDamageOnDeath>(s.deathEffect)) {
        py::dict dd; dd["type"] = "delayed_area_damage"; dd["radius"] = b->getRadius();
        dd["damage"] = b->getDamage(); dd["fuse_ticks"] = b->getFuseTicks();
        death = dd;
    } else if (s.deathEffect) {
        py::dict dd; dd["type"] = "other"; death = dd;
    }
    d["death_effect"] = death;
    // On-hit spawn (Evolved Skeletons).
    py::object ohs = py::none();
    if (auto c = std::dynamic_pointer_cast<CappedSpawnOnHitEffect>(s.onHitSpawnEffect)) {
        py::dict dd; dd["type"] = "capped_spawn"; dd["max_alive"] = c->getMaxAlive();
        if (depth < 2) dd["child"] = statsDict(c->child(), depth + 1);
        ohs = dd;
    } else if (s.onHitSpawnEffect) {
        py::dict dd; dd["type"] = "other"; ohs = dd;
    }
    d["on_hit_spawn"] = ohs;
    d["secondary_unit"] = (s.secondaryUnit && depth < 2) ? py::object(statsDict(*s.secondaryUnit, depth + 1)) : py::none();
    // Champion / Hero ability.
    d["is_champion"] = s.isChampion;
    d["is_hero"] = s.isHero;
    d["ability_cost"] = s.abilityElixirCost;
    d["ability_cooldown_ticks"] = s.abilityCooldownTicks;
    d["ability_uses_limit"] = s.abilityUsesLimit;   // -1 = unlimited
    d["initial_ability_cooldown_ticks"] = s.initialAbilityCooldownTicks;
    py::object ability = py::none();
    if (auto sp = std::dynamic_pointer_cast<SpawnOnAbility>(s.abilityEffect)) {
        py::dict dd; dd["type"] = "spawn";
        dd["forward_tiles"] = sp->getForwardTiles();
        dd["delay_ticks"] = sp->getDelayTicks();
        dd["landing_damage"] = sp->getLandingDamage();
        dd["landing_radius"] = sp->getLandingRadius();
        if (depth < 2) dd["child"] = statsDict(sp->child(), depth + 1);
        ability = dd;
    } else if (s.abilityEffect) {
        py::dict dd; dd["type"] = "other"; ability = dd;
    }
    d["ability_effect"] = ability;
    return d;
}

} // namespace

PYBIND11_MODULE(clash_royale_env, m) {
    m.doc() = "Clash Royale RL Environment with Pybind11";

    // --- arena geometry (ArenaLayout.h), so Python derives rather than copies
    // ---
    // Module-level: the arena belongs to the game, not to an env instance.
    m.attr("ARENA_WIDTH") = ArenaLayout::WIDTH;
    m.attr("ARENA_HEIGHT") = ArenaLayout::HEIGHT;
    m.attr("ARENA_CENTER_X") = ArenaLayout::CENTER_X;
    m.attr("ARENA_LEFT_LANE_X") = ArenaLayout::LEFT_LANE_X;
    m.attr("ARENA_RIGHT_LANE_X") = ArenaLayout::RIGHT_LANE_X;
    m.attr("ARENA_LEFT_BRIDGE_X") = ArenaLayout::LEFT_BRIDGE_X;
    m.attr("ARENA_RIGHT_BRIDGE_X") = ArenaLayout::RIGHT_BRIDGE_X;
    m.attr("ARENA_BRIDGE_Y") = ArenaLayout::BRIDGE_Y;
    m.def("arena_king_y", &ArenaLayout::kingY, py::arg("team"),
          "Row of `team`'s King Tower.");
    m.def("arena_princess_y", &ArenaLayout::princessY, py::arg("team"),
          "Row of `team`'s two Princess Towers.");

    // --- elixir phase schedule ---
    // Module-level because its main consumers have no env: perception's
    // observation encoder, which must write the same scalar the training
    // encoder does, and the replay viewer.
    m.attr("MAX_ELIXIR_MULTIPLIER") = GameManager::MAX_ELIXIR_MULTIPLIER;
    m.def("elixir_multiplier_at_tick", &GameManager::elixirMultiplierAtTick,
          py::arg("tick"),
          "Elixir regeneration multiplier (1.0 / 2.0 / 3.0) in force at `tick`. "
          "10 ticks = 1 second: double from 2:00, triple from 3:00.");
    // The level section's encoding (UPSTREAM item 44), so perception's encoder
    // derives it rather than restating (level - 11) / 5.
    m.attr("TOURNAMENT_LEVEL") = CardLevels::TOURNAMENT_LEVEL;
    m.def("level_scalar", &ClashEnv::levelScalar, py::arg("level"),
          "What a card or tower level reads in the observation's level section: "
          "(level - TOURNAMENT_LEVEL) / (MAX_LEVEL - TOURNAMENT_LEVEL).");

    py::class_<StepResult>(m, "StepResult")
        .def_readonly("observation", &StepResult::observation)
        .def_readonly("reward", &StepResult::reward)
        .def_readonly("done", &StepResult::done);

    py::class_<SelfPlayStepResult>(m, "SelfPlayStepResult")
        .def_readonly("observation0", &SelfPlayStepResult::observation0)
        .def_readonly("observation1", &SelfPlayStepResult::observation1)
        .def_readonly("reward0", &SelfPlayStepResult::reward0)
        .def_readonly("done", &SelfPlayStepResult::done);

    // No observation fields: a rollout gets this back, and neither vector is
    // built.
    py::class_<SelfPlayFastResult>(m, "SelfPlayFastResult")
        .def_readonly("reward0", &SelfPlayFastResult::reward0)
        .def_readonly("done", &SelfPlayFastResult::done);

    py::enum_<TowerTroopType>(m, "TowerTroopType")
        .value("NONE", TowerTroopType::None)
        .value("TOWER_PRINCESS", TowerTroopType::TowerPrincess)
        .value("CANNONEER", TowerTroopType::Cannoneer)
        .value("DAGGER_DUCHESS", TowerTroopType::DaggerDuchess)
        .value("ROYAL_CHEF", TowerTroopType::RoyalChef);

    // --- opponent state (UPSTREAM item 46) ---
    // The estimator ClashEnv runs inside training is the same class live
    // perception drives (live_ppo/opp_state.py), so the two never drift.
    using OSE = OpponentStateEstimator;
    py::class_<OSE::Config>(m, "OpponentStateConfig")
        .def(py::init<>())
        .def_readwrite("regen_per_tick", &OSE::Config::regenPerTick)
        .def_readwrite("assumed_miss_rate", &OSE::Config::assumedMissRate)
        .def_readwrite("missed_play_mean_cost", &OSE::Config::missedPlayMeanCost)
        .def_readwrite("unknown_cost_min", &OSE::Config::unknownCostMin)
        .def_readwrite("unknown_cost_mean", &OSE::Config::unknownCostMean)
        .def_readwrite("unknown_cost_max", &OSE::Config::unknownCostMax);
    py::class_<OSE::Report>(m, "OpponentReport")
        .def(py::init<>())
        .def_readwrite("tick", &OSE::Report::tick)
        .def_readwrite("kind", &OSE::Report::kind)
        .def_readwrite("card_id", &OSE::Report::cardId)
        .def_readwrite("cost", &OSE::Report::cost)
        .def_readwrite("evolved", &OSE::Report::evolved)
        .def_readwrite("seq", &OSE::Report::seq);
    py::class_<OSE::AbilityState>(m, "OpponentAbilityState")
        .def_readonly("card_id", &OSE::AbilityState::cardId)
        .def_readonly("last_activation_tick", &OSE::AbilityState::lastActivationTick)
        .def_readonly("cooldown_ticks", &OSE::AbilityState::cooldownTicks)
        .def_readonly("cost", &OSE::AbilityState::cost);
    py::class_<OSE>(m, "OpponentStateEstimator")
        .def(py::init<>())
        .def(py::init<const OSE::Config&>(), py::arg("config"))
        .def("reset", &OSE::reset, py::arg("tick") = 0)
        .def("reset_unknown", &OSE::resetUnknown, py::arg("tick"),
             "Start mid-match with the bar unknown: [0, 10], low confidence.")
        .def("declare_evolution", &OSE::declareEvolution, py::arg("evo_id"))
        .def("advance_to", &OSE::advanceTo, py::arg("tick"), py::arg("multiplier"))
        .def("advance_with_schedule", &OSE::advanceWithSchedule, py::arg("tick"),
             "Regen to `tick` on the engine's double/triple schedule.")
        .def("report", &OSE::report, py::arg("report"))
        .def("report_play", &OSE::reportPlay, py::arg("tick"), py::arg("card_id"),
             py::arg("cost") = -1.0f, py::arg("evolved") = -1)
        .def("report_ability", &OSE::reportAbility, py::arg("tick"), py::arg("card_id"),
             py::arg("cost") = -1.0f)
        .def_property_readonly("tick", &OSE::tick)
        .def("estimate", &OSE::estimate)
        .def("lower", &OSE::lower)
        .def("upper", &OSE::upper)
        .def("hard_upper", &OSE::hardUpper, "The bar if every unreported play cost nothing.")
        .def("sigma", &OSE::sigma)
        .def("confidence", &OSE::confidence)
        .def("contradictions", &OSE::contradictions)
        .def("last_contradiction_tick", &OSE::lastContradictionTick)
        .def("plays", &OSE::plays)
        .def("elixir_features", &OSE::elixirFeatures)
        .def("evo_charge_for", &OSE::evoChargeFor, py::arg("evo_id"))
        .def("cycles_until_evolved_for", &OSE::cyclesUntilEvolvedFor, py::arg("evo_id"))
        .def("known_evolutions", &OSE::knownEvolutions)
        .def("ability_states", [](const OSE& e) {
            const auto& a = e.abilityStates();
            return std::vector<OSE::AbilityState>(a.begin(), a.end());
        })
        .def("evo_blocks", [](const OSE& e) {
            std::vector<float> v(2 * ClashEnv::NUM_CARD_IDS, 0.0f);
            e.writeEvoBlocks(v.data(), ClashEnv::NUM_CARD_IDS);
            return v;
        }, "known[c] then charge[c], as the observation's OPP_EVO block.")
        .def("ability_features", [](const OSE& e) {
            std::vector<float> v(OSE::MAX_TRACKED_ABILITY_CARDS * OSE::ABILITY_FEATURES, 0.0f);
            e.writeAbilityFeatures(v.data());
            return v;
        }, "The observation's OPP_ABILITY block.")
        .def_property("config", &OSE::config, &OSE::setConfig)
        .def_static("evo_charge", &OSE::evoCharge, py::arg("cycles_until_evolved"), py::arg("threshold"))
        .def_static("base_id_of", &OSE::baseIdOf, py::arg("card_id"))
        .def_static("evolution_id_of", &OSE::evolutionIdOf, py::arg("base_id"))
        .def_readonly_static("KIND_PLAY", &OSE::KIND_PLAY)
        .def_readonly_static("KIND_ABILITY", &OSE::KIND_ABILITY)
        .def_readonly_static("MAX_TRACKED_ABILITY_CARDS", &OSE::MAX_TRACKED_ABILITY_CARDS)
        .def_readonly_static("ABILITY_FEATURES", &OSE::ABILITY_FEATURES)
        .def_readonly_static("ELIXIR_FEATURES", &OSE::ELIXIR_FEATURES)
        .def_readonly_static("ABILITY_RECENCY_TAU_TICKS", &OSE::ABILITY_RECENCY_TAU_TICKS)
        .def_readonly_static("REPLAY_WINDOW_TICKS", &OSE::REPLAY_WINDOW_TICKS);

    using DN = ClashEnv::DetectionNoise;
    py::class_<DN>(m, "OppDetectionNoise")
        .def(py::init<>())
        .def_readwrite("enabled", &DN::enabled)
        .def_readwrite("miss_troop", &DN::missTroop)
        .def_readwrite("miss_spell", &DN::missSpell)
        .def_readwrite("unknown_card", &DN::unknownCard)
        .def_readwrite("false_play", &DN::falsePlay)
        .def_readwrite("troop_emit_ticks", &DN::troopEmitTicks)
        .def_readwrite("birth_err_min_ticks", &DN::birthErrMinTicks)
        .def_readwrite("birth_err_max_ticks", &DN::birthErrMaxTicks)
        .def_readwrite("late_troop", &DN::lateTroop)
        .def_readwrite("late_extra_ticks", &DN::lateExtraTicks)
        .def_readwrite("spell_sight_max_ticks", &DN::spellSightMaxTicks)
        .def_readwrite("spell_emit_min_ticks", &DN::spellEmitMinTicks)
        .def_readwrite("spell_emit_max_ticks", &DN::spellEmitMaxTicks)
        .def_readwrite("evo_recognised", &DN::evoRecognised)
        .def_readwrite("ability_detected", &DN::abilityDetected)
        .def_readwrite("ability_emit_ticks", &DN::abilityEmitTicks);
    py::class_<ClashEnv::DeliveredReport>(m, "OppDeliveredReport")
        .def_readonly("true_tick", &ClashEnv::DeliveredReport::trueTick)
        .def_readonly("emit_tick", &ClashEnv::DeliveredReport::emitTick)
        .def_readonly("report", &ClashEnv::DeliveredReport::report)
        .def_readonly("true_card_id", &ClashEnv::DeliveredReport::trueCardId)
        .def_readonly("true_evolved", &ClashEnv::DeliveredReport::trueEvolved);

    py::class_<ClashEnv>(m, "ClashRoyaleEnv")
        .def(py::init<const std::vector<int>&, const std::vector<int>&, int, TowerTroopType, TowerTroopType>(),
            py::arg("ai_deck"), py::arg("opp_deck"), py::arg("max_ticks") = 1800,
            py::arg("ai_tower_troop") = TowerTroopType::None, py::arg("opp_tower_troop") = TowerTroopType::None)
        .def("reset", &ClashEnv::reset)
        .def("step", &ClashEnv::step, py::arg("card_index"), py::arg("target_x"), py::arg("target_y"),
            py::arg("skip_frames") = 10,
            py::arg("activate_ability_slot1") = false, py::arg("activate_ability_slot2") = false)
        .def("step_self_play", &ClashEnv::stepSelfPlay,
            py::arg("card_index0"), py::arg("target_x0"), py::arg("target_y0"),
            py::arg("card_index1"), py::arg("target_x1"), py::arg("target_y1"),
            py::arg("skip_frames") = 10,
            py::arg("activate_ability0_slot1") = false, py::arg("activate_ability0_slot2") = false,
            py::arg("activate_ability1_slot1") = false, py::arg("activate_ability1_slot2") = false)
        // The same advance with no observations built, for rollouts (see
        // SelfPlayFastResult in ClashEnv.h).
        .def("step_self_play_fast", &ClashEnv::stepSelfPlayFast,
            py::arg("card_index0"), py::arg("target_x0"), py::arg("target_y0"),
            py::arg("card_index1"), py::arg("target_x1"), py::arg("target_y1"),
            py::arg("skip_frames") = 10,
            py::arg("activate_ability0_slot1") = false, py::arg("activate_ability0_slot2") = false,
            py::arg("activate_ability1_slot1") = false, py::arg("activate_ability1_slot2") = false)
        // slot: 1 = Heroic, 2 = Wild Card (CardRegistry::validateDeckSlots).
        .def("is_champion_ability_ready", &ClashEnv::isChampionAbilityReady, py::arg("team"), py::arg("slot") = 1)
        .def("activate_champion_ability", &ClashEnv::activateChampionAbility, py::arg("team"), py::arg("slot") = 1)
        .def("get_hand", &ClashEnv::getHand)
        .def("get_hand_for_team", &ClashEnv::getHandForTeam, py::arg("team"))
        .def("get_elixir", &ClashEnv::getElixir)
        .def("get_elixir_for_team", &ClashEnv::getElixirForTeam, py::arg("team"))
        .def("get_observation_for_team", &ClashEnv::getObservationForTeam, py::arg("team"))
        .def("is_game_over", &ClashEnv::isGameOver)
        .def("observation_size", &ClashEnv::observationSize)
        .def("set_opp_detection_noise", &ClashEnv::setOppDetectionNoise, py::arg("noise"),
             "The simulated detector feeding the opponent-state estimate; from the next reset().")
        .def("get_opp_detection_noise", &ClashEnv::getOppDetectionNoise)
        .def("get_opp_state_estimate", &ClashEnv::getOppStateEstimator, py::arg("team"),
             py::return_value_policy::copy,
             "A copy of what `team` believes about its opponent, synced to now.")
        .def("get_opp_reports", &ClashEnv::getOppReports, py::arg("team"),
             py::return_value_policy::copy,
             "Every report delivered so far to `team`'s estimator, with the truth.")
        .def("inject_enemy", &ClashEnv::injectEnemy, py::arg("card_id"), py::arg("x"), py::arg("y"))
        // Defaults keep full health and a full deploy delay. Pass
        // deploy_ticks=0 for a unit perception can already see, or every
        // rollout grants it an extra second.
        .def("inject", &ClashEnv::inject, py::arg("card_id"), py::arg("x"), py::arg("y"),
             py::arg("team"), py::arg("hp") = -1.0f, py::arg("deploy_ticks") = -1)
        .def("set_opponent_deck", &ClashEnv::setOpponentDeck, py::arg("deck"))
        .def("set_opponent_elixir_multiplier", &ClashEnv::setOpponentElixirMultiplier, py::arg("multiplier"))
        // Card level 1..16 for `team` (CardLevels.h); tower_level -1 follows it.
        // Towers change at the next reset(). Returns False on a bad value.
        .def("set_team_level", &ClashEnv::setTeamLevel, py::arg("team"), py::arg("card_level"),
            py::arg("tower_level") = -1)
        .def("get_team_card_level", &ClashEnv::getTeamCardLevel, py::arg("team"))
        .def("get_team_tower_level", &ClashEnv::getTeamTowerLevel, py::arg("team"))
        .def("save_log", &ClashEnv::saveLog, py::arg("filepath"))
        .def("get_troop_damage_dealt", &ClashEnv::getTroopDamageDealt, py::arg("team"))
        .def("get_building_damage_dealt", &ClashEnv::getBuildingDamageDealt, py::arg("team"))
        .def("get_tower_damage_dealt", &ClashEnv::getTowerDamageDealt, py::arg("team"))
        .def("get_elixir_value_killed_by", &ClashEnv::getElixirValueKilledBy,
             py::arg("card_id"), py::arg("team"))
        .def("get_elixir_spent_on_card", &ClashEnv::getElixirSpentOnCard,
             py::arg("card_id"), py::arg("team"))
        // Cumulative damage dealt by one card, for the win-condition damage
        // term; see ClashEnv::getDamageDealtByCard.
        .def("get_damage_dealt_by_card", &ClashEnv::getDamageDealtByCard,
             py::arg("card_id"), py::arg("team"))
        // State-estimator write interface, so search evaluates the real
        // position rather than reset()'s. set_hand_for_team returns False and
        // changes nothing on an invalid hand: check it.
        .def("set_elixir_for_team", &ClashEnv::setElixirForTeam,
             py::arg("team"), py::arg("value"))
        .def("set_hand_for_team", &ClashEnv::setHandForTeam,
             py::arg("team"), py::arg("cards"))
        // Tower HP, the match clock and destruction
        // (perception/UPSTREAM_REQUESTS.md item 22).
        //
        // slot is 0 = King, 1 = left Princess, 2 = right Princess, in board
        // coordinates for both teams.
        //
        // set_tower_hp takes engine-absolute hp and refuses hp <= 0. Tower
        // levels differ between the engine and a real account, by a different
        // factor per player, so perception reports a fraction and multiplies by
        // get_tower_max_hp. Destroying a tower is destroy_tower's job (crown,
        // King wake-up, lane retargeting).
        .def("set_tower_hp", &ClashEnv::setTowerHp,
             py::arg("team"), py::arg("slot"), py::arg("hp"))
        .def("destroy_tower", &ClashEnv::destroyTower,
             py::arg("team"), py::arg("slot"))
        .def("get_tower_hp", &ClashEnv::getTowerHp,
             py::arg("team"), py::arg("slot"))
        .def("get_tower_max_hp", &ClashEnv::getTowerMaxHp,
             py::arg("team"), py::arg("slot"))
        // Clamped to [0, max_ticks]; sets both engine clocks.
        .def("set_current_tick", &ClashEnv::setCurrentTick, py::arg("tick"))
        .def("get_current_tick", &ClashEnv::getCurrentTick)
        // The elixir phase (1.0 / 2.0 / 3.0) now.
        .def("get_elixir_multiplier", &ClashEnv::getElixirMultiplier)
        // Card-cycle tracking (item 24). `inject` bypasses playCard, where the
        // cycle is recorded, so the live mirror reports plays through
        // note_played_card.
        .def("note_played_card", &ClashEnv::notePlayedCard,
             py::arg("team"), py::arg("card_id"))
        .def("get_last_played_tick", &ClashEnv::getLastPlayedTick,
             py::arg("team"), py::arg("card_id"))
        // Reproducible episodes: seeds both engine generators and re-deals
        // (ClashEnv::seed).
        .def("seed", &ClashEnv::seed, py::arg("seed"))
        .def("get_elixir_spent", &ClashEnv::getElixirSpent, py::arg("team"))
        // Surviving towers (King and Princesses) for one team; see
        // ClashEnv::getTowersAlive.
        .def("get_towers_alive", &ClashEnv::getTowersAlive, py::arg("team"))
        // The full timeout verdict: tower count, then weakest surviving tower,
        // then draw. Returns loserTeam: -1 draw, else the losing team. See
        // ClashEnv::resolveTimeoutOutcome.
        .def("resolve_timeout_outcome", &ClashEnv::resolveTimeoutOutcome)
        // The engine's enforced placement bounds, for scaling the action space.
        .def("get_max_placement_x", &ClashEnv::getMaxPlacementX)
        .def("get_own_half_max_y", &ClashEnv::getOwnHalfMaxY)
        .def("is_valid_placement", &ClashEnv::isValidPlacementForCard,
             py::arg("card_id"), py::arg("x"), py::arg("y"), py::arg("team") = 0,
             "Would playCard accept this card at this point? The exact "
             "predicate playCard uses, so the Python placement mask derives "
             "from the engine's legality rule. Read-only.")
        // A deep copy for decision-time search. Moved into the Python object
        // rather than copied a second time.
        .def("snapshot", &ClashEnv::snapshot, py::return_value_policy::move,
             "Independent deep copy of this environment, for decision-time "
             "search. Stepping the copy cannot affect the original: entities "
             "and stats collectors are duplicated, and projectiles in flight "
             "are re-pointed at the copy's own targets. Cumulative statistics "
             "carry over; the replay log does not.")
        // Structural constants, as read-only class attributes, so Python
        // derives its dimensions from them.
        .def_readonly_static("BOARD_WIDTH", &ClashEnv::BOARD_WIDTH)
        .def_readonly_static("BOARD_HEIGHT", &ClashEnv::BOARD_HEIGHT)
        .def_readonly_static("NUM_CHANNELS", &ClashEnv::NUM_CHANNELS)
        .def_readonly_static("HAND_SIZE", &ClashEnv::HAND_SIZE)
        .def_readonly_static("NUM_CARD_IDS", &ClashEnv::NUM_CARD_IDS)
        .def_readonly_static("MAX_TROOP_HP", &ClashEnv::MAX_TROOP_HP)
        .def_readonly_static("MAX_BUILDING_HP", &ClashEnv::MAX_BUILDING_HP)
        // Attribute-channel indices; the Python side indexes the raw
        // observation with them.
        .def_readonly_static("CH_COUNT", &ClashEnv::CH_COUNT)
        .def_readonly_static("CH_FLYING", &ClashEnv::CH_FLYING)
        .def_readonly_static("CH_ANTIAIR", &ClashEnv::CH_ANTIAIR)
        .def_readonly_static("CH_DPS", &ClashEnv::CH_DPS)
        .def_readonly_static("CH_RANGE", &ClashEnv::CH_RANGE)
        .def_readonly_static("CH_SPEED", &ClashEnv::CH_SPEED)
        // Opponent card-cycle block sizes (item 24); MicroRoyaleNet sizes its
        // scalar input from them.
        .def_readonly_static("NUM_CYCLE_BLOCKS", &ClashEnv::NUM_CYCLE_BLOCKS)
        .def_readonly_static("CYCLE_BLOCK_SIZE", &ClashEnv::CYCLE_BLOCK_SIZE)
        .def_readonly_static("CYCLE_RECENCY_TAU_TICKS", &ClashEnv::CYCLE_RECENCY_TAU_TICKS)
        // Forward offsets into the observation: an append cannot move them.
        .def_readonly_static("EXTRA_SCALARS_START", &ClashEnv::EXTRA_SCALARS_START)
        .def_readonly_static("CYCLE_START", &ClashEnv::CYCLE_START)
        // The level section (UPSTREAM item 44), appended after the cycle blocks.
        .def_readonly_static("LEVEL_START", &ClashEnv::LEVEL_START)
        .def_readonly_static("NUM_LEVEL_SCALARS", &ClashEnv::NUM_LEVEL_SCALARS)
        // The opponent-state section (UPSTREAM item 46), after the level section.
        .def_readonly_static("OPP_STATE_START", &ClashEnv::OPP_STATE_START)
        .def_readonly_static("OPP_STATE_SIZE", &ClashEnv::OPP_STATE_SIZE)
        .def_readonly_static("OPP_ELIXIR_OFFSET", &ClashEnv::OPP_ELIXIR_OFFSET)
        .def_readonly_static("OWN_EVO_OFFSET", &ClashEnv::OWN_EVO_OFFSET)
        .def_readonly_static("OWN_ABILITY_OFFSET", &ClashEnv::OWN_ABILITY_OFFSET)
        .def_readonly_static("OPP_ABILITY_OFFSET", &ClashEnv::OPP_ABILITY_OFFSET)
        .def_readonly_static("OPP_EVO_OFFSET", &ClashEnv::OPP_EVO_OFFSET)
        .def_readonly_static("NUM_ABILITY_SLOTS", &ClashEnv::NUM_ABILITY_SLOTS)
        .def_readonly_static("NUM_OPP_ABILITY_CARDS", &ClashEnv::NUM_OPP_ABILITY_CARDS)
        .def_readonly_static("ABILITY_FEATURES", &ClashEnv::ABILITY_FEATURES)
        .def_readonly_static("NUM_EXTRA_SCALARS", &ClashEnv::NUM_EXTRA_SCALARS)
        .def_readonly_static("MAX_MATCH_ELIXIR", &ClashEnv::MAX_MATCH_ELIXIR)
        // Elixir phase boundaries in ticks, from GameManager.
        .def_readonly_static("DOUBLE_ELIXIR_TICK", &GameManager::DOUBLE_ELIXIR_TICK)
        .def_readonly_static("TRIPLE_ELIXIR_TICK", &GameManager::TRIPLE_ELIXIR_TICK)
        // Regen per tick at 1x, and the match clock, so Python reads them
        // instead of restating them.
        .def_readonly_static("ELIXIR_REGEN_RATE", &GameManager::ELIXIR_REGEN_RATE)
        .def_readonly_static("REGULATION_END_TICK", &MatchRules::REGULATION_END_TICK)
        .def_readonly_static("MATCH_END_TICK", &MatchRules::MATCH_END_TICK)
        .def_readonly_static("CYCLE_IN_COOLDOWN_TICKS", &PlayerState::CYCLE_IN_COOLDOWN_TICKS);

    m.def("get_all_card_ids", &getAllCardIds,
        "All ids CardRegistry currently has registered (real, playable cards only).");
    m.def("get_evolution_card_ids", &getEvolutionCardIds,
        "The Evolution ids, ascending. get_all_card_ids leaves them out.");
    m.def("face_card_id_of", &faceCardIdOf, py::arg("card_id"),
        "The card the screen shows for a play: an Evolution or Hero folds to its "
        "base card, anything else is itself. The cycle block records this id.");

    // Per-card registry facts: whether a card is a spell (spells are exempt
    // from the own-half rule), its cost (for the affordability mask), and
    // Champion / Hero flags. Read-only.
    m.def("get_card_info", [](int cardId) {
        const CardDefinition* def = CardRegistry::getInstance().getCard(cardId);
        if (!def) {
            throw std::invalid_argument("get_card_info: unknown card id " + std::to_string(cardId));
        }
        py::dict info;
        info["id"] = def->id;
        info["name"] = def->name;
        info["cost"] = def->cost;
        info["is_spell"] = def->isSpell;
        info["is_building"] = def->isBuilding;
        info["placement_radius"] = def->placementRadius;
        info["deploy_anywhere"] = def->deployAnywhere;
        info["is_champion"] = def->isChampion;
        info["is_hero"] = def->isHero;
        info["is_evolution"] = def->isEvolution;
        return info;
    }, py::arg("card_id"),
        "Registry facts for one card id: name, cost, is_spell, is_building, "
        "placement_radius, deploy_anywhere, is_champion, is_hero, is_evolution.");

    // Card-stats audit (patch 0003): the full CardStats behind a card id.
    // {"base": stats, "evolved": stats | None, "evolution_cycle_threshold",
    //  "evolved_uses", "units": {...}}. Units: ticks (10/s), tiles; speeds in
    // engine tiles/tick plus the wiki tier number they came from.
    m.def("get_card_stats", [](int cardId) {
        const CardDefinition* def = CardRegistry::getInstance().getCard(cardId);
        if (!def || !def->stats) {
            throw std::invalid_argument("get_card_stats: unknown card id " + std::to_string(cardId));
        }
        py::dict out;
        out["id"] = def->id;
        out["name"] = def->name;
        out["is_evolution"] = def->isEvolution;
        out["base"] = statsDict(*def->stats);
        out["evolved"] = def->evolvedStats ? py::object(statsDict(*def->evolvedStats)) : py::none();
        out["evolution_cycle_threshold"] = def->evolutionCycleThreshold;
        out["evolved_uses"] = def->evolvedUsesGranted;
        py::dict units;
        units["ticks_per_second"] = 10;
        units["distance"] = "tiles (ranges, radii, offsets); arena 18 x 34";
        units["movement_speed_scale"] = MOVEMENT_SPEED_SCALE;
        units["real_tiles_per_min_to_engine"] = REAL_TILES_PER_MIN_TO_ENGINE;
        units["speed_tiers_wiki"] = py::make_tuple(30, 45, 60, 90, 120);
        units["default_deploy_ticks"] = DEPLOY_TIME_TICKS;
        out["units"] = units;
        return out;
    }, py::arg("card_id"),
        "Engine CardStats for one card id (base, evolved form, hero ability child), in engine units.");

    // The slot-legality check GameManager::reset and setOpponentDeck enforce,
    // so Python validates decks without restating the rules. "" for a legal
    // deck, else the reason.
    m.def("validate_deck_slots", &validateDeckSlots,
        "Returns \"\" if `deck` (8 card ids) satisfies Evolution/Champion slot-position rules, else an error string.");

    // A random deck that is legal by construction. With a seed, drawn from a
    // generator private to the call: the process-global stream is shared by
    // every env in the process, so seeding it would only be reproducible for a
    // fixed call order. Without one, the global stream as before. This
    // generator is the one ClashEnv::seed cannot reach (item 23C).
    m.def("sample_random_deck", [](std::optional<unsigned int> seed) {
        if (seed.has_value()) {
            std::mt19937 local(seed.value());
            return sampleRandomDeck(local);
        }
        static std::mt19937 rng(std::random_device{}());
        return sampleRandomDeck(rng);
    }, py::arg("seed") = py::none(),
       "Builds a random 8-card deck that always satisfies Evolution/Champion slot-position rules by construction. "
       "Pass seed= for a reproducible deck drawn from a generator private to this call; omit it for the shared "
       "process-global stream.");
}
