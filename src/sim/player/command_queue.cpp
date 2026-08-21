#include "sim/player/command_queue.hpp"

#include "sim/components/player_slot.hpp"
#include "sim/components/tags.hpp"
#include "sim/player/player_commands.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"

#include <algorithm>

namespace aoa::sim::player {

namespace {

std::vector<entt::entity> filter_command_units(
    entt::registry& registry,
    const std::vector<entt::entity>& unit_ids,
    const std::uint8_t player_slot)
{
    std::vector<entt::entity> filtered{};
    filtered.reserve(unit_ids.size());

    for (const entt::entity entity : unit_ids) {
        if (!registry.valid(entity) || !registry.any_of<components::PlayerOwnedTag>(entity)) {
            continue;
        }

        if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        filtered.push_back(entity);
    }

    return filtered;
}

bool town_center_matches_slot(
    entt::registry& registry,
    const entt::entity town_center,
    const std::uint8_t player_slot)
{
    if (!registry.valid(town_center)) {
        return false;
    }

    if (!registry.all_of<components::TownCenterTag, components::PlayerOwnedTag>(town_center)) {
        return false;
    }

    return components::entity_player_slot(registry, town_center) == player_slot;
}

void sync_input_log_keys(
    std::vector<PlayerCommand>& input_log,
    const PlayerCommand& command)
{
    for (PlayerCommand& logged : input_log) {
        if (logged.sequence != command.sequence) {
            continue;
        }

        logged.unit_keys = command.unit_keys;
        logged.target_entity_key = command.target_entity_key;
        return;
    }
}

bool command_needs_entity_key_annotation(const PlayerCommand& command)
{
    if (!command.unit_ids.empty() && command.unit_keys.size() != command.unit_ids.size()) {
        return true;
    }

    if (command.type != PlayerCommandType::Attack && command.type != PlayerCommandType::SpawnWorker
        && command.type != PlayerCommandType::SpawnMilitia
        && command.type != PlayerCommandType::SpawnMage
        && command.type != PlayerCommandType::DestroyBuilding
        && command.type != PlayerCommandType::Garrison
        && command.type != PlayerCommandType::UnloadGarrison
        && command.type != PlayerCommandType::AdvanceAge
        && command.type != PlayerCommandType::ResumeBuild
        && command.type != PlayerCommandType::RenewFarm
        && command.type != PlayerCommandType::ResearchCartography
        && command.type != PlayerCommandType::ResearchTrades
        && command.type != PlayerCommandType::ResearchSpy
        && command.type != PlayerCommandType::MarketSellWood
        && command.type != PlayerCommandType::MarketSellFood
        && command.type != PlayerCommandType::MarketBuyWood
        && command.type != PlayerCommandType::MarketBuyFood) {
        return false;
    }

    return command.target_entity != entt::null && !command.target_entity_key.has_value();
}

void apply_player_command(entt::registry& registry, PlayerCommand command)
{
    if (command_needs_entity_key_annotation(command)) {
        snapshot::annotate_command_entity_keys(registry, command);
    }

    snapshot::resolve_command_entity_ids(registry, command);

    switch (command.type) {
    case PlayerCommandType::Move:
        issue_move_orders(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell,
            command.has_goal_world,
            command.goal_world_x,
            command.goal_world_y);
        break;
    case PlayerCommandType::Attack:
        issue_attack_orders(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.target_entity);
        break;
    case PlayerCommandType::Gather:
        issue_gather_orders(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::Deposit:
        issue_deposit_orders(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot));
        break;
    case PlayerCommandType::SpawnWorker:
        if (town_center_matches_slot(registry, command.target_entity, command.player_slot)) {
            issue_spawn_worker_order(registry, command.target_entity);
        }
        break;
    case PlayerCommandType::SpawnMilitia:
        if (registry.valid(command.target_entity)
            && registry.all_of<components::BarracksTag, components::PlayerOwnedTag>(
                command.target_entity)
            && components::entity_player_slot(registry, command.target_entity)
                == command.player_slot) {
            issue_spawn_militia_order(registry, command.target_entity);
        }
        break;
    case PlayerCommandType::SpawnMage:
        if (registry.valid(command.target_entity)
            && registry.all_of<components::MageAcademyTag, components::PlayerOwnedTag>(
                command.target_entity)
            && components::entity_player_slot(registry, command.target_entity)
                == command.player_slot) {
            issue_spawn_mage_order(registry, command.target_entity);
        }
        break;
    case PlayerCommandType::KillUnits:
        issue_kill_orders(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot));
        break;
    case PlayerCommandType::Stop:
        issue_stop_orders(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot));
        break;
    case PlayerCommandType::BuildTownCenter:
        issue_build_town_center_order(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::BuildHouse:
        issue_build_house_order(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::BuildLumberCamp:
        issue_build_lumber_camp_order(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::BuildExtractor:
        issue_build_extractor_order(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::BuildMill:
        issue_build_mill_order(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::BuildMiningCamp:
        issue_build_mining_camp_order(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::BuildBarracks:
        issue_build_barracks_order(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::BuildMageAcademy:
        issue_build_mage_academy_order(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::BuildTower:
        issue_build_tower_order(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::BuildMarket:
        issue_build_market_order(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::BuildGarden:
        issue_build_garden_order(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::BuildReservoir:
        issue_build_reservoir_order(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::BuildFarm:
        issue_build_farm_order(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.cell);
        break;
    case PlayerCommandType::Garrison:
        issue_garrison_orders(
            registry,
            filter_command_units(registry, command.unit_ids, command.player_slot),
            command.target_entity);
        break;
    case PlayerCommandType::UnloadGarrison:
        if (town_center_matches_slot(registry, command.target_entity, command.player_slot)) {
            issue_unload_garrison_order(registry, command.target_entity);
        }
        break;
    case PlayerCommandType::AdvanceAge:
        if (town_center_matches_slot(registry, command.target_entity, command.player_slot)) {
            issue_advance_age_order(registry, command.target_entity);
        }
        break;
    case PlayerCommandType::CheatGrantResources:
        issue_cheat_oknocraft_infinity(registry);
        break;
    case PlayerCommandType::ResumeBuild:
        if (registry.valid(command.target_entity)
            && registry.all_of<components::BuildingTag, components::PlayerOwnedTag>(
                command.target_entity)
            && components::entity_player_slot(registry, command.target_entity)
                == command.player_slot) {
            issue_resume_build_order(
                registry,
                filter_command_units(registry, command.unit_ids, command.player_slot),
                command.target_entity);
        }
        break;
    case PlayerCommandType::RenewFarm:
        if (registry.valid(command.target_entity)
            && registry.all_of<
                components::FarmTag,
                components::BuildingTag,
                components::PlayerOwnedTag>(command.target_entity)
            && components::entity_player_slot(registry, command.target_entity)
                == command.player_slot) {
            issue_renew_farm_order(
                registry,
                filter_command_units(registry, command.unit_ids, command.player_slot),
                command.target_entity);
        }
        break;
    case PlayerCommandType::ResearchCartography:
        if (registry.valid(command.target_entity)
            && registry.all_of<components::MarketTag, components::PlayerOwnedTag>(
                command.target_entity)
            && components::entity_player_slot(registry, command.target_entity)
                == command.player_slot) {
            issue_research_cartography_order(registry, command.target_entity);
        }
        break;
    case PlayerCommandType::ResearchTrades:
        if (registry.valid(command.target_entity)
            && registry.all_of<components::MarketTag, components::PlayerOwnedTag>(
                command.target_entity)
            && components::entity_player_slot(registry, command.target_entity)
                == command.player_slot) {
            issue_research_trades_order(registry, command.target_entity);
        }
        break;
    case PlayerCommandType::ResearchSpy:
        if (registry.valid(command.target_entity)
            && registry.all_of<components::TownCenterTag, components::PlayerOwnedTag>(
                command.target_entity)
            && components::entity_player_slot(registry, command.target_entity)
                == command.player_slot) {
            issue_research_spy_order(registry, command.target_entity);
        }
        break;
    case PlayerCommandType::MarketSellWood:
        if (registry.valid(command.target_entity)
            && registry.all_of<components::MarketTag, components::PlayerOwnedTag>(
                command.target_entity)
            && components::entity_player_slot(registry, command.target_entity)
                == command.player_slot) {
            issue_market_sell_wood_order(registry, command.target_entity);
        }
        break;
    case PlayerCommandType::MarketSellFood:
        if (registry.valid(command.target_entity)
            && registry.all_of<components::MarketTag, components::PlayerOwnedTag>(
                command.target_entity)
            && components::entity_player_slot(registry, command.target_entity)
                == command.player_slot) {
            issue_market_sell_food_order(registry, command.target_entity);
        }
        break;
    case PlayerCommandType::MarketBuyWood:
        if (registry.valid(command.target_entity)
            && registry.all_of<components::MarketTag, components::PlayerOwnedTag>(
                command.target_entity)
            && components::entity_player_slot(registry, command.target_entity)
                == command.player_slot) {
            issue_market_buy_wood_order(registry, command.target_entity);
        }
        break;
    case PlayerCommandType::MarketBuyFood:
        if (registry.valid(command.target_entity)
            && registry.all_of<components::MarketTag, components::PlayerOwnedTag>(
                command.target_entity)
            && components::entity_player_slot(registry, command.target_entity)
                == command.player_slot) {
            issue_market_buy_food_order(registry, command.target_entity);
        }
        break;
    case PlayerCommandType::SendTrade:
        issue_send_trade_order(
            registry,
            command.player_slot,
            static_cast<std::uint8_t>(command.cell.x),
            command.cell.y,
            command.goal_world_x.to_int(),
            command.goal_world_y.to_int(),
            static_cast<int>(entt::to_integral(command.target_entity)));
        break;
    case PlayerCommandType::SetDiplomacy:
        issue_set_diplomacy_order(
            registry,
            command.player_slot,
            static_cast<std::uint8_t>(command.cell.x),
            command.cell.y != 0);
        break;
    case PlayerCommandType::Resign:
        issue_resign_order(registry, command.player_slot);
        break;
    case PlayerCommandType::DestroyBuilding:
        if (!registry.valid(command.target_entity)
            || !registry.any_of<components::BuildingTag>(command.target_entity)) {
            command.target_entity = snapshot::find_owned_building_at_cell(
                registry, command.player_slot, command.cell);
        }
        if (town_center_matches_slot(registry, command.target_entity, command.player_slot)
            || (registry.valid(command.target_entity)
                && registry.all_of<components::BuildingTag, components::PlayerOwnedTag>(
                    command.target_entity)
                && components::entity_player_slot(registry, command.target_entity)
                    == command.player_slot)) {
            issue_destroy_building_order(registry, command.target_entity);
        }
        break;
    }
}

} // namespace

void CommandQueue::enqueue(PlayerCommand command)
{
    command.sequence = next_sequence_++;
    input_log_.push_back(command);
    pending_.push_back(std::move(command));
}

void CommandQueue::enqueue_network(PlayerCommand command)
{
    input_log_.push_back(command);
    pending_.push_back(std::move(command));

    if (command.sequence >= next_sequence_) {
        next_sequence_ = command.sequence + 1U;
    }
}

void CommandQueue::apply_pending(entt::registry& registry, const std::uint64_t tick)
{
    std::vector<PlayerCommand> due{};
    due.reserve(pending_.size());

    for (PlayerCommand& command : pending_) {
        if (command.execute_tick != tick) {
            continue;
        }

        due.push_back(command);
    }

    pending_.erase(
        std::remove_if(
            pending_.begin(),
            pending_.end(),
            [tick](const PlayerCommand& command) { return command.execute_tick == tick; }),
        pending_.end());

    std::sort(due.begin(), due.end(), [](const PlayerCommand& left, const PlayerCommand& right) {
        if (left.sequence != right.sequence) {
            return left.sequence < right.sequence;
        }

        if (left.player_slot != right.player_slot) {
            return left.player_slot < right.player_slot;
        }

        return static_cast<std::uint8_t>(left.type) < static_cast<std::uint8_t>(right.type);
    });

    for (PlayerCommand command : due) {
        apply_player_command(registry, command);
        sync_input_log_keys(input_log_, command);
    }
}

void CommandQueue::restore_input_log(
    std::vector<PlayerCommand> log,
    const std::uint64_t next_sequence)
{
    input_log_ = std::move(log);
    pending_ = input_log_;
    next_sequence_ = next_sequence;
}

void CommandQueue::clear_pending()
{
    pending_.clear();
}

} // namespace aoa::sim::player
