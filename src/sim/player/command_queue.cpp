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
        && command.type != PlayerCommandType::DestroyBuilding) {
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
        if (town_center_matches_slot(registry, command.target_entity, command.player_slot)) {
            issue_spawn_militia_order(registry, command.target_entity);
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
    case PlayerCommandType::BuildLumberjack:
        issue_build_lumberjack_order(
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
    case PlayerCommandType::DestroyBuilding:
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
