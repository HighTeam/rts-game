#pragma once

#include "core/grid.hpp"
#include "math/fixed.hpp"
#include "sim/components/map_grid.hpp"

#include <entt/entt.hpp>

namespace aoa::sim::systems {

void run_gameplay_systems(entt::registry& registry);
void compute_state_hash(entt::registry& registry);
void snapshot_world_positions_for_render(entt::registry& registry);
void assign_unit_path(
    entt::registry& registry,
    entt::entity entity,
    const components::MapGrid& map,
    core::GridPos goal,
    entt::entity also_ignore = entt::null,
    bool allow_knight_steps = true,
    bool has_goal_world = false,
    math::Fixed goal_world_x = {},
    math::Fixed goal_world_y = {},
    bool use_attack_pathfinding = false);

} // namespace aoa::sim::systems
