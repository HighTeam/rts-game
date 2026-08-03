#pragma once

#include "sim/player/command_queue.hpp"
#include "sim/player/player_command.hpp"

#include <entt/entt.hpp>
#include <cstdint>

namespace aoa::sim {

class Simulation {
public:
    Simulation();

    void tick();

    void snapshot_world_positions_for_render();

    void enqueue_player_command(player::PlayerCommand command);

    [[nodiscard]] std::uint64_t tick_count() const { return tick_count_; }

    [[nodiscard]] std::uint64_t state_hash() const;

    [[nodiscard]] std::uint64_t next_command_execute_tick() const;

    [[nodiscard]] entt::registry& registry() { return registry_; }
    [[nodiscard]] const entt::registry& registry() const { return registry_; }

    [[nodiscard]] const player::CommandQueue& command_queue() const { return command_queue_; }

private:
    entt::registry registry_;
    player::CommandQueue command_queue_;
    std::uint64_t tick_count_{0U};
};

} // namespace aoa::sim
