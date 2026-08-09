#include "sim/systems/gameplay_systems.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/content_pack.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/fog_of_war.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/fog_of_war.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/sfx_events.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/player/player_economy.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"
#include "sim/systems/pathfinding.hpp"
#include "sim/systems/visibility_system.hpp"

#include "math/fixed.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace aoa::sim::systems {

namespace detail {

math::Fixed clamp_fixed(const math::Fixed value, const math::Fixed min_value, const math::Fixed max_value)
{
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

std::pair<math::Fixed, math::Fixed> clamp_world_goal_to_cell(
    const math::Fixed world_x,
    const math::Fixed world_y,
    const core::GridPos cell)
{
    const math::Fixed inset = math::Fixed::from_float(constants::MOVE_GOAL_TILE_EDGE_INSET);
    const math::Fixed min_x = math::Fixed::from_int(cell.x) + inset;
    const math::Fixed max_x = math::Fixed::from_int(cell.x + 1) - inset;
    const math::Fixed min_y = math::Fixed::from_int(cell.y) + inset;
    const math::Fixed max_y = math::Fixed::from_int(cell.y + 1) - inset;
    return {
        clamp_fixed(world_x, min_x, max_x),
        clamp_fixed(world_y, min_y, max_y),
    };
}

void path_step_destination(
    const components::MovePath& path,
    const core::GridPos step,
    const bool is_last_step,
    math::Fixed& out_x,
    math::Fixed& out_y)
{
    if (is_last_step && path.has_goal_world) {
        out_x = path.goal_world_x;
        out_y = path.goal_world_y;
        return;
    }

    out_x = math::tile_center_coord(step.x);
    out_y = math::tile_center_coord(step.y);
}

void sync_grid_from_world(entt::registry& registry, const entt::entity entity)
{
    if (!registry.all_of<components::WorldPosition, components::GridPosition>(entity)) {
        return;
    }

    const auto& world = registry.get<components::WorldPosition>(entity);
    auto& grid = registry.get<components::GridPosition>(entity);
    grid.cell.x = world.x.to_int();
    grid.cell.y = world.y.to_int();
}

void sync_previous_world_position_from_current(
    entt::registry& registry,
    const entt::entity entity)
{
    if (!registry.any_of<components::WorldPosition>(entity)) {
        return;
    }

    const auto& world = registry.get<components::WorldPosition>(entity);
    auto& previous = registry.get_or_emplace<components::PreviousWorldPosition>(entity);
    previous.x = world.x;
    previous.y = world.y;
}

void cancel_move_segment_for_repath(entt::registry& registry, const entt::entity entity)
{
    if (!registry.any_of<components::MoveSegment>(entity)) {
        return;
    }

    if (registry.all_of<components::MoveSegment, components::WorldPosition>(entity)) {
        auto& segment = registry.get<components::MoveSegment>(entity);
        auto& world = registry.get<components::WorldPosition>(entity);

        if (segment.ticks_total > 0) {
            const int lookahead_ticks = std::min(
                segment.ticks_elapsed + constants::MOVE_REPATH_SEGMENT_LOOKAHEAD_TICKS,
                segment.ticks_total);
            const math::Fixed progress = math::Fixed::from_int(lookahead_ticks)
                / math::Fixed::from_int(segment.ticks_total);
            world.x = math::fixed_lerp(segment.from_x, segment.to_x, progress);
            world.y = math::fixed_lerp(segment.from_y, segment.to_y, progress);
        }
    }

    registry.remove<components::MoveSegment>(entity);
    sync_grid_from_world(registry, entity);
}

void advance_path_past_reached_steps(entt::registry& registry, const entt::entity entity)
{
    if (!registry.all_of<components::MovePath, components::WorldPosition>(entity)) {
        return;
    }

    auto& path = registry.get<components::MovePath>(entity);
    const auto& world = registry.get<components::WorldPosition>(entity);
    const math::Fixed reach_threshold = math::Fixed::from_float(
        constants::MOVE_PATH_STEP_REACHED_TILE_DISTANCE);
    const math::Fixed reach_threshold_squared = reach_threshold * reach_threshold;

    while (path.next_index < static_cast<int>(path.cells.size())) {
        const core::GridPos step = path.cells[static_cast<std::size_t>(path.next_index)];
        const bool is_last_step = path.next_index + 1 >= static_cast<int>(path.cells.size());
        math::Fixed step_x = math::tile_center_coord(step.x);
        math::Fixed step_y = math::tile_center_coord(step.y);
        path_step_destination(path, step, is_last_step, step_x, step_y);
        const math::Fixed delta_x = world.x - step_x;
        const math::Fixed delta_y = world.y - step_y;
        const math::Fixed distance_squared = delta_x * delta_x + delta_y * delta_y;
        if (distance_squared > reach_threshold_squared) {
            break;
        }

        ++path.next_index;
    }

    if (path.next_index >= static_cast<int>(path.cells.size())) {
        registry.remove<components::MovePath>(entity);
    }
}

} // namespace detail

void assign_unit_path(
    entt::registry& registry,
    const entt::entity entity,
    const components::MapGrid& map,
    const core::GridPos goal,
    const entt::entity also_ignore,
    const bool allow_knight_steps,
    const bool has_goal_world,
    const math::Fixed goal_world_x,
    const math::Fixed goal_world_y,
    const bool use_attack_pathfinding)
{
    detail::cancel_move_segment_for_repath(registry, entity);
    detail::sync_grid_from_world(registry, entity);

    const core::GridPos start = unit_movement_grid_cell(registry, entity);
    auto& path = registry.get_or_emplace<components::MovePath>(entity);
    if (use_attack_pathfinding) {
        path.cells = find_attack_path(map, start, goal, registry, entity, false, also_ignore);
    }
    else {
        path.cells = find_path(map, start, goal, registry, entity, false, also_ignore, allow_knight_steps);
    }
    path.next_index = 0;
    path.has_goal_world = has_goal_world;
    if (has_goal_world) {
        const auto [clamped_x, clamped_y] = detail::clamp_world_goal_to_cell(goal_world_x, goal_world_y, goal);
        path.goal_world_x = clamped_x;
        path.goal_world_y = clamped_y;
    }

    if (path.cells.empty() && start == goal && has_goal_world) {
        path.cells.push_back(goal);
    }

    detail::advance_path_past_reached_steps(registry, entity);
    detail::sync_previous_world_position_from_current(registry, entity);
}

namespace {

entt::entity find_world_entity(entt::registry& registry)
{
    const auto view = registry.view<components::WorldTag>();
    if (view.begin() == view.end()) {
        return entt::null;
    }

    return *view.begin();
}

const data::ArchetypeDefinition* find_unit_archetype_from_ref(
    const components::ContentPack& content_pack,
    const components::DefinitionRef& definition_ref)
{
    return data::find_unit_archetype(content_pack.content, definition_ref.id);
}

entt::entity find_town_center_for_player_slot(
    entt::registry& registry,
    const std::uint8_t player_slot)
{
    const auto town_center_view =
        registry.view<components::TownCenterTag, components::PlayerOwnedTag, components::GridPosition>();
    entt::entity under_construction = entt::null;
    for (const entt::entity candidate : town_center_view) {
        if (components::entity_player_slot(registry, candidate) != player_slot) {
            continue;
        }

        if (!registry.any_of<components::UnderConstructionTag>(candidate)) {
            return candidate;
        }

        if (under_construction == entt::null) {
            under_construction = candidate;
        }
    }

    return under_construction;
}

void deplete_forest_tile(components::MapGrid& map, const core::GridPos cell)
{
    if (!core::is_inside_grid(cell, map.width, map.height)) {
        return;
    }

    const int index = core::grid_index(cell, map.width);
    map.forest_wood[static_cast<std::size_t>(index)] = 0;
}

void deplete_bush_tile(components::MapGrid& map, const core::GridPos cell)
{
    if (!core::is_inside_grid(cell, map.width, map.height)) {
        return;
    }

    const int index = core::grid_index(cell, map.width);
    map.bush_food[static_cast<std::size_t>(index)] = 0;
    map.tiles[static_cast<std::size_t>(index)] = components::TileType::Grass;
}

void deplete_gold_mine_tile(components::MapGrid& map, const core::GridPos cell)
{
    if (!core::is_inside_grid(cell, map.width, map.height)) {
        return;
    }

    const int index = core::grid_index(cell, map.width);
    if (static_cast<std::size_t>(index) < map.mine_money.size()) {
        map.mine_money[static_cast<std::size_t>(index)] = 0;
    }
    map.tiles[static_cast<std::size_t>(index)] = components::TileType::Grass;
}

bool is_occupied(entt::registry& registry, const core::GridPos cell, const entt::entity ignore)
{
    return is_movement_blocked(registry, cell, ignore);
}

[[nodiscard]] bool is_bush_tile(const components::TileType tile)
{
    return tile == components::TileType::Berries || tile == components::TileType::Blueberries;
}

bool forest_tile_has_wood(const components::MapGrid& map, const core::GridPos cell)
{
    if (!core::is_inside_grid(cell, map.width, map.height)) {
        return false;
    }

    const int index = core::grid_index(cell, map.width);
    if (map.tiles[static_cast<std::size_t>(index)] != components::TileType::Forest) {
        return false;
    }

    return map.forest_wood[static_cast<std::size_t>(index)] > 0;
}

bool bush_tile_has_food(const components::MapGrid& map, const core::GridPos cell)
{
    if (!core::is_inside_grid(cell, map.width, map.height)) {
        return false;
    }

    const int index = core::grid_index(cell, map.width);
    if (!is_bush_tile(map.tiles[static_cast<std::size_t>(index)])) {
        return false;
    }

    return map.bush_food[static_cast<std::size_t>(index)] > 0;
}

bool gold_mine_has_money(const components::MapGrid& map, const core::GridPos cell)
{
    if (!core::is_inside_grid(cell, map.width, map.height)) {
        return false;
    }

    const int index = core::grid_index(cell, map.width);
    if (map.tiles[static_cast<std::size_t>(index)] != components::TileType::GoldMine) {
        return false;
    }

    return static_cast<std::size_t>(index) < map.mine_money.size()
        && map.mine_money[static_cast<std::size_t>(index)] > 0;
}

bool resource_tile_has_remaining(const components::MapGrid& map, const core::GridPos cell)
{
    return forest_tile_has_wood(map, cell)
        || bush_tile_has_food(map, cell)
        || gold_mine_has_money(map, cell);
}

[[nodiscard]] bool resource_tile_matches_type(
    const components::MapGrid& map,
    const core::GridPos cell,
    const components::TileType resource_type)
{
    if (!core::is_inside_grid(cell, map.width, map.height)) {
        return false;
    }

    const int index = core::grid_index(cell, map.width);
    if (map.tiles[static_cast<std::size_t>(index)] != resource_type) {
        return false;
    }

    switch (resource_type) {
    case components::TileType::Forest:
        return map.forest_wood[static_cast<std::size_t>(index)] > 0;
    case components::TileType::Berries:
    case components::TileType::Blueberries:
        return map.bush_food[static_cast<std::size_t>(index)] > 0;
    case components::TileType::GoldMine:
        return static_cast<std::size_t>(index) < map.mine_money.size()
            && map.mine_money[static_cast<std::size_t>(index)] > 0;
    case components::TileType::Grass:
        break;
    }

    return false;
}

core::GridPos find_nearest_resource_tile(
    const components::MapGrid& map,
    const core::GridPos from,
    const components::TileType preferred_type = components::TileType::Grass)
{
    core::GridPos best{-1, -1};
    int best_distance = std::numeric_limits<int>::max();
    const bool filter_type = preferred_type != components::TileType::Grass;

    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const core::GridPos cell{x, y};
            if (filter_type) {
                if (!resource_tile_matches_type(map, cell, preferred_type)) {
                    continue;
                }
            }
            else if (!resource_tile_has_remaining(map, cell)) {
                continue;
            }

            const int distance = std::abs(from.x - x) + std::abs(from.y - y);
            if (distance < best_distance) {
                best_distance = distance;
                best = cell;
            }
        }
    }

