#include "sim/systems/pathfinding.hpp"

#include "core/constants.hpp"
#include "math/fixed.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/components/match_session.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/spawn/unit_spawn.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <queue>
#include <limits>
#include <vector>

namespace aoa::sim::systems {

namespace {

struct PathStepOffset {
    core::GridPos offset{};
    int cost{0};
};

constexpr std::array<PathStepOffset, 16> path_step_offsets = {{
    PathStepOffset{{1, 0}, constants::PATHFIND_CARDINAL_STEP_COST},
    PathStepOffset{{2, 1}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{1, 1}, constants::PATHFIND_DIAGONAL_STEP_COST},
    PathStepOffset{{1, 2}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{0, 1}, constants::PATHFIND_CARDINAL_STEP_COST},
    PathStepOffset{{-1, 2}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{-1, 1}, constants::PATHFIND_DIAGONAL_STEP_COST},
    PathStepOffset{{-2, 1}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{-1, 0}, constants::PATHFIND_CARDINAL_STEP_COST},
    PathStepOffset{{-2, -1}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{-1, -1}, constants::PATHFIND_DIAGONAL_STEP_COST},
    PathStepOffset{{-1, -2}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{0, -1}, constants::PATHFIND_CARDINAL_STEP_COST},
    PathStepOffset{{1, -2}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{1, -1}, constants::PATHFIND_DIAGONAL_STEP_COST},
    PathStepOffset{{2, -1}, constants::PATHFIND_KNIGHT_STEP_COST},
}};

struct StepCells {
    std::array<core::GridPos, constants::PATHFIND_STEP_CELLS_MAX> cells{};
    int count{0};

