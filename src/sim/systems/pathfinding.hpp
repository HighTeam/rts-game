#pragma once

#include "core/grid.hpp"
#include "math/fixed.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/world_position.hpp"

#include <entt/entt.hpp>
#include <vector>

namespace aoa::sim::systems {

[[nodiscard]] bool is_tile_walkable(
    const components::MapGrid& map,
    core::GridPos pos,
    bool allow_forest);

[[nodiscard]] bool is_movement_blocked(
    entt::registry& registry,
    core::GridPos cell,
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

[[nodiscard]] core::GridPos find_best_melee_stand_tile(
    const components::MapGrid& map,
    entt::registry& registry,
    core::GridPos target_cell,
    entt::entity mover,
    entt::entity target_entity = entt::null);

[[nodiscard]] std::vector<core::GridPos> find_path(
    const components::MapGrid& map,
    core::GridPos start,
    core::GridPos goal,
    entt::registry& registry,
    entt::entity ignore = entt::null,
    bool allow_forest = false,
    entt::entity also_ignore = entt::null,
    bool allow_knight_steps = true);

// AoE2-style combat approach: cardinal + diagonal only. Knight leaps cause zigzags near the target.
[[nodiscard]] std::vector<core::GridPos> find_attack_path(
    const components::MapGrid& map,
    core::GridPos start,
    core::GridPos goal,
    entt::registry& registry,
    entt::entity ignore = entt::null,
    bool allow_forest = false,
    entt::entity also_ignore = entt::null);

[[nodiscard]] bool attack_path_follows_direct_line(
    core::GridPos start,
    core::GridPos goal,
    const std::vector<core::GridPos>& path_cells,
    int path_start_index);

} // namespace aoa::sim::systems
