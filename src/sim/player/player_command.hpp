#pragma once

#include "core/grid.hpp"

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
    core::GridPos cell{};
    entt::entity target_entity{entt::null};
};

[[nodiscard]] std::vector<std::byte> encode_player_command(const PlayerCommand& command);

[[nodiscard]] std::optional<PlayerCommand> decode_player_command(std::span<const std::byte> bytes);

} // namespace aoa::sim::player