    return best;
}

core::GridPos find_nearest_forest_with_wood(
    const components::MapGrid& map,
    const core::GridPos from)
{
    return find_nearest_resource_tile(map, from, components::TileType::Forest);
}

core::GridPos find_adjacent_walkable(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos target,
    const entt::entity ignore)
{
    const std::array<core::GridPos, 8> offsets = {
        core::GridPos{0, -1},
        core::GridPos{1, 0},
        core::GridPos{0, 1},
        core::GridPos{-1, 0},
        core::GridPos{1, -1},
        core::GridPos{1, 1},
        core::GridPos{-1, 1},
        core::GridPos{-1, -1},
    };

    for (const core::GridPos offset : offsets) {
        const core::GridPos candidate{target.x + offset.x, target.y + offset.y};
        if (!is_tile_walkable(map, candidate, false)) {
            continue;
        }

        if (!is_occupied(registry, candidate, ignore)) {
            return candidate;
        }
    }

    return target;
}

void assign_path(
    entt::registry& registry,
    const entt::entity entity,
    const components::MapGrid& map,
    const core::GridPos goal,
    const entt::entity also_ignore = entt::null,
    const bool allow_knight_steps = true,
    const bool use_attack_pathfinding = false)
{
    assign_unit_path(
        registry,
        entity,
        map,
        goal,
        also_ignore,
        allow_knight_steps,
        false,
        {},
        {},
        use_attack_pathfinding);
}

bool is_next_path_step_blocked(
    entt::registry& registry,
    const components::MapGrid& map,
    const entt::entity entity)
{
    if (!registry.any_of<components::MovePath>(entity)) {
        return false;
    }

    const auto& path = registry.get<components::MovePath>(entity);
    if (path.next_index >= static_cast<int>(path.cells.size())) {
        return false;
    }

    const core::GridPos current = unit_movement_grid_cell(registry, entity);
    const core::GridPos next_cell = path.cells[static_cast<std::size_t>(path.next_index)];
    return is_step_movement_blocked(registry, map, current, next_cell, entity, false);
}

void replan_path_if_blocked(
    entt::registry& registry,
    const entt::entity entity,
    const components::MapGrid& map,
    const int move_ticks_per_tile)
{
    (void)move_ticks_per_tile;

    if (!registry.any_of<components::MovePath>(entity) || registry.any_of<components::MoveSegment>(entity)) {
        return;
    }

    if (!is_next_path_step_blocked(registry, map, entity)) {
        return;
    }

    const auto& path = registry.get<components::MovePath>(entity);
    if (path.cells.empty()) {
        return;
    }

    const core::GridPos goal = path.cells.back();
    const bool use_attack_pathfinding = registry.any_of<components::AttackOrder>(entity);
    assign_path(registry, entity, map, goal, entt::null, true, use_attack_pathfinding);
    registry.get_or_emplace<components::MoveCooldown>(entity).ticks_remaining =
        constants::MOVE_BLOCKED_REPATH_COOLDOWN_TICKS;
}

bool reassign_worker_resource_target(
    entt::registry& registry,
    const entt::entity worker,
    components::MapGrid& map,
    const core::GridPos worker_pos,
    const components::TileType preferred_type)
{
    const core::GridPos next_resource =
        find_nearest_resource_tile(map, worker_pos, preferred_type);
    if (next_resource.x < 0) {
        registry.remove<components::GatherTarget>(worker);
        registry.get<components::WorkerBrain>(worker).state = components::WorkerState::Idle;
        return false;
    }

    auto& gather_target = registry.get_or_emplace<components::GatherTarget>(worker);
    gather_target.cell = next_resource;
    gather_target.resource_type = preferred_type != components::TileType::Grass
        ? preferred_type
        : map.tiles[static_cast<std::size_t>(core::grid_index(next_resource, map.width))];
    registry.get<components::WorkerBrain>(worker).state = components::WorkerState::MovingToResource;
    const core::GridPos stand_tile = find_adjacent_walkable(map, registry, next_resource, worker);
    assign_path(registry, worker, map, stand_tile);
    return true;
}

