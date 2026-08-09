#pragma once

#include "sim/player/command_queue.hpp"
#include "sim/player/player_command.hpp"
#include "sim/snapshot/sim_snapshot.hpp"

#include <entt/entt.hpp>
#include <cstdint>
#include <span>

namespace aoa::sim {

class Simulation {
public:
    explicit Simulation(std::uint8_t player_count = 2U);

    void tick();

    void snapshot_world_positions_for_render();

    void enqueue_player_command(player::PlayerCommand command);

    void enqueue_network_command(player::PlayerCommand command);

    void set_player_ai_controlled(std::uint8_t player_slot, bool enabled);

    [[nodiscard]] bool is_player_ai_controlled(std::uint8_t player_slot) const;

    [[nodiscard]] SimSnapshot export_snapshot() const;

    [[nodiscard]] bool apply_snapshot(std::span<const std::byte> snapshot_bytes);

    void restore_command_log(std::vector<player::PlayerCommand> input_log, std::uint64_t next_sequence);

    void set_snapshot_replay_active(const bool active) { snapshot_replay_active_ = active; }

    void set_tick_count(std::uint64_t tick_count) { tick_count_ = tick_count; }

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
    bool snapshot_replay_active_{false};
};

} // namespace aoa::sim
