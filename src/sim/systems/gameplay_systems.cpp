#include "sim/systems/gameplay_systems.hpp"
#include "sim/systems/match_outcome.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/building_process.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/content_pack.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/fog_of_war.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/match_announcements.hpp"
#include "sim/components/match_session.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/fog_of_war.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/sfx_events.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/player/player_commands.hpp"
#include "sim/player/player_economy.hpp"
#include "sim/spawn/building_unit_spawn.hpp"
#include "sim/spawn/unit_spawn.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"
#include "sim/systems/pathfinding.hpp"
#include "sim/systems/visibility_system.hpp"

#include "math/fixed.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
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
    const bool /*allow_knight_steps*/,
    const bool has_goal_world,
    const math::Fixed goal_world_x,
    const math::Fixed goal_world_y,
    const bool /*use_attack_pathfinding*/)
{
    detail::cancel_move_segment_for_repath(registry, entity);
    detail::sync_grid_from_world(registry, entity);

    const core::GridPos start = unit_movement_grid_cell(registry, entity);
    auto& path = registry.get_or_emplace<components::MovePath>(entity);
    path.cells = find_path(map, start, goal, registry, entity, false, also_ignore);
    path.next_index = 0;
    path.has_goal_world = has_goal_world;
    if (path.cells.empty() && start != goal) {
        registry.get_or_emplace<components::MoveCooldown>(entity).ticks_remaining =
            constants::MOVE_BLOCKED_REPATH_COOLDOWN_TICKS;
    }
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

template <typename DropOffTag>
entt::entity find_nearest_tagged_drop_off(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const core::GridPos worker_pos)
{
    entt::entity best = entt::null;
    int best_distance = std::numeric_limits<int>::max();
    const auto dropoff_view = registry.view<
        DropOffTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::Health>();
    for (const entt::entity candidate : dropoff_view) {
        if (components::entity_player_slot(registry, candidate) != player_slot) {
            continue;
        }

        if (registry.any_of<components::UnderConstructionTag>(candidate)) {
            continue;
        }

        if (dropoff_view.get<components::Health>(candidate).current.raw() <= 0) {
            continue;
        }

        const auto& anchor = dropoff_view.get<components::GridPosition>(candidate);
        const components::BuildingFootprint footprint =
            registry.any_of<components::BuildingFootprint>(candidate)
            ? registry.get<components::BuildingFootprint>(candidate)
            : components::BuildingFootprint{};
        const int distance =
            components::chebyshev_distance_to_footprint(worker_pos, anchor, footprint);
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    }

    return best;
}

entt::entity find_nearest_wood_drop_off(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const core::GridPos worker_pos)
{
    return find_nearest_tagged_drop_off<components::WoodDropOffTag>(registry, player_slot, worker_pos);
}

void deplete_forest_tile(components::MapGrid& map, const core::GridPos cell)
{
    if (!core::is_inside_grid(cell, map.width, map.height)) {
        return;
    }

    const int index = core::grid_index(cell, map.width);
    map.forest_wood[static_cast<std::size_t>(index)] = 0;
    map.layer_hash_valid = false;
}

void deplete_bush_tile(components::MapGrid& map, const core::GridPos cell)
{
    if (!core::is_inside_grid(cell, map.width, map.height)) {
        return;
    }

    const int index = core::grid_index(cell, map.width);
    map.bush_food[static_cast<std::size_t>(index)] = 0;
    map.tiles[static_cast<std::size_t>(index)] = components::TileType::Grass;
    map.layer_hash_valid = false;
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
    map.layer_hash_valid = false;
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

[[nodiscard]] entt::entity gatherable_farm_at_cell(
    entt::registry& registry,
    const core::GridPos cell)
{
    const entt::entity farm = spawn::find_farm_at_cell(registry, cell);
    if (farm == entt::null
        || registry.any_of<components::UnderConstructionTag>(farm)
        || !registry.any_of<components::FarmFood>(farm)
        || registry.get<components::FarmFood>(farm).remaining <= 0) {
        return entt::null;
    }

    return farm;
}

[[nodiscard]] bool gather_cell_has_remaining(
    entt::registry& registry,
    const components::MapGrid& map,
    const core::GridPos cell)
{
    if (gatherable_farm_at_cell(registry, cell) != entt::null) {
        return true;
    }

    return resource_tile_has_remaining(map, cell);
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

[[nodiscard]] int job_vision_range_for_entity(const entt::registry& registry, const entt::entity entity)
{
    if (registry.any_of<components::WorkerUnitTag>(entity)) {
        return constants::DEFAULT_WORKER_VISION_RANGE;
    }

    if (registry.any_of<components::TownCenterTag>(entity)) {
        return constants::DEFAULT_TOWN_CENTER_VISION_RANGE;
    }

    if (registry.any_of<components::UnitTag>(entity)) {
        return constants::DEFAULT_UNIT_VISION_RANGE;
    }

    return constants::DEFAULT_STRUCTURE_VISION_RANGE;
}

[[nodiscard]] bool cell_in_job_retarget_range(
    entt::registry& registry,
    const entt::entity worker,
    const core::GridPos cell)
{
    if (!registry.any_of<components::GridPosition>(worker)) {
        return false;
    }

    const core::GridPos worker_pos = registry.get<components::GridPosition>(worker).cell;
    const int worker_vision = job_vision_range_for_entity(registry, worker);
    if (core::chebyshev_distance(worker_pos, cell) <= worker_vision) {
        return true;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, worker);
    entt::entity nearest_friendly = entt::null;
    int nearest_distance = std::numeric_limits<int>::max();
    const auto view = registry.view<components::PlayerOwnedTag, components::GridPosition, components::Health>();
    for (const entt::entity friendly : view) {
        if (friendly == worker) {
            continue;
        }

        if (components::entity_player_slot(registry, friendly) != player_slot) {
            continue;
        }

        if (view.get<components::Health>(friendly).current.raw() <= 0) {
            continue;
        }

        const core::GridPos friendly_pos = view.get<components::GridPosition>(friendly).cell;
        const int distance = core::chebyshev_distance(worker_pos, friendly_pos);
        if (distance > worker_vision || distance >= nearest_distance) {
            continue;
        }

        nearest_distance = distance;
        nearest_friendly = friendly;
    }

    if (nearest_friendly == entt::null) {
        return false;
    }

    const int friendly_vision = job_vision_range_for_entity(registry, nearest_friendly);
    const int half_vision = std::max(1, friendly_vision / constants::JOB_RETARGET_VISION_HALF_DIVISOR);
    const core::GridPos friendly_pos = registry.get<components::GridPosition>(nearest_friendly).cell;
    return core::chebyshev_distance(cell, friendly_pos) <= half_vision;
}

core::GridPos find_nearest_resource_tile(
    entt::registry& registry,
    const components::MapGrid& map,
    const core::GridPos from,
    const components::TileType preferred_type,
    const entt::entity worker)
{
    core::GridPos best{-1, -1};
    int best_distance = std::numeric_limits<int>::max();
    const bool filter_type = preferred_type != components::TileType::Grass;

    const auto consider = [&](const core::GridPos cell) {
        if (!core::is_inside_grid(cell, map.width, map.height)) {
            return;
        }

        if (filter_type) {
            if (!resource_tile_matches_type(map, cell, preferred_type)) {
                return;
            }
        }
        else if (!resource_tile_has_remaining(map, cell)) {
            return;
        }

        const int distance = std::abs(from.x - cell.x) + std::abs(from.y - cell.y);
        if (distance < best_distance) {
            best_distance = distance;
            best = cell;
        }
    };

    const auto consider_disk = [&](const core::GridPos origin, const int radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) > radius) {
                    continue;
                }

                consider({origin.x + dx, origin.y + dy});
            }
        }
    };

    if (worker == entt::null || !registry.any_of<components::GridPosition>(worker)) {
        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                consider({x, y});
            }
        }
        return best;
    }

    const core::GridPos worker_pos = registry.get<components::GridPosition>(worker).cell;
    const int worker_vision = job_vision_range_for_entity(registry, worker);
    consider_disk(worker_pos, worker_vision);

    const std::uint8_t player_slot = components::entity_player_slot(registry, worker);
    entt::entity nearest_friendly = entt::null;
    int nearest_distance = std::numeric_limits<int>::max();
    const auto friendly_view =
        registry.view<components::PlayerOwnedTag, components::GridPosition, components::Health>();
    for (const entt::entity friendly : friendly_view) {
        if (friendly == worker) {
            continue;
        }

        if (components::entity_player_slot(registry, friendly) != player_slot) {
            continue;
        }

        if (friendly_view.get<components::Health>(friendly).current.raw() <= 0) {
            continue;
        }

        const core::GridPos friendly_pos = friendly_view.get<components::GridPosition>(friendly).cell;
        const int distance = core::chebyshev_distance(worker_pos, friendly_pos);
        if (distance > worker_vision || distance >= nearest_distance) {
            continue;
        }

        nearest_distance = distance;
        nearest_friendly = friendly;
    }

    if (nearest_friendly != entt::null) {
        const int friendly_vision = job_vision_range_for_entity(registry, nearest_friendly);
        const int half_vision =
            std::max(1, friendly_vision / constants::JOB_RETARGET_VISION_HALF_DIVISOR);
        consider_disk(registry.get<components::GridPosition>(nearest_friendly).cell, half_vision);
    }

    return best;
}