void snapshot_previous_world_positions(entt::registry& registry)
{
    const auto view = registry.view<components::WorldPosition>();
    for (const entt::entity entity : view) {
        const auto& world = view.get<components::WorldPosition>(entity);
        registry.get_or_emplace<components::PreviousWorldPosition>(entity).x = world.x;
        registry.get_or_emplace<components::PreviousWorldPosition>(entity).y = world.y;
    }
}

void begin_move_segment(
    entt::registry& registry,
    const entt::entity entity,
    const math::Fixed to_x,
    const math::Fixed to_y,
    const int move_ticks_per_tile)
{
    auto& world = registry.get<components::WorldPosition>(entity);
    auto& segment = registry.get_or_emplace<components::MoveSegment>(entity);
    segment.from_x = world.x;
    segment.from_y = world.y;
    segment.to_x = to_x;
    segment.to_y = to_y;
    segment.ticks_elapsed = 0;
    segment.ticks_total = math::compute_move_segment_ticks(
        segment.from_x,
        segment.from_y,
        segment.to_x,
        segment.to_y,
        move_ticks_per_tile);
    segment.blocked_ticks = 0;

    auto& previous = registry.get_or_emplace<components::PreviousWorldPosition>(entity);
    previous.x = segment.from_x;
    previous.y = segment.from_y;
}

bool try_begin_next_path_step(
    entt::registry& registry,
    const entt::entity entity,
    const components::MapGrid& map,
    const int move_ticks_per_tile)
{
    if (registry.any_of<components::MoveSegment>(entity)) {
        return false;
    }

    if (!registry.any_of<components::MovePath>(entity)) {
        return false;
    }

    detail::advance_path_past_reached_steps(registry, entity);
    if (!registry.any_of<components::MovePath>(entity)) {
        return false;
    }

    auto& path = registry.get<components::MovePath>(entity);
    if (path.next_index >= static_cast<int>(path.cells.size())) {
        registry.remove<components::MovePath>(entity);
        return false;
    }

    const core::GridPos current = unit_movement_grid_cell(registry, entity);
    const core::GridPos path_target = path.cells[static_cast<std::size_t>(path.next_index)];
    if (is_step_movement_blocked(registry, map, current, path_target, entity, false)) {
        if (registry.any_of<components::AttackOrder>(entity)) {
            const entt::entity attack_target = registry.get<components::AttackOrder>(entity).target;
            if (attack_target != entt::null && registry.valid(attack_target)
                && unit_in_melee_range(registry, entity, attack_target)) {
                registry.remove<components::MovePath>(entity);
            }
        }

        // Respect occupancy hitboxes: wait; replan happens in replan_path_if_blocked.
        return false;
    }

    const bool is_last_step = path.next_index + 1 >= static_cast<int>(path.cells.size());
    math::Fixed to_x = math::tile_center_coord(path_target.x);
    math::Fixed to_y = math::tile_center_coord(path_target.y);
    detail::path_step_destination(path, path_target, is_last_step, to_x, to_y);
    if (is_unit_radius_blocked_at_world(registry, to_x, to_y, entity)) {
        return false;
    }

    begin_move_segment(registry, entity, to_x, to_y, move_ticks_per_tile);
    return true;
}

[[nodiscard]] entt::entity find_radius_blocking_unit(
    entt::registry& registry,
    const math::Fixed point_x,
    const math::Fixed point_y,
    const entt::entity ignore)
{
    const math::Fixed radius = math::Fixed::from_float(constants::MOVE_UNIT_COLLISION_RADIUS_TILES);
    const math::Fixed min_distance = radius + radius;
    const math::Fixed min_distance_sq = min_distance * min_distance;

    entt::entity best = entt::null;
    const auto unit_view = registry.view<components::UnitTag, components::Health>();
    for (const entt::entity candidate : unit_view) {
        if (candidate == ignore) {
            continue;
        }

        const auto& health = unit_view.get<components::Health>(candidate);
        if (health.current.raw() <= 0) {
            continue;
        }

        math::Fixed unit_x{};
        math::Fixed unit_y{};
        if (registry.all_of<components::WorldPosition>(candidate)) {
            const auto& world = registry.get<components::WorldPosition>(candidate);
            unit_x = world.x;
            unit_y = world.y;
        }
        else if (registry.all_of<components::GridPosition>(candidate)) {
            const core::GridPos cell = registry.get<components::GridPosition>(candidate).cell;
            const math::Fixed half = math::Fixed::from_int(1) / math::Fixed::from_int(2);
            unit_x = math::Fixed::from_int(cell.x) + half;
            unit_y = math::Fixed::from_int(cell.y) + half;
        }
        else {
            continue;
        }

        const math::Fixed delta_x = unit_x - point_x;
        const math::Fixed delta_y = unit_y - point_y;
        if (delta_x * delta_x + delta_y * delta_y >= min_distance_sq) {
            continue;
        }

        if (best == entt::null
            || snapshot::compare_entities_for_deterministic_iteration(registry, candidate, best)) {
            best = candidate;
        }
    }

    return best;
}

[[nodiscard]] bool radius_block_should_yield(
    entt::registry& registry,
    const entt::entity mover,
    const math::Fixed point_x,
    const math::Fixed point_y)
{
    if (!is_unit_radius_blocked_at_world(registry, point_x, point_y, mover)) {
        return false;
    }

    const entt::entity blocker = find_radius_blocking_unit(registry, point_x, point_y, mover);
    if (blocker == entt::null) {
        return true;
    }

    // Higher snapshot key yields: allow the lower-key mover to advance.
    return !snapshot::compare_entities_for_deterministic_iteration(registry, mover, blocker);
}

bool try_divert_around_block(
    entt::registry& registry,
    const entt::entity entity,
    const components::MapGrid& map,
    const components::MoveSegment& segment)
{
    const core::GridPos current = unit_movement_grid_cell(registry, entity);
    const int dir_x = segment.to_x.to_int() - segment.from_x.to_int();
    const int dir_y = segment.to_y.to_int() - segment.from_y.to_int();
    const int step_x = dir_x == 0 ? 0 : (dir_x > 0 ? 1 : -1);
    const int step_y = dir_y == 0 ? 0 : (dir_y > 0 ? 1 : -1);

    const std::array<core::GridPos, 2> side_offsets{{
        {-step_y, step_x},
        {step_y, -step_x},
    }};

    core::GridPos resume_goal{-1, -1};
    if (registry.any_of<components::MovePath>(entity)) {
        const auto& path = registry.get<components::MovePath>(entity);
        if (!path.cells.empty()) {
            resume_goal = path.cells.back();
        }
    }

    for (const core::GridPos offset : side_offsets) {
        if (offset.x == 0 && offset.y == 0) {
            continue;
        }

        const core::GridPos neighbor{current.x + offset.x, current.y + offset.y};
        if (!is_tile_walkable(map, neighbor, false)) {
            continue;
        }

        if (is_movement_blocked(registry, neighbor, entity)) {
            continue;
        }

        std::vector<core::GridPos> divert_path{neighbor};
        if (resume_goal.x >= 0) {
            divert_path.push_back(resume_goal);
        }

        registry.remove<components::MovePath>(entity);
        auto& path = registry.emplace<components::MovePath>(entity);
        path.cells = std::move(divert_path);
        path.next_index = 0;
        path.has_goal_world = false;
        return true;
    }

    return false;
}

