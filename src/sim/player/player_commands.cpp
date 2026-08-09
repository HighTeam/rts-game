#include "sim/player/player_commands.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "data/content_types.hpp"
#include "render/game_renderer.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/content_pack.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/fog_of_war.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/player/player_economy.hpp"
#include "sim/spawn/unit_spawn.hpp"
#include "sim/systems/gameplay_systems.hpp"
#include "sim/systems/visibility_system.hpp"
#include "sim/systems/pathfinding.hpp"

#include "math/fixed.hpp"

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

// Prefer the south edge of the footprint (higher grid Y → below the building
// on the classic isometric view) so new units appear under the TC, not above.
[[nodiscard]] core::GridPos find_building_spawn_cell(
    const components::MapGrid& map,
    entt::registry& registry,
    const components::GridPosition& depot_anchor,
    const components::BuildingFootprint& depot_footprint)
{
    for (int y = depot_anchor.cell.y + depot_footprint.height; y >= depot_anchor.cell.y - 1; --y) {
        for (int x = depot_anchor.cell.x - 1; x <= depot_anchor.cell.x + depot_footprint.width; ++x) {
            if (x >= depot_anchor.cell.x && x < depot_anchor.cell.x + depot_footprint.width
                && y >= depot_anchor.cell.y && y < depot_anchor.cell.y + depot_footprint.height) {
                continue;
            }

            const core::GridPos candidate{x, y};
            if (!systems::is_tile_walkable(map, candidate, false)) {
                continue;
            }

            if (!is_occupied(registry, candidate, entt::null)) {
                return candidate;
            }
        }
    }

    return depot_anchor.cell;
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

bool issue_move_order(
    entt::registry& registry,
    const entt::entity entity,
    const core::GridPos goal,
    const bool has_goal_world,
    const math::Fixed goal_world_x,
    const math::Fixed goal_world_y)
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

    math::Fixed move_goal_x = goal_world_x;
    math::Fixed move_goal_y = goal_world_y;
    if (has_goal_world) {
        const math::Fixed inset = math::Fixed::from_float(constants::MOVE_GOAL_TILE_EDGE_INSET);
        move_goal_x = std::max(
            math::Fixed::from_int(goal.x) + inset,
            std::min(math::Fixed::from_int(goal.x + 1) - inset, goal_world_x));
        move_goal_y = std::max(
            math::Fixed::from_int(goal.y) + inset,
            std::min(math::Fixed::from_int(goal.y + 1) - inset, goal_world_y));

        const components::WorldPosition goal_world{move_goal_x, move_goal_y};
        if (systems::is_world_position_movement_blocked(registry, map, goal_world, entity, false)) {
            return false;
        }
    }

    mark_manual_control(registry, entity);
    registry.remove<components::AttackOrder>(entity);
    registry.remove<components::GatherTarget>(entity);
    registry.remove<components::BuildOrder>(entity);
    systems::assign_unit_path(
        registry,
        entity,
        map,
        goal,
        entt::null,
        true,
        has_goal_world,
        move_goal_x,
        move_goal_y);
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

    if (!registry.any_of<components::EnemyTag>(target)
        && !components::is_valid_attack_target(registry, entity, target)) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    const std::uint8_t attacker_slot = components::entity_player_slot(registry, entity);
    if (registry.any_of<components::FogOfWarState>(world)) {
        const auto& fog = registry.get<components::FogOfWarState>(world);
        if (!systems::is_opponent_entity_visible_to_slot(registry, fog, target, attacker_slot)) {
            return false;
        }
    }

    const auto& map = registry.get<components::MapGrid>(world);
    const core::GridPos target_pos = registry.get<components::GridPosition>(target).cell;
    const core::GridPos stand_tile =
        systems::find_best_melee_stand_tile(map, registry, target_pos, entity, target);
    if (stand_tile == target_pos) {
        return false;
    }

    mark_manual_control(registry, entity);
    registry.remove<components::BuildOrder>(entity);
    auto& attack_order = registry.get_or_emplace<components::AttackOrder>(entity);
    attack_order.target = target;
    attack_order.last_known_cell = target_pos;
    systems::assign_unit_path(
        registry,
        entity,
        map,
        stand_tile,
        entt::null,
        true,
        false,
        {},
        {},
        true);
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

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    if (!core::is_inside_grid(forest_cell, map.width, map.height)) {
        return false;
    }

    const int index = core::grid_index(forest_cell, map.width);
    const components::TileType tile = map.tiles[static_cast<std::size_t>(index)];
    const bool is_forest = tile == components::TileType::Forest;
    const bool is_bush =
        tile == components::TileType::Berries || tile == components::TileType::Blueberries;
    const bool is_gold_mine = tile == components::TileType::GoldMine;
    if (!is_forest && !is_bush && !is_gold_mine) {
        return false;
    }

    const bool has_resource = is_forest
        ? map.forest_wood[static_cast<std::size_t>(index)] > 0
        : (is_gold_mine
            ? (static_cast<std::size_t>(index) < map.mine_money.size()
                && map.mine_money[static_cast<std::size_t>(index)] > 0)
            : map.bush_food[static_cast<std::size_t>(index)] > 0);
    const core::GridPos stand_tile = has_resource
        ? find_adjacent_walkable(map, registry, forest_cell, entity)
        : forest_cell;

    mark_manual_control(registry, entity);
    registry.remove<components::AttackOrder>(entity);
    registry.remove<components::BuildOrder>(entity);
    auto& gather_target = registry.get_or_emplace<components::GatherTarget>(entity);
    gather_target.cell = forest_cell;
    gather_target.resource_type = tile;
    registry.get_or_emplace<components::WorkerBrain>(entity).state = components::WorkerState::MovingToResource;
    systems::assign_unit_path(registry, entity, map, stand_tile);
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

bool issue_spawn_worker_order(entt::registry& registry, const entt::entity town_center)
{
    if (!registry.valid(town_center)) {
        return false;
    }

    if (!registry.all_of<
            components::TownCenterTag,
            components::PlayerOwnedTag,
            components::Stockpile,
            components::GridPosition,
            components::DefinitionRef>(town_center)) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    if (registry.any_of<components::UnderConstructionTag>(town_center)) {
        return false;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, town_center);
    if (!player_can_spawn_units(registry, player_slot)) {
        return false;
    }

    const auto& content_pack = registry.get<components::ContentPack>(world);
    const auto& definition_ref = registry.get<components::DefinitionRef>(town_center);
    const auto* town_center_archetype =
        data::find_structure_archetype(content_pack.content, definition_ref.id);
    if (town_center_archetype == nullptr || town_center_archetype->spawn_worker_food_cost <= 0) {
        return false;
    }

    if (!can_afford_player_food(
            registry,
            player_slot,
            town_center_archetype->spawn_worker_food_cost)) {
        return false;
    }

    const auto* worker_archetype = data::find_unit_archetype(
        content_pack.content,
        std::string(constants::WORKER_UNIT_ID));
    if (worker_archetype == nullptr) {
        return false;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    const auto& depot_anchor = registry.get<components::GridPosition>(town_center);
    const components::BuildingFootprint depot_footprint = components::effective_building_footprint(
        registry.any_of<components::BuildingFootprint>(town_center)
        ? registry.get<components::BuildingFootprint>(town_center)
        : components::BuildingFootprint{},
        true);
    const core::GridPos spawn_cell =
        find_building_spawn_cell(map, registry, depot_anchor, depot_footprint);
    if (components::building_contains_cell(depot_anchor, depot_footprint, spawn_cell)
        || spawn_cell == depot_anchor.cell
        || is_occupied(registry, spawn_cell, entt::null)) {
        return false;
    }

    if (!try_deduct_player_food(
            registry,
            player_slot,
            town_center_archetype->spawn_worker_food_cost)) {
        return false;
    }

    (void)spawn::spawn_player_worker(registry, *worker_archetype, spawn_cell, player_slot);
    return true;
}

bool issue_spawn_militia_order(entt::registry& registry, const entt::entity town_center)
{
    if (!registry.valid(town_center)) {
        return false;
    }

    if (!registry.all_of<
            components::TownCenterTag,
            components::PlayerOwnedTag,
            components::Stockpile,
            components::GridPosition,
            components::DefinitionRef>(town_center)) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    if (registry.any_of<components::UnderConstructionTag>(town_center)) {
        return false;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, town_center);
    if (!player_can_spawn_units(registry, player_slot)) {
        return false;
    }

    const auto& content_pack = registry.get<components::ContentPack>(world);
    const auto& definition_ref = registry.get<components::DefinitionRef>(town_center);
    const auto* town_center_archetype =
        data::find_structure_archetype(content_pack.content, definition_ref.id);
    if (town_center_archetype == nullptr || town_center_archetype->spawn_militia_food_cost <= 0) {
        return false;
    }

    if (!can_afford_player_food(
            registry,
            player_slot,
            town_center_archetype->spawn_militia_food_cost)) {
        return false;
    }

    const auto* militia_archetype = data::find_unit_archetype(
        content_pack.content,
        std::string(constants::MILITIA_UNIT_ID));
    if (militia_archetype == nullptr) {
        return false;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    const auto& depot_anchor = registry.get<components::GridPosition>(town_center);
    const components::BuildingFootprint depot_footprint = components::effective_building_footprint(
        registry.any_of<components::BuildingFootprint>(town_center)
        ? registry.get<components::BuildingFootprint>(town_center)
        : components::BuildingFootprint{},
        true);
    const core::GridPos spawn_cell =
        find_building_spawn_cell(map, registry, depot_anchor, depot_footprint);
    if (components::building_contains_cell(depot_anchor, depot_footprint, spawn_cell)
        || spawn_cell == depot_anchor.cell
        || is_occupied(registry, spawn_cell, entt::null)) {
        return false;
    }

    if (!try_deduct_player_food(
            registry,
            player_slot,
            town_center_archetype->spawn_militia_food_cost)) {
        return false;
    }

    (void)spawn::spawn_player_militia(registry, *militia_archetype, spawn_cell, player_slot);
    return true;
}

void issue_kill_orders(entt::registry& registry, const std::vector<entt::entity>& entities)
{
    for (const entt::entity entity : entities) {
        if (!is_alive_player_unit(registry, entity)) {
            continue;
        }

        if (!registry.any_of<components::Health>(entity)) {
            continue;
        }

        registry.get<components::Health>(entity).current = math::Fixed::from_int(0);
    }
}

void issue_stop_orders(entt::registry& registry, const std::vector<entt::entity>& entities)
{
    for (const entt::entity entity : entities) {
        if (!is_alive_player_unit(registry, entity)) {
            continue;
        }

        mark_manual_control(registry, entity);
        registry.remove<components::AttackOrder>(entity);
        registry.remove<components::GatherTarget>(entity);
        registry.remove<components::BuildOrder>(entity);
        registry.remove<components::MovePath>(entity);
        registry.remove<components::MoveSegment>(entity);
    }
}

namespace {

[[nodiscard]] bool is_ignored_worker(
    const std::vector<entt::entity>& ignore_workers,
    const entt::entity entity)
{
    return std::find(ignore_workers.begin(), ignore_workers.end(), entity) != ignore_workers.end();
}

[[nodiscard]] bool can_place_town_center_footprint(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos anchor,
    const components::BuildingFootprint& footprint,
    const std::vector<entt::entity>& ignore_workers)
{
    for (int y = 0; y < footprint.height; ++y) {
        for (int x = 0; x < footprint.width; ++x) {
            const core::GridPos cell{anchor.x + x, anchor.y + y};
            if (!systems::is_tile_walkable(map, cell, false)) {
                return false;
            }

            const auto building_view = registry.view<
                components::BuildingTag,
                components::GridPosition,
                components::Health>();
            for (const entt::entity building : building_view) {
                if (building_view.get<components::Health>(building).current.raw() <= 0) {
                    continue;
                }

                components::BuildingFootprint other_footprint{};
                if (registry.any_of<components::BuildingFootprint>(building)) {
                    other_footprint = registry.get<components::BuildingFootprint>(building);
                }
                other_footprint = components::effective_building_footprint(
                    other_footprint,
                    registry.any_of<components::TownCenterTag>(building));
                if (components::building_contains_cell(
                        building_view.get<components::GridPosition>(building),
                        other_footprint,
                        cell)) {
                    return false;
                }
            }

            const auto unit_view =
                registry.view<components::UnitTag, components::GridPosition, components::Health>();
            for (const entt::entity entity : unit_view) {
                if (unit_view.get<components::Health>(entity).current.raw() <= 0) {
                    continue;
                }

                if (is_ignored_worker(ignore_workers, entity)) {
                    continue;
                }

                if (systems::unit_occupancy_grid_cell(registry, entity) == cell) {
                    return false;
                }
            }
        }
    }

    return true;
}

} // namespace

bool issue_destroy_building_order(entt::registry& registry, const entt::entity building)
{
    if (!registry.valid(building)) {
        return false;
    }

    if (!registry.all_of<components::BuildingTag, components::PlayerOwnedTag, components::Health>(
            building)) {
        return false;
    }

    auto& health = registry.get<components::Health>(building);
    if (health.current.raw() <= 0) {
        return false;
    }

    const bool unstarted_construction =
        registry.any_of<components::UnderConstructionTag>(building)
        && health.current == math::Fixed::from_int(1);
    if (unstarted_construction) {
        const std::uint8_t player_slot = components::entity_player_slot(registry, building);
        const entt::entity world = find_world_entity(registry);
        int refund = 0;
        if (world != entt::null && registry.any_of<components::DefinitionRef>(building)
            && registry.any_of<components::ContentPack>(world)) {
            const auto& content_pack = registry.get<components::ContentPack>(world);
            const auto& definition_ref = registry.get<components::DefinitionRef>(building);
            const auto* archetype =
                data::find_structure_archetype(content_pack.content, definition_ref.id);
            if (archetype != nullptr && archetype->build_wood_cost > 0) {
                refund = archetype->build_wood_cost;
            }
            else if (registry.any_of<components::TownCenterTag>(building)) {
                refund = constants::TOWN_CENTER_BUILD_WOOD_COST;
            }
            else if (registry.any_of<components::HouseTag>(building)) {
                refund = constants::HOUSE_BUILD_WOOD_COST;
            }
        }

        if (refund > 0) {
            add_player_wood(registry, player_slot, refund);
        }
    }

    health.current = math::Fixed::from_int(0);
    return true;
}

bool issue_resume_build_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const entt::entity building)
{
    if (!registry.valid(building)) {
        return false;
    }

    if (!registry.all_of<
            components::BuildingTag,
            components::PlayerOwnedTag,
            components::UnderConstructionTag,
            components::Health,
            components::GridPosition>(building)) {
        return false;
    }

    if (registry.get<components::Health>(building).current.raw() <= 0) {
        return false;
    }

    const std::uint8_t building_slot = components::entity_player_slot(registry, building);
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    const core::GridPos anchor_cell = registry.get<components::GridPosition>(building).cell;

    bool assigned = false;
    for (const entt::entity worker : workers) {
        if (!is_alive_player_unit(registry, worker)) {
            continue;
        }

        if (!registry.any_of<components::WorkerUnitTag>(worker)) {
            continue;
        }

        if (components::entity_player_slot(registry, worker) != building_slot) {
            continue;
        }

        mark_manual_control(registry, worker);
        registry.remove<components::AttackOrder>(worker);
        registry.remove<components::GatherTarget>(worker);
        registry.emplace_or_replace<components::BuildOrder>(
            worker,
            components::BuildOrder{building, 0});
        if (registry.any_of<components::WorkerBrain>(worker)) {
            registry.get<components::WorkerBrain>(worker).state = components::WorkerState::Idle;
        }

        if (!systems::unit_grid_adjacent(registry, worker, building)) {
            const core::GridPos stand_tile = systems::find_best_melee_stand_tile(
                map,
                registry,
                anchor_cell,
                worker,
                building);
            if (stand_tile != anchor_cell) {
                systems::assign_unit_path(
                    registry,
                    worker,
                    map,
                    stand_tile,
                    building,
                    true,
                    false,
                    {},
                    {},
                    false);
            }
        }

        assigned = true;
    }

    return assigned;
}

bool issue_build_town_center_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const core::GridPos anchor_cell)
{
    bool has_worker = false;
    std::uint8_t player_slot = 0U;
    std::vector<entt::entity> valid_workers{};
    valid_workers.reserve(workers.size());
    for (const entt::entity entity : workers) {
        if (!is_alive_player_unit(registry, entity)) {
            continue;
        }

        if (!registry.any_of<components::WorkerUnitTag>(entity)) {
            continue;
        }

        if (!has_worker) {
            player_slot = components::entity_player_slot(registry, entity);
            has_worker = true;
        }
        else if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        valid_workers.push_back(entity);
    }

    if (!has_worker || valid_workers.empty()) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    const auto& content_pack = registry.get<components::ContentPack>(world);
    const auto* town_center_archetype = data::find_structure_archetype(
        content_pack.content,
        std::string(constants::TOWN_CENTER_BUILDING_ID));
    if (town_center_archetype == nullptr) {
        return false;
    }

    const int build_cost = town_center_archetype->build_wood_cost > 0
        ? town_center_archetype->build_wood_cost
        : constants::TOWN_CENTER_BUILD_WOOD_COST;
    if (!can_afford_player_wood(registry, player_slot, build_cost)) {
        return false;
    }

    const components::BuildingFootprint footprint = components::effective_building_footprint(
        components::BuildingFootprint{
            std::max(1, town_center_archetype->footprint_width),
            std::max(1, town_center_archetype->footprint_height),
        },
        true);

    const auto& map = registry.get<components::MapGrid>(world);
    if (!can_place_town_center_footprint(map, registry, anchor_cell, footprint, valid_workers)) {
        return false;
    }

    if (!try_deduct_player_wood(registry, player_slot, build_cost)) {
        return false;
    }

    const entt::entity building = spawn::spawn_player_town_center(
        registry,
        *town_center_archetype,
        anchor_cell,
        player_slot,
        components::Stockpile{},
        true);

    for (const entt::entity worker : valid_workers) {
        mark_manual_control(registry, worker);
        registry.remove<components::AttackOrder>(worker);
        registry.remove<components::GatherTarget>(worker);
        registry.emplace_or_replace<components::BuildOrder>(
            worker,
            components::BuildOrder{building, 0});
        if (registry.any_of<components::WorkerBrain>(worker)) {
            registry.get<components::WorkerBrain>(worker).state = components::WorkerState::Idle;
        }

        const core::GridPos stand_tile = systems::find_best_melee_stand_tile(
            map,
            registry,
            anchor_cell,
            worker,
            building);
        if (stand_tile != anchor_cell) {
            systems::assign_unit_path(
                registry,
                worker,
                map,
                stand_tile,
                building,
                true,
                false,
                {},
                {},
                false);
        }
    }

    return true;
}

bool issue_build_house_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const core::GridPos anchor_cell)
{
    bool has_worker = false;
    std::uint8_t player_slot = 0U;
    std::vector<entt::entity> valid_workers{};
    valid_workers.reserve(workers.size());
    for (const entt::entity entity : workers) {
        if (!is_alive_player_unit(registry, entity)) {
            continue;
        }

        if (!registry.any_of<components::WorkerUnitTag>(entity)) {
            continue;
        }

        if (!has_worker) {
            player_slot = components::entity_player_slot(registry, entity);
            has_worker = true;
        }
        else if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        valid_workers.push_back(entity);
    }

    if (!has_worker || valid_workers.empty()) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    const auto& content_pack = registry.get<components::ContentPack>(world);
    const auto* house_archetype = data::find_structure_archetype(
        content_pack.content,
        std::string(constants::HOUSE_BUILDING_ID));
    if (house_archetype == nullptr) {
        return false;
    }

    const int build_cost = house_archetype->build_wood_cost > 0
        ? house_archetype->build_wood_cost
        : constants::HOUSE_BUILD_WOOD_COST;
    if (!can_afford_player_wood(registry, player_slot, build_cost)) {
        return false;
    }

    const components::BuildingFootprint footprint{
        std::max(1, house_archetype->footprint_width),
        std::max(1, house_archetype->footprint_height),
    };

    const auto& map = registry.get<components::MapGrid>(world);
    if (!can_place_town_center_footprint(map, registry, anchor_cell, footprint, valid_workers)) {
        return false;
    }

    if (!try_deduct_player_wood(registry, player_slot, build_cost)) {
        return false;
    }

    const entt::entity building = spawn::spawn_player_house(
        registry,
        *house_archetype,
        anchor_cell,
        player_slot,
        true);

    for (const entt::entity worker : valid_workers) {
        mark_manual_control(registry, worker);
        registry.remove<components::AttackOrder>(worker);
        registry.remove<components::GatherTarget>(worker);
        registry.emplace_or_replace<components::BuildOrder>(
            worker,
            components::BuildOrder{building, 0});
        if (registry.any_of<components::WorkerBrain>(worker)) {
            registry.get<components::WorkerBrain>(worker).state = components::WorkerState::Idle;
        }

        const core::GridPos stand_tile = systems::find_best_melee_stand_tile(
            map,
            registry,
            anchor_cell,
            worker,
            building);
        if (stand_tile != anchor_cell) {
            systems::assign_unit_path(
                registry,
                worker,
                map,
                stand_tile,
                building,
                true,
                false,
                {},
                {},
                false);
        }
    }

    return true;
}

