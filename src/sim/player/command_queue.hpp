#pragma once

#include "sim/player/player_command.hpp"

#include <entt/entt.hpp>
#include <cstdint>
#include <vector>

namespace aoa::sim::player {

class CommandQueue {
public:
    void enqueue(PlayerCommand command);

    void enqueue_network(PlayerCommand command);

    void apply_pending(entt::registry& registry, std::uint64_t tick);

    [[nodiscard]] const std::vector<PlayerCommand>& input_log() const { return input_log_; }

private:
    std::uint64_t next_sequence_{1U};
    std::vector<PlayerCommand> pending_{};
    std::vector<PlayerCommand> input_log_{};
};

} // namespace aoa::sim::player
