#pragma once

#include "core/grid.hpp"
#include "math/fixed.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"

#include <cstdint>
#include <entt/entt.hpp>
#include <optional>
#include <span>
#include <vector>

namespace aoa::sim::player {

enum class PlayerCommandType : std::uint8_t {
    Move = 0,
    Attack = 1,
    Gather = 2,
    Deposit = 3,
    SpawnWorker = 4,
};

struct PlayerCommand {
    std::uint64_t sequence{0U};
    std::uint64_t execute_tick{0U};
    std::uint8_t player_slot{0U};
    PlayerCommandType type{PlayerCommandType::Move};
    std::vector<entt::entity> unit_ids{};
    std::vector<snapshot::EntitySnapshotKey> unit_keys{};
    core::GridPos cell{};
    bool has_goal_world{false};
    math::Fixed goal_world_x{};
    math::Fixed goal_world_y{};
    entt::entity target_entity{entt::null};
    std::optional<snapshot::EntitySnapshotKey> target_entity_key{};
};

[[nodiscard]] std::vector<std::byte> encode_player_command(const PlayerCommand& command);

[[nodiscard]] std::vector<std::byte> encode_player_command_with_keys(const PlayerCommand& command);

[[nodiscard]] std::optional<PlayerCommand> decode_player_command(std::span<const std::byte> bytes);

[[nodiscard]] std::optional<PlayerCommand> decode_player_command_with_keys(std::span<const std::byte> bytes);

[[nodiscard]] std::optional<PlayerCommand> decode_player_command_body(std::span<const std::byte>& cursor);

} // namespace aoa::sim::player
