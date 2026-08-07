#include "sim/simulation.hpp"

#include "core/constants.hpp"
#include "data/content_loader.hpp"
#include "sim/components/match_session.hpp"
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

void Simulation::enqueue_network_command(player::PlayerCommand command)
{
    command_queue_.enqueue_network(std::move(command));
}

void Simulation::set_player_ai_controlled(const std::uint8_t player_slot, const bool enabled)
{
    const auto world_view = registry_.view<components::WorldTag>();
    if (world_view.begin() == world_view.end()) {
        return;
    }

    const entt::entity world = *world_view.begin();
    auto& session = registry_.get_or_emplace<components::MatchSession>(world);
    components::set_slot_ai_controlled(session, player_slot, enabled);

    if (!snapshot_replay_active_) {
        session.ai_control_transitions.push_back(
            components::AiControlTransition{tick_count_, player_slot, enabled});
    }

    if (!enabled) {
        if (session.ai_controlled_slots == 0U) {
            session.ai_controlled_since_tick = 0U;
        }
        return;
    }

    if (session.ai_controlled_since_tick == 0U) {
        session.ai_controlled_since_tick = tick_count_;
    }

    const auto worker_view = registry_.view<components::WorkerUnitTag, components::PlayerOwnedTag>();
    for (const entt::entity worker : worker_view) {
        registry_.remove<components::ManualControlTag>(worker);
    }
}

bool Simulation::is_player_ai_controlled(const std::uint8_t player_slot) const
{
    const auto view = registry_.view<components::WorldTag, components::MatchSession>();
    if (view.begin() == view.end()) {
        return false;
    }

    const auto& session = view.get<components::MatchSession>(*view.begin());
    return components::is_slot_ai_controlled(session, player_slot);
}

void Simulation::restore_command_log(
    std::vector<player::PlayerCommand> input_log,
    const std::uint64_t next_sequence)
{
    command_queue_.restore_input_log(std::move(input_log), next_sequence);
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
