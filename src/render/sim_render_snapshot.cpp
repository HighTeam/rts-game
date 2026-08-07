#include "render/sim_render_snapshot.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "math/fixed.hpp"
#include "net/net_constants.hpp"
#include "render/game_renderer.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/fog_of_war.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/player/player_commands.hpp"
#include "sim/systems/visibility_system.hpp"

#include <algorithm>
#include <cmath>

namespace aoa::render {

namespace {

RenderEntityPose capture_entity_pose(
    const entt::registry& registry,
    const entt::entity entity,
    const std::uint8_t local_player_slot,
    const bool shrouded = false)
{
    RenderEntityPose pose{};
    pose.entity = entity;
    pose.shrouded = shrouded;

    if (registry.any_of<sim::components::GridPosition>(entity)) {
        const auto& cell = registry.get<sim::components::GridPosition>(entity).cell;
        pose.grid_x = cell.x;
        pose.grid_y = cell.y;
    }

    if (registry.all_of<sim::components::PreviousWorldPosition, sim::components::WorldPosition>(entity)) {
        const auto& previous = registry.get<sim::components::PreviousWorldPosition>(entity);
        const auto& current = registry.get<sim::components::WorldPosition>(entity);
        pose.prev_x = previous.x.to_float();
        pose.prev_y = previous.y.to_float();
        pose.cur_x = current.x.to_float();
        pose.cur_y = current.y.to_float();
    }
    else if (registry.any_of<sim::components::WorldPosition>(entity)) {
        const auto& current = registry.get<sim::components::WorldPosition>(entity);
        pose.prev_x = current.x.to_float();
        pose.prev_y = current.y.to_float();
        pose.cur_x = current.x.to_float();
        pose.cur_y = current.y.to_float();
    }

    if (registry.all_of<sim::components::MoveSegment>(entity)) {
        const auto& segment = registry.get<sim::components::MoveSegment>(entity);
        pose.has_move_segment = segment.ticks_total > 0;
        pose.move_from_x = segment.from_x.to_float();
        pose.move_from_y = segment.from_y.to_float();
        pose.move_to_x = segment.to_x.to_float();
        pose.move_to_y = segment.to_y.to_float();
        pose.move_ticks_elapsed = segment.ticks_elapsed;
        pose.move_ticks_total = segment.ticks_total;
    }

    if (registry.any_of<sim::components::Health>(entity)) {
        const auto& health = registry.get<sim::components::Health>(entity);
        pose.health_current = health.current.to_int();
        pose.health_max = health.max.to_int();
    }

    pose.is_enemy = registry.any_of<sim::components::EnemyTag>(entity)
        || (registry.any_of<sim::components::PlayerOwnedTag>(entity)
            && sim::components::entity_player_slot(registry, entity) != local_player_slot);
    pose.is_worker = registry.any_of<sim::components::WorkerUnitTag>(entity);
    pose.is_town_center = registry.any_of<sim::components::TownCenterTag>(entity);
    if (registry.any_of<sim::components::PlayerOwnedTag>(entity)) {
        pose.player_slot = sim::components::entity_player_slot(registry, entity);
    }

    return pose;
}

void capture_hud_stats(
    const entt::registry& registry,
    const std::uint8_t player_slot,
    RenderHudPlayerStats& stats)
{
    const auto town_center_view = registry.view<
        sim::components::TownCenterTag,
        sim::components::PlayerOwnedTag,
        sim::components::Stockpile>();
    for (const entt::entity entity : town_center_view) {
        if (sim::components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        stats.town_wood = town_center_view.get<sim::components::Stockpile>(entity).wood;
        break;
    }

    const auto worker_view = registry.view<
        sim::components::WorkerUnitTag,
        sim::components::PlayerOwnedTag,
        sim::components::CarriedWood>();
    for (const entt::entity entity : worker_view) {
        if (sim::components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        stats.carried_wood = worker_view.get<sim::components::CarriedWood>(entity).amount;
        break;
    }

    const auto militia_view = registry.view<
        sim::components::MilitiaUnitTag,
        sim::components::PlayerOwnedTag,
        sim::components::Health>();
    for (const entt::entity entity : militia_view) {
        if (sim::components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        const auto& health = militia_view.get<sim::components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        stats.militia_hp = health.current.to_int();
        stats.militia_max_hp = health.max.to_int();
        break;
    }
}

} // namespace

SimRenderSnapshot capture_sim_render_snapshot(
    const entt::registry& registry,
    const std::uint8_t local_player_slot)
{
    SimRenderSnapshot snapshot{};

    const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
    if (world_view.begin() == world_view.end()) {
        return snapshot;
    }

    const entt::entity world = *world_view.begin();
    const auto& map = world_view.get<sim::components::MapGrid>(world);
    snapshot.map_width = map.width;
    snapshot.map_height = map.height;
    snapshot.tiles = map.tiles;
    snapshot.forest_wood = map.forest_wood;

    const sim::components::FogOfWarState* fog = nullptr;
    if (registry.any_of<sim::components::FogOfWarState>(world)) {
        fog = &registry.get<sim::components::FogOfWarState>(world);
        const std::size_t cells_per_player =
            static_cast<std::size_t>(map.width * map.height);
        const std::size_t offset =
            static_cast<std::size_t>(local_player_slot) * cells_per_player;
        snapshot.fog_explored.assign(
            fog->explored.begin() + static_cast<std::ptrdiff_t>(offset),
            fog->explored.begin() + static_cast<std::ptrdiff_t>(offset + cells_per_player));
        snapshot.fog_visible.assign(
            fog->visible.begin() + static_cast<std::ptrdiff_t>(offset),
            fog->visible.begin() + static_cast<std::ptrdiff_t>(offset + cells_per_player));
        snapshot.fog_memory_tiles.assign(
            fog->memory_tiles.begin() + static_cast<std::ptrdiff_t>(offset),
            fog->memory_tiles.begin() + static_cast<std::ptrdiff_t>(offset + cells_per_player));
        snapshot.fog_memory_forest_wood.assign(
            fog->memory_forest_wood.begin() + static_cast<std::ptrdiff_t>(offset),
            fog->memory_forest_wood.begin() + static_cast<std::ptrdiff_t>(offset + cells_per_player));
    }

    const auto should_draw_entity = [&](const entt::entity entity) {
        if (fog == nullptr) {
            return true;
        }

        return sim::systems::is_entity_visible_to_slot(
            registry,
            *fog,
            entity,
            local_player_slot);
    };

    const auto building_view = registry.view<
        sim::components::BuildingTag,
        sim::components::GridPosition,
        sim::components::Health>();
    for (const entt::entity entity : building_view) {
        if (should_draw_entity(entity)) {
            snapshot.buildings.push_back(capture_entity_pose(registry, entity, local_player_slot));
            continue;
        }

        if (fog != nullptr
            && sim::systems::is_building_renderable_in_shroud(registry, *fog, entity, local_player_slot)) {
            snapshot.buildings.push_back(
                capture_entity_pose(registry, entity, local_player_slot, true));
        }
    }

    const auto unit_view = registry.view<
        sim::components::UnitTag,
        sim::components::GridPosition,
        sim::components::Health>();
    for (const entt::entity entity : unit_view) {
        const auto& health = unit_view.get<sim::components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        if (!should_draw_entity(entity)) {
            continue;
        }

        snapshot.units.push_back(capture_entity_pose(registry, entity, local_player_slot));
    }

    for (std::uint8_t player_slot = 0U;
        player_slot < static_cast<std::uint8_t>(snapshot.hud_by_player.size());
        ++player_slot) {
        capture_hud_stats(registry, player_slot, snapshot.hud_by_player[player_slot]);
    }

    return snapshot;
}

std::pair<float, float> interpolate_render_pose(
    const RenderEntityPose& pose,
    const float interpolation_alpha)
{
    const float max_extrapolation_alpha = net::constants::LOCKSTEP_MAX_RENDER_EXTRAPOLATION_ALPHA;

    if (pose.has_move_segment && pose.move_ticks_total > 0) {
        const float progress = (static_cast<float>(pose.move_ticks_elapsed)
                + std::clamp(interpolation_alpha, 0.0F, 1.0F + max_extrapolation_alpha))
            / static_cast<float>(pose.move_ticks_total);
        const math::Fixed t = math::Fixed::from_float(std::clamp(progress, 0.0F, 1.0F));
        return {
            math::fixed_lerp(
                math::Fixed::from_float(pose.move_from_x),
                math::Fixed::from_float(pose.move_to_x),
                t).to_float(),
            math::fixed_lerp(
                math::Fixed::from_float(pose.move_from_y),
                math::Fixed::from_float(pose.move_to_y),
                t).to_float(),
        };
    }

    if (interpolation_alpha <= 1.0F) {
        const math::Fixed t = math::Fixed::from_float(std::clamp(interpolation_alpha, 0.0F, 1.0F));
        return {
            math::fixed_lerp(
                math::Fixed::from_float(pose.prev_x),
                math::Fixed::from_float(pose.cur_x),
                t).to_float(),
            math::fixed_lerp(
                math::Fixed::from_float(pose.prev_y),
                math::Fixed::from_float(pose.cur_y),
                t).to_float(),
        };
    }

    const float extrapolation_alpha =
        std::min(interpolation_alpha - 1.0F, max_extrapolation_alpha);
    const math::Fixed delta_x =
        math::Fixed::from_float(pose.cur_x) - math::Fixed::from_float(pose.prev_x);
    const math::Fixed delta_y =
        math::Fixed::from_float(pose.cur_y) - math::Fixed::from_float(pose.prev_y);
    const math::Fixed extrapolation = math::Fixed::from_float(extrapolation_alpha);
    return {
        (math::Fixed::from_float(pose.cur_x) + delta_x * extrapolation).to_float(),
        (math::Fixed::from_float(pose.cur_y) + delta_y * extrapolation).to_float(),
    };
}

sf::Vector2f render_pose_screen_position(
    const GameRenderer& renderer,
    const RenderEntityPose& pose,
    const float interpolation_alpha)
{
    const auto [world_x, world_z] = interpolate_render_pose(pose, interpolation_alpha);
    return renderer.world_to_screen(world_x, 0.0F, world_z);
}

namespace {

[[nodiscard]] bool is_alive_player_unit_pose(
    const RenderEntityPose& pose,
    const std::uint8_t local_player_slot)
{
    return pose.entity != entt::null
        && pose.health_current > 0
        && !pose.is_enemy
        && pose.player_slot == local_player_slot;
}

[[nodiscard]] entt::entity pick_unit_at_screen_any_team(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const std::uint8_t local_player_slot,
    const bool pick_opponent)
{
    entt::entity best = entt::null;
    float best_distance_sq = pick_radius_px * pick_radius_px;

    for (const RenderEntityPose& pose : snapshot.units) {
        if (pick_opponent) {
            if (!pose.is_enemy || pose.health_current <= 0) {
                continue;
            }
        }
        else if (!is_alive_player_unit_pose(pose, local_player_slot)) {
            continue;
        }

        const auto [world_x, world_z] = interpolate_render_pose(pose, 1.0F);
        if (!snapshot.fog_visible.empty()) {
            const core::GridPos visibility_cell =
                snapshot_world_visibility_cell(world_x, world_z);
            if (!snapshot_cell_is_visible(snapshot, visibility_cell)) {
                continue;
            }
        }

        const sf::Vector2f unit_screen = render_pose_screen_position(renderer, pose, 1.0F);
        const float dx = unit_screen.x - screen_position.x;
        const float dy = unit_screen.y - screen_position.y;
        const float distance_sq = dx * dx + dy * dy;
        if (distance_sq > best_distance_sq) {
            continue;
        }

        if (best == entt::null || distance_sq < best_distance_sq
            || (distance_sq == best_distance_sq
                && static_cast<entt::id_type>(pose.entity)
                    < static_cast<entt::id_type>(best))) {
            best = pose.entity;
            best_distance_sq = distance_sq;
        }
    }

    return best;
}

[[nodiscard]] std::optional<core::GridPos> pick_resource_forest_at(
    const SimRenderSnapshot& snapshot,
    const core::GridPos cell)
{
    if (!core::is_inside_grid(cell, snapshot.map_width, snapshot.map_height)) {
        return std::nullopt;
    }

    const int index = core::grid_index(cell, snapshot.map_width);
    if (snapshot.tiles[static_cast<std::size_t>(index)] != sim::components::TileType::Forest) {
        return std::nullopt;
    }

    if (snapshot.forest_wood[static_cast<std::size_t>(index)] <= 0) {
        return std::nullopt;
    }

    return cell;
}

} // namespace

bool snapshot_cell_is_unexplored(const SimRenderSnapshot& snapshot, const core::GridPos cell)
{
    if (snapshot.fog_visible.empty() || snapshot.fog_explored.empty()) {
        return false;
    }

    if (!core::is_inside_grid(cell, snapshot.map_width, snapshot.map_height)) {
        return false;
    }

    const std::size_t index = static_cast<std::size_t>(cell.y * snapshot.map_width + cell.x);
    if (index >= snapshot.fog_visible.size() || index >= snapshot.fog_explored.size()) {
        return false;
    }

    return snapshot.fog_visible[index] == 0U && snapshot.fog_explored[index] == 0U;
}

bool snapshot_cell_is_visible(const SimRenderSnapshot& snapshot, const core::GridPos cell)
{
    if (snapshot.fog_visible.empty()) {
        return true;
    }

    if (!core::is_inside_grid(cell, snapshot.map_width, snapshot.map_height)) {
        return false;
    }

    const std::size_t index = static_cast<std::size_t>(cell.y * snapshot.map_width + cell.x);
    if (index >= snapshot.fog_visible.size()) {
        return false;
    }

    return snapshot.fog_visible[index] != 0U;
}

core::GridPos snapshot_world_visibility_cell(const float world_x, const float world_z)
{
    return {
        static_cast<int>(std::floor(world_x)),
        static_cast<int>(std::floor(world_z)),
    };
}

entt::entity pick_hovered_unit_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px)
{
    entt::entity best = entt::null;
    float best_distance_sq = pick_radius_px * pick_radius_px;

    for (const RenderEntityPose& pose : snapshot.units) {
        if (pose.health_current <= 0) {
            continue;
        }

        const auto [world_x, world_z] = interpolate_render_pose(pose, 1.0F);
        if (!snapshot.fog_visible.empty()) {
            const core::GridPos visibility_cell =
                snapshot_world_visibility_cell(world_x, world_z);
            if (!snapshot_cell_is_visible(snapshot, visibility_cell)) {
                continue;
            }
        }

        const sf::Vector2f unit_screen = render_pose_screen_position(renderer, pose, 1.0F);
        const float dx = unit_screen.x - screen_position.x;
        const float dy = unit_screen.y - screen_position.y;
        const float distance_sq = dx * dx + dy * dy;
        if (distance_sq > best_distance_sq) {
            continue;
        }

        if (best == entt::null || distance_sq < best_distance_sq
            || (distance_sq == best_distance_sq
                && static_cast<entt::id_type>(pose.entity)
                    < static_cast<entt::id_type>(best))) {
            best = pose.entity;
            best_distance_sq = distance_sq;
        }
    }

    return best;
}

entt::entity pick_player_unit_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const std::uint8_t local_player_slot)
{
    return pick_unit_at_screen_any_team(
        snapshot,
        renderer,
        screen_position,
        pick_radius_px,
        local_player_slot,
        false);
}

entt::entity pick_enemy_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const std::uint8_t local_player_slot)
{
    return pick_unit_at_screen_any_team(
        snapshot,
        renderer,
        screen_position,
        pick_radius_px,
        local_player_slot,
        true);
}

entt::entity pick_player_building_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const std::uint8_t local_player_slot)
{
    const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
    if (!grid_cell.has_value()) {
        return entt::null;
    }

    entt::entity best = entt::null;
    float best_distance_sq = pick_radius_px * pick_radius_px;

    for (const RenderEntityPose& pose : snapshot.buildings) {
        if (pose.player_slot != local_player_slot || pose.health_current <= 0) {
            continue;
        }

        if (pose.grid_x != grid_cell->x || pose.grid_y != grid_cell->y) {
            continue;
        }

        const sf::Vector2f building_screen = renderer.tile_center_screen(
            pose.grid_x,
            pose.grid_y,
            constants::RENDER_BUILDING_HEIGHT * 0.5F);
        const float dx = building_screen.x - screen_position.x;
        const float dy = building_screen.y - screen_position.y;
        const float distance_sq = dx * dx + dy * dy;
        if (distance_sq > best_distance_sq) {
            continue;
        }

        if (best == entt::null || distance_sq < best_distance_sq
            || (distance_sq == best_distance_sq
                && static_cast<entt::id_type>(pose.entity)
                    < static_cast<entt::id_type>(best))) {
            best = pose.entity;
            best_distance_sq = distance_sq;
        }
    }

    return best;
}