    void push(const core::GridPos cell)
    {
        if (count >= constants::PATHFIND_STEP_CELLS_MAX) {
            return;
        }

        cells[static_cast<std::size_t>(count)] = cell;
        ++count;
    }
};

thread_local PathfindProfile g_pathfind_profile{};

// Every cell the center-to-center line touches (Amanatides-Woo). Knight (2,1)
// steps must not skip a tree the world-space move would clip.
StepCells cells_along_step(const core::GridPos from, const core::GridPos to)
{
    StepCells cells{};
    cells.push(from);
    if (from == to) {
        return cells;
    }

    const float origin_x = static_cast<float>(from.x) + 0.5F;
    const float origin_y = static_cast<float>(from.y) + 0.5F;
    const float dest_x = static_cast<float>(to.x) + 0.5F;
    const float dest_y = static_cast<float>(to.y) + 0.5F;
    const float delta_x = dest_x - origin_x;
    const float delta_y = dest_y - origin_y;
    const int step_x = delta_x > 0.0F ? 1 : (delta_x < 0.0F ? -1 : 0);
    const int step_y = delta_y > 0.0F ? 1 : (delta_y < 0.0F ? -1 : 0);
    const float inf = std::numeric_limits<float>::infinity();
    const float t_delta_x = step_x != 0 ? std::abs(1.0F / delta_x) : inf;
    const float t_delta_y = step_y != 0 ? std::abs(1.0F / delta_y) : inf;

    float t_max_x = inf;
    if (step_x > 0) {
        t_max_x = (static_cast<float>(from.x + 1) - origin_x) / delta_x;
    }
    else if (step_x < 0) {
        t_max_x = (static_cast<float>(from.x) - origin_x) / delta_x;
    }

    float t_max_y = inf;
    if (step_y > 0) {
        t_max_y = (static_cast<float>(from.y + 1) - origin_y) / delta_y;
    }
    else if (step_y < 0) {
        t_max_y = (static_cast<float>(from.y) - origin_y) / delta_y;
    }

    int cell_x = from.x;
    int cell_y = from.y;
    const int max_cells = std::abs(to.x - from.x) + std::abs(to.y - from.y) + 4;
    for (int i = 0; i < max_cells; ++i) {
        if (t_max_x > 1.0F + constants::PATHFIND_LINE_T_EPSILON
            && t_max_y > 1.0F + constants::PATHFIND_LINE_T_EPSILON) {
            break;
        }

        const float t_diff = t_max_x - t_max_y;
        if (t_diff < -constants::PATHFIND_LINE_T_EPSILON) {
            if (t_max_x > 1.0F + constants::PATHFIND_LINE_T_EPSILON) {
                break;
            }
            cell_x += step_x;
            t_max_x += t_delta_x;
        }
        else if (t_diff > constants::PATHFIND_LINE_T_EPSILON) {
            if (t_max_y > 1.0F + constants::PATHFIND_LINE_T_EPSILON) {
                break;
            }
            cell_y += step_y;
            t_max_y += t_delta_y;
        }
        else {
            if (t_max_x > 1.0F + constants::PATHFIND_LINE_T_EPSILON) {
                break;
            }
            cells.push({cell_x + step_x, cell_y});
            cells.push({cell_x, cell_y + step_y});
            cell_x += step_x;
            cell_y += step_y;
            t_max_x += t_delta_x;
            t_max_y += t_delta_y;
        }

        cells.push({cell_x, cell_y});
        if (cell_x == to.x && cell_y == to.y) {
            break;
        }
    }

    return cells;
}

core::GridPos unit_effective_cell(
    entt::registry& registry,
    const entt::entity entity,
    const core::GridPos fallback)
{
    if (registry.any_of<components::MoveSegment>(entity)
        && registry.any_of<components::GridPosition>(entity)) {
        return registry.get<components::GridPosition>(entity).cell;
    }

    if (registry.all_of<components::WorldPosition>(entity)) {
        const auto& world = registry.get<components::WorldPosition>(entity);
        return {world.x.to_int(), world.y.to_int()};
    }

    return fallback;
}

} // namespace

core::GridPos move_segment_destination_cell(const math::Fixed to_x, const math::Fixed to_y)
{
    const math::Fixed half_tile = math::Fixed::from_int(1) / math::Fixed::from_int(2);
    return {
        (to_x - half_tile).to_int(),
        (to_y - half_tile).to_int(),
    };
}

core::GridPos first_grid_step_toward(const core::GridPos from, const core::GridPos to)
{
    if (from == to) {
        return to;
    }

    const StepCells steps = cells_along_step(from, to);
    if (steps.count >= 2) {
        return steps.cells[1U];
    }

    return to;
}

core::GridPos world_position_to_grid_cell(const components::WorldPosition& world)
{
    return {world.x.to_int(), world.y.to_int()};
}

core::GridPos unit_movement_grid_cell(entt::registry& registry, const entt::entity entity)
{
    if (registry.all_of<components::WorldPosition>(entity)) {
        return world_position_to_grid_cell(registry.get<components::WorldPosition>(entity));
    }

    if (registry.all_of<components::GridPosition>(entity)) {
        return registry.get<components::GridPosition>(entity).cell;
    }

    return {0, 0};
}

core::GridPos unit_occupancy_grid_cell(entt::registry& registry, const entt::entity entity)
{
    if (registry.all_of<components::GridPosition>(entity)) {
        return registry.get<components::GridPosition>(entity).cell;
    }

    if (registry.all_of<components::WorldPosition>(entity)) {
        return world_position_to_grid_cell(registry.get<components::WorldPosition>(entity));
    }

    return {0, 0};
}

bool unit_grid_adjacent(
    entt::registry& registry,
    const entt::entity from,
    const entt::entity to)
{
    if (!registry.valid(from) || !registry.valid(to)) {
        return false;
    }

    const core::GridPos from_cell = unit_occupancy_grid_cell(registry, from);
    if (registry.any_of<components::BuildingTag>(to)
        && registry.any_of<components::GridPosition>(to)) {
        components::BuildingFootprint footprint{};
        if (registry.any_of<components::BuildingFootprint>(to)) {
            footprint = registry.get<components::BuildingFootprint>(to);
        }
        footprint = components::effective_building_footprint(
            footprint,
            registry.any_of<components::TownCenterTag>(to));
        const int distance = components::chebyshev_distance_to_footprint(
            from_cell,
            registry.get<components::GridPosition>(to),
            footprint);
        if (registry.any_of<components::FarmTag>(to)) {
            return distance <= 1;
        }

        return distance == 1;
    }

    return core::chebyshev_distance(from_cell, unit_occupancy_grid_cell(registry, to)) == 1;
}

bool try_unit_world_position(
    entt::registry& registry,
    const entt::entity entity,
    float& world_x,
    float& world_z)
{
    if (registry.all_of<components::WorldPosition>(entity)) {
        const auto& world = registry.get<components::WorldPosition>(entity);
        world_x = world.x.to_float();
        world_z = world.y.to_float();
        return true;
    }

    if (registry.all_of<components::GridPosition>(entity)) {
        const core::GridPos cell = registry.get<components::GridPosition>(entity).cell;
        world_x = static_cast<float>(cell.x) + 0.5F;
        world_z = static_cast<float>(cell.y) + 0.5F;
        return true;
    }

    return false;
}

namespace {

float melee_max_center_distance_sq_for_cells(
    const core::GridPos from_cell,
    const core::GridPos to_cell)
{
    (void)from_cell;
    (void)to_cell;
    return constants::MELEE_STRIKE_MAX_CENTER_DISTANCE_SQ;
}

bool is_entity_ignored_for_movement(
    const entt::entity entity,
    const entt::entity ignore,
    const entt::entity also_ignore)
{
    return entity == ignore || entity == also_ignore;
}

} // namespace

bool unit_in_melee_range(
    entt::registry& registry,
    const entt::entity from,
    const entt::entity to)
{
    if (!registry.valid(from) || !registry.valid(to)) {
        return false;
    }

    if (!unit_grid_adjacent(registry, from, to)) {
        return false;
    }

    if (registry.any_of<components::BuildingTag>(to)) {
        return true;
    }

    float from_x = 0.0F;
    float from_z = 0.0F;
    float to_x = 0.0F;
    float to_z = 0.0F;
    if (!try_unit_world_position(registry, from, from_x, from_z)
        || !try_unit_world_position(registry, to, to_x, to_z)) {
        return false;
    }

    const float delta_x = from_x - to_x;
    const float delta_z = from_z - to_z;
    const float distance_sq = delta_x * delta_x + delta_z * delta_z;
    return distance_sq <= melee_max_center_distance_sq_for_cells(
        unit_occupancy_grid_cell(registry, from),
        unit_occupancy_grid_cell(registry, to));
}

bool is_grid16_neighbor(const core::GridPos from, const core::GridPos to)
{
    const int dx = to.x - from.x;
    const int dy = to.y - from.y;
    for (const PathStepOffset& step : path_step_offsets) {
        if (step.offset.x == dx && step.offset.y == dy) {
            return true;
        }
    }

    return false;
}

bool is_cell_on_building_facing(
    const core::GridPos cell,
    const components::GridPosition& anchor,
    const components::BuildingFootprint& footprint)
{
    // Stand-tile facing: true edge adjacency only (not knight facings).
    if (components::building_contains_cell(anchor, footprint, cell)) {
        return false;
    }

    return components::chebyshev_distance_to_footprint(cell, anchor, footprint) == 1;
}

namespace {

[[nodiscard]] void unit_work_world_xy(
    entt::registry& registry,
    const entt::entity unit,
    float& out_x,
    float& out_y)
{
    if (registry.any_of<components::WorldPosition>(unit)) {
        const auto& world = registry.get<components::WorldPosition>(unit);
        out_x = world.x.to_float();
        out_y = world.y.to_float();
        return;
    }

    const core::GridPos cell = unit_occupancy_grid_cell(registry, unit);
    out_x = static_cast<float>(cell.x) + 0.5F;
    out_y = static_cast<float>(cell.y) + 0.5F;
}

[[nodiscard]] float distance_to_aabb(
    const float point_x,
    const float point_y,
    const float min_x,
    const float min_y,
    const float max_x,
    const float max_y)
{
    const float clamped_x = std::max(min_x, std::min(max_x, point_x));
    const float clamped_y = std::max(min_y, std::min(max_y, point_y));
    const float delta_x = point_x - clamped_x;
    const float delta_y = point_y - clamped_y;
    return std::sqrt(delta_x * delta_x + delta_y * delta_y);
}

[[nodiscard]] bool is_cardinal_neighbor_cell(
    const core::GridPos cell,
    const core::GridPos target)
{
    const int dx = std::abs(cell.x - target.x);
    const int dy = std::abs(cell.y - target.y);
    return (dx + dy) == 1;
}

[[nodiscard]] bool is_cardinal_work_stand_for_footprint(
    const core::GridPos cell,
    const components::GridPosition& anchor,
    const components::BuildingFootprint& footprint)
{
    if (components::building_contains_cell(anchor, footprint, cell)) {
        return false;
    }

    if (components::chebyshev_distance_to_footprint(cell, anchor, footprint) != 1) {
        return false;
    }

    const int min_x = anchor.cell.x;
    const int min_y = anchor.cell.y;
    const int max_x = anchor.cell.x + std::max(1, footprint.width) - 1;
    const int max_y = anchor.cell.y + std::max(1, footprint.height) - 1;
    const int signed_dx =
        cell.x < min_x ? cell.x - min_x : (cell.x > max_x ? cell.x - max_x : 0);
    const int signed_dy =
        cell.y < min_y ? cell.y - min_y : (cell.y > max_y ? cell.y - max_y : 0);
    // Edge (not corner): exactly one axis sticks out.
    return (signed_dx == 0) != (signed_dy == 0);
}

} // namespace

bool unit_in_work_interact_range_cell(
    entt::registry& registry,
    const entt::entity unit,
    const core::GridPos cell)
{
    float unit_x = 0.0F;
    float unit_y = 0.0F;
    unit_work_world_xy(registry, unit, unit_x, unit_y);
    const float distance = distance_to_aabb(
        unit_x,
        unit_y,
        static_cast<float>(cell.x),
        static_cast<float>(cell.y),
        static_cast<float>(cell.x + 1),
        static_cast<float>(cell.y + 1));
    return distance <= constants::WORK_INTERACT_RANGE_TILES;
}

bool unit_in_work_interact_range_building(
    entt::registry& registry,
    const entt::entity unit,
    const components::GridPosition& anchor,
    const components::BuildingFootprint& footprint)
{
    float unit_x = 0.0F;
    float unit_y = 0.0F;
    unit_work_world_xy(registry, unit, unit_x, unit_y);
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    const float distance = distance_to_aabb(
        unit_x,
        unit_y,
        static_cast<float>(anchor.cell.x),
        static_cast<float>(anchor.cell.y),
        static_cast<float>(anchor.cell.x + width),
        static_cast<float>(anchor.cell.y + height));
    return distance <= constants::WORK_INTERACT_RANGE_TILES;
}

bool unit_can_work_cell(
    entt::registry& registry,
    const entt::entity unit,
    const core::GridPos cell)
{
    const core::GridPos occupancy = unit_occupancy_grid_cell(registry, unit);
    const entt::entity farm = spawn::find_farm_at_cell(registry, cell);
    if (farm != entt::null && registry.any_of<components::GridPosition>(farm)) {
        components::BuildingFootprint footprint{};
        if (registry.any_of<components::BuildingFootprint>(farm)) {
            footprint = registry.get<components::BuildingFootprint>(farm);
        }
        const auto& anchor = registry.get<components::GridPosition>(farm);
        if (!components::building_contains_cell(anchor, footprint, occupancy)) {
            return false;
        }

        return unit_in_work_interact_range_building(registry, unit, anchor, footprint);
    }

    if (core::chebyshev_distance(occupancy, cell) != 1) {
        return false;
    }

    return unit_in_work_interact_range_cell(registry, unit, cell);
}

bool unit_can_work_building(
    entt::registry& registry,
    const entt::entity unit,
    const components::GridPosition& anchor,
    const components::BuildingFootprint& footprint)
{
    const core::GridPos occupancy = unit_occupancy_grid_cell(registry, unit);
    if (!is_cardinal_work_stand_for_footprint(occupancy, anchor, footprint)) {
        return false;
    }

    return unit_in_work_interact_range_building(registry, unit, anchor, footprint);
}

namespace {

void work_stand_world_goal_for_aabb(
    const core::GridPos stand_cell,
    const float target_min_x,
    const float target_min_y,
    const float target_max_x,
    const float target_max_y,
    math::Fixed& out_x,
    math::Fixed& out_y)
{
    const float inset = constants::MOVE_GOAL_TILE_EDGE_INSET;
    const float stand_min_x = static_cast<float>(stand_cell.x) + inset;
    const float stand_min_y = static_cast<float>(stand_cell.y) + inset;
    const float stand_max_x = static_cast<float>(stand_cell.x + 1) - inset;
    const float stand_max_y = static_cast<float>(stand_cell.y + 1) - inset;
    const float target_x = std::max(target_min_x, std::min(target_max_x, (stand_min_x + stand_max_x) * 0.5F));
    const float target_y = std::max(target_min_y, std::min(target_max_y, (stand_min_y + stand_max_y) * 0.5F));
    const float goal_x = std::max(stand_min_x, std::min(stand_max_x, target_x));
    const float goal_y = std::max(stand_min_y, std::min(stand_max_y, target_y));
    out_x = math::Fixed::from_float(goal_x);
    out_y = math::Fixed::from_float(goal_y);
}

[[nodiscard]] bool work_stand_goal_in_interact_range(
    const core::GridPos stand_cell,
    const float target_min_x,
    const float target_min_y,
    const float target_max_x,
    const float target_max_y)
{
    math::Fixed goal_x{};
    math::Fixed goal_y{};
    work_stand_world_goal_for_aabb(
        stand_cell,
        target_min_x,
        target_min_y,
        target_max_x,
        target_max_y,
        goal_x,
        goal_y);
    return distance_to_aabb(
               goal_x.to_float(),
               goal_y.to_float(),
               target_min_x,
               target_min_y,
               target_max_x,
               target_max_y)
        <= constants::WORK_INTERACT_RANGE_TILES;
}

} // namespace

void work_stand_world_goal_for_cell(
    const core::GridPos stand_cell,
    const core::GridPos target_cell,
    math::Fixed& out_x,
    math::Fixed& out_y)
{
    work_stand_world_goal_for_aabb(
        stand_cell,
        static_cast<float>(target_cell.x),
        static_cast<float>(target_cell.y),
        static_cast<float>(target_cell.x + 1),
        static_cast<float>(target_cell.y + 1),
        out_x,
        out_y);
}

void work_stand_world_goal_for_building(
    const core::GridPos stand_cell,
    const components::GridPosition& anchor,
    const components::BuildingFootprint& footprint,
    math::Fixed& out_x,
    math::Fixed& out_y)
{
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    work_stand_world_goal_for_aabb(
        stand_cell,
        static_cast<float>(anchor.cell.x),
        static_cast<float>(anchor.cell.y),
        static_cast<float>(anchor.cell.x + width),
        static_cast<float>(anchor.cell.y + height),
        out_x,
        out_y);
}

core::GridPos find_best_deposit_stand_tile(
    const components::MapGrid& map,
    entt::registry& registry,
    const entt::entity depot,
    const entt::entity mover)
{
    if (depot == entt::null || !registry.valid(depot)
        || !registry.any_of<components::GridPosition>(depot)) {
        return unit_movement_grid_cell(registry, mover);
    }

    const auto& anchor = registry.get<components::GridPosition>(depot);
    components::BuildingFootprint footprint{};
    if (registry.any_of<components::BuildingFootprint>(depot)) {
        footprint = registry.get<components::BuildingFootprint>(depot);
    }
    footprint = components::effective_building_footprint(
        footprint,
        registry.any_of<components::TownCenterTag>(depot));

    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    const float target_min_x = static_cast<float>(anchor.cell.x);
    const float target_min_y = static_cast<float>(anchor.cell.y);
    const float target_max_x = static_cast<float>(anchor.cell.x + width);
    const float target_max_y = static_cast<float>(anchor.cell.y + height);

    const core::GridPos mover_cell = unit_movement_grid_cell(registry, mover);
    if (work_stand_goal_in_interact_range(
            mover_cell, target_min_x, target_min_y, target_max_x, target_max_y)
        && is_tile_walkable(map, mover_cell, false)
        && !is_movement_blocked(registry, mover_cell, mover)
        && !components::building_contains_cell(anchor, footprint, mover_cell)
        && is_cardinal_work_stand_for_footprint(mover_cell, anchor, footprint)) {
        return mover_cell;
    }

    // Prefer cardinal edge stands over corners (corners look "shorter" but cause clips).
    core::GridPos best = anchor.cell;
    int best_travel = std::numeric_limits<int>::max();
    bool best_cardinal = false;
    bool found = false;

    for (int y = anchor.cell.y - 1; y <= anchor.cell.y + height; ++y) {
        for (int x = anchor.cell.x - 1; x <= anchor.cell.x + width; ++x) {
            const core::GridPos candidate{x, y};
            if (components::building_contains_cell(anchor, footprint, candidate)) {
                continue;
            }

            if (!work_stand_goal_in_interact_range(
                    candidate, target_min_x, target_min_y, target_max_x, target_max_y)) {
                continue;
            }

            if (!is_tile_walkable(map, candidate, false)) {
                continue;
            }

            if (is_movement_blocked(registry, candidate, mover)) {
                continue;
            }

            const bool cardinal = is_cardinal_work_stand_for_footprint(candidate, anchor, footprint);
            const int travel = core::chebyshev_distance(mover_cell, candidate);
            if (!found
                || (cardinal && !best_cardinal)
                || (cardinal == best_cardinal && travel < best_travel)
                || (cardinal == best_cardinal && travel == best_travel
                    && (candidate.y < best.y
                        || (candidate.y == best.y && candidate.x < best.x)))) {
                found = true;
                best_cardinal = cardinal;
                best_travel = travel;
                best = candidate;
            }
        }
    }

    return best;
}

core::GridPos find_best_farm_stand_tile(
    const components::MapGrid& map,
    entt::registry& registry,
    const entt::entity farm,
    const entt::entity mover,
    const core::GridPos prefer_not,
    const bool avoid_unit_occupancy)
{
    if (farm == entt::null || !registry.valid(farm)
        || !registry.any_of<components::GridPosition>(farm)) {
        return {-1, -1};
    }

    components::BuildingFootprint footprint{};
    if (registry.any_of<components::BuildingFootprint>(farm)) {
        footprint = registry.get<components::BuildingFootprint>(farm);
    }
    const auto& anchor = registry.get<components::GridPosition>(farm);
    const core::GridPos mover_cell = unit_occupancy_grid_cell(registry, mover);

    core::GridPos best{-1, -1};
    int best_travel = std::numeric_limits<int>::max();
    bool found = false;
    bool found_preferred = false;
    for (int y = 0; y < footprint.height; ++y) {
        for (int x = 0; x < footprint.width; ++x) {
            const core::GridPos candidate{anchor.cell.x + x, anchor.cell.y + y};
            if (!is_tile_walkable(map, candidate, false)) {
                continue;
            }

            if (is_movement_blocked(registry, candidate, mover)) {
                continue;
            }

            if (avoid_unit_occupancy
                && is_unit_occupying_or_reserving_cell(registry, candidate, mover)) {
                continue;
            }

            const bool is_avoided = candidate.x == prefer_not.x && candidate.y == prefer_not.y;
            if (found && found_preferred && is_avoided) {
                continue;
            }

            const int travel = core::chebyshev_distance(mover_cell, candidate);
            const bool better_class = !is_avoided && !found_preferred;
            if (!found
                || better_class
                || (is_avoided == !found_preferred
                    && (travel < best_travel
                        || (travel == best_travel
                            && (candidate.y < best.y
                                || (candidate.y == best.y && candidate.x < best.x)))))) {
                found = true;
                found_preferred = !is_avoided;
                best_travel = travel;
                best = candidate;
            }
        }
    }

    return best;
}

core::GridPos pick_farm_wander_cell(
    const components::MapGrid& map,
    entt::registry& registry,
    const entt::entity farm,
    const entt::entity mover,
    const core::GridPos avoid,
    const std::uint32_t seed)
{
    if (farm == entt::null || !registry.valid(farm)
        || !registry.any_of<components::GridPosition>(farm)) {
        return {-1, -1};
    }

    components::BuildingFootprint footprint{};
    if (registry.any_of<components::BuildingFootprint>(farm)) {
        footprint = registry.get<components::BuildingFootprint>(farm);
    }
    const auto& anchor = registry.get<components::GridPosition>(farm);
    std::array<core::GridPos, 8> cells{};
    int count = 0;
    for (int y = 0; y < footprint.height && count < static_cast<int>(cells.size()); ++y) {
        for (int x = 0; x < footprint.width && count < static_cast<int>(cells.size()); ++x) {
            const core::GridPos candidate{anchor.cell.x + x, anchor.cell.y + y};
            if (candidate.x == avoid.x && candidate.y == avoid.y) {
                continue;
            }

            if (!is_tile_walkable(map, candidate, false)) {
                continue;
            }

            if (is_movement_blocked(registry, candidate, mover)) {
                continue;
            }

            cells[static_cast<std::size_t>(count)] = candidate;
            ++count;
        }
    }

    if (count <= 0) {
        return find_best_farm_stand_tile(map, registry, farm, mover, {-1, -1});
    }

    return cells[static_cast<std::size_t>(seed % static_cast<std::uint32_t>(count))];
}

core::GridPos find_best_melee_stand_tile(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos target_cell,
    const entt::entity mover,
    const entt::entity target_entity)
{
    const core::GridPos mover_cell = unit_movement_grid_cell(registry, mover);

    // Buildings: stand on nearest Chebyshev-1 edge so melee strike range still works.
    if (target_entity != entt::null && registry.valid(target_entity)
        && registry.any_of<components::BuildingTag, components::GridPosition>(target_entity)) {
        if (registry.any_of<components::FarmTag>(target_entity)) {
            const core::GridPos farm_stand =
                find_best_farm_stand_tile(map, registry, target_entity, mover);
            if (farm_stand.x >= 0) {
                return farm_stand;
            }
        }

        components::BuildingFootprint footprint{};
        if (registry.any_of<components::BuildingFootprint>(target_entity)) {
            footprint = registry.get<components::BuildingFootprint>(target_entity);
        }
        footprint = components::effective_building_footprint(
            footprint,
            registry.any_of<components::TownCenterTag>(target_entity));
        const auto& anchor = registry.get<components::GridPosition>(target_entity);

        core::GridPos best{-1, -1};
        int best_travel = std::numeric_limits<int>::max();
        bool best_cardinal = false;
        bool found = false;
        for (int y = anchor.cell.y - 1; y <= anchor.cell.y + footprint.height; ++y) {
            for (int x = anchor.cell.x - 1; x <= anchor.cell.x + footprint.width; ++x) {
                const core::GridPos candidate{x, y};
                if (components::building_contains_cell(anchor, footprint, candidate)) {
                    continue;
                }

                if (components::chebyshev_distance_to_footprint(candidate, anchor, footprint) != 1) {
                    continue;
                }

                if (!is_tile_walkable(map, candidate, false)) {
                    continue;
                }

                if (is_movement_blocked(registry, candidate, mover)) {
                    continue;
                }

                const bool cardinal =
                    is_cardinal_work_stand_for_footprint(candidate, anchor, footprint);
                const int travel = core::chebyshev_distance(mover_cell, candidate);
                if (!found
                    || (cardinal && !best_cardinal)
                    || (cardinal == best_cardinal && travel < best_travel)
                    || (cardinal == best_cardinal && travel == best_travel
                        && (candidate.y < best.y
                            || (candidate.y == best.y && candidate.x < best.x)))) {
                    found = true;
                    best_cardinal = cardinal;
                    best_travel = travel;
                    best = candidate;
                }
            }
        }

        if (!found) {
            return {-1, -1};
        }

        return best;
    }

    const int toward_mover_x = mover_cell.x - target_cell.x;
    const int toward_mover_y = mover_cell.y - target_cell.y;

    const std::array<core::GridPos, 8> neighbor_offsets = {{
        core::GridPos{0, -1},
        core::GridPos{1, 0},
        core::GridPos{0, 1},
        core::GridPos{-1, 0},
        core::GridPos{1, -1},
        core::GridPos{1, 1},
        core::GridPos{-1, 1},
        core::GridPos{-1, -1},
    }};

    core::GridPos best = target_cell;
    bool found = false;
    bool best_between = false;
    int best_travel_distance = std::numeric_limits<int>::max();
    int best_approach_alignment = std::numeric_limits<int>::min();

    const bool horizontal_approach = toward_mover_x != 0 && toward_mover_y == 0;
    const bool vertical_approach = toward_mover_x == 0 && toward_mover_y != 0;

    const auto approach_alignment = [&](const core::GridPos offset, const bool is_cardinal) {
        if (horizontal_approach) {
            return is_cardinal && offset.y == 0 ? 2 : (is_cardinal ? 1 : 0);
        }

        if (vertical_approach) {
            return is_cardinal && offset.x == 0 ? 2 : (is_cardinal ? 1 : 0);
        }

        return !is_cardinal ? 2 : 1;
    };

    const auto consider = [&](const core::GridPos offset) {
        const core::GridPos candidate{target_cell.x + offset.x, target_cell.y + offset.y};
        if (!is_tile_walkable(map, candidate, false)) {
            return;
        }

        if (is_movement_blocked(registry, candidate, mover)) {
            return;
        }

        const bool between = (offset.x * toward_mover_x + offset.y * toward_mover_y) > 0;
        const bool is_cardinal = offset.x == 0 || offset.y == 0;
        const int travel_distance = core::chebyshev_distance(mover_cell, candidate);
        const int alignment = approach_alignment(offset, is_cardinal);

        if (found) {
            if (between != best_between) {
                if (!between) {
                    return;
                }
            }
            else if (travel_distance != best_travel_distance) {
                if (travel_distance > best_travel_distance) {
                    return;
                }
            }
            else if (alignment <= best_approach_alignment) {
                return;
            }
        }

        found = true;
        best_between = between;
        best_travel_distance = travel_distance;
        best_approach_alignment = alignment;
        best = candidate;
    };

    for (const core::GridPos offset : neighbor_offsets) {
        consider(offset);
    }

    return best;
}

bool is_unstarted_construction(const entt::registry& registry, const entt::entity entity)
{
    if (!registry.valid(entity)
        || !registry.any_of<components::UnderConstructionTag>(entity)
        || !registry.any_of<components::Health>(entity)) {
        return false;
    }

    return registry.get<components::Health>(entity).current == math::Fixed::from_int(1);
}

bool unstarted_construction_visible_to_slot(
    const entt::registry& registry,
    const entt::entity entity,
    const std::uint8_t viewer_slot)
{
    if (!is_unstarted_construction(registry, entity)) {
        return true;
    }

    const std::uint8_t owner_slot = components::entity_player_slot(registry, entity);
    if (owner_slot == viewer_slot) {
        return true;
    }

    const auto world_view = registry.view<components::WorldTag, components::MatchSession>();
    if (world_view.begin() == world_view.end()) {
        return false;
    }

    const auto& session = world_view.get<components::MatchSession>(*world_view.begin());
    return components::slots_are_allied(session, owner_slot, viewer_slot);
}

namespace {

bool is_building_blocking_cell(
    entt::registry& registry,
    const core::GridPos cell,
    const entt::entity ignore,
    const entt::entity also_ignore)
{
    const auto building_view = registry.view<components::BuildingTag, components::GridPosition, components::Health>();
    for (const entt::entity entity : building_view) {
        if (is_entity_ignored_for_movement(entity, ignore, also_ignore)) {
            continue;
        }

        const auto& health = building_view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        if (registry.any_of<components::FarmTag>(entity)) {
            continue;
        }

        if (is_unstarted_construction(registry, entity)) {
            continue;
        }

        const auto& anchor = building_view.get<components::GridPosition>(entity);
        components::BuildingFootprint footprint{};
        if (registry.any_of<components::BuildingFootprint>(entity)) {
            footprint = registry.get<components::BuildingFootprint>(entity);
        }
        footprint = components::effective_building_footprint(
            footprint,
            registry.any_of<components::TownCenterTag>(entity));
        if (components::building_contains_cell(anchor, footprint, cell)) {
            return true;
        }
    }

    // Mana lakes carry no BuildingTag, yet units must still walk around them.
    const auto lake_view = registry.view<
        components::ManaLakeTag,
        components::GridPosition,
        components::BuildingFootprint>();
    for (const entt::entity lake : lake_view) {
        if (is_entity_ignored_for_movement(lake, ignore, also_ignore)) {
            continue;
        }

        if (components::building_contains_cell(
                lake_view.get<components::GridPosition>(lake),
                lake_view.get<components::BuildingFootprint>(lake),
                cell)) {
            return true;
        }
    }

    return false;
}

} // namespace

namespace {

bool units_block_world_point(
    entt::registry& registry,
    const math::Fixed point_x,
    const math::Fixed point_y,
    const entt::entity ignore,
    const entt::entity also_ignore)
{
    const math::Fixed radius = math::Fixed::from_float(constants::MOVE_UNIT_COLLISION_RADIUS_TILES);
    const math::Fixed min_distance = radius + radius;
    const math::Fixed min_distance_sq = min_distance * min_distance;

    const auto unit_view = registry.view<components::UnitTag, components::Health>();
    for (const entt::entity entity : unit_view) {
        if (is_entity_ignored_for_movement(entity, ignore, also_ignore)) {
            continue;
        }

        const auto& health = unit_view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        if (registry.any_of<components::GarrisonedTag>(entity)) {
            continue;
        }

        math::Fixed unit_x{};
        math::Fixed unit_y{};
        if (registry.all_of<components::WorldPosition>(entity)) {
            const auto& world = registry.get<components::WorldPosition>(entity);
            unit_x = world.x;
            unit_y = world.y;
        }
        else if (registry.all_of<components::GridPosition>(entity)) {
            const core::GridPos cell = registry.get<components::GridPosition>(entity).cell;
            const math::Fixed half = math::Fixed::from_int(1) / math::Fixed::from_int(2);
            unit_x = math::Fixed::from_int(cell.x) + half;
            unit_y = math::Fixed::from_int(cell.y) + half;
        }
        else {
            continue;
        }

        const math::Fixed delta_x = unit_x - point_x;
        const math::Fixed delta_y = unit_y - point_y;
        if (delta_x * delta_x + delta_y * delta_y < min_distance_sq) {
            return true;
        }
    }

    return false;
}

bool unit_reserves_cell(
    entt::registry& registry,
    const entt::entity entity,
    const core::GridPos cell)
{
    if (!registry.any_of<components::MovePath>(entity)) {
        return false;
    }

    const auto& path = registry.get<components::MovePath>(entity);
    return !path.cells.empty() && path.cells.back() == cell;
}

struct PathOccupancy {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> blocked{};

