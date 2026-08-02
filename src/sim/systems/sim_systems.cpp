#include "sim/systems/sim_systems.hpp"

#include "sim/systems/gameplay_systems.hpp"

namespace aoa::sim::systems {

void run_sim_systems(entt::registry& registry)
{
    run_gameplay_systems(registry);
    compute_state_hash(registry);
}

} // namespace aoa::sim::systems
