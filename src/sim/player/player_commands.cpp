#include "sim/player/player_commands.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "data/content_types.hpp"
#include "render/game_renderer.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/building_process.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/content_pack.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/fog_of_war.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/match_session.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/player/player_economy.hpp"
#include "sim/systems/match_outcome.hpp"
#include "sim/spawn/building_unit_spawn.hpp"
#include "sim/spawn/unit_spawn.hpp"
#include "sim/systems/gameplay_systems.hpp"
#include "sim/systems/visibility_system.hpp"
#include "sim/systems/pathfinding.hpp"

#include "math/fixed.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
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
    return systems::find_best_approach_stand_tile(map, registry, target, ignore);
}

void mark_manual_control(entt::registry& registry, const entt::entity entity)
{
    registry.get_or_emplace<components::ManualControlTag>(entity);

    if (registry.any_of<components::WorkerBrain>(entity)) {
        registry.get<components::WorkerBrain>(entity).state = components::WorkerState::Idle;
    }
}

[[nodiscard]] bool slot_meets_structure_requirement(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const std::string_view archetype_id)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null || !registry.any_of<components::MatchSession>(world)) {
        return false;
    }

    const auto& session = registry.get<components::MatchSession>(world);
    if (archetype_id == constants::MARKET_BUILDING_ID
        || archetype_id == constants::EXTRACTOR_BUILDING_ID
        || archetype_id == constants::RESERVOIR_BUILDING_ID
        || archetype_id == constants::MAGE_ACADEMY_BUILDING_ID
        || archetype_id == constants::TOWER_BUILDING_ID) {
        return components::slot_age_at_least(
            session, player_slot, constants::PlayerAge::Magic);
    }

    if (archetype_id == constants::GARDEN_BUILDING_ID) {
        return components::slot_age_at_least(
            session, player_slot, constants::PlayerAge::Technology);
    }

    if (archetype_id == constants::FARM_BUILDING_ID) {
        return components::slot_has_built_mill(session, player_slot)
            || player_has_completed_mill(registry, player_slot);
    }

    return true;
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

    if (registry.any_of<components::GarrisonedTag>(entity)) {
        return false;
    }

    return registry.get<components::Health>(entity).current.raw() > 0;
}

} // namespace

bool try_start_building_process(
    entt::registry& registry,
    const entt::entity building,
    const components::BuildingProcessKind kind,
    const int ticks)
{
    if (components::building_has_active_process(registry, building) || ticks <= 0) {
        return false;
    }

    auto& process = registry.get_or_emplace<components::BuildingProcess>(building);
    process.kind = kind;
    process.ticks_remaining = ticks;
    process.ticks_total = ticks;
    return true;
}

