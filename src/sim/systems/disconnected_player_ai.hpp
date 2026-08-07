#pragma once

#include "sim/player/player_command.hpp"

#include <cstdint>
#include <entt/entt.hpp>
#include <vector>

namespace aoa::sim::systems {

[[nodiscard]] std::vector<player::PlayerCommand> generate_ai_commands_for_slot(
    entt::registry& registry,
    std::uint8_t player_slot,
    std::uint64_t execute_tick,
    std::uint64_t& next_sequence);

} // namespace aoa::sim::systems