std::vector<entt::entity> pick_player_units_in_screen_rect(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f rect_min,
    const sf::Vector2f rect_max,
    const std::uint8_t local_player_slot)
{
    const float min_x = std::min(rect_min.x, rect_max.x);
    const float max_x = std::max(rect_min.x, rect_max.x);
    const float min_y = std::min(rect_min.y, rect_max.y);
    const float max_y = std::max(rect_min.y, rect_max.y);

    std::vector<entt::entity> picked{};
    for (const RenderEntityPose& pose : snapshot.units) {
        if (!is_alive_player_unit_pose(pose, local_player_slot)) {
            continue;
        }

        const sf::Vector2f unit_screen = render_pose_screen_position(renderer, pose, 1.0F);
        if (unit_screen.x < min_x || unit_screen.x > max_x || unit_screen.y < min_y
            || unit_screen.y > max_y) {
            continue;
        }

        picked.push_back(pose.entity);
    }

    std::sort(picked.begin(), picked.end(), [](const entt::entity left, const entt::entity right) {
        return static_cast<entt::id_type>(left) < static_cast<entt::id_type>(right);
    });

    return picked;
}

std::optional<core::GridPos> pick_resource_forest_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const std::uint8_t local_player_slot)
{
    const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
    if (!grid_cell.has_value()) {
        return std::nullopt;
    }

    if (snapshot_cell_is_unexplored(snapshot, *grid_cell)) {
        return std::nullopt;
    }

    if (!snapshot.fog_visible.empty()) {
        const std::size_t index = static_cast<std::size_t>(grid_cell->y * snapshot.map_width + grid_cell->x);
        if (index >= snapshot.fog_visible.size() || snapshot.fog_visible[index] == 0U) {
            return std::nullopt;
        }
    }

    if (pick_resource_forest_at(snapshot, *grid_cell).has_value()) {
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

    return pick_resource_forest_at(snapshot, *grid_cell);
}

bool snapshot_has_town_center(const SimRenderSnapshot& snapshot, const entt::entity entity)
{
    for (const RenderEntityPose& pose : snapshot.buildings) {
        if (pose.entity == entity) {
            return pose.is_town_center;
        }
    }

    return false;
}

bool snapshot_has_town_center_at_cell(
    const SimRenderSnapshot& snapshot,
    const core::GridPos cell,
    const std::uint8_t local_player_slot)
{
    for (const RenderEntityPose& pose : snapshot.buildings) {
        if (!pose.is_town_center || pose.player_slot != local_player_slot) {
            continue;
        }

        if (pose.grid_x == cell.x && pose.grid_y == cell.y) {
            return true;
        }
    }

    return false;
}

void prune_dead_selection(
    std::vector<entt::entity>& selected,
    const SimRenderSnapshot& snapshot,
    const std::uint8_t local_player_slot)
{
    selected.erase(
        std::remove_if(
            selected.begin(),
            selected.end(),
            [&snapshot, local_player_slot](const entt::entity entity) {
                for (const RenderEntityPose& pose : snapshot.units) {
                    if (pose.entity != entity) {
                        continue;
                    }

                    return !is_alive_player_unit_pose(pose, local_player_slot);
                }

                return true;
            }),
        selected.end());
}

void apply_selection_from_snapshot(
    std::vector<entt::entity>& selected,
    const SimRenderSnapshot& snapshot,
    const std::vector<entt::entity>& picked,
    const sim::player::SelectionModifyMode mode,
    const std::uint8_t local_player_slot)
{
    if (mode == sim::player::SelectionModifyMode::Replace) {
        selected = picked;
        return;
    }

    for (const entt::entity entity : picked) {
        bool alive = false;
        for (const RenderEntityPose& pose : snapshot.units) {
            if (pose.entity == entity && is_alive_player_unit_pose(pose, local_player_slot)) {
                alive = true;
                break;
            }
        }

        if (!alive) {
            continue;
        }

        const auto iterator = std::find(selected.begin(), selected.end(), entity);
        if (mode == sim::player::SelectionModifyMode::Add) {
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

} // namespace aoa::render
