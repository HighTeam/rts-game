#include "sim/player/command_queue.hpp"

#include "sim/player/player_commands.hpp"

#include <algorithm>

namespace aoa::sim::player {

namespace {

void apply_player_command(entt::registry& registry, const PlayerCommand& command)
{
    switch (command.type) {
    case PlayerCommandType::Move:
        issue_move_orders(registry, command.unit_ids, command.cell);
        break;
    case PlayerCommandType::Attack:
        issue_attack_orders(registry, command.unit_ids, command.target_entity);
        break;
    case PlayerCommandType::Gather:
        issue_gather_orders(registry, command.unit_ids, command.cell);
        break;
    case PlayerCommandType::Deposit:
        issue_deposit_orders(registry, command.unit_ids);
        break;
    case PlayerCommandType::SpawnWorker:
        issue_spawn_worker_order(registry, command.target_entity);
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

    for (const PlayerCommand& command : due) {
        apply_player_command(registry, command);
    }
}

} // namespace aoa::sim::player