void abort_blocked_move_segment(
    entt::registry& registry,
    const entt::entity entity,
    const components::MapGrid& map,
    const components::MoveSegment& segment)
{
    registry.remove<components::MoveSegment>(entity);
    detail::sync_grid_from_world(registry, entity);
    if (try_divert_around_block(registry, entity, map, segment)) {
        return;
    }

    registry.get_or_emplace<components::MoveCooldown>(entity).ticks_remaining =
        constants::MOVE_BLOCKED_REPATH_COOLDOWN_TICKS;
}

void pause_or_abort_radius_blocked_segment(
    entt::registry& registry,
    const entt::entity entity,
    components::MoveSegment& segment,
    const components::MapGrid& map)
{
    --segment.ticks_elapsed;
    ++segment.blocked_ticks;
    if (segment.blocked_ticks < constants::MOVE_SEGMENT_RADIUS_BLOCK_WAIT_TICKS) {
        return;
    }

    abort_blocked_move_segment(registry, entity, map, segment);
}

void advance_move_segment(
    entt::registry& registry,
    const entt::entity entity,
    const components::MapGrid& map,
    const int move_ticks_per_tile)
{
    auto& segment = registry.get<components::MoveSegment>(entity);
    if (segment.ticks_total <= 0) {
        registry.remove<components::MoveSegment>(entity);
        return;
    }

    ++segment.ticks_elapsed;
    if (segment.ticks_elapsed > segment.ticks_total) {
        segment.ticks_elapsed = segment.ticks_total;
    }

    const math::Fixed progress = math::Fixed::from_int(segment.ticks_elapsed)
        / math::Fixed::from_int(segment.ticks_total);
    auto& world = registry.get<components::WorldPosition>(entity);
    const math::Fixed proposed_x = math::fixed_lerp(segment.from_x, segment.to_x, progress);
    const math::Fixed proposed_y = math::fixed_lerp(segment.from_y, segment.to_y, progress);

    if (segment.ticks_elapsed < segment.ticks_total) {
        if (radius_block_should_yield(registry, entity, proposed_x, proposed_y)) {
            pause_or_abort_radius_blocked_segment(registry, entity, segment, map);
            return;
        }

        segment.blocked_ticks = 0;
        world.x = proposed_x;
        world.y = proposed_y;
        return;
    }

    if (radius_block_should_yield(registry, entity, segment.to_x, segment.to_y)) {
        pause_or_abort_radius_blocked_segment(registry, entity, segment, map);
        return;
    }

    world.x = segment.to_x;
    world.y = segment.to_y;
    detail::sync_grid_from_world(registry, entity);
    const core::GridPos reached_cell = move_segment_destination_cell(segment.to_x, segment.to_y);
    registry.remove<components::MoveSegment>(entity);

    if (registry.any_of<components::MovePath>(entity)) {
        auto& path = registry.get<components::MovePath>(entity);
        const core::GridPos path_target = path.cells[static_cast<std::size_t>(path.next_index)];
        if (reached_cell == path_target) {
            ++path.next_index;
            if (path.next_index >= static_cast<int>(path.cells.size())) {
                registry.remove<components::MovePath>(entity);
            }
        }
    }

    if (!try_begin_next_path_step(registry, entity, map, move_ticks_per_tile)) {
        return;
    }
}

void run_worker_system(entt::registry& registry, components::MapGrid& map, const components::ContentPack& content)
{
    const auto view = registry.view<
        components::WorkerUnitTag,
        components::GridPosition,
        components::WorkerBrain,
        components::CarriedWood,
        components::CarriedFood,
        components::CarriedMoney,
        components::DefinitionRef,
        components::Health>();

    const std::vector<entt::entity> workers =
        snapshot::sort_entities_by_snapshot_key(registry, std::vector<entt::entity>(view.begin(), view.end()));

    for (const entt::entity worker : workers) {
        const bool manual = registry.any_of<components::ManualControlTag>(worker);
        const bool has_gather_target = registry.any_of<components::GatherTarget>(worker);

        auto& brain = registry.get<components::WorkerBrain>(worker);
        auto& carried_wood = registry.get<components::CarriedWood>(worker);
        auto& carried_food = registry.get<components::CarriedFood>(worker);
        auto& carried_money = registry.get<components::CarriedMoney>(worker);
        const auto& definition_ref = registry.get<components::DefinitionRef>(worker);
        const auto* definition = find_unit_archetype_from_ref(content, definition_ref);
        if (definition == nullptr) {
            continue;
        }

        const auto& worker_pos = registry.get<components::GridPosition>(worker).cell;
        const std::uint8_t player_slot = components::entity_player_slot(registry, worker);
        const entt::entity town_center = find_town_center_for_player_slot(registry, player_slot);

        if (town_center == entt::null) {
            continue;
        }

        const auto& depot_anchor = registry.get<components::GridPosition>(town_center);
        const components::BuildingFootprint depot_footprint =
            registry.any_of<components::BuildingFootprint>(town_center)
            ? registry.get<components::BuildingFootprint>(town_center)
            : components::BuildingFootprint{};
        const core::GridPos depot_pos = depot_anchor.cell;
        const bool should_auto_deposit = !manual || has_gather_target;
        const int carried_total =
            carried_wood.amount + carried_food.amount + carried_money.amount;

        if (carried_total >= definition->carry_capacity && should_auto_deposit) {
            brain.state = components::WorkerState::MovingToDeposit;
            const core::GridPos stand_tile =
                find_adjacent_walkable(map, registry, depot_pos, worker);
            if (components::chebyshev_distance_to_footprint(worker_pos, depot_anchor, depot_footprint) <= 1) {
                brain.state = components::WorkerState::Depositing;
            }
            else if (!registry.any_of<components::MovePath>(worker)) {
                assign_path(registry, worker, map, stand_tile);
            }
        }
        else if (manual && !has_gather_target) {
            continue;
        }
        else if (carried_total < definition->carry_capacity) {
            core::GridPos resource{-1, -1};
            components::TileType preferred_type = components::TileType::Grass;
            if (has_gather_target) {
                const auto& gather_target = registry.get<components::GatherTarget>(worker);
                resource = gather_target.cell;
                preferred_type = gather_target.resource_type;
                if (preferred_type == components::TileType::Grass
                    && core::is_inside_grid(resource, map.width, map.height)) {
                    preferred_type =
                        map.tiles[static_cast<std::size_t>(core::grid_index(resource, map.width))];
                }
                if (!resource_tile_has_remaining(map, resource)) {
                    if (!reassign_worker_resource_target(
                            registry,
                            worker,
                            map,
                            worker_pos,
                            preferred_type)) {
                        continue;
                    }
                    resource = registry.get<components::GatherTarget>(worker).cell;
                }
            }
            else {
                resource = find_nearest_resource_tile(map, worker_pos);
            }

            if (resource.x < 0) {
                continue;
            }

            if (worker_pos == resource || core::chebyshev_distance(worker_pos, resource) == 1) {
                brain.state = components::WorkerState::Gathering;
            }
            else {
                brain.state = components::WorkerState::MovingToResource;
                if (!registry.any_of<components::MovePath>(worker)) {
                    const core::GridPos stand_tile = find_adjacent_walkable(map, registry, resource, worker);
                    assign_path(registry, worker, map, stand_tile);
                }
            }
        }

        if (brain.state == components::WorkerState::Gathering && carried_total < definition->carry_capacity) {
            auto& gather_cooldown = registry.get_or_emplace<components::GatherCooldown>(worker);
            if (gather_cooldown.ticks_remaining > 0) {
                gather_cooldown.ticks_remaining -= 1;
            }
            else {
                core::GridPos preferred_cell{-1, -1};
                if (registry.any_of<components::GatherTarget>(worker)) {
                    preferred_cell = registry.get<components::GatherTarget>(worker).cell;
                }

                bool gathered = false;
                auto try_gather_cell = [&](const core::GridPos cell) {
                    if (gathered || core::chebyshev_distance(worker_pos, cell) != 1) {
                        return;
                    }
                    if (!resource_tile_has_remaining(map, cell)) {
                        return;
                    }

                    const int index = core::grid_index(cell, map.width);
                    auto& wood = map.forest_wood[static_cast<std::size_t>(index)];
                    auto& food = map.bush_food[static_cast<std::size_t>(index)];
                    auto& money = map.mine_money[static_cast<std::size_t>(index)];
                    const components::TileType tile = map.tiles[static_cast<std::size_t>(index)];
                    const bool gather_wood = tile == components::TileType::Forest && wood > 0;
                    const bool gather_food = is_bush_tile(tile) && food > 0;
                    const bool gather_money = tile == components::TileType::GoldMine && money > 0;
                    if (!gather_wood && !gather_food && !gather_money) {
                        return;
                    }

                    const int free_capacity = definition->carry_capacity
                        - carried_wood.amount - carried_food.amount - carried_money.amount;
                    const int gather_amount = std::min(definition->gather_per_tick, free_capacity);
                    if (gather_amount <= 0) {
                        gathered = true;
                        return;
                    }

                    const components::TileType gathered_type = tile;
                    if (gather_wood) {
                        carried_food.amount = 0;
                        carried_money.amount = 0;
                        wood -= gather_amount;
                        carried_wood.amount += gather_amount;
                        if (wood <= 0) {
                            deplete_forest_tile(map, cell);
                        }
                    }
                    else if (gather_food) {
                        carried_wood.amount = 0;
                        carried_money.amount = 0;
                        food -= gather_amount;
                        carried_food.amount += gather_amount;
                        if (food <= 0) {
                            deplete_bush_tile(map, cell);
                        }
                    }
                    else {
                        carried_wood.amount = 0;
                        carried_food.amount = 0;
                        money -= gather_amount;
                        carried_money.amount += gather_amount;
                        if (money <= 0) {
                            deplete_gold_mine_tile(map, cell);
                        }
                    }

                    gather_cooldown.ticks_remaining = std::max(1, definition->gather_interval_ticks);
                    gathered = true;

                    const bool depleted =
                        (gather_wood && wood <= 0)
                        || (gather_food && food <= 0)
                        || (gather_money && money <= 0);
                    if (depleted && preferred_cell == cell) {
                        (void)reassign_worker_resource_target(
                            registry,
                            worker,
                            map,
                            worker_pos,
                            gathered_type);
                    }

                    if (carried_wood.amount + carried_food.amount + carried_money.amount
                        >= definition->carry_capacity) {
                        registry.remove<components::MovePath>(worker);
                    }
                };

                if (preferred_cell.x >= 0) {
                    try_gather_cell(preferred_cell);
                }
                else {
                    for (int y = 0; y < map.height && !gathered; ++y) {
                        for (int x = 0; x < map.width && !gathered; ++x) {
                            try_gather_cell({x, y});
                        }
                    }
                }
            }
        }

        if (brain.state == components::WorkerState::Depositing) {
            if (components::chebyshev_distance_to_footprint(worker_pos, depot_anchor, depot_footprint) > 1) {
                brain.state = components::WorkerState::MovingToDeposit;
                continue;
            }

            registry.remove<components::MovePath>(worker);
            brain.state = components::WorkerState::Idle;
        }
    }
}

