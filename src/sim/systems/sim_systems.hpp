#pragma once

#include <cstdint>
#include <entt/entt.hpp>

namespace aoa::sim::systems {

void run_sim_systems(
    entt::registry& registry,
    bool compute_hash = true,
    std::uint64_t tick_count = 0U);

} // namespace aoa::sim::systems