    [[nodiscard]] bool contains(const core::GridPos cell) const
    {
        if (cell.x < 0 || cell.y < 0 || cell.x >= width || cell.y >= height) {
            return true;
        }

        return blocked[static_cast<std::size_t>(cell.y * width + cell.x)] != 0U;
    }

    void mark(const core::GridPos cell)
    {
        if (cell.x < 0 || cell.y < 0 || cell.x >= width || cell.y >= height) {
            return;
        }

        blocked[static_cast<std::size_t>(cell.y * width + cell.x)] = 1U;
    }
};

void mark_building_footprint(
    PathOccupancy& occupancy,
    const components::GridPosition& anchor,
    components::BuildingFootprint footprint,
    const bool is_town_center)
{
    footprint = components::effective_building_footprint(footprint, is_town_center);
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            occupancy.mark({anchor.cell.x + x, anchor.cell.y + y});
        }
    }
}

PathOccupancy build_path_occupancy(
    entt::registry& registry,
    const components::MapGrid& map,
    const entt::entity ignore,
    const entt::entity also_ignore)
{
    PathOccupancy occupancy{};
    occupancy.width = map.width;
    occupancy.height = map.height;
    occupancy.blocked.assign(static_cast<std::size_t>(map.width * map.height), 0U);

    const auto building_view =
        registry.view<components::BuildingTag, components::GridPosition, components::Health>();
    for (const entt::entity entity : building_view) {
        if (is_entity_ignored_for_movement(entity, ignore, also_ignore)) {
            continue;
        }

        if (building_view.get<components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        if (is_unstarted_construction(registry, entity)) {
            continue;
        }

        if (registry.any_of<components::FarmTag>(entity)) {
            continue;
        }

        components::BuildingFootprint footprint{};
        if (registry.any_of<components::BuildingFootprint>(entity)) {
            footprint = registry.get<components::BuildingFootprint>(entity);
        }

        mark_building_footprint(
            occupancy,
            building_view.get<components::GridPosition>(entity),
            footprint,
            registry.any_of<components::TownCenterTag>(entity));
    }

    const auto lake_view = registry.view<
        components::ManaLakeTag,
        components::GridPosition,
        components::BuildingFootprint>();
    for (const entt::entity lake : lake_view) {
        if (is_entity_ignored_for_movement(lake, ignore, also_ignore)) {
            continue;
        }

        mark_building_footprint(
            occupancy,
            lake_view.get<components::GridPosition>(lake),
            lake_view.get<components::BuildingFootprint>(lake),
            false);
    }

    return occupancy;
}

bool is_step_blocked_by_occupancy(
    const PathOccupancy& occupancy,
    const components::MapGrid& map,
    const core::GridPos from,
    const core::GridPos to,
    const bool allow_forest)
{
    const StepCells step_cells = cells_along_step(from, to);
    for (int index = 1; index < step_cells.count; ++index) {
        const core::GridPos cell = step_cells.cells[static_cast<std::size_t>(index)];
        if (!is_tile_walkable(map, cell, allow_forest) || occupancy.contains(cell)) {
            return true;
        }
    }

    const int delta_x = to.x - from.x;
    const int delta_y = to.y - from.y;
    if (std::abs(delta_x) != 1 || std::abs(delta_y) != 1) {
        return false;
    }

    const int step_x = delta_x > 0 ? 1 : -1;
    const int step_y = delta_y > 0 ? 1 : -1;
    const core::GridPos cardinal_x{from.x + step_x, from.y};
    const core::GridPos cardinal_y{from.x, from.y + step_y};
    return occupancy.contains(cardinal_x) || occupancy.contains(cardinal_y)
        || !is_tile_walkable(map, cardinal_x, allow_forest)
        || !is_tile_walkable(map, cardinal_y, allow_forest);
}

[[nodiscard]] bool occupancy_line_is_clear(
    const PathOccupancy& occupancy,
    const components::MapGrid& map,
    const core::GridPos from,
    const core::GridPos to,
    const bool allow_forest)
{
    if (from == to) {
        return true;
    }

    const float origin_x = static_cast<float>(from.x) + 0.5F;
    const float origin_y = static_cast<float>(from.y) + 0.5F;
    const float dest_x = static_cast<float>(to.x) + 0.5F;
    const float dest_y = static_cast<float>(to.y) + 0.5F;
    const float delta_x = dest_x - origin_x;
    const float delta_y = dest_y - origin_y;
    const int step_x = delta_x > 0.0F ? 1 : (delta_x < 0.0F ? -1 : 0);
    const int step_y = delta_y > 0.0F ? 1 : (delta_y < 0.0F ? -1 : 0);
    const float inf = std::numeric_limits<float>::infinity();
    const float t_delta_x = step_x != 0 ? std::abs(1.0F / delta_x) : inf;
    const float t_delta_y = step_y != 0 ? std::abs(1.0F / delta_y) : inf;

    float t_max_x = inf;
    if (step_x > 0) {
        t_max_x = (static_cast<float>(from.x + 1) - origin_x) / delta_x;
    }
    else if (step_x < 0) {
        t_max_x = (static_cast<float>(from.x) - origin_x) / delta_x;
    }

    float t_max_y = inf;
    if (step_y > 0) {
        t_max_y = (static_cast<float>(from.y + 1) - origin_y) / delta_y;
    }
    else if (step_y < 0) {
        t_max_y = (static_cast<float>(from.y) - origin_y) / delta_y;
    }

    const auto cell_blocked = [&](const core::GridPos cell) {
        return !is_tile_walkable(map, cell, allow_forest) || occupancy.contains(cell);
    };

    int cell_x = from.x;
    int cell_y = from.y;
    const int max_cells = std::abs(to.x - from.x) + std::abs(to.y - from.y) + 4;
    for (int i = 0; i < max_cells; ++i) {
        if (t_max_x > 1.0F + constants::PATHFIND_LINE_T_EPSILON
            && t_max_y > 1.0F + constants::PATHFIND_LINE_T_EPSILON) {
            break;
        }

        const float t_diff = t_max_x - t_max_y;
        if (t_diff < -constants::PATHFIND_LINE_T_EPSILON) {
            if (t_max_x > 1.0F + constants::PATHFIND_LINE_T_EPSILON) {
                break;
            }
            cell_x += step_x;
            t_max_x += t_delta_x;
        }
        else if (t_diff > constants::PATHFIND_LINE_T_EPSILON) {
            if (t_max_y > 1.0F + constants::PATHFIND_LINE_T_EPSILON) {
                break;
            }
            cell_y += step_y;
            t_max_y += t_delta_y;
        }
        else {
            if (t_max_x > 1.0F + constants::PATHFIND_LINE_T_EPSILON) {
                break;
            }
            if (cell_blocked({cell_x + step_x, cell_y})
                || cell_blocked({cell_x, cell_y + step_y})) {
                return false;
            }
            cell_x += step_x;
            cell_y += step_y;
            t_max_x += t_delta_x;
            t_max_y += t_delta_y;
        }

        if (cell_blocked({cell_x, cell_y})) {
            return false;
        }

        if (cell_x == to.x && cell_y == to.y) {
            return true;
        }
    }

    return cell_x == to.x && cell_y == to.y && !cell_blocked(to);
}

[[nodiscard]] std::vector<core::GridPos> string_pull_path(
    const PathOccupancy& occupancy,
    const components::MapGrid& map,
    const core::GridPos start,
    const std::vector<core::GridPos>& path,
    const bool allow_forest)
{
    if (path.size() <= 1U) {
        return path;
    }

    std::vector<core::GridPos> pulled{};
    core::GridPos from = start;
    std::size_t last_added = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 0; index < path.size(); ++index) {
        const bool is_last = index + 1U == path.size();
        const bool can_skip = !is_last
            && occupancy_line_is_clear(occupancy, map, from, path[index + 1U], allow_forest);
        if (can_skip) {
            continue;
        }

        if (last_added != index) {
            pulled.push_back(path[index]);
            last_added = index;
        }
        from = path[index];
    }

    return pulled;
}