void run_attack_chase_system(entt::registry& registry, const components::MapGrid& map)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null || !registry.any_of<components::FogOfWarState>(world)) {
        return;
    }

    const auto& fog = registry.get<components::FogOfWarState>(world);

    const auto view = registry.view<
        components::UnitTag,
        components::GridPosition,
        components::Health,
        components::AttackOrder,
        components::PlayerOwnedTag>();

    std::vector<entt::entity> attackers(view.begin(), view.end());
    attackers = snapshot::sort_entities_by_snapshot_key(registry, std::move(attackers));

    for (const entt::entity attacker : attackers) {
        const auto& attacker_health = registry.get<components::Health>(attacker);
        if (attacker_health.current.raw() <= 0) {
            continue;
        }

        auto& attack_order = registry.get<components::AttackOrder>(attacker);
        const entt::entity target = attack_order.target;
        if (target == entt::null || !registry.valid(target)
            || !registry.all_of<components::Health, components::GridPosition>(target)) {
            registry.remove<components::AttackOrder>(attacker);
            continue;
        }

        const auto& target_health = registry.get<components::Health>(target);
        if (target_health.current.raw() <= 0) {
            registry.remove<components::AttackOrder>(attacker);
            continue;
        }

        const std::uint8_t attacker_slot = components::entity_player_slot(registry, attacker);
        const core::GridPos target_pos = registry.get<components::GridPosition>(target).cell;
        const bool target_visible =
            is_opponent_entity_visible_to_slot(registry, fog, target, attacker_slot);

        core::GridPos chase_cell = target_pos;
        if (target_visible) {
            attack_order.last_known_cell = target_pos;
        }
        else if (attack_order.last_known_cell.x >= 0 && attack_order.last_known_cell.y >= 0) {
            chase_cell = attack_order.last_known_cell;
        }
        else {
            registry.remove<components::AttackOrder>(attacker);
            continue;
        }

        if (unit_in_melee_range(registry, attacker, target)) {
            registry.remove<components::MovePath>(attacker);
            registry.remove<components::MoveSegment>(attacker);
            continue;
        }

        if (unit_grid_adjacent(registry, attacker, target)) {
            registry.remove<components::MovePath>(attacker);
            registry.remove<components::MoveSegment>(attacker);
            continue;
        }

        const core::GridPos stand_tile =
            find_best_melee_stand_tile(map, registry, chase_cell, attacker, target);
        if (stand_tile == chase_cell) {
            continue;
        }

        if (registry.any_of<components::MoveSegment>(attacker)) {
            continue;
        }

        if (registry.any_of<components::MovePath>(attacker)) {
            const auto& path = registry.get<components::MovePath>(attacker);
            if (!path.cells.empty()) {
                const core::GridPos path_goal = path.cells.back();
                if (path_goal == stand_tile) {
                    const core::GridPos current = unit_movement_grid_cell(registry, attacker);
                    const bool path_is_direct = attack_path_follows_direct_line(
                        current,
                        stand_tile,
                        path.cells,
                        path.next_index);
                    if (path_is_direct && !is_next_path_step_blocked(registry, map, attacker)) {
                        continue;
                    }
                }
            }
        }

        assign_path(registry, attacker, map, stand_tile, entt::null, true, true);
    }
}

void run_enemy_militia_ai(entt::registry& registry)
{
    const auto view = registry.view<components::MilitiaUnitTag, components::EnemyTag, components::GridPosition>();
    const std::vector<entt::entity> militia_units =
        snapshot::sort_entities_by_snapshot_key(registry, std::vector<entt::entity>(view.begin(), view.end()));

    for (const entt::entity militia : militia_units) {
        entt::entity target = entt::null;
        int best_distance = std::numeric_limits<int>::max();
        const core::GridPos militia_pos = view.get<components::GridPosition>(militia).cell;

        const auto player_view =
            registry.view<components::PlayerOwnedTag, components::GridPosition, components::Health>();
        for (const entt::entity player_entity : player_view) {
            if (!registry.any_of<components::UnitTag>(player_entity)) {
                continue;
            }

            const auto& health = player_view.get<components::Health>(player_entity);
            if (health.current.raw() <= 0) {
                continue;
            }

            const core::GridPos player_pos = player_view.get<components::GridPosition>(player_entity).cell;
            const int distance = core::chebyshev_distance(militia_pos, player_pos);
            if (distance < best_distance) {
                best_distance = distance;
                target = player_entity;
            }
        }

        if (target == entt::null) {
            continue;
        }

        registry.get_or_emplace<components::AttackOrder>(militia).target = target;
        if (target != entt::null && registry.any_of<components::GridPosition>(target)) {
            registry.get<components::AttackOrder>(militia).last_known_cell =
                registry.get<components::GridPosition>(target).cell;
        }
    }
}

