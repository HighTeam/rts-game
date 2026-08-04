#include "sim/simulation.hpp"

#include "core/constants.hpp"
#include "data/content_loader.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/tags.hpp"
#include "sim/scenario/test_scenario.hpp"
#include "sim/systems/gameplay_systems.hpp"
#include "sim/systems/sim_systems.hpp"

#include <string>

namespace aoa::sim {

Simulation::Simulation()
{
    const data::ContentDatabase content = data::load_content_database(
        data::default_data_directory());

    scenario::load_test_scenario(registry_, content);
}

void Simulation::enqueue_player_command(player::PlayerCommand command)
{
    if (command.execute_tick == 0U) {
        command.execute_tick = next_command_execute_tick();
    }

    command_queue_.enqueue(std::move(command));
}

std::uint64_t Simulation::next_command_execute_tick() const
{
    return tick_count_ + static_cast<std::uint64_t>(constants::PLAYER_COMMAND_DELAY_TICKS);
}

void Simulation::tick()
{
    ++tick_count_;
    command_queue_.apply_pending(registry_, tick_count_);
    systems::run_sim_systems(registry_);
}

void Simulation::snapshot_world_positions_for_render()
{
    systems::snapshot_world_positions_for_render(registry_);
}

std::uint64_t Simulation::state_hash() const
{
    const auto view = registry_.view<components::WorldTag, components::SimState>();
    if (view.begin() == view.end()) {
        return 0U;
    }

    return registry_.get<components::SimState>(*view.begin()).state_hash;
}

} // namespace aoa::sim