core::GridPos find_nearest_forest_with_wood(
    entt::registry& registry,
    const components::MapGrid& map,
    const core::GridPos from,
    const entt::entity worker)
{
    return find_nearest_resource_tile(
        registry, map, from, components::TileType::Forest, worker);
}

core::GridPos find_adjacent_walkable(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos target,
    const entt::entity ignore)
{
    return find_best_approach_stand_tile(map, registry, target, ignore);
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

void assign_gather_stand_path(
    entt::registry& registry,
    const entt::entity worker,
    const components::MapGrid& map,
    const core::GridPos stand_tile,
    const core::GridPos resource_cell)
{
    math::Fixed goal_x{};
    math::Fixed goal_y{};
    work_stand_world_goal_for_cell(stand_tile, resource_cell, goal_x, goal_y);
    assign_unit_path(
        registry,
        worker,
        map,
        stand_tile,
        entt::null,
        true,
        true,
        goal_x,
        goal_y,
        false);
}

void assign_deposit_stand_path(
    entt::registry& registry,
    const entt::entity worker,
    const components::MapGrid& map,
    const core::GridPos stand_tile,
    const components::GridPosition& depot_anchor,
    const components::BuildingFootprint& depot_footprint)
{
    math::Fixed goal_x{};
    math::Fixed goal_y{};
    work_stand_world_goal_for_building(
        stand_tile, depot_anchor, depot_footprint, goal_x, goal_y);
    assign_unit_path(
        registry,
        worker,
        map,
        stand_tile,
        entt::null,
        true,
        true,
        goal_x,
        goal_y,
        false);
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
    if (path.next_index < 0 || path.next_index >= static_cast<int>(path.cells.size())) {
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
        find_nearest_resource_tile(registry, map, worker_pos, preferred_type, worker);
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
    assign_gather_stand_path(registry, worker, map, stand_tile, next_resource);
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
    if (path.next_index < 0 || path.next_index >= static_cast<int>(path.cells.size())) {
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
    if (is_world_position_solid_blocked(
            registry, map, components::WorldPosition{to_x, to_y}, entity, false)) {
        return false;
    }

    math::Fixed from_x = to_x;
    math::Fixed from_y = to_y;
    if (registry.any_of<components::WorldPosition>(entity)) {
        const auto& world = registry.get<components::WorldPosition>(entity);
        from_x = world.x;
        from_y = world.y;
    }
    if (is_world_segment_movement_blocked(
            registry, map, from_x, from_y, to_x, to_y, entity, false)) {
        const math::Fixed center_x = math::tile_center_coord(current.x);
        const math::Fixed center_y = math::tile_center_coord(current.y);
        const math::Fixed recenter_threshold = math::Fixed::from_float(
            constants::MOVE_PATH_STEP_REACHED_TILE_DISTANCE);
        const math::Fixed recenter_threshold_sq = recenter_threshold * recenter_threshold;
        const math::Fixed delta_x = from_x - center_x;
        const math::Fixed delta_y = from_y - center_y;
        const bool off_center =
            (delta_x * delta_x + delta_y * delta_y) > recenter_threshold_sq;
        if (off_center
            && !is_world_segment_movement_blocked(
                registry, map, from_x, from_y, center_x, center_y, entity, false)
            && !is_world_position_solid_blocked(
                registry, map, components::WorldPosition{center_x, center_y}, entity, false)) {
            begin_move_segment(registry, entity, center_x, center_y, move_ticks_per_tile);
            return true;
        }

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
    return is_unit_radius_blocked_at_world(registry, point_x, point_y, mover);
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

        if (is_step_movement_blocked(registry, map, current, neighbor, entity, false)) {
            continue;
        }

        registry.remove<components::MovePath>(entity);
        auto& path = registry.emplace<components::MovePath>(entity);
        path.cells = {neighbor};
        if (resume_goal.x >= 0 && resume_goal != neighbor) {
            std::vector<core::GridPos> rest =
                find_path(map, neighbor, resume_goal, registry, entity, false, entt::null);
            path.cells.insert(path.cells.end(), rest.begin(), rest.end());
        }
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
        if (is_world_position_solid_blocked(
                registry,
                map,
                components::WorldPosition{proposed_x, proposed_y},
                entity,
                false)
            || radius_block_should_yield(registry, entity, proposed_x, proposed_y)) {
            pause_or_abort_radius_blocked_segment(registry, entity, segment, map);
            return;
        }

        segment.blocked_ticks = 0;
        world.x = proposed_x;
        world.y = proposed_y;
        return;
    }

    if (is_world_position_solid_blocked(
            registry,
            map,
            components::WorldPosition{segment.to_x, segment.to_y},
            entity,
            false)
        || radius_block_should_yield(registry, entity, segment.to_x, segment.to_y)) {
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
        if (path.next_index < 0 || path.next_index >= static_cast<int>(path.cells.size())) {
            registry.remove<components::MovePath>(entity);
            return;
        }

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
        const bool should_auto_deposit = !manual || has_gather_target;
        const int carried_total =
            carried_wood.amount + carried_food.amount + carried_money.amount;

        if (carried_total >= definition->carry_capacity && should_auto_deposit) {
            const entt::entity depot = find_deposit_building(
                registry,
                player_slot,
                worker_pos,
                carried_wood.amount,
                carried_food.amount,
                carried_money.amount);
            if (depot == entt::null) {
                continue;
            }

            const auto& depot_anchor = registry.get<components::GridPosition>(depot);
            const components::BuildingFootprint depot_footprint =
                registry.any_of<components::BuildingFootprint>(depot)
                ? registry.get<components::BuildingFootprint>(depot)
                : components::BuildingFootprint{};
            brain.state = components::WorkerState::MovingToDeposit;
            if (unit_can_work_building(
                    registry, worker, depot_anchor, depot_footprint)) {
                brain.state = components::WorkerState::Depositing;
            }
            else {
                const core::GridPos stand_tile =
                    find_best_deposit_stand_tile(map, registry, depot, worker);
                bool needs_path = !registry.any_of<components::MovePath>(worker);
                if (!needs_path) {
                    const auto& path = registry.get<components::MovePath>(worker);
                    needs_path = path.cells.empty() || path.cells.back() != stand_tile;
                }
                if (needs_path
                    && (!registry.any_of<components::MoveCooldown>(worker)
                        || registry.get<components::MoveCooldown>(worker).ticks_remaining <= 0)) {
                    assign_deposit_stand_path(
                        registry, worker, map, stand_tile, depot_anchor, depot_footprint);
                }
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
                if (!gather_cell_has_remaining(registry, map, resource)) {
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
                resource = find_nearest_resource_tile(
                    registry, map, worker_pos, components::TileType::Grass, worker);
            }

            if (resource.x < 0) {
                continue;
            }

            bool moving_to_stand = registry.any_of<components::MoveSegment>(worker);
            if (!moving_to_stand && registry.any_of<components::MovePath>(worker)) {
                const auto& path = registry.get<components::MovePath>(worker);
                moving_to_stand = path.next_index < static_cast<int>(path.cells.size());
            }

            if (unit_can_work_cell(registry, worker, resource) && !moving_to_stand) {
                brain.state = components::WorkerState::Gathering;
            }
            else if (!unit_can_work_cell(registry, worker, resource)) {
                brain.state = components::WorkerState::MovingToResource;
                const entt::entity farm = gatherable_farm_at_cell(registry, resource);
                const core::GridPos stand_tile = farm != entt::null
                    ? find_best_farm_stand_tile(map, registry, farm, worker)
                    : find_adjacent_walkable(map, registry, resource, worker);
                if (stand_tile.x < 0) {
                    continue;
                }

                bool needs_path = !registry.any_of<components::MovePath>(worker);
                if (!needs_path) {
                    const auto& path = registry.get<components::MovePath>(worker);
                    needs_path = path.cells.empty() || path.cells.back() != stand_tile;
                }
                if (needs_path
                    && (!registry.any_of<components::MoveCooldown>(worker)
                        || registry.get<components::MoveCooldown>(worker).ticks_remaining <= 0)) {
                    assign_gather_stand_path(registry, worker, map, stand_tile, resource);
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
                    if (gathered || !unit_can_work_cell(registry, worker, cell)) {
                        return;
                    }

                    const entt::entity farm = spawn::find_farm_at_cell(registry, cell);
                    if (farm != entt::null
                        && !registry.any_of<components::UnderConstructionTag>(farm)
                        && registry.any_of<components::FarmFood>(farm)
                        && registry.get<components::FarmFood>(farm).remaining > 0) {
                        auto& farm_food = registry.get<components::FarmFood>(farm);
                        const int free_capacity = definition->carry_capacity
                            - carried_wood.amount - carried_food.amount - carried_money.amount;
                        const int gather_amount = std::min(definition->gather_per_tick, free_capacity);
                        if (gather_amount <= 0) {
                            gathered = true;
                            return;
                        }

                        carried_wood.amount = 0;
                        carried_money.amount = 0;
                        const int taken = std::min(gather_amount, farm_food.remaining);
                        farm_food.remaining -= taken;
                        carried_food.amount += taken;
                        gather_cooldown.ticks_remaining = std::max(1, definition->gather_interval_ticks);
                        gathered = true;
                        if (farm_food.remaining <= 0 && preferred_cell == cell) {
                            (void)reassign_worker_resource_target(
                                registry,
                                worker,
                                map,
                                worker_pos,
                                components::TileType::Grass);
                        }
                        if (carried_wood.amount + carried_food.amount + carried_money.amount
                            >= definition->carry_capacity) {
                            registry.remove<components::MovePath>(worker);
                        }
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
                    map.layer_hash_valid = false;
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

                if (gathered
                    && carried_wood.amount + carried_food.amount + carried_money.amount
                        < definition->carry_capacity
                    && !registry.any_of<components::MoveSegment>(worker)) {
                    bool moving = false;
                    if (registry.any_of<components::MovePath>(worker)) {
                        const auto& path = registry.get<components::MovePath>(worker);
                        moving = path.next_index < static_cast<int>(path.cells.size());
                    }

                    const core::GridPos occupancy = unit_occupancy_grid_cell(registry, worker);
                    const entt::entity farm = gatherable_farm_at_cell(registry, occupancy);
                    if (!moving && farm != entt::null) {
                        const auto& farm_food = registry.get<components::FarmFood>(farm);
                        const int taken_total = farm_food.max - farm_food.remaining;
                        if (taken_total > 0
                            && taken_total % constants::FARM_GATHER_WANDER_EVERY_FOOD == 0) {
                            const std::uint32_t seed =
                                static_cast<std::uint32_t>(farm_food.remaining)
                                ^ (static_cast<std::uint32_t>(entt::to_integral(worker))
                                    * 1664525U);
                            const core::GridPos next = pick_farm_wander_cell(
                                map, registry, farm, worker, occupancy, seed);
                            if (next.x >= 0
                                && (next.x != occupancy.x || next.y != occupancy.y)) {
                                auto& gather_target =
                                    registry.get_or_emplace<components::GatherTarget>(worker);
                                gather_target.cell = next;
                                brain.state = components::WorkerState::MovingToResource;
                                assign_gather_stand_path(registry, worker, map, next, next);
                            }
                        }
                    }
                }
            }
        }

        if (brain.state == components::WorkerState::Depositing) {
            const entt::entity depot = find_deposit_building(
                registry,
                player_slot,
                worker_pos,
                carried_wood.amount,
                carried_food.amount,
                carried_money.amount);
            if (depot == entt::null) {
                brain.state = components::WorkerState::Idle;
                continue;
            }

            const auto& depositing_depot_anchor = registry.get<components::GridPosition>(depot);
            const components::BuildingFootprint depositing_depot_footprint =
                registry.any_of<components::BuildingFootprint>(depot)
                ? registry.get<components::BuildingFootprint>(depot)
                : components::BuildingFootprint{};
            if (!unit_can_work_building(
                    registry,
                    worker,
                    depositing_depot_anchor,
                    depositing_depot_footprint)) {
                brain.state = components::WorkerState::MovingToDeposit;
                continue;
            }

            registry.remove<components::MovePath>(worker);
            brain.state = components::WorkerState::Idle;
        }
    }
}

[[nodiscard]] bool is_attack_strike_locked(const entt::registry& registry, const entt::entity entity)
{
    if (!registry.any_of<components::AttackCooldown>(entity)) {
        return false;
    }

    return registry.get<components::AttackCooldown>(entity).ticks_remaining > 0;
}

void clear_unit_movement(entt::registry& registry, const entt::entity entity)
{
    if (registry.any_of<components::MoveSegment>(entity)) {
        registry.remove<components::MoveSegment>(entity);
    }
    if (registry.any_of<components::MovePath>(entity)) {
        registry.remove<components::MovePath>(entity);
    }
    detail::sync_grid_from_world(registry, entity);
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

        if (is_attack_strike_locked(registry, attacker)) {
            clear_unit_movement(registry, attacker);
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

        bool needs_repath = !registry.any_of<components::MovePath>(attacker);
        if (!needs_repath) {
            const auto& path = registry.get<components::MovePath>(attacker);
            needs_repath = path.cells.empty() || path.cells.back() != stand_tile;
            if (!needs_repath && is_next_path_step_blocked(registry, map, attacker)) {
                needs_repath = true;
            }
        }

        if (!needs_repath) {
            continue;
        }

        // Repath cancels any in-flight MoveSegment so a re-spotted target is chased immediately.
        assign_path(registry, attacker, map, stand_tile);
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
        if (registry.any_of<components::GarrisonedTag>(entity)) {
            continue;
        }

        const auto& definition_ref = registry.get<components::DefinitionRef>(entity);
        const auto* definition = find_unit_archetype_from_ref(content, definition_ref);
        if (definition == nullptr) {
            continue;
        }

        if (is_attack_strike_locked(registry, entity)) {
            clear_unit_movement(registry, entity);
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

        if (registry.any_of<components::WorkerBrain>(worker)) {
            const components::WorkerState state =
                registry.get<components::WorkerBrain>(worker).state;
            if (state == components::WorkerState::Gathering
                || state == components::WorkerState::MovingToResource) {
                continue;
            }
        }

        const std::uint8_t player_slot = components::entity_player_slot(registry, worker);
        const core::GridPos worker_pos = registry.get<components::GridPosition>(worker).cell;
        const entt::entity depot = find_deposit_building(
            registry,
            player_slot,
            worker_pos,
            carried_wood.amount,
            carried_food.amount,
            carried_money.amount);
        if (depot == entt::null) {
            continue;
        }

        const auto& depot_anchor = registry.get<components::GridPosition>(depot);
        const components::BuildingFootprint depot_footprint =
            registry.any_of<components::BuildingFootprint>(depot)
            ? registry.get<components::BuildingFootprint>(depot)
            : components::BuildingFootprint{};
        if (!unit_can_work_building(registry, worker, depot_anchor, depot_footprint)) {
            continue;
        }

        int deposited_wood = 0;
        int deposited_food = 0;
        int deposited_money = 0;
        if (registry.any_of<components::TownCenterTag>(depot)
            || registry.any_of<components::WoodDropOffTag>(depot)
            || registry.any_of<components::FoodDropOffTag>(depot)
            || registry.any_of<components::MoneyDropOffTag>(depot)) {
            deposited_wood = carried_wood.amount;
            deposited_food = carried_food.amount;
            deposited_money = carried_money.amount;
            player::add_player_wood(registry, player_slot, deposited_wood);
            player::add_player_food(registry, player_slot, deposited_food);
            player::add_player_money(registry, player_slot, deposited_money);
            carried_wood.amount = 0;
            carried_food.amount = 0;
            carried_money.amount = 0;
        }
        else {
            continue;
        }

        note_resources_collected(
            registry,
            player_slot,
            deposited_wood,
            deposited_food,
            deposited_money);
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

        if (registry.any_of<components::GarrisonedTag>(unit)) {
            continue;
        }

        if (is_attack_strike_locked(registry, unit)) {
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
        if (registry.any_of<components::GarrisonedTag>(attacker)) {
            continue;
        }

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

        const auto* definition = find_unit_archetype_from_ref(
            content,
            registry.get<components::DefinitionRef>(attacker));
        if (definition == nullptr) {
            continue;
        }

        if (definition->pierce_attack > 0 && definition->attack_range > 0) {
            const auto& attacker_pos = registry.get<components::GridPosition>(attacker);
            const core::GridPos target_cell = entity_visibility_cell(registry, target);
            const int range = std::max(
                std::abs(attacker_pos.cell.x - target_cell.x),
                std::abs(attacker_pos.cell.y - target_cell.y));
            if (range > definition->attack_range) {
                continue;
            }

            const std::uint8_t owner_slot = components::entity_player_slot(registry, attacker);
            if (definition->attack_mana_cost > 0
                && !player::try_deduct_player_mana(
                    registry, owner_slot, definition->attack_mana_cost)) {
                continue;
            }

            const auto& attacker_world = registry.get<components::WorldPosition>(attacker);
            (void)spawn::spawn_rock_projectile(
                registry,
                attacker_world.x,
                attacker_world.y,
                target,
                owner_slot,
                definition->pierce_attack);
            cooldown.ticks_remaining = definition->attack_cooldown_ticks;
            clear_unit_movement(registry, attacker);
            continue;
        }

        if (!unit_in_melee_range(registry, attacker, target)) {
            continue;
        }

        if (definition->melee_attack <= 0) {
            continue;
        }

        target_health.current = target_health.current - math::Fixed::from_int(definition->melee_attack);
        if (registry.any_of<components::PlayerOwnedTag>(target)) {
            components::note_player_attacked(
                registry,
                components::entity_player_slot(registry, target),
                components::entity_player_slot(registry, attacker));
        }
        cooldown.ticks_remaining = definition->attack_cooldown_ticks;
        clear_unit_movement(registry, attacker);
        if (target_health.current.raw() <= 0) {
            note_entity_killed(registry, target, attacker);
        }

        const core::GridPos impact_cell = entity_visibility_cell(registry, target);
        if (registry.any_of<components::MilitiaUnitTag>(attacker)) {
            components::push_sfx_event(
                registry,
                components::SfxEventKind::MilitiaMeleeHit,
                impact_cell);
        }
        else if (registry.any_of<components::WorkerUnitTag>(attacker)) {
            components::push_sfx_event(
                registry,
                components::SfxEventKind::WorkerMeleeHit,
                impact_cell);
        }
    }
}

void run_death_cleanup(entt::registry& registry)
{
    std::vector<entt::entity> to_destroy{};

    const auto unit_view = registry.view<components::UnitTag, components::Health>();
    for (const entt::entity entity : unit_view) {
        if (unit_view.get<components::Health>(entity).current.raw() <= 0) {
            components::push_sfx_event(
                registry,
                components::SfxEventKind::UnitDeath,
                entity_visibility_cell(registry, entity));
            to_destroy.push_back(entity);
        }
    }

    std::vector<std::uint8_t> mana_cap_slots{};
    const auto building_view = registry.view<components::BuildingTag, components::Health>();
    for (const entt::entity entity : building_view) {
        if (building_view.get<components::Health>(entity).current.raw() > 0) {
            continue;
        }

        if (registry.any_of<components::ExtractorTag>(entity)
            || registry.any_of<components::ReservoirTag>(entity)) {
            mana_cap_slots.push_back(components::entity_player_slot(registry, entity));
        }

        player::eject_garrisoned_units(registry, entity);
        to_destroy.push_back(entity);
    }

    for (const entt::entity entity : to_destroy) {
        registry.destroy(entity);
    }

    for (const std::uint8_t player_slot : mana_cap_slots) {
        player::clamp_player_mana_to_cap(registry, player_slot);
    }
}

void run_extractor_mana_generation(entt::registry& registry)
{
    const auto view = registry.view<
        components::ExtractorTag,
        components::PlayerOwnedTag,
        components::ManaGenerationCooldown,
        components::Health>();

    std::vector<entt::entity> extractors(view.begin(), view.end());
    extractors = snapshot::sort_entities_by_snapshot_key(registry, std::move(extractors));

    for (const entt::entity entity : extractors) {
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
        player::add_player_mana(registry, player_slot, constants::EXTRACTOR_MANA_GEN_AMOUNT);
        cooldown.ticks_remaining = constants::EXTRACTOR_MANA_GEN_INTERVAL_TICKS;
    }
}

void run_garden_production(entt::registry& registry)
{
    const auto view = registry.view<
        components::GardenTag,
        components::PlayerOwnedTag,
        components::ManaGenerationCooldown,
        components::Health>();

    std::vector<entt::entity> gardens(view.begin(), view.end());
    gardens = snapshot::sort_entities_by_snapshot_key(registry, std::move(gardens));

    for (const entt::entity entity : gardens) {
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
        player::add_player_wood(registry, player_slot, constants::GARDEN_PROD_WOOD);
        player::add_player_food(registry, player_slot, constants::GARDEN_PROD_FOOD);
        cooldown.ticks_remaining = constants::GARDEN_PROD_INTERVAL_TICKS;
    }
}

[[nodiscard]] bool unit_has_active_move(const entt::registry& registry, const entt::entity entity)
{
    if (registry.any_of<components::MoveSegment>(entity)) {
        return true;
    }

    if (!registry.any_of<components::MovePath>(entity)) {
        return false;
    }

    const auto& path = registry.get<components::MovePath>(entity);
    return path.next_index < static_cast<int>(path.cells.size());
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
            if (!unit_has_active_move(registry, worker)) {
                const core::GridPos stand_tile = find_best_melee_stand_tile(
                    map,
                    registry,
                    registry.get<components::GridPosition>(building).cell,
                    worker,
                    building);
                if (stand_tile.x < 0) {
                    registry.remove<components::BuildOrder>(worker);
                    continue;
                }

                assign_path(registry, worker, map, stand_tile, building, true);
            }
            continue;
        }

        if (build_order.hit_cooldown_ticks > 0) {
            --build_order.hit_cooldown_ticks;
            continue;
        }

        const bool first_construction_hit =
            building_health.current == math::Fixed::from_int(1);
        if (first_construction_hit) {
            components::BuildingFootprint start_footprint{};
            if (registry.any_of<components::BuildingFootprint>(building)) {
                start_footprint = registry.get<components::BuildingFootprint>(building);
            }
            start_footprint = components::effective_building_footprint(
                start_footprint,
                registry.any_of<components::TownCenterTag>(building));
            const auto& start_anchor = registry.get<components::GridPosition>(building);
            bool units_on_shadow = false;
            const auto blocking_units =
                registry.view<components::UnitTag, components::Health, components::GridPosition>();
            for (const entt::entity occupant : blocking_units) {
                if (blocking_units.get<components::Health>(occupant).current.raw() <= 0) {
                    continue;
                }

                if (registry.any_of<components::BuildOrder>(occupant)
                    && registry.get<components::BuildOrder>(occupant).building == building) {
                    continue;
                }

                if (components::building_contains_cell(
                        start_anchor,
                        start_footprint,
                        blocking_units.get<components::GridPosition>(occupant).cell)) {
                    units_on_shadow = true;
                    break;
                }
            }

            if (units_on_shadow) {
                build_order.hit_cooldown_ticks = constants::WORKER_BUILD_HIT_INTERVAL_TICKS;
                continue;
            }
        }

        building_health.current =
            building_health.current + math::Fixed::from_int(constants::WORKER_BUILD_HP_PER_HIT);
        if (building_health.current > building_health.max) {
            building_health.current = building_health.max;
        }
        build_order.hit_cooldown_ticks = constants::WORKER_BUILD_HIT_INTERVAL_TICKS;

        if (first_construction_hit) {
            const auto& winner_anchor = registry.get<components::GridPosition>(building);
            components::BuildingFootprint winner_footprint{};
            if (registry.any_of<components::BuildingFootprint>(building)) {
                winner_footprint = registry.get<components::BuildingFootprint>(building);
            }
            winner_footprint = components::effective_building_footprint(
                winner_footprint,
                registry.any_of<components::TownCenterTag>(building));
            const std::uint8_t winner_slot = components::entity_player_slot(registry, building);
            std::vector<entt::entity> to_refund{};
            const auto other_view = registry.view<
                components::BuildingTag,
                components::GridPosition,
                components::Health,
                components::UnderConstructionTag>();
            for (const entt::entity other : other_view) {
                if (other == building) {
                    continue;
                }

                if (!is_unstarted_construction(registry, other)) {
                    continue;
                }

                if (components::entity_player_slot(registry, other) == winner_slot) {
                    continue;
                }

                components::BuildingFootprint other_footprint{};
                if (registry.any_of<components::BuildingFootprint>(other)) {
                    other_footprint = registry.get<components::BuildingFootprint>(other);
                }
                other_footprint = components::effective_building_footprint(
                    other_footprint,
                    registry.any_of<components::TownCenterTag>(other));
                const auto& other_anchor = other_view.get<components::GridPosition>(other);
                bool overlaps = false;
                for (int y = 0; y < winner_footprint.height && !overlaps; ++y) {
                    for (int x = 0; x < winner_footprint.width; ++x) {
                        if (components::building_contains_cell(
                                other_anchor,
                                other_footprint,
                                {winner_anchor.cell.x + x, winner_anchor.cell.y + y})) {
                            overlaps = true;
                            break;
                        }
                    }
                }

                if (overlaps) {
                    to_refund.push_back(other);
                }
            }

            for (const entt::entity other : to_refund) {
                player::issue_destroy_building_order(registry, other);
            }
        }

        if (building_health.current < building_health.max) {
            continue;
        }

        registry.remove<components::UnderConstructionTag>(building);
        if (registry.any_of<components::MillTag>(building)) {
            const entt::entity world = find_world_entity(registry);
            if (world != entt::null && registry.any_of<components::MatchSession>(world)) {
                auto& session = registry.get<components::MatchSession>(world);
                const std::uint8_t owner_slot = components::entity_player_slot(registry, building);
                if (owner_slot < session.player_built_mill.size()) {
                    session.player_built_mill[owner_slot] = 1U;
                }
            }
        }
        if (!registry.any_of<components::ManaGenerationCooldown>(building)
            && registry.any_of<components::ExtractorTag>(building)) {
            registry.emplace<components::ManaGenerationCooldown>(
                building,
                components::ManaGenerationCooldown{constants::EXTRACTOR_MANA_GEN_INTERVAL_TICKS});
        }
        if (!registry.any_of<components::ManaGenerationCooldown>(building)
            && registry.any_of<components::GardenTag>(building)) {
            registry.emplace<components::ManaGenerationCooldown>(
                building,
                components::ManaGenerationCooldown{constants::GARDEN_PROD_INTERVAL_TICKS});
        }
        if (registry.any_of<components::FarmFood>(building)) {
            auto& farm_food = registry.get<components::FarmFood>(building);
            farm_food.max = constants::FARM_FOOD_AMOUNT;
            farm_food.remaining = constants::FARM_FOOD_AMOUNT;
        }

        const bool completed_farm = registry.any_of<components::FarmTag>(building);
        const entt::entity farm_gather_worker = completed_farm ? worker : entt::null;
        const core::GridPos farm_anchor = registry.any_of<components::GridPosition>(building)
            ? registry.get<components::GridPosition>(building).cell
            : core::GridPos{-1, -1};

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

            if (other == farm_gather_worker && farm_anchor.x >= 0) {
                player::issue_gather_order(registry, other, farm_anchor);
                continue;
            }

            registry.get_or_emplace<components::ManualControlTag>(other);
            registry.remove<components::GatherTarget>(other);
            if (registry.any_of<components::WorkerBrain>(other)) {
                registry.get<components::WorkerBrain>(other).state = components::WorkerState::Idle;
            }

            entt::entity next_building = entt::null;
            int best_distance = std::numeric_limits<int>::max();
            if (!registry.any_of<components::GridPosition>(other)) {
                registry.remove<components::MovePath>(other);
                registry.remove<components::MoveSegment>(other);
                continue;
            }

            const core::GridPos worker_pos = registry.get<components::GridPosition>(other).cell;
            const std::uint8_t player_slot = components::entity_player_slot(registry, other);
            const auto construction_view = registry.view<
                components::BuildingTag,
                components::UnderConstructionTag,
                components::GridPosition,
                components::Health,
                components::PlayerOwnedTag>();
            for (const entt::entity candidate : construction_view) {
                if (candidate == building) {
                    continue;
                }

                if (components::entity_player_slot(registry, candidate) != player_slot) {
                    continue;
                }

                if (construction_view.get<components::Health>(candidate).current.raw() <= 0) {
                    continue;
                }

                const core::GridPos site = construction_view.get<components::GridPosition>(candidate).cell;
                if (!cell_in_job_retarget_range(registry, other, site)) {
                    continue;
                }

                const int distance = core::chebyshev_distance(worker_pos, site);
                if (distance < best_distance) {
                    best_distance = distance;
                    next_building = candidate;
                }
            }

            if (next_building == entt::null) {
                registry.remove<components::MovePath>(other);
                registry.remove<components::MoveSegment>(other);
                continue;
            }

            const core::GridPos stand_tile = find_best_melee_stand_tile(
                map,
                registry,
                registry.get<components::GridPosition>(next_building).cell,
                other,
                next_building);
            if (stand_tile.x < 0) {
                registry.remove<components::MovePath>(other);
                registry.remove<components::MoveSegment>(other);
                continue;
            }

            registry.emplace<components::BuildOrder>(other, components::BuildOrder{next_building, 0});
            assign_path(registry, other, map, stand_tile, next_building, true);
        }
    }
}

void try_enter_garrison(entt::registry& registry, const entt::entity unit, const entt::entity building)
{
    if (!registry.valid(building) || !registry.any_of<components::GarrisonHold>(building)) {
        registry.remove<components::GarrisonOrder>(unit);
        return;
    }

    if (registry.any_of<components::UnderConstructionTag>(building)) {
        return;
    }

    auto& hold = registry.get<components::GarrisonHold>(building);
    if (static_cast<int>(hold.units.size()) >= static_cast<int>(hold.capacity)) {
        registry.remove<components::GarrisonOrder>(unit);
        return;
    }

    hold.units.push_back(unit);
    registry.emplace_or_replace<components::GarrisonedTag>(
        unit,
        components::GarrisonedTag{building});
    registry.remove<components::GarrisonOrder>(unit);
    registry.remove<components::AttackOrder>(unit);
    registry.remove<components::GatherTarget>(unit);
    registry.remove<components::BuildOrder>(unit);
    registry.remove<components::MovePath>(unit);
    registry.remove<components::MoveSegment>(unit);
    registry.remove<components::ManualControlTag>(unit);
}

void run_garrison_enter_system(entt::registry& registry)
{
    const auto view = registry.view<
        components::UnitTag,
        components::GarrisonOrder,
        components::GridPosition,
        components::Health>();
    std::vector<entt::entity> units(view.begin(), view.end());
    units = snapshot::sort_entities_by_snapshot_key(registry, std::move(units));
    for (const entt::entity unit : units) {
        if (view.get<components::Health>(unit).current.raw() <= 0) {
            continue;
        }

        if (registry.any_of<components::GarrisonedTag>(unit)) {
            continue;
        }

        const entt::entity building = view.get<components::GarrisonOrder>(unit).building;
        if (!registry.valid(building) || !registry.any_of<components::GridPosition>(building)) {
            registry.remove<components::GarrisonOrder>(unit);
            continue;
        }

        const auto& depot_anchor = registry.get<components::GridPosition>(building);
        const components::BuildingFootprint depot_footprint =
            registry.any_of<components::BuildingFootprint>(building)
            ? registry.get<components::BuildingFootprint>(building)
            : components::BuildingFootprint{};
        if (unit_can_work_building(registry, unit, depot_anchor, depot_footprint)) {
            try_enter_garrison(registry, unit, building);
        }
    }
}

[[nodiscard]] math::Fixed entity_world_x(const entt::registry& registry, const entt::entity entity)
{
    if (registry.any_of<components::WorldPosition>(entity)) {
        return registry.get<components::WorldPosition>(entity).x;
    }

    if (registry.any_of<components::GridPosition>(entity)) {
        const auto& pos = registry.get<components::GridPosition>(entity);
        const components::BuildingFootprint footprint =
            registry.any_of<components::BuildingFootprint>(entity)
            ? registry.get<components::BuildingFootprint>(entity)
            : components::BuildingFootprint{1, 1};
        return math::Fixed::from_int(pos.cell.x)
            + math::Fixed::from_int(footprint.width) / math::Fixed::from_int(2);
    }

    return {};
}

[[nodiscard]] math::Fixed entity_world_y(const entt::registry& registry, const entt::entity entity)
{
    if (registry.any_of<components::WorldPosition>(entity)) {
        return registry.get<components::WorldPosition>(entity).y;
    }

    if (registry.any_of<components::GridPosition>(entity)) {
        const auto& pos = registry.get<components::GridPosition>(entity);
        const components::BuildingFootprint footprint =
            registry.any_of<components::BuildingFootprint>(entity)
            ? registry.get<components::BuildingFootprint>(entity)
            : components::BuildingFootprint{1, 1};
        return math::Fixed::from_int(pos.cell.y)
            + math::Fixed::from_int(footprint.height) / math::Fixed::from_int(2);
    }

    return {};
}

[[nodiscard]] entt::entity find_nearest_ranged_target(
    entt::registry& registry,
    const entt::entity attacker,
    const components::GridPosition& origin,
    const components::BuildingFootprint& footprint,
    const int attack_range,
    const int blind_range)
{
    entt::entity best = entt::null;
    math::Fixed best_distance_sq = math::Fixed::from_int(attack_range + 1)
        * math::Fixed::from_int(attack_range + 1);
    const std::uint8_t owner_slot = components::entity_player_slot(registry, attacker);
    const auto target_view = registry.view<components::Health, components::GridPosition>();
    for (const entt::entity candidate : target_view) {
        if (candidate == attacker) {
            continue;
        }

        if (registry.any_of<components::GarrisonedTag>(candidate)
            || registry.any_of<components::Projectile>(candidate)) {
            continue;
        }

        if (!components::is_opponent_entity(registry, candidate, owner_slot)) {
            continue;
        }

        if (target_view.get<components::Health>(candidate).current.raw() <= 0) {
            continue;
        }

        const math::Fixed target_x = entity_world_x(registry, candidate);
        const math::Fixed target_y = entity_world_y(registry, candidate);
        if (!components::in_circle_attack_range(
                target_x, target_y, origin, footprint, attack_range, blind_range)) {
            continue;
        }

        const math::Fixed dist_sq = components::euclidean_distance_sq_to_footprint_center(
            target_x, target_y, origin, footprint);
        if (dist_sq < best_distance_sq) {
            best_distance_sq = dist_sq;
            best = candidate;
        }
    }

    return best;
}

void pulse_attack_reveal(
    entt::registry& registry,
    const entt::entity building,
    const std::uint8_t viewer_slot)
{
    if (viewer_slot >= static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)) {
        return;
    }

    components::MatchSession* session = match_session(registry);
    if (session == nullptr || !registry.any_of<components::GridPosition>(building)) {
        return;
    }

    const auto& anchor = registry.get<components::GridPosition>(building);
    const components::BuildingFootprint footprint =
        registry.any_of<components::BuildingFootprint>(building)
        ? registry.get<components::BuildingFootprint>(building)
        : components::BuildingFootprint{1, 1};
    for (auto& flare : session->attack_reveal_flares) {
        if (flare.viewer_slot == viewer_slot && flare.x == static_cast<std::int16_t>(anchor.cell.x)
            && flare.y == static_cast<std::int16_t>(anchor.cell.y)) {
            flare.ticks_remaining =
                static_cast<std::uint16_t>(constants::ATTACK_REVEAL_DURATION_TICKS);
            flare.width = static_cast<std::uint8_t>(std::max(1, footprint.width));
            flare.height = static_cast<std::uint8_t>(std::max(1, footprint.height));
            return;
        }
    }

    session->attack_reveal_flares.push_back(components::AttackRevealFlare{
        static_cast<std::int16_t>(anchor.cell.x),
        static_cast<std::int16_t>(anchor.cell.y),
        static_cast<std::uint8_t>(std::max(1, footprint.width)),
        static_cast<std::uint8_t>(std::max(1, footprint.height)),
        viewer_slot,
        static_cast<std::uint16_t>(constants::ATTACK_REVEAL_DURATION_TICKS),
    });
}

void fire_building_projectile(
    entt::registry& registry,
    const entt::entity building,
    const data::ArchetypeDefinition& definition,
    const bool is_arrow)
{
    if (definition.pierce_attack <= 0 || definition.attack_range <= 0) {
        return;
    }

    if (!registry.any_of<components::AttackCooldown>(building)) {
        registry.emplace<components::AttackCooldown>(building);
    }

    auto& cooldown = registry.get<components::AttackCooldown>(building);
    if (cooldown.ticks_remaining > 0) {
        --cooldown.ticks_remaining;
        return;
    }

    const auto& anchor = registry.get<components::GridPosition>(building);
    const components::BuildingFootprint footprint =
        registry.any_of<components::BuildingFootprint>(building)
        ? registry.get<components::BuildingFootprint>(building)
        : components::BuildingFootprint{};
    const int blind_range = is_arrow ? constants::TOWER_BLIND_RANGE_TILES : 0;
    const entt::entity target = find_nearest_ranged_target(
        registry,
        building,
        anchor,
        footprint,
        definition.attack_range,
        blind_range);
    if (target == entt::null) {
        return;
    }

    const math::Fixed spawn_x =
        math::Fixed::from_int(anchor.cell.x) + math::Fixed::from_int(footprint.width) / math::Fixed::from_int(2);
    const math::Fixed spawn_y =
        math::Fixed::from_int(anchor.cell.y) + math::Fixed::from_int(footprint.height) / math::Fixed::from_int(2);
    const std::uint8_t viewer_slot = components::entity_player_slot(registry, target);
    (void)spawn::spawn_rock_projectile(
        registry,
        spawn_x,
        spawn_y,
        target,
        components::entity_player_slot(registry, building),
        definition.pierce_attack,
        is_arrow,
        viewer_slot);
    pulse_attack_reveal(registry, building, viewer_slot);
    cooldown.ticks_remaining = definition.attack_cooldown_ticks > 0
        ? definition.attack_cooldown_ticks
        : constants::TOWN_CENTER_ATTACK_COOLDOWN_TICKS;
}

void run_building_autoattack_system(entt::registry& registry, const components::ContentPack& content)
{
    const auto view = registry.view<
        components::BuildingTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::DefinitionRef,
        components::Health>();
    std::vector<entt::entity> buildings(view.begin(), view.end());
    buildings = snapshot::sort_entities_by_snapshot_key(registry, std::move(buildings));
    for (const entt::entity building : buildings) {
        if (view.get<components::Health>(building).current.raw() <= 0) {
            continue;
        }

        if (registry.any_of<components::UnderConstructionTag>(building)) {
            continue;
        }

        const bool is_tower = registry.any_of<components::TowerTag>(building);
        const bool is_town_center = registry.any_of<components::TownCenterTag>(building);
        if (!is_tower && !is_town_center) {
            continue;
        }

        if (is_town_center) {
            const auto* hold = registry.try_get<components::GarrisonHold>(building);
            if (hold == nullptr || hold->units.empty()) {
                if (registry.any_of<components::AttackCooldown>(building)
                    && registry.get<components::AttackCooldown>(building).ticks_remaining > 0) {
                    --registry.get<components::AttackCooldown>(building).ticks_remaining;
                }
                continue;
            }
        }

        const auto* definition = data::find_structure_archetype(
            content.content,
            view.get<components::DefinitionRef>(building).id);
        if (definition == nullptr) {
            continue;
        }

        fire_building_projectile(registry, building, *definition, is_tower);
    }
}

void run_projectile_system(entt::registry& registry)
{
    const auto view = registry.view<
        components::Projectile,
        components::WorldPosition,
        components::GridPosition>();
    std::vector<entt::entity> projectiles(view.begin(), view.end());
    projectiles = snapshot::sort_entities_by_snapshot_key(registry, std::move(projectiles));
    std::vector<entt::entity> to_destroy{};
    const math::Fixed step = math::Fixed::from_float(constants::ROCK_PROJECTILE_TILES_PER_TICK);
    const math::Fixed hit_distance = math::Fixed::from_float(constants::ROCK_PROJECTILE_HIT_DISTANCE);
    for (const entt::entity projectile : projectiles) {
        auto& shot = view.get<components::Projectile>(projectile);
        if (shot.target == entt::null || !registry.valid(shot.target)
            || !registry.any_of<components::Health>(shot.target)
            || registry.get<components::Health>(shot.target).current.raw() <= 0) {
            to_destroy.push_back(projectile);
            continue;
        }

        math::Fixed target_x{};
        math::Fixed target_y{};
        if (registry.any_of<components::WorldPosition>(shot.target)) {
            const auto& target_world = registry.get<components::WorldPosition>(shot.target);
            target_x = target_world.x;
            target_y = target_world.y;
        }
        else if (registry.any_of<components::GridPosition>(shot.target)) {
            const auto& target_grid = registry.get<components::GridPosition>(shot.target);
            const components::BuildingFootprint target_footprint =
                registry.any_of<components::BuildingFootprint>(shot.target)
                ? registry.get<components::BuildingFootprint>(shot.target)
                : components::BuildingFootprint{1, 1};
            target_x = math::Fixed::from_int(target_grid.cell.x)
                + math::Fixed::from_int(target_footprint.width) / math::Fixed::from_int(2);
            target_y = math::Fixed::from_int(target_grid.cell.y)
                + math::Fixed::from_int(target_footprint.height) / math::Fixed::from_int(2);
        }
        else {
            to_destroy.push_back(projectile);
            continue;
        }

        auto& world = view.get<components::WorldPosition>(projectile);
        const math::Fixed delta_x = target_x - world.x;
        const math::Fixed delta_y = target_y - world.y;
        const float distance = std::sqrt((delta_x * delta_x + delta_y * delta_y).to_float());
        if (distance <= hit_distance.to_float() || distance <= step.to_float()) {
            auto& health = registry.get<components::Health>(shot.target);
            health.current = health.current - math::Fixed::from_int(shot.pierce_damage);
            if (registry.any_of<components::PlayerOwnedTag>(shot.target)) {
                components::note_player_attacked(
                    registry,
                    components::entity_player_slot(registry, shot.target),
                    shot.owner_slot);
            }
            if (health.current.raw() <= 0) {
                note_entity_killed(registry, shot.target, projectile);
            }
            to_destroy.push_back(projectile);
            continue;
        }

        const float nx = delta_x.to_float() / distance;
        const float ny = delta_y.to_float() / distance;
        world.x = world.x + math::Fixed::from_float(nx * step.to_float());
        world.y = world.y + math::Fixed::from_float(ny * step.to_float());
        view.get<components::GridPosition>(projectile).cell = core::GridPos{
            world.x.to_int(),
            world.y.to_int()};
    }

    for (const entt::entity projectile : to_destroy) {
        if (registry.valid(projectile)) {
            registry.destroy(projectile);
        }
    }
}

bool try_complete_trained_unit_spawn(
    entt::registry& registry,
    const entt::entity building,
    const std::string_view unit_id)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null
        || !registry.all_of<components::MapGrid, components::ContentPack>(world)
        || !registry.any_of<components::GridPosition>(building)) {
        return false;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, building);
    if (!player::player_can_spawn_units(registry, player_slot)) {
        return false;
    }

    const auto& content_pack = registry.get<components::ContentPack>(world);
    const auto* unit_archetype =
        data::find_unit_archetype(content_pack.content, std::string(unit_id));
    if (unit_archetype == nullptr) {
        return false;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    const auto& depot_anchor = registry.get<components::GridPosition>(building);
    const bool is_town_center = registry.any_of<components::TownCenterTag>(building);
    const components::BuildingFootprint depot_footprint = components::effective_building_footprint(
        registry.any_of<components::BuildingFootprint>(building)
            ? registry.get<components::BuildingFootprint>(building)
            : components::BuildingFootprint{},
        is_town_center);
    const std::optional<core::GridPos> spawn_cell =
        spawn::find_building_unit_spawn_cell(map, registry, depot_anchor, depot_footprint);
    if (!spawn_cell.has_value()) {
        return false;
    }

    if (unit_id == constants::WORKER_UNIT_ID) {
        return spawn::spawn_player_worker(registry, *unit_archetype, *spawn_cell, player_slot)
            != entt::null;
    }
    if (unit_id == constants::MILITIA_UNIT_ID) {
        return spawn::spawn_player_militia(registry, *unit_archetype, *spawn_cell, player_slot)
            != entt::null;
    }
    if (unit_id == constants::MAGE_UNIT_ID) {
        return spawn::spawn_player_mage(registry, *unit_archetype, *spawn_cell, player_slot)
            != entt::null;
    }

    return false;
}

void run_building_process_system(entt::registry& registry)
{
    std::vector<entt::entity> buildings{};
    const auto view = registry.view<components::BuildingProcess, components::PlayerOwnedTag>();
    for (const entt::entity building : view) {
        buildings.push_back(building);
    }

    for (const entt::entity building : buildings) {
        if (!registry.valid(building) || !registry.any_of<components::BuildingProcess>(building)) {
            continue;
        }

        auto& process = registry.get<components::BuildingProcess>(building);
        if (process.kind == components::BuildingProcessKind::None || process.ticks_total <= 0) {
            continue;
        }

        if (process.ticks_remaining > 0) {
            --process.ticks_remaining;
        }

        if (process.ticks_remaining > 0) {
            continue;
        }

        bool completed = false;
        switch (process.kind) {
        case components::BuildingProcessKind::TrainWorker:
            completed = try_complete_trained_unit_spawn(
                registry, building, constants::WORKER_UNIT_ID);
            break;
        case components::BuildingProcessKind::TrainMilitia:
            completed = try_complete_trained_unit_spawn(
                registry, building, constants::MILITIA_UNIT_ID);
            break;
        case components::BuildingProcessKind::TrainMage:
            completed = try_complete_trained_unit_spawn(
                registry, building, constants::MAGE_UNIT_ID);
            break;
        case components::BuildingProcessKind::AdvanceAge: {
            const entt::entity world = find_world_entity(registry);
            if (world != entt::null && registry.any_of<components::MatchSession>(world)) {
                auto& session = registry.get<components::MatchSession>(world);
                const std::uint8_t player_slot = components::entity_player_slot(registry, building);
                const components::AgeAdvanceCost cost = components::age_advance_cost(
                    components::player_age(session, player_slot));
                if (cost.can_advance && player_slot < session.player_ages.size()) {
                    session.player_ages[player_slot] = static_cast<std::uint8_t>(
                        session.player_ages[player_slot] + 1U);
                    components::push_age_advanced_announcement(
                        registry,
                        player_slot,
                        session.player_ages[player_slot]);
                }
                completed = true;
            }
            break;
        }
        case components::BuildingProcessKind::ResearchCartography: {
            const entt::entity world = find_world_entity(registry);
            if (world != entt::null && registry.any_of<components::MatchSession>(world)) {
                auto& session = registry.get<components::MatchSession>(world);
                const std::uint8_t player_slot = components::entity_player_slot(registry, building);
                if (player_slot < session.player_cartography.size()) {
                    session.player_cartography[player_slot] = 1U;
                }
                rebuild_fog_visibility(registry);
                completed = true;
            }
            break;
        }
        case components::BuildingProcessKind::ResearchTrades: {
            const entt::entity world = find_world_entity(registry);
            if (world != entt::null && registry.any_of<components::MatchSession>(world)) {
                auto& session = registry.get<components::MatchSession>(world);
                const std::uint8_t player_slot = components::entity_player_slot(registry, building);
                if (player_slot < session.player_trades.size()) {
                    session.player_trades[player_slot] = 1U;
                }
                completed = true;
            }
            break;
        }
        case components::BuildingProcessKind::ResearchSpy: {
            const entt::entity world = find_world_entity(registry);
            if (world != entt::null && registry.any_of<components::MatchSession>(world)) {
                auto& session = registry.get<components::MatchSession>(world);
                const std::uint8_t player_slot = components::entity_player_slot(registry, building);
                if (player_slot < session.player_spy.size()) {
                    session.player_spy[player_slot] = 1U;
                }
                rebuild_fog_visibility(registry);
                completed = true;
            }
            break;
        }
        case components::BuildingProcessKind::None:
            completed = true;
            break;
        }

        if (completed) {
            components::clear_building_process(process);
        }
    }
}

} // namespace

entt::entity find_deposit_building(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const core::GridPos worker_pos,
    const int carried_wood,
    const int carried_food,
    const int carried_money)
{
    const bool wood_only = carried_wood > 0 && carried_food <= 0 && carried_money <= 0;
    const bool food_only = carried_food > 0 && carried_wood <= 0 && carried_money <= 0;
    const bool money_only = carried_money > 0 && carried_wood <= 0 && carried_food <= 0;
    if (wood_only) {
        return find_nearest_tagged_drop_off<components::WoodDropOffTag>(
            registry, player_slot, worker_pos);
    }

    if (food_only) {
        return find_nearest_tagged_drop_off<components::FoodDropOffTag>(
            registry, player_slot, worker_pos);
    }

    if (money_only) {
        return find_nearest_tagged_drop_off<components::MoneyDropOffTag>(
            registry, player_slot, worker_pos);
    }

    return find_town_center_for_player_slot(registry, player_slot);
}

void run_gameplay_systems(entt::registry& registry)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return;
    }

    components::tick_match_announcement_cooldowns(registry);

    auto& map = registry.get<components::MapGrid>(world);
    const auto& content = registry.get<components::ContentPack>(world);

    begin_movement_query_cache(registry, map);
    run_building_process_system(registry);
    run_visibility_system(registry);
    run_worker_system(registry, map, content);
    run_worker_deposit_system(registry);
    run_builder_system(registry, map);
    run_extractor_mana_generation(registry);
    run_garden_production(registry);
    run_garrison_enter_system(registry);
    run_enemy_militia_ai(registry);
    run_attack_chase_system(registry, map);
    run_movement_system(registry, map, content);
    run_melee_contact_system(registry);
    run_combat_system(registry, content);
    run_building_autoattack_system(registry, content);
    run_projectile_system(registry);
    run_death_cleanup(registry);
    end_movement_query_cache();
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

    auto& map = registry.get<components::MapGrid>(world);
    if (!map.layer_hash_valid) {
        mix(static_cast<std::uint64_t>(map.width));
        mix(static_cast<std::uint64_t>(map.height));
        for (const components::TileType tile : map.tiles) {
            mix(static_cast<std::uint64_t>(tile));
        }
        for (const components::GroundType ground : map.ground) {
            mix(static_cast<std::uint64_t>(ground));
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
        map.cached_layer_hash = hash;
        map.layer_hash_valid = true;
        if (registry.any_of<components::FogOfWarState>(world)) {
            registry.get<components::FogOfWarState>(world).hash_valid = false;
        }
    }
    else {
        hash = map.cached_layer_hash;
    }

    if (registry.any_of<components::FogOfWarState>(world)) {
        auto& fog = registry.get<components::FogOfWarState>(world);
        if (!fog.hash_valid) {
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
            fog.cached_hash = hash;
            fog.hash_valid = true;
        }
        else {
            hash = fog.cached_hash;
        }
    }

    if (registry.any_of<components::MatchSession>(world)) {
        const auto& session = registry.get<components::MatchSession>(world);
        for (const std::uint8_t age : session.player_ages) {
            mix(static_cast<std::uint64_t>(age));
        }
        for (const std::uint8_t civ : session.player_civilizations) {
            mix(static_cast<std::uint64_t>(civ));
        }
        for (const std::uint8_t side : session.player_side_indices) {
            mix(static_cast<std::uint64_t>(side));
        }
        for (const std::uint8_t cartography : session.player_cartography) {
            mix(static_cast<std::uint64_t>(cartography));
        }
        for (const std::uint8_t trades : session.player_trades) {
            mix(static_cast<std::uint64_t>(trades));
        }
        for (const std::uint8_t mask : session.player_ally_mask) {
            mix(static_cast<std::uint64_t>(mask));
        }
        for (const std::uint8_t victory : session.player_ally_victory) {
            mix(static_cast<std::uint64_t>(victory));
        }
        mix(session.block_team_changes ? 1U : 0U);
        for (const std::uint8_t spy : session.player_spy) {
            mix(static_cast<std::uint64_t>(spy));
        }
        for (const std::uint8_t built_mill : session.player_built_mill) {
            mix(static_cast<std::uint64_t>(built_mill));
        }
        for (const auto& stockpile : session.player_stockpiles) {
            mix(static_cast<std::uint64_t>(stockpile.wood));
            mix(static_cast<std::uint64_t>(stockpile.food));
            mix(static_cast<std::uint64_t>(stockpile.money));
            mix(static_cast<std::uint64_t>(stockpile.mana));
        }
        for (const auto& stats : session.player_stats) {
            mix(static_cast<std::uint64_t>(stats.units_created));
            mix(static_cast<std::uint64_t>(stats.units_lost));
            mix(static_cast<std::uint64_t>(stats.units_killed));
            mix(static_cast<std::uint64_t>(stats.buildings_created));
            mix(static_cast<std::uint64_t>(stats.buildings_lost));
            mix(static_cast<std::uint64_t>(stats.buildings_destroyed));
            mix(static_cast<std::uint64_t>(stats.wood_collected));
            mix(static_cast<std::uint64_t>(stats.food_collected));
            mix(static_cast<std::uint64_t>(stats.money_collected));
            mix(static_cast<std::uint64_t>(stats.mana_collected));
            mix(static_cast<std::uint64_t>(stats.trades_sent));
            mix(static_cast<std::uint64_t>(stats.trades_received));
        }
        mix(static_cast<std::uint64_t>(session.attack_reveal_flares.size()));
        for (const auto& flare : session.attack_reveal_flares) {
            mix(static_cast<std::uint64_t>(flare.x));
            mix(static_cast<std::uint64_t>(flare.y));
            mix(static_cast<std::uint64_t>(flare.width));
            mix(static_cast<std::uint64_t>(flare.height));
            mix(static_cast<std::uint64_t>(flare.viewer_slot));
            mix(static_cast<std::uint64_t>(flare.ticks_remaining));
        }
    }

    struct HashableEntity {
        entt::entity entity{entt::null};
        snapshot::EntitySnapshotKey key{};
    };

    thread_local std::vector<HashableEntity> entities;
    entities.clear();
    for (const entt::entity entity : registry.view<components::GridPosition>()) {
        if (registry.all_of<components::WorldTag>(entity)) {
            continue;
        }

        const std::optional<snapshot::EntitySnapshotKey> entity_key =
            snapshot::compute_entity_snapshot_key(registry, entity);
        if (!entity_key.has_value()) {
            continue;
        }

        entities.push_back(HashableEntity{entity, *entity_key});
    }

    std::sort(
        entities.begin(),
        entities.end(),
        [](const HashableEntity& left, const HashableEntity& right) {
            return snapshot::compare_entity_snapshot_keys(left.key, right.key);
        });

    for (const HashableEntity& item : entities) {
        const entt::entity entity = item.entity;
        snapshot::mix_entity_snapshot_key(hash, item.key);

        if (registry.any_of<components::UnitSex>(entity)) {
            mix(static_cast<std::uint64_t>(
                static_cast<std::uint8_t>(registry.get<components::UnitSex>(entity).value)));
        }

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

        if (registry.any_of<components::FarmFood>(entity)) {
            const auto& farm_food = registry.get<components::FarmFood>(entity);
            mix(static_cast<std::uint64_t>(farm_food.remaining));
            mix(static_cast<std::uint64_t>(farm_food.max));
        }

        if (registry.any_of<components::BuildingProcess>(entity)) {
            const auto& process = registry.get<components::BuildingProcess>(entity);
            mix(static_cast<std::uint64_t>(process.kind));
            mix(static_cast<std::uint64_t>(process.ticks_remaining));
            mix(static_cast<std::uint64_t>(process.ticks_total));
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