thread_local PathOccupancy g_frame_occupancy{};
thread_local bool g_frame_occupancy_active{false};

} // namespace

void begin_movement_query_cache(entt::registry& registry, const components::MapGrid& map)
{
    g_frame_occupancy = build_path_occupancy(registry, map, entt::null, entt::null);
    g_frame_occupancy_active = true;
}

void end_movement_query_cache()
{
    g_frame_occupancy_active = false;
}

void reset_pathfind_profile()
{
    g_pathfind_profile = {};
}

PathfindProfile pathfind_profile()
{
    return g_pathfind_profile;
}

bool is_unit_radius_blocked_at_world(
    entt::registry& registry,
    const math::Fixed point_x,
    const math::Fixed point_y,
    const entt::entity ignore,
    const entt::entity also_ignore)
{
    if (g_frame_occupancy_active) {
        const core::GridPos cell{point_x.to_int(), point_y.to_int()};
        bool nearby = g_frame_occupancy.contains(cell);
        if (!nearby) {
            for (int dy = -1; dy <= 1 && !nearby; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }

                    if (g_frame_occupancy.contains({cell.x + dx, cell.y + dy})) {
                        nearby = true;
                        break;
                    }
                }
            }
        }

        if (!nearby) {
            return false;
        }
    }

    return units_block_world_point(registry, point_x, point_y, ignore, also_ignore);
}

