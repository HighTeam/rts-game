#pragma once

#include "core/grid.hpp"
#include "math/fixed.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/world_position.hpp"

#include <entt/entt.hpp>
#include <vector>

namespace aoa::sim::systems {

void begin_movement_query_cache(entt::registry& registry, const components::MapGrid& map);
void end_movement_query_cache();

class MovementQueryCacheGuard {
public:
    MovementQueryCacheGuard(entt::registry& registry, const components::MapGrid& map)
    {
        begin_movement_query_cache(registry, map);
    }

    ~MovementQueryCacheGuard()
    {
        end_movement_query_cache();
    }

    MovementQueryCacheGuard(const MovementQueryCacheGuard&) = delete;
    MovementQueryCacheGuard& operator=(const MovementQueryCacheGuard&) = delete;
};

struct PathfindProfile {
    int calls{0};
    int failed{0};
    int expanded_nodes{0};
};

void reset_pathfind_profile();
[[nodiscard]] PathfindProfile pathfind_profile();

[[nodiscard]] bool is_tile_walkable(
    const components::MapGrid& map,
    core::GridPos pos,
    bool allow_forest);

[[nodiscard]] bool is_movement_blocked(
    entt::registry& registry,
    core::GridPos cell,
    entt::entity ignore = entt::null,
    entt::entity also_ignore = entt::null);

[[nodiscard]] bool is_unit_radius_blocked_at_world(
    entt::registry& registry,
    math::Fixed point_x,
    math::Fixed point_y,
    entt::entity ignore = entt::null,
    entt::entity also_ignore = entt::null);

[[nodiscard]] bool is_step_movement_blocked(
    entt::registry& registry,
    const components::MapGrid& map,
    core::GridPos from,
    core::GridPos to,
    entt::entity ignore = entt::null,
    bool allow_forest = false,
    entt::entity also_ignore = entt::null);

/// Terrain cell + building/lake radius overlap (no unit-unit checks).
[[nodiscard]] bool is_world_position_solid_blocked(
    entt::registry& registry,
    const components::MapGrid& map,
    const components::WorldPosition& world,
    entt::entity ignore = entt::null,
    bool allow_forest = false);

[[nodiscard]] bool is_world_position_movement_blocked(
    entt::registry& registry,
    const components::MapGrid& map,
    const components::WorldPosition& world,
    entt::entity ignore = entt::null,
    bool allow_forest = false);

[[nodiscard]] bool is_world_segment_movement_blocked(
    entt::registry& registry,
    const components::MapGrid& map,
    math::Fixed from_x,
    math::Fixed from_y,
    math::Fixed to_x,
    math::Fixed to_y,
    entt::entity ignore = entt::null,
    bool allow_forest = false);

[[nodiscard]] core::GridPos first_grid_step_toward(core::GridPos from, core::GridPos to);

[[nodiscard]] core::GridPos move_segment_destination_cell(
    math::Fixed to_x,
    math::Fixed to_y);

[[nodiscard]] core::GridPos world_position_to_grid_cell(const components::WorldPosition& world);

[[nodiscard]] core::GridPos unit_movement_grid_cell(
    entt::registry& registry,
    entt::entity entity);

[[nodiscard]] core::GridPos unit_occupancy_grid_cell(
    entt::registry& registry,
    entt::entity entity);

[[nodiscard]] bool unit_grid_adjacent(
    entt::registry& registry,
    entt::entity from,
    entt::entity to);

[[nodiscard]] bool unit_in_melee_range(
    entt::registry& registry,
    entt::entity from,
    entt::entity to);

/// Nearest walkable Chebyshev-1 stand around a building, or a melee neighbor for units.
/// Building stands return {-1,-1} when no walkable tile exists.
[[nodiscard]] core::GridPos find_best_melee_stand_tile(
    const components::MapGrid& map,
    entt::registry& registry,
    core::GridPos target_cell,
    entt::entity mover,
    entt::entity target_entity = entt::null);

/// Nearest free stand whose work goal is within WORK_INTERACT_RANGE of the depot AABB.
[[nodiscard]] core::GridPos find_best_deposit_stand_tile(
    const components::MapGrid& map,
    entt::registry& registry,
    entt::entity depot,
    entt::entity mover);

/// Nearest free stand whose work goal is within WORK_INTERACT_RANGE of the target cell AABB.
[[nodiscard]] core::GridPos find_best_approach_stand_tile(
    const components::MapGrid& map,
    entt::registry& registry,
    core::GridPos target_cell,
    entt::entity mover);

/// World goal inside `stand_cell` pulled toward a target cell AABB (gather).
void work_stand_world_goal_for_cell(
    core::GridPos stand_cell,
    core::GridPos target_cell,
    math::Fixed& out_x,
    math::Fixed& out_y);

/// World goal inside `stand_cell` pulled toward a building footprint AABB (deposit).
void work_stand_world_goal_for_building(
    core::GridPos stand_cell,
    const components::GridPosition& anchor,
    const components::BuildingFootprint& footprint,
    math::Fixed& out_x,
    math::Fixed& out_y);

[[nodiscard]] bool is_grid16_neighbor(core::GridPos from, core::GridPos to);

/// True when `cell` is on the building edge (Chebyshev distance 1 to footprint).
[[nodiscard]] bool is_cell_on_building_facing(
    core::GridPos cell,
    const components::GridPosition& anchor,
    const components::BuildingFootprint& footprint);

/// Gather/deposit interact: Euclidean distance from unit world pos to cell AABB ≤ work range.
[[nodiscard]] bool unit_in_work_interact_range_cell(
    entt::registry& registry,
    entt::entity unit,
    core::GridPos cell);

/// Gather/deposit interact: Euclidean distance from unit world pos to footprint AABB ≤ work range.
[[nodiscard]] bool unit_in_work_interact_range_building(
    entt::registry& registry,
    entt::entity unit,
    const components::GridPosition& anchor,
    const components::BuildingFootprint& footprint);

/// True when the unit occupies a cardinal-adjacent stand cell and is in gather range.
[[nodiscard]] bool unit_can_work_cell(
    entt::registry& registry,
    entt::entity unit,
    core::GridPos cell);

/// True when the unit occupies a cardinal-adjacent stand cell and is in deposit range.
[[nodiscard]] bool unit_can_work_building(
    entt::registry& registry,
    entt::entity unit,
    const components::GridPosition& anchor,
    const components::BuildingFootprint& footprint);

[[nodiscard]] std::vector<core::GridPos> find_path(
    const components::MapGrid& map,
    core::GridPos start,
    core::GridPos goal,
    entt::registry& registry,
    entt::entity ignore = entt::null,
    bool allow_forest = false,
    entt::entity also_ignore = entt::null);

[[nodiscard]] core::GridPos find_nearest_walkable_goal(
    const components::MapGrid& map,
    entt::registry& registry,
    core::GridPos from,
    core::GridPos preferred,
    entt::entity ignore = entt::null);

} // namespace aoa::sim::systems
