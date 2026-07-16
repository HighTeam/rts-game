#include "sim/systems/sim_systems.hpp"

#include "sim/components/motion_state.hpp"

namespace aoa::sim::systems {

void run_sim_systems(entt::registry& registry)
{
    const auto view = registry.view<components::MotionState>();

    for (const entt::entity entity : view) {
        auto& motion = view.get<components::MotionState>(entity);
        motion.value = motion.value + math::fixed_sim_delta();
    }
}

} // namespace aoa::sim::systems