void issue_deposit_orders(entt::registry& registry, const std::vector<entt::entity>& entities)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return;
    }

    const auto& map = registry.get<components::MapGrid>(world);

    for (const entt::entity entity : entities) {
        if (!is_alive_player_unit(registry, entity)) {
            continue;
        }

        if (!registry.any_of<components::WorkerUnitTag>(entity)) {
            continue;
        }

        const std::uint8_t player_slot = components::entity_player_slot(registry, entity);
        entt::entity town_center = entt::null;
        const auto town_center_view =
            registry.view<components::TownCenterTag, components::PlayerOwnedTag, components::GridPosition>();
        for (const entt::entity candidate : town_center_view) {
            if (components::entity_player_slot(registry, candidate) != player_slot) {
                continue;
            }

            town_center = candidate;
            break;
        }

        if (town_center == entt::null) {
            continue;
        }

        const core::GridPos depot_pos = registry.get<components::GridPosition>(town_center).cell;
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
    const std::uint8_t local_player_slot,
    const bool pick_opponent)
{
    entt::entity best = entt::null;
    float best_distance_sq = pick_radius_px * pick_radius_px;

    const auto view = registry.view<
        components::UnitTag,
        components::GridPosition,
        components::Health>();

    for (const entt::entity entity : view) {
        if (pick_opponent) {
            if (!components::is_opponent_entity(registry, entity, local_player_slot)) {
                continue;
            }

            const entt::entity world = find_world_entity(registry);
            if (world != entt::null && registry.any_of<components::FogOfWarState>(world)) {
                const auto& fog = registry.get<components::FogOfWarState>(world);
                if (!systems::is_opponent_entity_visible_to_slot(registry, fog, entity, local_player_slot)) {
                    continue;
                }
            }
        }
        else if (!registry.any_of<components::PlayerOwnedTag>(entity)
            || components::entity_player_slot(registry, entity) != local_player_slot) {
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
    const float pick_radius_px,
    const std::uint8_t local_player_slot)
{
    return pick_unit_at_screen_any_team(
        registry,
        renderer,
        screen_position,
        pick_radius_px,
        local_player_slot,
        false);
}

entt::entity pick_enemy_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const std::uint8_t local_player_slot)
{
    return pick_unit_at_screen_any_team(
        registry,
        renderer,
        screen_position,
        pick_radius_px,
        local_player_slot,
        true);
}

entt::entity pick_hovered_unit_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const std::uint8_t local_player_slot)
{
    entt::entity best = entt::null;
    float best_distance_sq = pick_radius_px * pick_radius_px;

    const entt::entity world = find_world_entity(registry);
    const components::FogOfWarState* fog = nullptr;
    if (world != entt::null && registry.any_of<components::FogOfWarState>(world)) {
        fog = &registry.get<components::FogOfWarState>(world);
    }

    const auto view = registry.view<
        components::UnitTag,
        components::GridPosition,
        components::Health>();

    for (const entt::entity entity : view) {
        const auto& health = view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        if (fog != nullptr
            && !systems::is_entity_visible_to_slot(registry, *fog, entity, local_player_slot)) {
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
    const float pick_radius_px,
    const std::uint8_t local_player_slot)
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
        if (components::entity_player_slot(registry, entity) != local_player_slot) {
            continue;
        }
        const auto& health = view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        const auto& anchor = view.get<components::GridPosition>(entity);
        components::BuildingFootprint footprint{};
        if (registry.any_of<components::BuildingFootprint>(entity)) {
            footprint = registry.get<components::BuildingFootprint>(entity);
        }
        footprint = components::effective_building_footprint(
            footprint,
            registry.any_of<components::TownCenterTag>(entity));
        if (!components::building_contains_cell(anchor, footprint, *grid_cell)) {
            continue;
        }

        const sf::Vector2f tile_screen = renderer.tile_center_screen(
            grid_cell->x,
            grid_cell->y,
            constants::RENDER_ENTITY_BASE_LIFT);
        const float dx = tile_screen.x - screen_position.x;
        const float dy = tile_screen.y - screen_position.y;
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

entt::entity pick_enemy_building_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
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

    const auto view = registry.view<
        components::BuildingTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::Health>();

    for (const entt::entity entity : view) {
        if (!components::is_opponent_entity(registry, entity, local_player_slot)) {
            continue;
        }

        const auto& health = view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        const entt::entity world = find_world_entity(registry);
        if (world != entt::null && registry.any_of<components::FogOfWarState>(world)) {
            const auto& fog = registry.get<components::FogOfWarState>(world);
            if (!systems::is_opponent_entity_visible_to_slot(registry, fog, entity, local_player_slot)) {
                continue;
            }
        }

        const auto& anchor = view.get<components::GridPosition>(entity);
        components::BuildingFootprint footprint{};
        if (registry.any_of<components::BuildingFootprint>(entity)) {
            footprint = registry.get<components::BuildingFootprint>(entity);
        }
        footprint = components::effective_building_footprint(
            footprint,
            registry.any_of<components::TownCenterTag>(entity));
        if (!components::building_contains_cell(anchor, footprint, *grid_cell)) {
            continue;
        }

        const sf::Vector2f tile_screen = renderer.tile_center_screen(
            grid_cell->x,
            grid_cell->y,
            constants::RENDER_ENTITY_BASE_LIFT);
        const float dx = tile_screen.x - screen_position.x;
        const float dy = tile_screen.y - screen_position.y;
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
    const sf::Vector2f rect_max,
    const std::uint8_t local_player_slot)
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
        if (components::entity_player_slot(registry, entity) != local_player_slot) {
            continue;
        }
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
    const core::GridPos goal,
    const bool has_goal_world,
    const math::Fixed goal_world_x,
    const math::Fixed goal_world_y)
{
    for (const entt::entity entity : entities) {
        issue_move_order(registry, entity, goal, has_goal_world, goal_world_x, goal_world_y);
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
    const components::TileType tile = map.tiles[static_cast<std::size_t>(index)];
    if (tile == components::TileType::Forest) {
        if (map.forest_wood[static_cast<std::size_t>(index)] <= 0) {
            return std::nullopt;
        }

        return cell;
    }

    if (tile == components::TileType::Berries || tile == components::TileType::Blueberries) {
        if (map.bush_food[static_cast<std::size_t>(index)] <= 0) {
            return std::nullopt;
        }

        return cell;
    }

    if (tile == components::TileType::GoldMine) {
        if (static_cast<std::size_t>(index) >= map.mine_money.size()
            || map.mine_money[static_cast<std::size_t>(index)] <= 0) {
            return std::nullopt;
        }

        return cell;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<core::GridPos> pick_resource_forest_known_at(
    entt::registry& registry,
    const core::GridPos cell,
    const std::uint8_t local_player_slot)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return std::nullopt;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    if (!core::is_inside_grid(cell, map.width, map.height)) {
        return std::nullopt;
    }

    if (!registry.any_of<components::FogOfWarState>(world)) {
        return pick_resource_forest_at(registry, cell);
    }

    const auto& fog = registry.get<components::FogOfWarState>(world);
    if (!systems::is_cell_explored_to_slot(fog, cell, local_player_slot)) {
        return std::nullopt;
    }

    if (systems::is_cell_visible_to_slot(fog, cell, local_player_slot)) {
        return pick_resource_forest_at(registry, cell);
    }

    const components::TileType memory_tile =
        components::fog_memory_tile_type(fog, map, cell.x, cell.y, local_player_slot);
    if (memory_tile == components::TileType::Forest) {
        if (components::fog_memory_forest_wood(fog, map, cell.x, cell.y, local_player_slot) <= 0) {
            return std::nullopt;
        }

        return cell;
    }

    if (memory_tile == components::TileType::Berries
        || memory_tile == components::TileType::Blueberries) {
        if (components::fog_memory_bush_food(fog, map, cell.x, cell.y, local_player_slot) <= 0) {
            return std::nullopt;
        }

        return cell;
    }

    if (memory_tile == components::TileType::GoldMine) {
        if (components::fog_memory_mine_money(fog, map, cell.x, cell.y, local_player_slot) <= 0) {
            return std::nullopt;
        }

        return cell;
    }

    return std::nullopt;
}

std::optional<core::GridPos> pick_resource_forest_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const std::uint8_t local_player_slot)
{
    const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
    if (!grid_cell.has_value()) {
        return std::nullopt;
    }

    if (pick_resource_forest_known_at(registry, *grid_cell, local_player_slot).has_value()) {
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

    return pick_resource_forest_known_at(registry, *grid_cell, local_player_slot);
}

} // namespace aoa::sim::player
