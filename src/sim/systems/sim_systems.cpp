#include "sim/systems/sim_systems.hpp"

#include "sim/systems/gameplay_systems.hpp"
#include "sim/systems/match_outcome.hpp"

namespace aoa::sim::systems {

void run_sim_systems(
    entt::registry& registry,
    const bool compute_hash,
    const std::uint64_t tick_count)
{
    if (!match_is_finished(registry)) {
        run_gameplay_systems(registry);
        update_match_outcome(registry, tick_count);
    }

    if (!compute_hash) {
        return;
    }

    compute_state_hash(registry);
}

} // namespace aoa::sim::systems