void run_movement_system(
    entt::registry& registry,
    const components::MapGrid& map,
    const components::ContentPack& content)
{
    const auto view = registry.view<
        components::UnitTag,
        components::WorldPosition,
        components::GridPosition,
        components::DefinitionRef>();

    const std::vector<entt::entity> units =
        snapshot::sort_entities_by_snapshot_key(registry, std::vector<entt::entity>(view.begin(), view.end()));

    for (const entt::entity entity : units) {
        const auto& definition_ref = registry.get<components::DefinitionRef>(entity);
        const auto* definition = find_unit_archetype_from_ref(content, definition_ref);
        if (definition == nullptr) {
            continue;
        }

        if (registry.any_of<components::MoveSegment>(entity)) {
            advance_move_segment(registry, entity, map, definition->move_ticks_per_tile);
            continue;
        }

        if (registry.any_of<components::MoveCooldown>(entity)) {
            auto& cooldown = registry.get<components::MoveCooldown>(entity);
            if (cooldown.ticks_remaining > 0) {
                --cooldown.ticks_remaining;
                continue;
            }
        }

        replan_path_if_blocked(registry, entity, map, definition->move_ticks_per_tile);
        try_begin_next_path_step(registry, entity, map, definition->move_ticks_per_tile);
    }
}

void run_worker_deposit_system(entt::registry& registry)
{
    const auto town_center_view =
        registry.view<components::TownCenterTag, components::PlayerOwnedTag, components::GridPosition, components::Stockpile>();
    if (town_center_view.begin() == town_center_view.end()) {
        return;
    }

    const auto worker_view = registry.view<
        components::WorkerUnitTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::CarriedWood,
        components::CarriedFood,
        components::CarriedMoney,
        components::Health>();

    const std::vector<entt::entity> workers = snapshot::sort_entities_by_snapshot_key(
        registry,
        std::vector<entt::entity>(worker_view.begin(), worker_view.end()));

    for (const entt::entity worker : workers) {
        const auto& health = registry.get<components::Health>(worker);
        if (health.current.raw() <= 0) {
            continue;
        }

        auto& carried_wood = registry.get<components::CarriedWood>(worker);
        auto& carried_food = registry.get<components::CarriedFood>(worker);
        auto& carried_money = registry.get<components::CarriedMoney>(worker);
        if (carried_wood.amount <= 0 && carried_food.amount <= 0 && carried_money.amount <= 0) {
            continue;
        }

        const std::uint8_t player_slot = components::entity_player_slot(registry, worker);
        const entt::entity town_center = find_town_center_for_player_slot(registry, player_slot);
        if (town_center == entt::null) {
            continue;
        }

        const auto& depot_anchor = registry.get<components::GridPosition>(town_center);
        const components::BuildingFootprint depot_footprint =
            registry.any_of<components::BuildingFootprint>(town_center)
            ? registry.get<components::BuildingFootprint>(town_center)
            : components::BuildingFootprint{};
        const core::GridPos worker_pos = registry.get<components::GridPosition>(worker).cell;
        if (components::chebyshev_distance_to_footprint(worker_pos, depot_anchor, depot_footprint) > 1) {
            continue;
        }

        auto& stockpile = registry.get<components::Stockpile>(town_center);
        stockpile.wood += carried_wood.amount;
        stockpile.food += carried_food.amount;
        stockpile.money += carried_money.amount;
        carried_wood.amount = 0;
        carried_food.amount = 0;
        carried_money.amount = 0;
        registry.remove<components::MovePath>(worker);
        registry.remove<components::MoveSegment>(worker);

        if (registry.any_of<components::WorkerBrain>(worker)) {
            registry.get<components::WorkerBrain>(worker).state = components::WorkerState::Idle;
        }
    }
}

void run_melee_contact_system(entt::registry& registry)
{
    const auto view = registry.view<
        components::UnitTag,
        components::WorldPosition,
        components::GridPosition,
        components::AttackOrder,
        components::Health>();

    std::vector<entt::entity> units(view.begin(), view.end());
    units = snapshot::sort_entities_by_snapshot_key(registry, std::move(units));

    const math::Fixed max_contact_distance = math::Fixed::from_float(
        constants::MELEE_CONTACT_CENTER_DISTANCE_TILES);
    const math::Fixed max_contact_distance_sq = max_contact_distance * max_contact_distance;

    for (const entt::entity unit : units) {
        if (view.get<components::Health>(unit).current.raw() <= 0) {
            continue;
        }

        const entt::entity target = view.get<components::AttackOrder>(unit).target;
        if (target == entt::null || !registry.valid(target) || !registry.any_of<components::WorldPosition>(target)) {
            continue;
        }

        if (!registry.any_of<components::Health>(target)
            || registry.get<components::Health>(target).current.raw() <= 0) {
            continue;
        }

        if (!unit_grid_adjacent(registry, unit, target)) {
            continue;
        }

        auto& world = view.get<components::WorldPosition>(unit);
        const auto& target_world = registry.get<components::WorldPosition>(target);

        const math::Fixed delta_x = target_world.x - world.x;
        const math::Fixed delta_y = target_world.y - world.y;
        const math::Fixed distance_sq = delta_x * delta_x + delta_y * delta_y;
        if (distance_sq.raw() <= max_contact_distance_sq.raw()) {
            continue;
        }

        const float distance = std::sqrt(distance_sq.to_float());
        if (distance <= 0.0001F) {
            continue;
        }

        if (distance <= constants::MELEE_STRIKE_MAX_CENTER_DISTANCE_TILES) {
            const float contact = constants::MELEE_CONTACT_CENTER_DISTANCE_TILES;
            const float nx = delta_x.to_float() / distance;
            const float nz = delta_y.to_float() / distance;
            world.x = target_world.x - math::Fixed::from_float(nx * contact);
            world.y = target_world.y - math::Fixed::from_float(nz * contact);
            continue;
        }

        const float excess = distance - constants::MELEE_CONTACT_CENTER_DISTANCE_TILES;
        if (excess <= 0.0F) {
            continue;
        }

        bool target_attacks_back = false;
        if (registry.any_of<components::AttackOrder>(target)) {
            const entt::entity target_attack = registry.get<components::AttackOrder>(target).target;
            target_attacks_back = target_attack == unit;
        }

        const float share = target_attacks_back ? 0.5F : 1.0F;
        const float close_by = std::min(constants::MELEE_CONTACT_SLIDE_PER_TICK, excess * share);
        if (close_by <= 0.0F) {
            continue;
        }

        const float step_x = delta_x.to_float() / distance * close_by;
        const float step_z = delta_y.to_float() / distance * close_by;
        world.x = world.x + math::Fixed::from_float(step_x);
        world.y = world.y + math::Fixed::from_float(step_z);
    }
}