bool is_unit_occupying_or_reserving_cell(
    entt::registry& registry,
    const core::GridPos cell,
    const entt::entity ignore,
    const entt::entity also_ignore)
{
    const auto unit_view = registry.view<components::UnitTag, components::Health>();
    for (const entt::entity entity : unit_view) {
        if (is_entity_ignored_for_movement(entity, ignore, also_ignore)) {
            continue;
        }

        const auto& health = unit_view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        if (registry.any_of<components::GarrisonedTag>(entity)) {
            continue;
        }

        if (unit_movement_grid_cell(registry, entity) == cell) {
            return true;
        }
    }

    return false;
}

bool is_movement_blocked(
    entt::registry& registry,
    const core::GridPos cell,
    const entt::entity ignore,
    const entt::entity also_ignore)
{
    if (g_frame_occupancy_active && !g_frame_occupancy.contains(cell)) {
        return false;
    }

    if (is_building_blocking_cell(registry, cell, ignore, also_ignore)) {
        return true;
    }

    return false;
}

bool is_cell_blocked_for_building(
    entt::registry& registry,
    const core::GridPos cell,
    const entt::entity ignore,
    const entt::entity also_ignore)
{
    if (is_building_blocking_cell(registry, cell, ignore, also_ignore)) {
        return true;
    }

    const auto farm_view = registry.view<
        components::FarmTag,
        components::GridPosition,
        components::Health>();
    for (const entt::entity farm : farm_view) {
        if (is_entity_ignored_for_movement(farm, ignore, also_ignore)) {
            continue;
        }

        if (farm_view.get<components::Health>(farm).current.raw() <= 0) {
            continue;
        }

        components::BuildingFootprint footprint{};
        if (registry.any_of<components::BuildingFootprint>(farm)) {
            footprint = registry.get<components::BuildingFootprint>(farm);
        }
        if (components::building_contains_cell(
                farm_view.get<components::GridPosition>(farm),
                footprint,
                cell)) {
            return true;
        }
    }

    const auto unit_view = registry.view<components::UnitTag, components::Health>();
    for (const entt::entity entity : unit_view) {
        if (is_entity_ignored_for_movement(entity, ignore, also_ignore)) {
            continue;
        }

        const auto& health = unit_view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        if (registry.any_of<components::GarrisonedTag>(entity)) {
            continue;
        }

        if (unit_movement_grid_cell(registry, entity) == cell) {
            return true;
        }
    }

    const math::Fixed half = math::Fixed::from_int(1) / math::Fixed::from_int(2);
    const math::Fixed point_x = math::Fixed::from_int(cell.x) + half;
    const math::Fixed point_y = math::Fixed::from_int(cell.y) + half;
    return units_block_world_point(registry, point_x, point_y, ignore, also_ignore);
}