bool market_is_ready(entt::registry& registry, const entt::entity market)
{
    return registry.valid(market)
        && registry.all_of<components::MarketTag, components::PlayerOwnedTag>(market)
        && !registry.any_of<components::UnderConstructionTag>(market);
}

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

    const core::GridPos start = registry.any_of<components::GridPosition>(entity)
        ? registry.get<components::GridPosition>(entity).cell
        : goal;
    core::GridPos move_goal = systems::find_nearest_walkable_goal(map, registry, start, goal, entity);
    if (move_goal.x < 0) {
        return false;
    }

    math::Fixed move_goal_x = goal_world_x;
    math::Fixed move_goal_y = goal_world_y;
    bool use_goal_world = has_goal_world && move_goal == goal;
    if (use_goal_world) {
        const math::Fixed inset = math::Fixed::from_float(constants::MOVE_GOAL_TILE_EDGE_INSET);
        move_goal_x = std::max(
            math::Fixed::from_int(move_goal.x) + inset,
            std::min(math::Fixed::from_int(move_goal.x + 1) - inset, goal_world_x));
        move_goal_y = std::max(
            math::Fixed::from_int(move_goal.y) + inset,
            std::min(math::Fixed::from_int(move_goal.y + 1) - inset, goal_world_y));

        const components::WorldPosition goal_world{move_goal_x, move_goal_y};
        if (systems::is_world_position_movement_blocked(registry, map, goal_world, entity, false)) {
            use_goal_world = false;
        }
    }

    if (registry.any_of<components::AttackCooldown>(entity)
        && registry.get<components::AttackCooldown>(entity).ticks_remaining > 0) {
        return false;
    }

    mark_manual_control(registry, entity);
    registry.remove<components::AttackOrder>(entity);
    registry.remove<components::GatherTarget>(entity);
    registry.remove<components::BuildOrder>(entity);
    systems::assign_unit_path(
        registry,
        entity,
        map,
        move_goal,
        entt::null,
        true,
        use_goal_world,
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

    if (registry.any_of<components::AttackCooldown>(entity)
        && registry.get<components::AttackCooldown>(entity).ticks_remaining > 0) {
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
    const entt::entity farm = spawn::find_farm_at_cell(registry, forest_cell);
    const bool is_farm = farm != entt::null
        && !registry.any_of<components::UnderConstructionTag>(farm)
        && registry.any_of<components::FarmFood>(farm)
        && registry.get<components::FarmFood>(farm).remaining > 0;
    if (!is_forest && !is_bush && !is_gold_mine && !is_farm) {
        return false;
    }

    const bool has_resource = is_farm
        || (is_forest
            ? map.forest_wood[static_cast<std::size_t>(index)] > 0
            : (is_gold_mine
                ? (static_cast<std::size_t>(index) < map.mine_money.size()
                    && map.mine_money[static_cast<std::size_t>(index)] > 0)
                : map.bush_food[static_cast<std::size_t>(index)] > 0));
    const core::GridPos stand_tile = is_farm
        ? systems::find_best_farm_stand_tile(map, registry, farm, entity)
        : (has_resource ? find_adjacent_walkable(map, registry, forest_cell, entity) : forest_cell);
    if (stand_tile.x < 0) {
        return false;
    }

    mark_manual_control(registry, entity);
    registry.remove<components::AttackOrder>(entity);
    registry.remove<components::BuildOrder>(entity);
    auto& gather_target = registry.get_or_emplace<components::GatherTarget>(entity);
    gather_target.cell = forest_cell;
    gather_target.resource_type = tile;
    registry.get_or_emplace<components::WorkerBrain>(entity).state = components::WorkerState::MovingToResource;
    math::Fixed gather_goal_x{};
    math::Fixed gather_goal_y{};
    systems::work_stand_world_goal_for_cell(stand_tile, forest_cell, gather_goal_x, gather_goal_y);
    systems::assign_unit_path(
        registry,
        entity,
        map,
        stand_tile,
        entt::null,
        true,
        true,
        gather_goal_x,
        gather_goal_y,
        false);
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

    if (components::building_has_active_process(registry, town_center)) {
        return false;
    }

    if (!try_deduct_player_food(
            registry,
            player_slot,
            town_center_archetype->spawn_worker_food_cost)) {
        return false;
    }

    return try_start_building_process(
        registry,
        town_center,
        components::BuildingProcessKind::TrainWorker,
        constants::WORKER_TRAIN_TICKS);
}

bool issue_spawn_militia_order(entt::registry& registry, const entt::entity barracks)
{
    if (!registry.valid(barracks)) {
        return false;
    }

    if (!registry.all_of<
            components::BarracksTag,
            components::PlayerOwnedTag,
            components::GridPosition,
            components::DefinitionRef>(barracks)) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    if (registry.any_of<components::UnderConstructionTag>(barracks)) {
        return false;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, barracks);
    if (!player_can_spawn_units(registry, player_slot)) {
        return false;
    }

    const auto& content_pack = registry.get<components::ContentPack>(world);
    const auto& definition_ref = registry.get<components::DefinitionRef>(barracks);
    const auto* barracks_archetype =
        data::find_structure_archetype(content_pack.content, definition_ref.id);
    if (barracks_archetype == nullptr || barracks_archetype->spawn_militia_food_cost <= 0) {
        return false;
    }

    const int militia_food_cost = barracks_archetype->spawn_militia_food_cost;
    const int militia_money_cost = barracks_archetype->spawn_militia_money_cost > 0
        ? barracks_archetype->spawn_militia_money_cost
        : constants::MILITIA_MONEY_COST;

    if (!can_afford_player_food(registry, player_slot, militia_food_cost)
        || !can_afford_player_money(registry, player_slot, militia_money_cost)) {
        return false;
    }

    const auto* militia_archetype = data::find_unit_archetype(
        content_pack.content,
        std::string(constants::MILITIA_UNIT_ID));
    if (militia_archetype == nullptr) {
        return false;
    }

    if (components::building_has_active_process(registry, barracks)) {
        return false;
    }

    if (!try_deduct_player_food(registry, player_slot, militia_food_cost)) {
        return false;
    }

    if (!try_deduct_player_money(registry, player_slot, militia_money_cost)) {
        add_player_food(registry, player_slot, militia_food_cost);
        return false;
    }

    return try_start_building_process(
        registry,
        barracks,
        components::BuildingProcessKind::TrainMilitia,
        constants::MILITIA_TRAIN_TICKS);
}

bool issue_spawn_mage_order(entt::registry& registry, const entt::entity mage_academy)
{
    if (!registry.valid(mage_academy)) {
        return false;
    }

    if (!registry.all_of<
            components::MageAcademyTag,
            components::PlayerOwnedTag,
            components::GridPosition,
            components::DefinitionRef>(mage_academy)) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    if (registry.any_of<components::UnderConstructionTag>(mage_academy)) {
        return false;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, mage_academy);
    if (!player_can_spawn_units(registry, player_slot)) {
        return false;
    }

    const auto& content_pack = registry.get<components::ContentPack>(world);
    const auto& definition_ref = registry.get<components::DefinitionRef>(mage_academy);
    const auto* academy_archetype =
        data::find_structure_archetype(content_pack.content, definition_ref.id);
    const int mage_money_cost = academy_archetype != nullptr && academy_archetype->spawn_mage_money_cost > 0
        ? academy_archetype->spawn_mage_money_cost
        : constants::MAGE_MONEY_COST;
    const int mage_mana_cost = academy_archetype != nullptr && academy_archetype->spawn_mage_mana_cost > 0
        ? academy_archetype->spawn_mage_mana_cost
        : constants::MAGE_MANA_COST;
    if (!can_afford_player_money(registry, player_slot, mage_money_cost)
        || !can_afford_player_mana(registry, player_slot, mage_mana_cost)) {
        return false;
    }

    const auto* mage_archetype = data::find_unit_archetype(
        content_pack.content,
        std::string(constants::MAGE_UNIT_ID));
    if (mage_archetype == nullptr) {
        return false;
    }

    if (components::building_has_active_process(registry, mage_academy)) {
        return false;
    }

    if (!try_deduct_player_money(registry, player_slot, mage_money_cost)) {
        return false;
    }

    if (!try_deduct_player_mana(registry, player_slot, mage_mana_cost)) {
        add_player_money(registry, player_slot, mage_money_cost);
        return false;
    }

    return try_start_building_process(
        registry,
        mage_academy,
        components::BuildingProcessKind::TrainMage,
        constants::MAGE_TRAIN_TICKS);
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

[[nodiscard]] bool footprint_fully_explored(
    const components::FogOfWarState& fog,
    const core::GridPos anchor,
    const components::BuildingFootprint& footprint,
    const std::uint8_t player_slot)
{
    for (int y = 0; y < footprint.height; ++y) {
        for (int x = 0; x < footprint.width; ++x) {
            const core::GridPos cell{anchor.x + x, anchor.y + y};
            if (!systems::is_cell_explored_to_slot(fog, cell, player_slot)) {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] bool can_place_town_center_footprint(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos anchor,
    const components::BuildingFootprint& footprint,
    const std::vector<entt::entity>& ignore_workers,
    const std::uint8_t player_slot,
    const bool units_block_placement = true)
{
    (void)ignore_workers;
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

            const auto lake_view = registry.view<
                components::ManaLakeTag,
                components::GridPosition,
                components::BuildingFootprint>();
            for (const entt::entity lake : lake_view) {
                if (components::building_contains_cell(
                        lake_view.get<components::GridPosition>(lake),
                        lake_view.get<components::BuildingFootprint>(lake),
                        cell)) {
                    return false;
                }
            }

            if (units_block_placement && systems::is_cell_blocked_for_building(registry, cell)) {
                return false;
            }
        }
    }

    const entt::entity world = find_world_entity(registry);
    if (world != entt::null && registry.any_of<components::FogOfWarState>(world)) {
        const auto& fog = registry.get<components::FogOfWarState>(world);
        if (!footprint_fully_explored(fog, anchor, footprint, player_slot)) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] entt::entity find_free_mana_lake_for_extractor(
    entt::registry& registry,
    const core::GridPos anchor,
    const components::BuildingFootprint& footprint)
{
    const entt::entity lake = spawn::find_mana_lake_at_anchor(registry, anchor);
    if (lake == entt::null) {
        return entt::null;
    }

    const auto* lake_footprint = registry.try_get<components::BuildingFootprint>(lake);
    if (lake_footprint == nullptr || lake_footprint->width != footprint.width
        || lake_footprint->height != footprint.height) {
        return entt::null;
    }

    if (spawn::find_extractor_on_mana_lake(registry, lake) != entt::null) {
        return entt::null;
    }

    return lake;
}

[[nodiscard]] bool can_place_extractor_footprint(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos anchor,
    const components::BuildingFootprint& footprint,
    const std::vector<entt::entity>& ignore_workers,
    const std::uint8_t player_slot)
{
    (void)ignore_workers;
    const entt::entity lake = find_free_mana_lake_for_extractor(registry, anchor, footprint);
    if (lake == entt::null) {
        return false;
    }

    for (int y = 0; y < footprint.height; ++y) {
        for (int x = 0; x < footprint.width; ++x) {
            const core::GridPos cell{anchor.x + x, anchor.y + y};
            if (!core::is_inside_grid(cell, map.width, map.height)) {
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

            if (systems::is_cell_blocked_for_building(registry, cell, lake)) {
                return false;
            }
        }
    }

    const entt::entity world = find_world_entity(registry);
    if (world != entt::null && registry.any_of<components::FogOfWarState>(world)) {
        const auto& fog = registry.get<components::FogOfWarState>(world);
        if (!footprint_fully_explored(fog, anchor, footprint, player_slot)) {
            return false;
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

    eject_garrisoned_units(registry, building);

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
            else if (registry.any_of<components::LumberCampTag>(building)) {
                refund = constants::LUMBER_CAMP_BUILD_WOOD_COST;
            }
        }

        if (refund > 0) {
            add_player_wood(registry, player_slot, refund);
        }

        if (registry.any_of<components::ExtractorTag>(building)) {
            add_player_money(registry, player_slot, constants::EXTRACTOR_BUILD_MONEY_COST);
        }
    }

    health.current = math::Fixed::from_int(0);
    systems::note_entity_killed(registry, building, entt::null);
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
            if (stand_tile.x >= 0) {
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
    const int money_cost = town_center_archetype->build_money_cost > 0
        ? town_center_archetype->build_money_cost
        : constants::TOWN_CENTER_BUILD_MONEY_COST;
    const int mana_cost = town_center_archetype->build_mana_cost > 0
        ? town_center_archetype->build_mana_cost
        : constants::TOWN_CENTER_BUILD_MANA_COST;
    if (!can_afford_player_wood(registry, player_slot, build_cost)
        || !can_afford_player_money(registry, player_slot, money_cost)
        || !can_afford_player_mana(registry, player_slot, mana_cost)) {
        return false;
    }

    const components::BuildingFootprint footprint = components::effective_building_footprint(
        components::BuildingFootprint{
            std::max(1, town_center_archetype->footprint_width),
            std::max(1, town_center_archetype->footprint_height),
        },
        true);

    const auto& map = registry.get<components::MapGrid>(world);
    if (!can_place_town_center_footprint(
            map, registry, anchor_cell, footprint, valid_workers, player_slot)) {
        return false;
    }

    if (!try_deduct_player_wood(registry, player_slot, build_cost)) {
        return false;
    }

    if (!try_deduct_player_money(registry, player_slot, money_cost)) {
        add_player_wood(registry, player_slot, build_cost);
        return false;
    }

    if (!try_deduct_player_mana(registry, player_slot, mana_cost)) {
        add_player_wood(registry, player_slot, build_cost);
        add_player_money(registry, player_slot, money_cost);
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
        if (stand_tile.x >= 0) {
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
    if (!can_place_town_center_footprint(
            map, registry, anchor_cell, footprint, valid_workers, player_slot)) {
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
        if (stand_tile.x >= 0) {
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

bool issue_build_lumber_camp_order(
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
    const auto* lumber_camp_archetype = data::find_structure_archetype(
        content_pack.content,
        std::string(constants::LUMBER_CAMP_BUILDING_ID));
    if (lumber_camp_archetype == nullptr) {
        return false;
    }

    const int build_cost = lumber_camp_archetype->build_wood_cost > 0
        ? lumber_camp_archetype->build_wood_cost
        : constants::LUMBER_CAMP_BUILD_WOOD_COST;
    if (!can_afford_player_wood(registry, player_slot, build_cost)) {
        return false;
    }

    const components::BuildingFootprint footprint{
        std::max(1, lumber_camp_archetype->footprint_width),
        std::max(1, lumber_camp_archetype->footprint_height),
    };

    const auto& map = registry.get<components::MapGrid>(world);
    if (!can_place_town_center_footprint(
            map, registry, anchor_cell, footprint, valid_workers, player_slot)) {
        return false;
    }

    if (!try_deduct_player_wood(registry, player_slot, build_cost)) {
        return false;
    }

    const entt::entity building = spawn::spawn_player_lumber_camp(
        registry,
        *lumber_camp_archetype,
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
        if (stand_tile.x >= 0) {
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

bool can_build_extractor_at(
    entt::registry& registry,
    const core::GridPos anchor_cell,
    const std::uint8_t player_slot)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    const auto& content_pack = registry.get<components::ContentPack>(world);
    const auto* extractor_archetype = data::find_structure_archetype(
        content_pack.content,
        std::string(constants::EXTRACTOR_BUILDING_ID));
    if (extractor_archetype == nullptr) {
        return false;
    }

    const components::BuildingFootprint footprint{
        std::max(1, extractor_archetype->footprint_width),
        std::max(1, extractor_archetype->footprint_height),
    };

    const auto& map = registry.get<components::MapGrid>(world);
    return can_place_extractor_footprint(map, registry, anchor_cell, footprint, {}, player_slot);
}

std::optional<core::GridPos> extractor_snap_anchor(
    entt::registry& registry,
    const core::GridPos hover_cell)
{
    const components::BuildingFootprint extractor_footprint{
        constants::EXTRACTOR_FOOTPRINT_TILES,
        constants::EXTRACTOR_FOOTPRINT_TILES,
    };
    const auto lakes = registry.view<
        components::ManaLakeTag,
        components::GridPosition,
        components::BuildingFootprint>();
    for (const entt::entity lake : lakes) {
        const auto& lake_footprint = lakes.get<components::BuildingFootprint>(lake);
        if (lake_footprint.width != extractor_footprint.width
            || lake_footprint.height != extractor_footprint.height) {
            continue;
        }

        if (spawn::find_extractor_on_mana_lake(registry, lake) != entt::null) {
            continue;
        }

        const auto& lake_anchor = lakes.get<components::GridPosition>(lake);
        if (!components::building_contains_cell(lake_anchor, lake_footprint, hover_cell)) {
            continue;
        }

        return lake_anchor.cell;
    }

    return std::nullopt;
}

bool issue_build_extractor_order(
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

    if (!slot_meets_structure_requirement(
            registry, player_slot, constants::EXTRACTOR_BUILDING_ID)) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    const auto& content_pack = registry.get<components::ContentPack>(world);
    const auto* extractor_archetype = data::find_structure_archetype(
        content_pack.content,
        std::string(constants::EXTRACTOR_BUILDING_ID));
    if (extractor_archetype == nullptr) {
        return false;
    }

    const int wood_cost = extractor_archetype->build_wood_cost > 0
        ? extractor_archetype->build_wood_cost
        : constants::EXTRACTOR_BUILD_WOOD_COST;
    const int money_cost = constants::EXTRACTOR_BUILD_MONEY_COST;
    if (!can_afford_player_wood(registry, player_slot, wood_cost)
        || !can_afford_player_money(registry, player_slot, money_cost)) {
        return false;
    }

    const components::BuildingFootprint footprint{
        std::max(1, extractor_archetype->footprint_width),
        std::max(1, extractor_archetype->footprint_height),
    };

    const auto& map = registry.get<components::MapGrid>(world);
    if (!can_place_extractor_footprint(
            map, registry, anchor_cell, footprint, valid_workers, player_slot)) {
        return false;
    }

    if (!try_deduct_player_wood(registry, player_slot, wood_cost)) {
        return false;
    }

    if (!try_deduct_player_money(registry, player_slot, money_cost)) {
        add_player_wood(registry, player_slot, wood_cost);
        return false;
    }

    const entt::entity building = spawn::spawn_player_extractor(
        registry,
        *extractor_archetype,
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
        if (stand_tile.x >= 0) {
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

namespace {

void assign_workers_to_new_building(
    entt::registry& registry,
    const components::MapGrid& map,
    const std::vector<entt::entity>& valid_workers,
    const entt::entity building,
    const core::GridPos anchor_cell)
{
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
        if (stand_tile.x >= 0) {
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
}

std::vector<entt::entity> collect_build_workers(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    std::uint8_t& player_slot)
{
    std::vector<entt::entity> valid_workers{};
    valid_workers.reserve(workers.size());
    bool has_worker = false;
    for (const entt::entity entity : workers) {
        if (!is_alive_player_unit(registry, entity) || !registry.any_of<components::WorkerUnitTag>(entity)) {
            continue;
        }

        if (registry.any_of<components::GarrisonedTag>(entity)) {
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

    return valid_workers;
}

bool issue_build_simple_structure(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const core::GridPos anchor_cell,
    const std::string& archetype_id,
    const int fallback_wood,
    const int fallback_money,
    const int fallback_mana,
    const auto& spawn_fn)
{
    std::uint8_t player_slot = 0U;
    const std::vector<entt::entity> valid_workers =
        collect_build_workers(registry, workers, player_slot);
    if (valid_workers.empty()) {
        return false;
    }

    if (!slot_meets_structure_requirement(registry, player_slot, archetype_id)) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return false;
    }

    const auto& content_pack = registry.get<components::ContentPack>(world);
    const auto* archetype = data::find_structure_archetype(content_pack.content, archetype_id);
    if (archetype == nullptr) {
        return false;
    }

    const int wood_cost = archetype->build_wood_cost > 0 ? archetype->build_wood_cost : fallback_wood;
    const int money_cost = archetype->build_money_cost > 0 ? archetype->build_money_cost : fallback_money;
    const int mana_cost = archetype->build_mana_cost > 0 ? archetype->build_mana_cost : fallback_mana;
    if (!can_afford_player_wood(registry, player_slot, wood_cost)
        || !can_afford_player_money(registry, player_slot, money_cost)
        || !can_afford_player_mana(registry, player_slot, mana_cost)) {
        return false;
    }

    const components::BuildingFootprint footprint{
        std::max(1, archetype->footprint_width),
        std::max(1, archetype->footprint_height),
    };
    const auto& map = registry.get<components::MapGrid>(world);
    if (!can_place_town_center_footprint(
            map,
            registry,
            anchor_cell,
            footprint,
            valid_workers,
            player_slot,
            archetype_id != constants::FARM_BUILDING_ID)) {
        return false;
    }

    if (!try_deduct_player_wood(registry, player_slot, wood_cost)) {
        return false;
    }

    if (!try_deduct_player_money(registry, player_slot, money_cost)) {
        add_player_wood(registry, player_slot, wood_cost);
        return false;
    }

    if (!try_deduct_player_mana(registry, player_slot, mana_cost)) {
        add_player_wood(registry, player_slot, wood_cost);
        add_player_money(registry, player_slot, money_cost);
        return false;
    }

    const entt::entity building = spawn_fn(registry, *archetype, anchor_cell, player_slot, true);
    assign_workers_to_new_building(registry, map, valid_workers, building, anchor_cell);
    return true;
}

} // namespace

bool issue_build_mill_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const core::GridPos anchor_cell)
{
    return issue_build_simple_structure(
        registry,
        workers,
        anchor_cell,
        std::string(constants::MILL_BUILDING_ID),
        constants::MILL_BUILD_WOOD_COST,
        0,
        0,
        spawn::spawn_player_mill);
}

bool issue_build_mining_camp_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const core::GridPos anchor_cell)
{
    return issue_build_simple_structure(
        registry,
        workers,
        anchor_cell,
        std::string(constants::MINING_CAMP_BUILDING_ID),
        constants::MINING_CAMP_BUILD_WOOD_COST,
        0,
        0,
        spawn::spawn_player_mining_camp);
}

bool issue_build_barracks_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const core::GridPos anchor_cell)
{
    return issue_build_simple_structure(
        registry,
        workers,
        anchor_cell,
        std::string(constants::BARRACKS_BUILDING_ID),
        constants::BARRACKS_BUILD_WOOD_COST,
        0,
        0,
        spawn::spawn_player_barracks);
}

bool issue_build_mage_academy_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const core::GridPos anchor_cell)
{
    return issue_build_simple_structure(
        registry,
        workers,
        anchor_cell,
        std::string(constants::MAGE_ACADEMY_BUILDING_ID),
        constants::MAGE_ACADEMY_BUILD_WOOD_COST,
        constants::MAGE_ACADEMY_BUILD_MONEY_COST,
        constants::MAGE_ACADEMY_BUILD_MANA_COST,
        spawn::spawn_player_mage_academy);
}

bool issue_build_tower_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const core::GridPos anchor_cell)
{
    return issue_build_simple_structure(
        registry,
        workers,
        anchor_cell,
        std::string(constants::TOWER_BUILDING_ID),
        constants::TOWER_BUILD_WOOD_COST,
        constants::TOWER_BUILD_MONEY_COST,
        0,
        spawn::spawn_player_tower);
}

bool issue_build_market_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const core::GridPos anchor_cell)
{
    return issue_build_simple_structure(
        registry,
        workers,
        anchor_cell,
        std::string(constants::MARKET_BUILDING_ID),
        constants::MARKET_BUILD_WOOD_COST,
        0,
        0,
        spawn::spawn_player_market);
}

bool issue_build_garden_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const core::GridPos anchor_cell)
{
    return issue_build_simple_structure(
        registry,
        workers,
        anchor_cell,
        std::string(constants::GARDEN_BUILDING_ID),
        constants::GARDEN_BUILD_WOOD_COST,
        constants::GARDEN_BUILD_MONEY_COST,
        constants::GARDEN_BUILD_MANA_COST,
        spawn::spawn_player_garden);
}

bool issue_build_reservoir_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const core::GridPos anchor_cell)
{
    return issue_build_simple_structure(
        registry,
        workers,
        anchor_cell,
        std::string(constants::RESERVOIR_BUILDING_ID),
        constants::RESERVOIR_BUILD_WOOD_COST,
        constants::RESERVOIR_BUILD_MONEY_COST,
        0,
        spawn::spawn_player_reservoir);
}

bool issue_build_farm_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const core::GridPos anchor_cell)
{
    return issue_build_simple_structure(
        registry,
        workers,
        anchor_cell,
        std::string(constants::FARM_BUILDING_ID),
        constants::FARM_BUILD_WOOD_COST,
        0,
        0,
        spawn::spawn_player_farm);
}

bool issue_renew_farm_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    const entt::entity farm)
{
    if (!registry.valid(farm)
        || !registry.all_of<
            components::FarmTag,
            components::BuildingTag,
            components::PlayerOwnedTag,
            components::Health,
            components::FarmFood>(farm)
        || registry.any_of<components::UnderConstructionTag>(farm)) {
        return false;
    }

    if (registry.get<components::Health>(farm).current.raw() <= 0) {
        return false;
    }

    if (registry.get<components::FarmFood>(farm).remaining > 0) {
        return false;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, farm);
    const int wood_cost = constants::FARM_BUILD_WOOD_COST;
    if (!can_afford_player_wood(registry, player_slot, wood_cost)) {
        return false;
    }

    if (!try_deduct_player_wood(registry, player_slot, wood_cost)) {
        return false;
    }

    auto& health = registry.get<components::Health>(farm);
    health.current = math::Fixed::from_int(1);
    registry.get<components::FarmFood>(farm).remaining = 0;
    registry.emplace<components::UnderConstructionTag>(farm);
    return issue_resume_build_order(registry, workers, farm);
}

void eject_garrisoned_units(entt::registry& registry, const entt::entity building)
{
    if (!registry.valid(building) || !registry.any_of<components::GarrisonHold>(building)) {
        return;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    auto& hold = registry.get<components::GarrisonHold>(building);
    const auto& depot_anchor = registry.get<components::GridPosition>(building);
    const components::BuildingFootprint depot_footprint =
        registry.any_of<components::BuildingFootprint>(building)
        ? registry.get<components::BuildingFootprint>(building)
        : components::BuildingFootprint{};

    std::vector<core::GridPos> reserved_spawn_cells{};
    reserved_spawn_cells.reserve(hold.units.size());
    std::vector<entt::entity> remaining{};
    remaining.reserve(hold.units.size());

    for (const entt::entity unit : hold.units) {
        if (!registry.valid(unit) || !registry.any_of<components::GarrisonedTag>(unit)) {
            continue;
        }

        const std::optional<core::GridPos> spawn_cell = spawn::find_building_unit_spawn_cell(
            map, registry, depot_anchor, depot_footprint, reserved_spawn_cells);
        if (!spawn_cell.has_value()) {
            remaining.push_back(unit);
            continue;
        }

        reserved_spawn_cells.push_back(*spawn_cell);
        registry.remove<components::GarrisonedTag>(unit);
        registry.get<components::GridPosition>(unit).cell = *spawn_cell;
        if (registry.any_of<components::WorldPosition>(unit)) {
            registry.get<components::WorldPosition>(unit) = components::WorldPosition{
                math::tile_center_coord(spawn_cell->x),
                math::tile_center_coord(spawn_cell->y)};
        }
        if (registry.any_of<components::PreviousWorldPosition>(unit)) {
            registry.get<components::PreviousWorldPosition>(unit) = components::PreviousWorldPosition{
                math::tile_center_coord(spawn_cell->x),
                math::tile_center_coord(spawn_cell->y)};
        }
        registry.remove<components::MovePath>(unit);
        registry.remove<components::MoveSegment>(unit);
        registry.remove<components::GarrisonOrder>(unit);
    }

    hold.units = std::move(remaining);
}

bool issue_unload_garrison_order(entt::registry& registry, const entt::entity town_center)
{
    if (!registry.valid(town_center) || !registry.any_of<components::TownCenterTag>(town_center)) {
        return false;
    }

    if (registry.any_of<components::UnderConstructionTag>(town_center)) {
        return false;
    }

    eject_garrisoned_units(registry, town_center);
    return true;
}

bool issue_advance_age_order(entt::registry& registry, const entt::entity town_center)
{
    if (!registry.valid(town_center) || !registry.any_of<components::TownCenterTag>(town_center)) {
        return false;
    }

    if (registry.any_of<components::UnderConstructionTag>(town_center)) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null || !registry.any_of<components::MatchSession>(world)) {
        return false;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, town_center);
    auto& session = registry.get<components::MatchSession>(world);
    const components::AgeAdvanceCost cost = components::age_advance_cost(
        components::player_age(session, player_slot));
    if (!cost.can_advance) {
        return false;
    }

    if (!player::can_afford_player_food(registry, player_slot, cost.food)
        || !player::can_afford_player_money(registry, player_slot, cost.money)
        || !player::can_afford_player_mana(registry, player_slot, cost.mana)) {
        return false;
    }

    if (components::building_has_active_process(registry, town_center)) {
        return false;
    }

    if (cost.food > 0 && !player::try_deduct_player_food(registry, player_slot, cost.food)) {
        return false;
    }
    if (cost.money > 0 && !player::try_deduct_player_money(registry, player_slot, cost.money)) {
        if (cost.food > 0) {
            player::add_player_food(registry, player_slot, cost.food);
        }
        return false;
    }
    if (cost.mana > 0 && !player::try_deduct_player_mana(registry, player_slot, cost.mana)) {
        if (cost.food > 0) {
            player::add_player_food(registry, player_slot, cost.food);
        }
        if (cost.money > 0) {
            player::add_player_money(registry, player_slot, cost.money);
        }
        return false;
    }

    return try_start_building_process(
        registry,
        town_center,
        components::BuildingProcessKind::AdvanceAge,
        constants::TECH_ADVANCE_TICKS);
}

bool issue_research_cartography_order(entt::registry& registry, const entt::entity market)
{
    if (!market_is_ready(registry, market)) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null || !registry.any_of<components::MatchSession>(world)) {
        return false;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, market);
    auto& session = registry.get<components::MatchSession>(world);
    if (components::slot_has_cartography(session, player_slot)) {
        return false;
    }

    if (components::building_has_active_process(registry, market)) {
        return false;
    }

    if (!try_deduct_player_money(registry, player_slot, constants::CARTOGRAPHY_GOLD_COST)) {
        return false;
    }

    return try_start_building_process(
        registry,
        market,
        components::BuildingProcessKind::ResearchCartography,
        constants::TECH_ADVANCE_TICKS);
}

bool issue_research_spy_order(entt::registry& registry, const entt::entity town_center)
{
    if (!registry.valid(town_center) || !registry.any_of<components::TownCenterTag>(town_center)) {
        return false;
    }

    if (registry.any_of<components::UnderConstructionTag>(town_center)) {
        return false;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null || !registry.any_of<components::MatchSession>(world)) {
        return false;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, town_center);
    auto& session = registry.get<components::MatchSession>(world);
    if (components::slot_has_spy(session, player_slot)
        || !components::slot_age_at_least(session, player_slot, constants::PlayerAge::Spirit)) {
        return false;
    }

    if (components::building_has_active_process(registry, town_center)) {
        return false;
    }

    const int gold_cost =
        count_enemy_units(registry, player_slot) * constants::SPY_GOLD_PER_ENEMY_UNIT;
    if (gold_cost > 0 && !try_deduct_player_money(registry, player_slot, gold_cost)) {
        return false;
    }

    return try_start_building_process(
        registry,
        town_center,
        components::BuildingProcessKind::ResearchSpy,
        constants::TECH_ADVANCE_TICKS);
}

bool issue_market_sell_wood_order(entt::registry& registry, const entt::entity market)
{
    if (!market_is_ready(registry, market)) {
        return false;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, market);
    if (!try_deduct_player_wood(registry, player_slot, constants::MARKET_TRADE_RESOURCE_AMOUNT)) {
        return false;
    }

    add_player_money(registry, player_slot, constants::MARKET_SELL_GOLD_AMOUNT);
    return true;
}

bool issue_market_sell_food_order(entt::registry& registry, const entt::entity market)
{
    if (!market_is_ready(registry, market)) {
        return false;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, market);
    if (!try_deduct_player_food(registry, player_slot, constants::MARKET_TRADE_RESOURCE_AMOUNT)) {
        return false;
    }

    add_player_money(registry, player_slot, constants::MARKET_SELL_GOLD_AMOUNT);
    return true;
}

bool issue_market_buy_wood_order(entt::registry& registry, const entt::entity market)
{
    if (!market_is_ready(registry, market)) {
        return false;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, market);
    if (!try_deduct_player_money(registry, player_slot, constants::MARKET_BUY_GOLD_AMOUNT)) {
        return false;
    }

    add_player_wood(registry, player_slot, constants::MARKET_TRADE_RESOURCE_AMOUNT);
    return true;
}

bool issue_market_buy_food_order(entt::registry& registry, const entt::entity market)
{
    if (!market_is_ready(registry, market)) {
        return false;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, market);
    if (!try_deduct_player_money(registry, player_slot, constants::MARKET_BUY_GOLD_AMOUNT)) {
        return false;
    }

    add_player_food(registry, player_slot, constants::MARKET_TRADE_RESOURCE_AMOUNT);
    return true;
}

void issue_garrison_orders(
    entt::registry& registry,
    const std::vector<entt::entity>& entities,
    const entt::entity building)
{
    if (!registry.valid(building)
        || !registry.any_of<components::TownCenterTag>(building)
        || registry.any_of<components::UnderConstructionTag>(building)) {
        return;
    }

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    const std::uint8_t building_slot = components::entity_player_slot(registry, building);
    for (const entt::entity entity : entities) {
        if (!is_alive_player_unit(registry, entity)) {
            continue;
        }

        if (registry.any_of<components::GarrisonedTag>(entity)) {
            continue;
        }

        if (components::entity_player_slot(registry, entity) != building_slot) {
            continue;
        }

        mark_manual_control(registry, entity);
        registry.remove<components::AttackOrder>(entity);
        registry.remove<components::GatherTarget>(entity);
        registry.remove<components::BuildOrder>(entity);
        registry.emplace_or_replace<components::GarrisonOrder>(
            entity,
            components::GarrisonOrder{building});
        const core::GridPos stand_tile =
            systems::find_best_deposit_stand_tile(map, registry, building, entity);
        if (stand_tile.x < 0) {
            continue;
        }

        const auto& depot_grid = registry.get<components::GridPosition>(building);
        const components::BuildingFootprint depot_footprint =
            registry.any_of<components::BuildingFootprint>(building)
            ? registry.get<components::BuildingFootprint>(building)
            : components::BuildingFootprint{};
        math::Fixed goal_x{};
        math::Fixed goal_y{};
        systems::work_stand_world_goal_for_building(
            stand_tile, depot_grid, depot_footprint, goal_x, goal_y);
        issue_move_order(registry, entity, stand_tile, true, goal_x, goal_y);
        registry.emplace_or_replace<components::GarrisonOrder>(
            entity,
            components::GarrisonOrder{building});
    }
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
        const int carried_wood = registry.any_of<components::CarriedWood>(entity)
            ? registry.get<components::CarriedWood>(entity).amount
            : 0;
        const int carried_food = registry.any_of<components::CarriedFood>(entity)
            ? registry.get<components::CarriedFood>(entity).amount
            : 0;
        const int carried_money = registry.any_of<components::CarriedMoney>(entity)
            ? registry.get<components::CarriedMoney>(entity).amount
            : 0;

        const auto& worker_pos = registry.get<components::GridPosition>(entity);
        const entt::entity depot = systems::find_deposit_building(
            registry,
            player_slot,
            worker_pos.cell,
            carried_wood,
            carried_food,
            carried_money);

        if (depot == entt::null) {
            continue;
        }

        const core::GridPos stand_tile =
            systems::find_best_deposit_stand_tile(map, registry, depot, entity);
        const auto& depot_grid = registry.get<components::GridPosition>(depot);
        const components::BuildingFootprint depot_footprint =
            registry.any_of<components::BuildingFootprint>(depot)
            ? registry.get<components::BuildingFootprint>(depot)
            : components::BuildingFootprint{};
        if (components::building_contains_cell(depot_grid, depot_footprint, stand_tile)
            || is_occupied(registry, stand_tile, entity)) {
            continue;
        }

        math::Fixed deposit_goal_x{};
        math::Fixed deposit_goal_y{};
        systems::work_stand_world_goal_for_building(
            stand_tile, depot_grid, depot_footprint, deposit_goal_x, deposit_goal_y);
        issue_move_order(
            registry, entity, stand_tile, true, deposit_goal_x, deposit_goal_y);
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

    if (registry.any_of<components::GarrisonedTag>(entity)) {
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

        if (registry.any_of<components::GarrisonedTag>(entity)) {
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

        if (registry.any_of<components::GarrisonedTag>(entity)) {
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

entt::entity pick_mana_lake_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const std::uint8_t local_player_slot)
{
    const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
    if (!grid_cell.has_value()) {
        return entt::null;
    }

    const entt::entity world = find_world_entity(registry);
    const components::FogOfWarState* fog = nullptr;
    if (world != entt::null && registry.any_of<components::FogOfWarState>(world)) {
        fog = &registry.get<components::FogOfWarState>(world);
    }

    const auto view = registry.view<
        components::ManaLakeTag,
        components::GridPosition,
        components::BuildingFootprint>();
    for (const entt::entity lake : view) {
        if (!components::building_contains_cell(
                view.get<components::GridPosition>(lake),
                view.get<components::BuildingFootprint>(lake),
                *grid_cell)) {
            continue;
        }

        if (fog != nullptr
            && !systems::is_cell_explored_to_slot(*fog, *grid_cell, local_player_slot)) {
            continue;
        }

        // An extractor on the lake takes over selection, geyser style.
        if (spawn::find_extractor_on_mana_lake(registry, lake) != entt::null) {
            return entt::null;
        }

        return lake;
    }

    return entt::null;
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

        if (registry.any_of<components::GarrisonedTag>(entity)) {
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
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    std::vector<core::GridPos> reserved_goals{};
    reserved_goals.reserve(entities.size());

    const auto is_reserved = [&reserved_goals](const core::GridPos cell) {
        for (const core::GridPos reserved : reserved_goals) {
            if (reserved == cell) {
                return true;
            }
        }
        return false;
    };

    for (const entt::entity entity : entities) {
        if (!is_alive_player_unit(registry, entity)) {
            continue;
        }

        core::GridPos assigned = goal;
        bool found = false;
        for (int ring = 0; ring <= constants::MOVE_FORMATION_GOAL_MAX_RING && !found; ++ring) {
            for (int dy = -ring; dy <= ring && !found; ++dy) {
                for (int dx = -ring; dx <= ring && !found; ++dx) {
                    if (ring > 0 && std::max(std::abs(dx), std::abs(dy)) != ring) {
                        continue;
                    }

                    const core::GridPos candidate{goal.x + dx, goal.y + dy};
                    if (!core::is_inside_grid(candidate, map.width, map.height)) {
                        continue;
                    }

                    if (!systems::is_tile_walkable(map, candidate, false)) {
                        continue;
                    }

                    if (is_reserved(candidate)) {
                        continue;
                    }

                    if (is_occupied(registry, candidate, entity)) {
                        continue;
                    }

                    assigned = candidate;
                    found = true;
                }
            }
        }

        if (!found) {
            continue;
        }

        const bool use_world = has_goal_world && assigned == goal;
        if (!issue_move_order(
                registry,
                entity,
                assigned,
                use_world,
                goal_world_x,
                goal_world_y)) {
            continue;
        }

        reserved_goals.push_back(assigned);
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

void issue_cheat_oknocraft_infinity(entt::registry& registry)
{
    const auto world_view = registry.view<components::WorldTag, components::MatchSession>();
    if (world_view.begin() == world_view.end()) {
        return;
    }

    const auto& session = world_view.get<components::MatchSession>(*world_view.begin());
    if (!session.cheats_enabled) {
        return;
    }

    for (std::uint8_t slot = 0U; slot < static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS);
         ++slot) {
        add_player_wood(registry, slot, constants::CHEAT_OKNOCRAFT_INFINITY_WOOD);
        add_player_food(registry, slot, constants::CHEAT_OKNOCRAFT_INFINITY_FOOD);
        add_player_money(registry, slot, constants::CHEAT_OKNOCRAFT_INFINITY_MONEY);
        add_player_mana(registry, slot, constants::CHEAT_OKNOCRAFT_INFINITY_MANA);
    }
}

} // namespace aoa::sim::player