void run_combat_system(entt::registry& registry, const components::ContentPack& content)
{
    const auto view = registry.view<
        components::UnitTag,
        components::GridPosition,
        components::DefinitionRef,
        components::AttackOrder,
        components::AttackCooldown,
        components::Health>();

    std::vector<entt::entity> attackers(view.begin(), view.end());
    attackers = snapshot::sort_entities_by_snapshot_key(registry, std::move(attackers));

    for (const entt::entity attacker : attackers) {
        auto& cooldown = registry.get<components::AttackCooldown>(attacker);
        if (cooldown.ticks_remaining > 0) {
            --cooldown.ticks_remaining;
            continue;
        }

        const entt::entity target = registry.get<components::AttackOrder>(attacker).target;
        if (target == entt::null || !registry.valid(target) || !registry.any_of<components::Health>(target)) {
            continue;
        }

        auto& target_health = registry.get<components::Health>(target);
        if (target_health.current.raw() <= 0) {
            continue;
        }

        if (!unit_in_melee_range(registry, attacker, target)) {
            continue;
        }

        const auto* definition = find_unit_archetype_from_ref(
            content,
            registry.get<components::DefinitionRef>(attacker));
        if (definition == nullptr || definition->melee_attack <= 0) {
            continue;
        }

        target_health.current = target_health.current - math::Fixed::from_int(definition->melee_attack);
        cooldown.ticks_remaining = definition->attack_cooldown_ticks;

        if (registry.any_of<components::MilitiaUnitTag>(attacker)) {
            components::push_sfx_event(registry, components::SfxEventKind::MilitiaMeleeHit);
        }
        else if (registry.any_of<components::WorkerUnitTag>(attacker)) {
            components::push_sfx_event(registry, components::SfxEventKind::WorkerMeleeHit);
        }
    }
}

void run_death_cleanup(entt::registry& registry)
{
    std::vector<entt::entity> to_destroy{};

    const auto unit_view = registry.view<components::UnitTag, components::Health>();
    for (const entt::entity entity : unit_view) {
        if (unit_view.get<components::Health>(entity).current.raw() <= 0) {
            components::push_sfx_event(registry, components::SfxEventKind::UnitDeath);
            to_destroy.push_back(entity);
        }
    }

    const auto building_view = registry.view<components::BuildingTag, components::Health>();
    for (const entt::entity entity : building_view) {
        if (building_view.get<components::Health>(entity).current.raw() <= 0) {
            to_destroy.push_back(entity);
        }
    }

    for (const entt::entity entity : to_destroy) {
        registry.destroy(entity);
    }
}

void run_town_center_mana_generation(entt::registry& registry)
{
    const auto view = registry.view<
        components::TownCenterTag,
        components::PlayerOwnedTag,
        components::Stockpile,
        components::ManaGenerationCooldown,
        components::Health>();

    std::vector<entt::entity> town_centers(view.begin(), view.end());
    town_centers = snapshot::sort_entities_by_snapshot_key(registry, std::move(town_centers));

    for (const entt::entity entity : town_centers) {
        if (view.get<components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        if (registry.any_of<components::UnderConstructionTag>(entity)) {
            continue;
        }

        auto& cooldown = view.get<components::ManaGenerationCooldown>(entity);
        if (cooldown.ticks_remaining > 0) {
            --cooldown.ticks_remaining;
        }

        if (cooldown.ticks_remaining > 0) {
            continue;
        }

        const std::uint8_t player_slot = components::entity_player_slot(registry, entity);
        player::add_player_mana(registry, player_slot, constants::TOWN_CENTER_MANA_GEN_AMOUNT);
        cooldown.ticks_remaining = constants::TOWN_CENTER_MANA_GEN_INTERVAL_TICKS;
    }
}

void run_builder_system(entt::registry& registry, const components::MapGrid& map)
{
    const auto view = registry.view<
        components::WorkerUnitTag,
        components::BuildOrder,
        components::Health,
        components::PlayerOwnedTag>();

    std::vector<entt::entity> builders(view.begin(), view.end());
    builders = snapshot::sort_entities_by_snapshot_key(registry, std::move(builders));

    for (const entt::entity worker : builders) {
        if (!registry.any_of<components::BuildOrder>(worker)) {
            continue;
        }

        if (view.get<components::Health>(worker).current.raw() <= 0) {
            registry.remove<components::BuildOrder>(worker);
            continue;
        }

        auto& build_order = registry.get<components::BuildOrder>(worker);
        const entt::entity building = build_order.building;
        if (building == entt::null || !registry.valid(building)
            || !registry.any_of<components::UnderConstructionTag>(building)
            || !registry.any_of<components::Health>(building)) {
            registry.remove<components::BuildOrder>(worker);
            continue;
        }

        auto& building_health = registry.get<components::Health>(building);
        if (building_health.current.raw() <= 0) {
            registry.remove<components::BuildOrder>(worker);
            continue;
        }

        if (!unit_grid_adjacent(registry, worker, building)) {
            if (!registry.any_of<components::MovePath>(worker)
                && !registry.any_of<components::MoveSegment>(worker)) {
                const core::GridPos stand_tile = find_best_melee_stand_tile(
                    map,
                    registry,
                    registry.get<components::GridPosition>(building).cell,
                    worker,
                    building);
                assign_path(registry, worker, map, stand_tile, building, true);
            }
            continue;
        }

        if (build_order.hit_cooldown_ticks > 0) {
            --build_order.hit_cooldown_ticks;
            continue;
        }

        building_health.current =
            building_health.current + math::Fixed::from_int(constants::WORKER_BUILD_HP_PER_HIT);
        if (building_health.current > building_health.max) {
            building_health.current = building_health.max;
        }
        build_order.hit_cooldown_ticks = constants::WORKER_BUILD_HIT_INTERVAL_TICKS;

        if (building_health.current < building_health.max) {
            continue;
        }

        registry.remove<components::UnderConstructionTag>(building);
        if (!registry.any_of<components::ManaGenerationCooldown>(building)
            && registry.any_of<components::TownCenterTag>(building)) {
            registry.emplace<components::ManaGenerationCooldown>(
                building,
                components::ManaGenerationCooldown{constants::TOWN_CENTER_MANA_GEN_INTERVAL_TICKS});
        }

        const auto builders_on_site = registry.view<components::BuildOrder>();
        std::vector<entt::entity> to_clear{};
        for (const entt::entity other : builders_on_site) {
            if (builders_on_site.get<components::BuildOrder>(other).building == building) {
                to_clear.push_back(other);
            }
        }

        for (const entt::entity other : to_clear) {
            if (registry.any_of<components::BuildOrder>(other)) {
                registry.remove<components::BuildOrder>(other);
            }
        }
    }
}

} // namespace

void run_gameplay_systems(entt::registry& registry)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return;
    }

    auto& map = registry.get<components::MapGrid>(world);
    const auto& content = registry.get<components::ContentPack>(world);

    run_visibility_system(registry);
    run_worker_system(registry, map, content);
    run_worker_deposit_system(registry);
    run_builder_system(registry, map);
    run_town_center_mana_generation(registry);
    run_enemy_militia_ai(registry);
    run_attack_chase_system(registry, map);
    run_movement_system(registry, map, content);
    run_melee_contact_system(registry);
    run_combat_system(registry, content);
    run_death_cleanup(registry);
}

void snapshot_world_positions_for_render(entt::registry& registry)
{
    snapshot_previous_world_positions(registry);
}