bool is_step_movement_blocked(
    entt::registry& registry,
    const components::MapGrid& map,
    const core::GridPos from,
    const core::GridPos to,
    const entt::entity ignore,
    const bool allow_forest,
    const entt::entity also_ignore)
{
    const StepCells step_cells = cells_along_step(from, to);
    for (int index = 1; index < step_cells.count; ++index) {
        const core::GridPos cell = step_cells.cells[static_cast<std::size_t>(index)];
        if (!is_tile_walkable(map, cell, allow_forest)) {
            return true;
        }

        if (is_movement_blocked(registry, cell, ignore, also_ignore)) {
            return true;
        }
    }

    const int delta_x = to.x - from.x;
    const int delta_y = to.y - from.y;
    const int abs_dx = std::abs(delta_x);
    const int abs_dy = std::abs(delta_y);
    if (abs_dx != 1 || abs_dy != 1) {
        return false;
    }

    const int step_x = delta_x > 0 ? 1 : -1;
    const int step_y = delta_y > 0 ? 1 : -1;
    const core::GridPos cardinal_x{from.x + step_x, from.y};
    const core::GridPos cardinal_y{from.x, from.y + step_y};

    const bool cardinal_x_blocked = !is_tile_walkable(map, cardinal_x, allow_forest)
        || is_building_blocking_cell(registry, cardinal_x, ignore, also_ignore);
    const bool cardinal_y_blocked = !is_tile_walkable(map, cardinal_y, allow_forest)
        || is_building_blocking_cell(registry, cardinal_y, ignore, also_ignore);
    if (cardinal_x_blocked || cardinal_y_blocked) {
        return true;
    }

    return false;
}

