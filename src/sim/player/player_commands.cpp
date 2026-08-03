#include "sim/player/player_commands.hpp"

#include "core/constants.hpp"
#include "core/constants.hpp"
#include "core/grid.hpp"
#include "render/game_renderer.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/systems/pathfinding.hpp"

#include <algorithm>
#include <vector>

namespace aoa::sim::player {

namespace {

entt::entity find_world_entity(entt::registry& registry)
{
    const auto view = registry.view<components::WorldTag>();
    if (view.begin() == view.end()) {
        return entt::null;
    }

    return *view.begin();
}

bool is_occupied(entt::registry& registry, const core::GridPos cell, const entt::entity ignore)
{
    return systems::is_movement_blocked(registry, cell, ignore);
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
        if (!systems::is_tile_walkable(map, candidate, false)) {
            continue;
        }

        if (!is_occupied(registry, candidate, ignore)) {
            return candidate;
        }
    }

    return target;
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

void assign_path(
    entt::registry& registry,
    const entt::entity entity,
    const components::MapGrid& map,
    const core::GridPos goal)
{
    if (registry.any_of<components::WorldPosition>(entity)) {
        sync_grid_from_world(registry, entity);
    }

    const auto& pos = registry.get<components::GridPosition>(entity).cell;
    auto& path = registry.get_or_emplace<components::MovePath>(entity);
    path.cells = systems::find_path(map, pos, goal, registry, entity, false);
    path.next_index = 0;
    registry.remove<components::MoveSegment>(entity);
}

void mark_manual_control(entt::registry& registry, const entt::entity entity)
{
    registry.get_or_emplace<components::ManualControlTag>(entity);

    if (registry.any_of<components::WorkerBrain>(entity)) {
        registry.get<components::WorkerBrain>(entity).state = components::WorkerState::Idle;
    }
}

bool is_alive_player_unit(entt::registry& registry, const entt::entity entity)
{
    if (!registry.valid(entity)) {
        return false;
    }

    if (!registry.all_of<components::UnitTag, components::PlayerOwnedTag, components::GridPosition, components::Health>(
            entity)) {
        return false;
    }

    return registry.get<components::Health>(entity).current.raw() > 0;
}

} // namespace

entt::entity pick_player_unit_at(entt::registry& registry, const core::GridPos cell)
{
    entt::entity best = entt::null;

    const auto view = registry.view<
        components::UnitTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::Health>();

    for (const entt::entity entity : view) {
        const auto& health = view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        if (view.get<components::GridPosition>(entity).cell != cell) {
            continue;
        }

        if (best == entt::null
            || static_cast<entt::id_type>(entity) < static_cast<entt::id_type>(best)) {
            best = entity;
        }
    }

    return best;
}

entt::entity pick_enemy_at(entt::registry& registry, const core::GridPos cell)
{
    entt::entity best = entt::null;

    const auto view = registry.view<
        components::UnitTag,
        components::EnemyTag,
        components::GridPosition,
        components::Health>();

    for (const entt::entity entity : view) {
        const auto& health = view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        if (view.get<components::GridPosition>(entity).cell != cell) {
            continue;
        }

        if (best == entt::null
            || static_cast<entt::id_type>(entity) < static_cast<entt::id_type>(best)) {
            best = entity;
        }
    }

    return best;
}

bool issue_move_order(entt::registry& registry, const entt::entity entity, const core::GridPos goal)
{
    if (!is_alive_player_unit(registry, entity)) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    if (!core::is_inside_grid(goal, map.width, map.height)) {
        return false;
    }

    if (!systems::is_tile_walkable(map, goal, false)) {
        return false;
    }

    if (is_occupied(registry, goal, entity)) {
        return false;
    }

    mark_manual_control(registry, entity);
    registry.remove<components::AttackOrder>(entity);
    registry.remove<components::GatherTarget>(entity);
    assign_path(registry, entity, map, goal);
    return true;
}

bool issue_attack_order(
    entt::registry& registry,
    const entt::entity entity,
    const entt::entity target)
{
    if (!is_alive_player_unit(registry, entity)) {
        return false;
    }

    if (!registry.valid(target) || !registry.any_of<components::Health, components::GridPosition>(target)) {
        return false;
    }

    if (registry.get<components::Health>(target).current.raw() <= 0) {
        return false;
    }

    if (!registry.any_of<components::EnemyTag>(target)) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    const core::GridPos target_pos = registry.get<components::GridPosition>(target).cell;
    const core::GridPos stand_tile = find_adjacent_walkable(map, registry, target_pos, entity);

    mark_manual_control(registry, entity);
    registry.get_or_emplace<components::AttackOrder>(entity).target = target;
    assign_path(registry, entity, map, stand_tile);
    return true;
}

bool issue_gather_order(
    entt::registry& registry,
    const entt::entity entity,
    const core::GridPos forest_cell)
{
    if (!is_alive_player_unit(registry, entity)) {
        return false;
    }

    if (!registry.any_of<components::WorkerUnitTag>(entity)) {
        return false;
    }

    if (!pick_resource_forest_at(registry, forest_cell).has_value()) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    const core::GridPos stand_tile = find_adjacent_walkable(map, registry, forest_cell, entity);

    mark_manual_control(registry, entity);
    registry.remove<components::AttackOrder>(entity);
    registry.get_or_emplace<components::GatherTarget>(entity).cell = forest_cell;
    registry.get_or_emplace<components::WorkerBrain>(entity).state = components::WorkerState::MovingToResource;
    assign_path(registry, entity, map, stand_tile);
    return true;
}

void issue_gather_orders(
    entt::registry& registry,
    const std::vector<entt::entity>& entities,
    const core::GridPos forest_cell)
{
    for (const entt::entity entity : entities) {
        issue_gather_order(registry, entity, forest_cell);
    }
}

void issue_deposit_orders(entt::registry& registry, const std::vector<entt::entity>& entities)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return;
    }

    const auto& map = registry.get<components::MapGrid>(world);

    entt::entity town_center = entt::null;
    const auto town_center_view = registry.view<components::TownCenterTag, components::GridPosition>();
    if (town_center_view.begin() != town_center_view.end()) {
        town_center = *town_center_view.begin();
    }

    if (town_center == entt::null) {
        return;
    }

    const core::GridPos depot_pos = registry.get<components::GridPosition>(town_center).cell;

    for (const entt::entity entity : entities) {
        if (!is_alive_player_unit(registry, entity)) {
            continue;
        }

        if (!registry.any_of<components::WorkerUnitTag>(entity)) {
            continue;
        }

        const core::GridPos stand_tile = find_adjacent_walkable(map, registry, depot_pos, entity);
        if (stand_tile == depot_pos || is_occupied(registry, stand_tile, entity)) {
            continue;
        }

        issue_move_order(registry, entity, stand_tile);
    }
}

