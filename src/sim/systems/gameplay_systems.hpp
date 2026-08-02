#pragma once

#include <entt/entt.hpp>

namespace aoa::sim::systems {

void run_gameplay_systems(entt::registry& registry);
void compute_state_hash(entt::registry& registry);

} // namespace aoa::sim::systems