bool is_world_segment_movement_blocked(
    entt::registry& registry,
    const components::MapGrid& map,
    const math::Fixed from_x,
    const math::Fixed from_y,
    const math::Fixed to_x,
    const math::Fixed to_y,
    const entt::entity ignore,
    const bool allow_forest)
{
    const core::GridPos from_cell{from_x.to_int(), from_y.to_int()};
    const core::GridPos to_cell{to_x.to_int(), to_y.to_int()};
    if (is_step_movement_blocked(registry, map, from_cell, to_cell, ignore, allow_forest)) {
        return true;
    }

    // Dense samples catch diagonal corner-cuts through 1x1 solids that grid Bresenham can miss
    // when the unit starts on a tile corner (e.g. after corner gather).
    for (int sample = 1; sample < constants::MOVE_SEGMENT_SOLID_SAMPLE_COUNT; ++sample) {
        const math::Fixed t = math::Fixed::from_int(sample)
            / math::Fixed::from_int(constants::MOVE_SEGMENT_SOLID_SAMPLE_COUNT);
        const math::Fixed sample_x = math::fixed_lerp(from_x, to_x, t);
        const math::Fixed sample_y = math::fixed_lerp(from_y, to_y, t);
        const core::GridPos sample_cell{sample_x.to_int(), sample_y.to_int()};
        if (!is_tile_walkable(map, sample_cell, allow_forest)) {
            return true;
        }

        if (is_building_blocking_cell(registry, sample_cell, ignore, entt::null)) {
            return true;
        }
    }

    return false;
}

bool is_world_position_solid_blocked(
    entt::registry& registry,
    const components::MapGrid& map,
    const components::WorldPosition& world,
    const entt::entity ignore,
    const bool allow_forest)
{
    // Center-cell solid check only (no unit occupancy): stops walking through 1x1 resources
    // mid-segment while still allowing edge overhang for gather/deposit.
    const core::GridPos cell = world_position_to_grid_cell(world);
    if (!is_tile_walkable(map, cell, allow_forest)) {
        return true;
    }

    return is_building_blocking_cell(registry, cell, ignore, entt::null);
}

bool is_world_position_movement_blocked(
    entt::registry& registry,
    const components::MapGrid& map,
    const components::WorldPosition& world,
    const entt::entity ignore,
    const bool allow_forest)
{
    if (is_world_position_solid_blocked(registry, map, world, ignore, allow_forest)) {
        return true;
    }

    return is_unit_radius_blocked_at_world(registry, world.x, world.y, ignore);
}

bool is_tile_walkable(
    const components::MapGrid& map,
    const core::GridPos pos,
    const bool allow_forest)
{
    if (!core::is_inside_grid(pos, map.width, map.height)) {
        return false;
    }

    const int index = core::grid_index(pos, map.width);
    const auto tile = map.tiles[static_cast<std::size_t>(index)];
    if (tile == components::TileType::Grass) {
        return true;
    }

    if (tile == components::TileType::Forest) {
        if (map.forest_wood[static_cast<std::size_t>(index)] <= 0) {
            return true;
        }

        return allow_forest;
    }

    if (tile == components::TileType::Berries || tile == components::TileType::Blueberries) {
        return map.bush_food[static_cast<std::size_t>(index)] <= 0;
    }

    if (tile == components::TileType::GoldMine) {
        if (static_cast<std::size_t>(index) >= map.mine_money.size()) {
            return true;
        }
        return map.mine_money[static_cast<std::size_t>(index)] <= 0;
    }

    if (tile == components::TileType::Rock) {
        return false;
    }

    return false;
}

core::GridPos find_best_approach_stand_tile(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos target_cell,
    const entt::entity mover,
    const bool avoid_unit_occupancy)
{
    const float target_min_x = static_cast<float>(target_cell.x);
    const float target_min_y = static_cast<float>(target_cell.y);
    const float target_max_x = static_cast<float>(target_cell.x + 1);
    const float target_max_y = static_cast<float>(target_cell.y + 1);

    const core::GridPos mover_cell = unit_movement_grid_cell(registry, mover);
    if (work_stand_goal_in_interact_range(
            mover_cell, target_min_x, target_min_y, target_max_x, target_max_y)
        && is_tile_walkable(map, mover_cell, false)
        && !is_movement_blocked(registry, mover_cell, mover)
        && (!avoid_unit_occupancy
            || !is_unit_occupying_or_reserving_cell(registry, mover_cell, mover))
        && mover_cell != target_cell
        && is_cardinal_neighbor_cell(mover_cell, target_cell)) {
        return mover_cell;
    }

    // Gather/deposit approach: cardinal edges first, corners only if no edge is free.
    core::GridPos best{-1, -1};
    int best_travel = std::numeric_limits<int>::max();
    bool best_cardinal = false;
    bool best_free = false;
    bool found = false;

    for (const PathStepOffset& step : path_step_offsets) {
        if (std::abs(step.offset.x) > 1 || std::abs(step.offset.y) > 1) {
            continue;
        }

        const core::GridPos candidate{
            target_cell.x + step.offset.x,
            target_cell.y + step.offset.y,
        };
        if (candidate == target_cell) {
            continue;
        }

        if (!work_stand_goal_in_interact_range(
                candidate, target_min_x, target_min_y, target_max_x, target_max_y)) {
            continue;
        }

        if (!is_tile_walkable(map, candidate, false)) {
            continue;
        }

        if (is_movement_blocked(registry, candidate, mover)) {
            continue;
        }

        const bool occupied = is_unit_occupying_or_reserving_cell(registry, candidate, mover);
        if (avoid_unit_occupancy && occupied) {
            continue;
        }

        const bool cardinal = is_cardinal_neighbor_cell(candidate, target_cell);
        const bool free = !occupied;
        const int travel = core::chebyshev_distance(mover_cell, candidate);
        if (!found
            || (cardinal && !best_cardinal)
            || (cardinal == best_cardinal && free && !best_free)
            || (cardinal == best_cardinal && free == best_free && travel < best_travel)
            || (cardinal == best_cardinal && free == best_free && travel == best_travel
                && (candidate.y < best.y
                    || (candidate.y == best.y && candidate.x < best.x)))) {
            found = true;
            best_cardinal = cardinal;
            best_free = free;
            best_travel = travel;
            best = candidate;
        }
    }

    if (!found) {
        return {-1, -1};
    }

    return best;
}