namespace {

bool is_alive_player_unit_entity(entt::registry& registry, const entt::entity entity)
{
    if (!registry.valid(entity)) {
        return false;
    }

    if (!registry.all_of<components::UnitTag, components::PlayerOwnedTag, components::Health>(entity)) {
        return false;
    }

    return registry.get<components::Health>(entity).current.raw() > 0;
}

bool selection_contains(const std::vector<entt::entity>& selected, const entt::entity entity)
{
    return std::find(selected.begin(), selected.end(), entity) != selected.end();
}

entt::entity pick_unit_at_screen_any_team(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const bool player_only)
{
    entt::entity best = entt::null;
    float best_distance_sq = pick_radius_px * pick_radius_px;

    const auto view = registry.view<
        components::UnitTag,
        components::GridPosition,
        components::Health>();

    for (const entt::entity entity : view) {
        if (player_only && !registry.any_of<components::PlayerOwnedTag>(entity)) {
            continue;
        }

        if (!player_only && !registry.any_of<components::EnemyTag>(entity)) {
            continue;
        }

        const auto& health = view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        const sf::Vector2f unit_screen = renderer.unit_screen_position(registry, entity, 1.0F);
        const float dx = unit_screen.x - screen_position.x;
        const float dy = unit_screen.y - screen_position.y;
        const float distance_sq = dx * dx + dy * dy;
        if (distance_sq > best_distance_sq) {
            continue;
        }

        if (best == entt::null || distance_sq < best_distance_sq
            || (distance_sq == best_distance_sq
                && static_cast<entt::id_type>(entity) < static_cast<entt::id_type>(best))) {
            best = entity;
            best_distance_sq = distance_sq;
        }
    }

    return best;
}

} // namespace

entt::entity pick_player_unit_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px)
{
    return pick_unit_at_screen_any_team(registry, renderer, screen_position, pick_radius_px, true);
}

entt::entity pick_enemy_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px)
{
    return pick_unit_at_screen_any_team(registry, renderer, screen_position, pick_radius_px, false);
}

entt::entity pick_hovered_unit_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px)
{
    entt::entity best = entt::null;
    float best_distance_sq = pick_radius_px * pick_radius_px;

    const auto view = registry.view<
        components::UnitTag,
        components::GridPosition,
        components::Health>();

    for (const entt::entity entity : view) {
        const auto& health = view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        const sf::Vector2f unit_screen = renderer.unit_screen_position(registry, entity, 1.0F);
        const float dx = unit_screen.x - screen_position.x;
        const float dy = unit_screen.y - screen_position.y;
        const float distance_sq = dx * dx + dy * dy;
        if (distance_sq > best_distance_sq) {
            continue;
        }

        if (best == entt::null || distance_sq < best_distance_sq
            || (distance_sq == best_distance_sq
                && static_cast<entt::id_type>(entity) < static_cast<entt::id_type>(best))) {
            best = entity;
            best_distance_sq = distance_sq;
        }
    }

    return best;
}