void compute_state_hash(entt::registry& registry)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return;
    }

    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](const std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };

    const auto& map = registry.get<components::MapGrid>(world);
    mix(static_cast<std::uint64_t>(map.width));
    mix(static_cast<std::uint64_t>(map.height));
    for (const components::TileType tile : map.tiles) {
        mix(static_cast<std::uint64_t>(tile));
    }
    for (const int wood : map.forest_wood) {
        mix(static_cast<std::uint64_t>(wood));
    }
    for (const int food : map.bush_food) {
        mix(static_cast<std::uint64_t>(food));
    }
    for (const int money : map.mine_money) {
        mix(static_cast<std::uint64_t>(money));
    }

    if (registry.any_of<components::FogOfWarState>(world)) {
        const auto& fog = registry.get<components::FogOfWarState>(world);
        mix(static_cast<std::uint64_t>(fog.width));
        mix(static_cast<std::uint64_t>(fog.height));
        for (std::size_t index = 0U; index < fog.explored.size(); ++index) {
            mix(static_cast<std::uint64_t>(fog.explored[index]));
            if (fog.explored[index] != 0U && index < fog.memory_tiles.size()) {
                mix(static_cast<std::uint64_t>(fog.memory_tiles[index]));
            }
            if (fog.explored[index] != 0U && index < fog.memory_forest_wood.size()) {
                mix(static_cast<std::uint64_t>(fog.memory_forest_wood[index]));
            }
            if (fog.explored[index] != 0U && index < fog.memory_bush_food.size()) {
                mix(static_cast<std::uint64_t>(fog.memory_bush_food[index]));
            }
            if (fog.explored[index] != 0U && index < fog.memory_mine_money.size()) {
                mix(static_cast<std::uint64_t>(fog.memory_mine_money[index]));
            }
        }
    }

    std::vector<entt::entity> entities{};
    for (const entt::entity entity : registry.view<components::GridPosition>()) {
        if (registry.all_of<components::WorldTag>(entity)) {
            continue;
        }

        entities.push_back(entity);
    }

    std::sort(entities.begin(), entities.end(), [&registry](const entt::entity left, const entt::entity right) {
        const std::optional<snapshot::EntitySnapshotKey> left_key =
            snapshot::compute_entity_snapshot_key(registry, left);
        const std::optional<snapshot::EntitySnapshotKey> right_key =
            snapshot::compute_entity_snapshot_key(registry, right);

        if (left_key.has_value() && right_key.has_value()) {
            return snapshot::compare_entity_snapshot_keys(*left_key, *right_key);
        }

        if (left_key.has_value() != right_key.has_value()) {
            return left_key.has_value();
        }

        const core::GridPos left_cell = registry.get<components::GridPosition>(left).cell;
        const core::GridPos right_cell = registry.get<components::GridPosition>(right).cell;
        if (left_cell.x != right_cell.x) {
            return left_cell.x < right_cell.x;
        }

        return left_cell.y < right_cell.y;
    });

    for (const entt::entity entity : entities) {
        const std::optional<snapshot::EntitySnapshotKey> entity_key =
            snapshot::compute_entity_snapshot_key(registry, entity);
        if (!entity_key.has_value()) {
            continue;
        }

        snapshot::mix_entity_snapshot_key(hash, *entity_key);

        if (registry.any_of<components::GridPosition>(entity)) {
            const auto& pos = registry.get<components::GridPosition>(entity).cell;
            mix(static_cast<std::uint64_t>(pos.x));
            mix(static_cast<std::uint64_t>(pos.y));
        }

        if (registry.any_of<components::WorldPosition>(entity)) {
            const auto& world_position = registry.get<components::WorldPosition>(entity);
            mix(static_cast<std::uint64_t>(world_position.x.raw()));
            mix(static_cast<std::uint64_t>(world_position.y.raw()));
        }

        if (registry.any_of<components::Health>(entity)) {
            const auto& health = registry.get<components::Health>(entity);
            mix(static_cast<std::uint64_t>(health.current.raw()));
            mix(static_cast<std::uint64_t>(health.max.raw()));
        }

        if (registry.any_of<components::CarriedWood>(entity)) {
            mix(static_cast<std::uint64_t>(registry.get<components::CarriedWood>(entity).amount));
        }

        if (registry.any_of<components::CarriedFood>(entity)) {
            mix(static_cast<std::uint64_t>(registry.get<components::CarriedFood>(entity).amount));
        }

        if (registry.any_of<components::CarriedMoney>(entity)) {
            mix(static_cast<std::uint64_t>(registry.get<components::CarriedMoney>(entity).amount));
        }

        if (registry.any_of<components::Stockpile>(entity)) {
            const auto& stockpile = registry.get<components::Stockpile>(entity);
            mix(static_cast<std::uint64_t>(stockpile.wood));
            mix(static_cast<std::uint64_t>(stockpile.food));
            mix(static_cast<std::uint64_t>(stockpile.money));
            mix(static_cast<std::uint64_t>(stockpile.mana));
        }

        if (registry.any_of<components::ManaGenerationCooldown>(entity)) {
            mix(static_cast<std::uint64_t>(
                registry.get<components::ManaGenerationCooldown>(entity).ticks_remaining));
        }

        if (registry.any_of<components::BuildOrder>(entity)) {
            const auto& build_order = registry.get<components::BuildOrder>(entity);
            const entt::entity building = build_order.building;
            if (building != entt::null && registry.valid(building)) {
                const std::optional<snapshot::EntitySnapshotKey> building_key =
                    snapshot::compute_entity_snapshot_key(registry, building);
                if (building_key.has_value()) {
                    snapshot::mix_entity_snapshot_key(hash, *building_key);
                }
            }

            mix(static_cast<std::uint64_t>(build_order.hit_cooldown_ticks));
        }

        if (registry.any_of<components::AttackOrder>(entity)) {
            const auto& attack_order = registry.get<components::AttackOrder>(entity);
            const entt::entity target = attack_order.target;
            if (target != entt::null && registry.valid(target)) {
                const std::optional<snapshot::EntitySnapshotKey> target_key =
                    snapshot::compute_entity_snapshot_key(registry, target);
                if (target_key.has_value()) {
                    snapshot::mix_entity_snapshot_key(hash, *target_key);
                }
            }

            if (attack_order.last_known_cell.x >= 0 && attack_order.last_known_cell.y >= 0) {
                mix(static_cast<std::uint64_t>(attack_order.last_known_cell.x));
                mix(static_cast<std::uint64_t>(attack_order.last_known_cell.y));
            }
        }

        if (registry.any_of<components::AttackCooldown>(entity)) {
            mix(static_cast<std::uint64_t>(
                registry.get<components::AttackCooldown>(entity).ticks_remaining));
        }

        if (registry.any_of<components::GatherCooldown>(entity)) {
            mix(static_cast<std::uint64_t>(
                registry.get<components::GatherCooldown>(entity).ticks_remaining));
        }

        if (registry.any_of<components::GatherTarget>(entity)) {
            const auto& gather_target = registry.get<components::GatherTarget>(entity);
            mix(static_cast<std::uint64_t>(gather_target.cell.x));
            mix(static_cast<std::uint64_t>(gather_target.cell.y));
        }

        if (registry.any_of<components::WorkerBrain>(entity)) {
            mix(static_cast<std::uint64_t>(
                static_cast<std::uint8_t>(registry.get<components::WorkerBrain>(entity).state)));
        }

        if (registry.any_of<components::ManualControlTag>(entity)) {
            mix(1U);
        }

        if (registry.any_of<components::PlayerSlot>(entity)) {
            mix(static_cast<std::uint64_t>(registry.get<components::PlayerSlot>(entity).value));
        }

        if (registry.any_of<components::MovePath>(entity)) {
            const auto& path = registry.get<components::MovePath>(entity);
            mix(static_cast<std::uint64_t>(path.next_index));
            mix(static_cast<std::uint64_t>(path.cells.size()));
            for (const core::GridPos cell : path.cells) {
                mix(static_cast<std::uint64_t>(cell.x));
                mix(static_cast<std::uint64_t>(cell.y));
            }
            mix(path.has_goal_world ? 1U : 0U);
            if (path.has_goal_world) {
                mix(static_cast<std::uint64_t>(path.goal_world_x.raw()));
                mix(static_cast<std::uint64_t>(path.goal_world_y.raw()));
            }
        }

        if (registry.any_of<components::MoveSegment>(entity)) {
            const auto& segment = registry.get<components::MoveSegment>(entity);
            mix(static_cast<std::uint64_t>(segment.from_x.raw()));
            mix(static_cast<std::uint64_t>(segment.from_y.raw()));
            mix(static_cast<std::uint64_t>(segment.to_x.raw()));
            mix(static_cast<std::uint64_t>(segment.to_y.raw()));
            mix(static_cast<std::uint64_t>(segment.ticks_elapsed));
            mix(static_cast<std::uint64_t>(segment.ticks_total));
            mix(static_cast<std::uint64_t>(segment.blocked_ticks));
        }
    }

    registry.get<components::SimState>(world).state_hash = hash;
}

} // namespace aoa::sim::systems