core::GridPos find_nearest_walkable_goal(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos from,
    const core::GridPos preferred,
    const entt::entity ignore)
{
    (void)from;
    const auto usable = [&](const core::GridPos cell) {
        if (!is_tile_walkable(map, cell, false)) {
            return false;
        }

        return !is_building_blocking_cell(registry, cell, ignore, entt::null);
    };

    if (usable(preferred)) {
        return preferred;
    }

    if (map.width <= 0 || map.height <= 0) {
        return {-1, -1};
    }

    const int cell_count = map.width * map.height;
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(cell_count), 0U);
    std::queue<core::GridPos> pending{};
    const auto try_enqueue = [&](const core::GridPos cell) {
        if (!core::is_inside_grid(cell, map.width, map.height)) {
            return;
        }

        const int index = core::grid_index(cell, map.width);
        if (visited[static_cast<std::size_t>(index)] != 0U) {
            return;
        }

        visited[static_cast<std::size_t>(index)] = 1U;
        pending.push(cell);
    };

    try_enqueue(preferred);
    int expanded = 0;
    static constexpr std::array<core::GridPos, 8> k_neighbors{{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
        {1, 1},
        {1, -1},
        {-1, 1},
        {-1, -1},
    }};
    while (!pending.empty() && expanded < constants::PATHFIND_MAX_EXPANDED_NODES) {
        const core::GridPos current = pending.front();
        pending.pop();
        ++expanded;
        if (usable(current)) {
            return current;
        }

        if (core::chebyshev_distance(current, preferred)
            >= constants::MOVE_UNWALKABLE_GOAL_SEARCH_RADIUS) {
            continue;
        }

        for (const core::GridPos offset : k_neighbors) {
            try_enqueue({current.x + offset.x, current.y + offset.y});
        }
    }

    return {-1, -1};
}

[[nodiscard]] std::vector<core::GridPos> reconstruct_path_cells(
    const std::vector<int>& parents,
    const int map_width,
    const core::GridPos start,
    const core::GridPos goal)
{
    std::vector<core::GridPos> path{};
    core::GridPos cursor = goal;
    while (!(cursor == start)) {
        path.push_back(cursor);
        const int parent_index = parents[static_cast<std::size_t>(cursor.y * map_width + cursor.x)];
        cursor = {parent_index % map_width, parent_index / map_width};
    }

    std::reverse(path.begin(), path.end());
    return path;
}

[[nodiscard]] std::vector<core::GridPos> find_closest_reachable_path(
    const PathOccupancy& occupancy,
    const components::MapGrid& map,
    const core::GridPos start,
    const core::GridPos preferred,
    const bool allow_forest)
{
    const int map_width = map.width;
    const int cell_count = map.width * map.height;
    if (cell_count <= 0 || start == preferred) {
        return {};
    }

    std::vector<int> parents(static_cast<std::size_t>(cell_count), -1);
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(cell_count), 0U);
    std::queue<core::GridPos> pending{};
    const int start_index = start.y * map_width + start.x;
    visited[static_cast<std::size_t>(start_index)] = 1U;
    pending.push(start);

    core::GridPos best = start;
    int best_distance = core::chebyshev_distance(start, preferred);
    int expanded = 0;
    while (!pending.empty() && expanded < constants::PATHFIND_MAX_EXPANDED_NODES) {
        const core::GridPos current = pending.front();
        pending.pop();
        ++expanded;

        const int distance = core::chebyshev_distance(current, preferred);
        if (distance < best_distance
            || (distance == best_distance
                && (current.y < best.y || (current.y == best.y && current.x < best.x)))) {
            best = current;
            best_distance = distance;
        }

        if (current == preferred) {
            break;
        }

        for (const PathStepOffset& step : path_step_offsets) {
            const core::GridPos neighbor{current.x + step.offset.x, current.y + step.offset.y};
            if (is_step_blocked_by_occupancy(occupancy, map, current, neighbor, allow_forest)) {
                continue;
            }

            const int neighbor_index = neighbor.y * map_width + neighbor.x;
            if (visited[static_cast<std::size_t>(neighbor_index)] != 0U) {
                continue;
            }

            visited[static_cast<std::size_t>(neighbor_index)] = 1U;
            parents[static_cast<std::size_t>(neighbor_index)] = current.y * map_width + current.x;
            pending.push(neighbor);
        }
    }

    g_pathfind_profile.expanded_nodes += expanded;
    if (best == start) {
        return {};
    }

    return string_pull_path(
        occupancy,
        map,
        start,
        reconstruct_path_cells(parents, map_width, start, best),
        allow_forest);
}

std::vector<core::GridPos> find_path(
    const components::MapGrid& map,
    core::GridPos start,
    core::GridPos goal,
    entt::registry& registry,
    const entt::entity ignore,
    const bool allow_forest,
    const entt::entity also_ignore)
{
    ++g_pathfind_profile.calls;
    const core::GridPos original_goal = goal;

    if (start == goal) {
        return {};
    }

    if (!is_tile_walkable(map, goal, allow_forest)
        || is_building_blocking_cell(registry, goal, ignore, also_ignore)) {
        goal = find_nearest_walkable_goal(map, registry, start, goal, ignore);
        if (goal.x < 0) {
            goal = start;
        }
        if (start == goal) {
            const PathOccupancy occupancy =
                build_path_occupancy(registry, map, ignore, also_ignore);
            auto closest = find_closest_reachable_path(
                occupancy, map, start, original_goal, allow_forest);
            if (closest.empty()) {
                ++g_pathfind_profile.failed;
            }
            return closest;
        }
    }

    const int map_width = map.width;
    const int cell_count = map.width * map.height;
    if (cell_count <= 0) {
        ++g_pathfind_profile.failed;
        return {};
    }

    const PathOccupancy occupancy = build_path_occupancy(registry, map, ignore, also_ignore);

    const auto cell_index = [map_width](const core::GridPos cell) {
        return cell.y * map_width + cell.x;
    };

    thread_local std::vector<int> g_scores{};
    thread_local std::vector<int> parents{};
    thread_local std::vector<std::uint8_t> closed{};
    g_scores.assign(static_cast<std::size_t>(cell_count), -1);
    parents.resize(static_cast<std::size_t>(cell_count));
    closed.assign(static_cast<std::size_t>(cell_count), 0U);

    const auto heuristic = [](const core::GridPos from, const core::GridPos to) {
        return std::max(std::abs(from.x - to.x), std::abs(from.y - to.y))
            * constants::PATHFIND_CARDINAL_STEP_COST;
    };

    struct QueueNode {
        core::GridPos pos{};
        int f_score{0};
    };

    const auto compare = [](const QueueNode& left, const QueueNode& right) {
        if (left.f_score != right.f_score) {
            return left.f_score > right.f_score;
        }

        if (left.pos.y != right.pos.y) {
            return left.pos.y > right.pos.y;
        }

        return left.pos.x > right.pos.x;
    };

    std::priority_queue<QueueNode, std::vector<QueueNode>, decltype(compare)> open(compare);
    const int start_index = cell_index(start);
    g_scores[static_cast<std::size_t>(start_index)] = 0;
    open.push(QueueNode{start, heuristic(start, goal)});

    int expanded = 0;
    while (!open.empty() && expanded < constants::PATHFIND_MAX_EXPANDED_NODES) {
        const core::GridPos current = open.top().pos;
        open.pop();

        const int current_index = cell_index(current);
        if (closed[static_cast<std::size_t>(current_index)] != 0U) {
            continue;
        }

        closed[static_cast<std::size_t>(current_index)] = 1U;
        ++expanded;

        if (current == goal) {
            g_pathfind_profile.expanded_nodes += expanded;
            return string_pull_path(
                occupancy,
                map,
                start,
                reconstruct_path_cells(parents, map_width, start, goal),
                allow_forest);
        }

        const int current_g = g_scores[static_cast<std::size_t>(current_index)];
        for (const PathStepOffset& step : path_step_offsets) {
            const core::GridPos neighbor{current.x + step.offset.x, current.y + step.offset.y};
            if (is_step_blocked_by_occupancy(occupancy, map, current, neighbor, allow_forest)) {
                continue;
            }

            const int neighbor_index = cell_index(neighbor);
            const int tentative_g = current_g + step.cost;
            int& existing_g = g_scores[static_cast<std::size_t>(neighbor_index)];
            if (existing_g >= 0 && tentative_g >= existing_g) {
                continue;
            }

            parents[static_cast<std::size_t>(neighbor_index)] = current_index;
            existing_g = tentative_g;
            open.push(QueueNode{neighbor, tentative_g + heuristic(neighbor, goal)});
        }
    }

    g_pathfind_profile.expanded_nodes += expanded;
    auto closest = find_closest_reachable_path(
        occupancy, map, start, original_goal, allow_forest);
    if (closest.empty()) {
        ++g_pathfind_profile.failed;
    }
    return closest;
}

} // namespace aoa::sim::systems