entt::entity pick_player_building_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px)
{
    const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
    if (!grid_cell.has_value()) {
        return entt::null;
    }

    entt::entity best = entt::null;
    float best_distance_sq = pick_radius_px * pick_radius_px;

    const auto view = registry.view<
        components::BuildingTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::Health>();

    for (const entt::entity entity : view) {
        const auto& health = view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        const core::GridPos cell = view.get<components::GridPosition>(entity).cell;
        if (cell != *grid_cell) {
            continue;
        }

        const sf::Vector2f building_screen = renderer.tile_center_screen(
            cell.x,
            cell.y,
            constants::RENDER_BUILDING_HEIGHT * 0.5F);
        const float dx = building_screen.x - screen_position.x;
        const float dy = building_screen.y - screen_position.y;
        const float distance_sq = dx * dx + dy * dy;
        if (distance_sq > best_distance_sq) {
            continue;
        }

        if (best == entt::null || distance_sq < best_distance_sq
            || (distance_sq == best_distance_sq
                && static_cast<entt::id_type>(entity) < static_cast<entt::id_type>(best))) {
            best = entity;
            best_distance_sq = distance_sq;
        }
    }

    return best;
}

std::vector<entt::entity> pick_player_units_in_screen_rect(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    const sf::Vector2f rect_min,
    const sf::Vector2f rect_max)
{
    const float min_x = std::min(rect_min.x, rect_max.x);
    const float max_x = std::max(rect_min.x, rect_max.x);
    const float min_y = std::min(rect_min.y, rect_max.y);
    const float max_y = std::max(rect_min.y, rect_max.y);

    std::vector<entt::entity> picked{};
    const auto view = registry.view<
        components::UnitTag,
        components::PlayerOwnedTag,
        components::Health>();

    for (const entt::entity entity : view) {
        const auto& health = view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        const sf::Vector2f unit_screen = renderer.unit_screen_position(registry, entity, 1.0F);
        if (unit_screen.x < min_x || unit_screen.x > max_x || unit_screen.y < min_y
            || unit_screen.y > max_y) {
            continue;
        }

        picked.push_back(entity);
    }

    std::sort(picked.begin(), picked.end(), [](const entt::entity left, const entt::entity right) {
        return static_cast<entt::id_type>(left) < static_cast<entt::id_type>(right);
    });

    return picked;
}

void apply_selection(
    std::vector<entt::entity>& selected,
    entt::registry& registry,
    const std::vector<entt::entity>& picked,
    const SelectionModifyMode mode)
{
    if (mode == SelectionModifyMode::Replace) {
        selected = picked;
        return;
    }

    for (const entt::entity entity : picked) {
        if (!is_alive_player_unit_entity(registry, entity)) {
            continue;
        }

        const auto iterator = std::find(selected.begin(), selected.end(), entity);
        if (mode == SelectionModifyMode::Add) {
            if (iterator == selected.end()) {
                selected.push_back(entity);
            }
            continue;
        }

        if (iterator == selected.end()) {
            selected.push_back(entity);
        }
        else {
            selected.erase(iterator);
        }
    }

    std::sort(selected.begin(), selected.end(), [](const entt::entity left, const entt::entity right) {
        return static_cast<entt::id_type>(left) < static_cast<entt::id_type>(right);
    });
}

void issue_move_orders(
    entt::registry& registry,
    const std::vector<entt::entity>& entities,
    const core::GridPos goal)
{
    for (const entt::entity entity : entities) {
        issue_move_order(registry, entity, goal);
    }
}

void issue_attack_orders(
    entt::registry& registry,
    const std::vector<entt::entity>& entities,
    const entt::entity target)
{
    for (const entt::entity entity : entities) {
        issue_attack_order(registry, entity, target);
    }
}

void prune_dead_selection(std::vector<entt::entity>& selected, entt::registry& registry)
{
    selected.erase(
        std::remove_if(
            selected.begin(),
            selected.end(),
            [&registry](const entt::entity entity) {
                return !is_alive_player_unit_entity(registry, entity);
            }),
        selected.end());
}

std::optional<core::GridPos> pick_resource_forest_at(
    entt::registry& registry,
    const core::GridPos cell)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return std::nullopt;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    if (!core::is_inside_grid(cell, map.width, map.height)) {
        return std::nullopt;
    }

    const int index = core::grid_index(cell, map.width);
    if (map.tiles[static_cast<std::size_t>(index)] != components::TileType::Forest) {
        return std::nullopt;
    }

    if (map.forest_wood[static_cast<std::size_t>(index)] <= 0) {
        return std::nullopt;
    }

    return cell;
}

std::optional<core::GridPos> pick_resource_forest_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px)
{
    const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
    if (!grid_cell.has_value()) {
        return std::nullopt;
    }

    if (pick_resource_forest_at(registry, *grid_cell).has_value()) {
        return grid_cell;
    }

    const sf::Vector2f center_screen = renderer.tile_center_screen(
        grid_cell->x,
        grid_cell->y,
        constants::RENDER_ENTITY_BASE_LIFT);
    const float dx = center_screen.x - screen_position.x;
    const float dy = center_screen.y - screen_position.y;
    if ((dx * dx + dy * dy) > pick_radius_px * pick_radius_px) {
        return std::nullopt;
    }

    return pick_resource_forest_at(registry, *grid_cell);
}

} // namespace aoa::sim::player
