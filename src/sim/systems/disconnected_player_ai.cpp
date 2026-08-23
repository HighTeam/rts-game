#include "sim/systems/disconnected_player_ai.hpp"

#include "core/constants.hpp"
#include "data/content_types.hpp"
#include "math/fixed.hpp"
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
#include "sim/components/player_slot.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/player/player_command.hpp"
#include "sim/player/player_economy.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"
#include "sim/spawn/unit_spawn.hpp"
#include "sim/systems/pathfinding.hpp"
#include "sim/systems/visibility_system.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <vector>

namespace aoa::sim::systems {

namespace {

enum class GatherNeed : std::uint8_t {
    Food = 0,
    Wood = 1,
    Gold = 2,
};

entt::entity find_world_entity(entt::registry& registry)
{
    const auto view = registry.view<components::WorldTag>();
    if (view.begin() == view.end()) {
        return entt::null;
    }

    return *view.begin();
}

bool is_alive_player_unit_for_slot(
    entt::registry& registry,
    const entt::entity entity,
    const std::uint8_t player_slot)
{
    if (!registry.valid(entity)) {
        return false;
    }

    if (!registry.all_of<
            components::UnitTag,
            components::PlayerOwnedTag,
            components::GridPosition,
            components::Health>(entity)) {
        return false;
    }

    if (components::entity_player_slot(registry, entity) != player_slot) {
        return false;
    }

    return registry.get<components::Health>(entity).current.raw() > 0;
}

core::GridPos find_nearest_resource_tile(
    const components::MapGrid& map,
    const core::GridPos from,
    const components::TileType preferred_type,
    const components::FogOfWarState* fog,
    const std::uint8_t player_slot)
{
    const auto tile_matches = [&](const int x, const int y) {
        if (!core::is_inside_grid({x, y}, map.width, map.height)) {
            return false;
        }

        if (fog != nullptr
            && !is_cell_explored_to_slot(*fog, {x, y}, player_slot)) {
            return false;
        }

        const int index = core::grid_index({x, y}, map.width);
        if (preferred_type == components::TileType::Forest) {
            return map.forest_wood[static_cast<std::size_t>(index)] > 0;
        }

        if (preferred_type == components::TileType::Berries
            || preferred_type == components::TileType::Blueberries) {
            return map.bush_food[static_cast<std::size_t>(index)] > 0
                && (map.tiles[static_cast<std::size_t>(index)] == preferred_type
                    || preferred_type == components::TileType::Berries);
        }

        if (preferred_type == components::TileType::GoldMine) {
            return static_cast<std::size_t>(index) < map.mine_money.size()
                && map.mine_money[static_cast<std::size_t>(index)] > 0;
        }

        const bool has_wood = map.forest_wood[static_cast<std::size_t>(index)] > 0;
        const bool has_food = map.bush_food[static_cast<std::size_t>(index)] > 0;
        const bool has_money = static_cast<std::size_t>(index) < map.mine_money.size()
            && map.mine_money[static_cast<std::size_t>(index)] > 0;
        return has_wood || has_food || has_money;
    };

    const int max_radius = map.width + map.height;
    for (int radius = 0; radius <= max_radius; ++radius) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int dy = radius - std::abs(dx);
            const int x = from.x + dx;
            if (tile_matches(x, from.y + dy)) {
                return {x, from.y + dy};
            }

            if (dy != 0 && tile_matches(x, from.y - dy)) {
                return {x, from.y - dy};
            }
        }
    }

    return {-1, -1};
}

[[nodiscard]] bool gather_target_still_has_resource(
    const components::MapGrid& map,
    entt::registry& registry,
    const components::GatherTarget& gather_target)
{
    if (!core::is_inside_grid(gather_target.cell, map.width, map.height)) {
        return false;
    }

    const int index = core::grid_index(gather_target.cell, map.width);
    if (gather_target.resource_type == components::TileType::Forest) {
        return map.forest_wood[static_cast<std::size_t>(index)] > 0;
    }

    if (gather_target.resource_type == components::TileType::Berries
        || gather_target.resource_type == components::TileType::Blueberries) {
        return map.bush_food[static_cast<std::size_t>(index)] > 0;
    }

    if (gather_target.resource_type == components::TileType::GoldMine) {
        return static_cast<std::size_t>(index) < map.mine_money.size()
            && map.mine_money[static_cast<std::size_t>(index)] > 0;
    }

    const entt::entity farm = spawn::find_farm_at_cell(registry, gather_target.cell);
    return farm != entt::null
        && !registry.any_of<components::UnderConstructionTag>(farm)
        && registry.any_of<components::FarmFood>(farm)
        && registry.get<components::FarmFood>(farm).remaining > 0;
}

[[nodiscard]] bool worker_is_gathering_food(
    entt::registry& registry,
    const components::MapGrid& map,
    const entt::entity worker)
{
    if (!registry.any_of<components::GatherTarget>(worker)) {
        return false;
    }

    const auto& gather_target = registry.get<components::GatherTarget>(worker);
    if (gather_target.resource_type == components::TileType::Berries
        || gather_target.resource_type == components::TileType::Blueberries) {
        return gather_target_still_has_resource(map, registry, gather_target);
    }

    const entt::entity farm = spawn::find_farm_at_cell(registry, gather_target.cell);
    return farm != entt::null
        && registry.any_of<components::FarmFood>(farm)
        && registry.get<components::FarmFood>(farm).remaining > 0;
}

entt::entity find_town_center_for_slot(entt::registry& registry, const std::uint8_t player_slot)
{
    const auto view = registry.view<
        components::TownCenterTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::Health>();
    for (const entt::entity entity : view) {
        if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        if (registry.any_of<components::UnderConstructionTag>(entity)) {
            continue;
        }

        if (view.get<components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        return entity;
    }

    return entt::null;
}

template <typename Tag>
[[nodiscard]] int count_owned_buildings(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const bool include_construction)
{
    int count = 0;
    const auto view = registry.view<Tag, components::PlayerOwnedTag, components::Health>();
    for (const entt::entity entity : view) {
        if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        if (view.get<components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        if (!include_construction && registry.any_of<components::UnderConstructionTag>(entity)) {
            continue;
        }

        ++count;
    }

    return count;
}

template <typename Tag>
[[nodiscard]] entt::entity find_owned_completed_building(
    entt::registry& registry,
    const std::uint8_t player_slot)
{
    const auto view = registry.view<Tag, components::PlayerOwnedTag, components::Health>();
    for (const entt::entity entity : view) {
        if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        if (registry.any_of<components::UnderConstructionTag>(entity)) {
            continue;
        }

        if (view.get<components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        return entity;
    }

    return entt::null;
}

[[nodiscard]] bool is_visible_opponent(
    entt::registry& registry,
    const components::FogOfWarState* fog,
    const entt::entity candidate,
    const std::uint8_t player_slot)
{
    if (!components::is_opponent_entity(registry, candidate, player_slot)) {
        return false;
    }

    if (!registry.any_of<components::Health>(candidate)
        || registry.get<components::Health>(candidate).current.raw() <= 0) {
        return false;
    }

    if (fog == nullptr) {
        return true;
    }

    return is_opponent_entity_visible_to_slot(registry, *fog, candidate, player_slot);
}

entt::entity find_nearest_visible_opponent_target(
    entt::registry& registry,
    const core::GridPos from,
    const std::uint8_t player_slot,
    const components::FogOfWarState* fog)
{
    entt::entity best = entt::null;
    int best_distance = std::numeric_limits<int>::max();

    const auto consider = [&](const entt::entity candidate, const core::GridPos pos) {
        if (!is_visible_opponent(registry, fog, candidate, player_slot)) {
            return;
        }

        const int distance = core::chebyshev_distance(from, pos);
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    };

    const auto unit_view =
        registry.view<components::UnitTag, components::GridPosition, components::Health>();
    for (const entt::entity candidate : unit_view) {
        consider(candidate, unit_view.get<components::GridPosition>(candidate).cell);
    }

    const auto building_view =
        registry.view<components::BuildingTag, components::GridPosition, components::Health>();
    for (const entt::entity candidate : building_view) {
        consider(candidate, building_view.get<components::GridPosition>(candidate).cell);
    }

    return best;
}

[[nodiscard]] int count_visible_enemy_units(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const components::FogOfWarState* fog)
{
    int count = 0;
    const auto unit_view =
        registry.view<components::UnitTag, components::GridPosition, components::Health>();
    for (const entt::entity candidate : unit_view) {
        if (is_visible_opponent(registry, fog, candidate, player_slot)) {
            ++count;
        }
    }

    return count;
}

[[nodiscard]] bool unit_has_live_attack_target(entt::registry& registry, const entt::entity unit)
{
    if (!registry.any_of<components::AttackOrder>(unit)) {
        return false;
    }

    const entt::entity current_target = registry.get<components::AttackOrder>(unit).target;
    return current_target != entt::null && registry.valid(current_target)
        && registry.any_of<components::Health>(current_target)
        && registry.get<components::Health>(current_target).current.raw() > 0;
}

[[nodiscard]] bool can_place_footprint(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos anchor,
    const int footprint)
{
    for (int y = 0; y < footprint; ++y) {
        for (int x = 0; x < footprint; ++x) {
            const core::GridPos cell{anchor.x + x, anchor.y + y};
            if (!is_tile_walkable(map, cell, false)) {
                return false;
            }

            if (is_cell_blocked_for_building(registry, cell)) {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] bool site_too_close_to_buildings(
    entt::registry& registry,
    const core::GridPos site_anchor,
    const int site_footprint,
    const int min_separation)
{
    const auto building_view = registry.view<
        components::BuildingTag,
        components::GridPosition,
        components::Health>();
    for (const entt::entity building : building_view) {
        if (building_view.get<components::Health>(building).current.raw() <= 0) {
            continue;
        }

        components::BuildingFootprint footprint{};
        if (registry.any_of<components::BuildingFootprint>(building)) {
            footprint = registry.get<components::BuildingFootprint>(building);
        }
        footprint = components::effective_building_footprint(
            footprint, registry.any_of<components::TownCenterTag>(building));
        const auto& grid = building_view.get<components::GridPosition>(building);
        for (int y = 0; y < site_footprint; ++y) {
            for (int x = 0; x < site_footprint; ++x) {
                const core::GridPos cell{site_anchor.x + x, site_anchor.y + y};
                if (components::chebyshev_distance_to_footprint(cell, grid, footprint)
                    < min_separation) {
                    return true;
                }
            }
        }
    }

    return false;
}

[[nodiscard]] bool footprint_has_walkable_stand(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos anchor,
    const int footprint)
{
    const components::GridPosition grid_anchor{anchor};
    const components::BuildingFootprint building_footprint{footprint, footprint};
    for (int y = anchor.y - 1; y <= anchor.y + footprint; ++y) {
        for (int x = anchor.x - 1; x <= anchor.x + footprint; ++x) {
            const core::GridPos candidate{x, y};
            if (components::building_contains_cell(grid_anchor, building_footprint, candidate)) {
                continue;
            }

            if (components::chebyshev_distance_to_footprint(
                    candidate, grid_anchor, building_footprint)
                != 1) {
                continue;
            }

            if (!is_tile_walkable(map, candidate, false)) {
                continue;
            }

            if (is_movement_blocked(registry, candidate)) {
                continue;
            }

            return true;
        }
    }

    return false;
}

[[nodiscard]] std::optional<core::GridPos> find_build_site_near(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos near_cell,
    const int near_width,
    const int near_height,
    const int footprint,
    const int min_separation,
    const int search_radius)
{
    core::GridPos best{-1, -1};
    int best_distance = std::numeric_limits<int>::max();

    for (int dy = -search_radius; dy <= search_radius + near_height; ++dy) {
        for (int dx = -search_radius; dx <= search_radius + near_width; ++dx) {
            const core::GridPos anchor{near_cell.x + dx, near_cell.y + dy};
            if (!can_place_footprint(map, registry, anchor, footprint)) {
                continue;
            }

            if (site_too_close_to_buildings(registry, anchor, footprint, min_separation)) {
                continue;
            }

            if (!footprint_has_walkable_stand(map, registry, anchor, footprint)) {
                continue;
            }

            const int distance = std::abs(dx) + std::abs(dy);
            if (distance < best_distance) {
                best_distance = distance;
                best = anchor;
            }
        }
    }

    if (best.x < 0) {
        return std::nullopt;
    }

    return best;
}

[[nodiscard]] std::optional<core::GridPos> find_free_mana_lake_anchor(
    entt::registry& registry,
    const core::GridPos from,
    const components::FogOfWarState* fog,
    const std::uint8_t player_slot)
{
    const auto lakes = registry.view<
        components::ManaLakeTag,
        components::GridPosition,
        components::BuildingFootprint>();
    std::optional<core::GridPos> best{};
    int best_distance = std::numeric_limits<int>::max();
    for (const entt::entity lake : lakes) {
        if (spawn::find_extractor_on_mana_lake(registry, lake) != entt::null) {
            continue;
        }

        const core::GridPos lake_cell = lakes.get<components::GridPosition>(lake).cell;
        if (fog != nullptr && !is_cell_explored_to_slot(*fog, lake_cell, player_slot)) {
            continue;
        }

        const auto& lake_footprint = lakes.get<components::BuildingFootprint>(lake);
        if (lake_footprint.width != constants::EXTRACTOR_FOOTPRINT_TILES
            || lake_footprint.height != constants::EXTRACTOR_FOOTPRINT_TILES) {
            continue;
        }

        const core::GridPos anchor = lake_cell;
        const int distance = core::chebyshev_distance(from, anchor);
        if (distance < best_distance) {
            best_distance = distance;
            best = anchor;
        }
    }

    return best;
}

[[nodiscard]] int count_natural_food_near(
    const components::MapGrid& map,
    const core::GridPos from,
    const int radius,
    const components::FogOfWarState* fog,
    const std::uint8_t player_slot)
{
    int remaining = 0;
    for (int y = from.y - radius; y <= from.y + radius; ++y) {
        for (int x = from.x - radius; x <= from.x + radius; ++x) {
            const core::GridPos cell{x, y};
            if (!core::is_inside_grid(cell, map.width, map.height)) {
                continue;
            }

            if (fog != nullptr && !is_cell_explored_to_slot(*fog, cell, player_slot)) {
                continue;
            }

            const int index = core::grid_index(cell, map.width);
            const auto tile = map.tiles[static_cast<std::size_t>(index)];
            if (tile != components::TileType::Berries
                && tile != components::TileType::Blueberries) {
                continue;
            }

            remaining += map.bush_food[static_cast<std::size_t>(index)];
        }
    }

    return remaining;
}

[[nodiscard]] core::GridPos find_nearest_farm_food(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const core::GridPos from)
{
    const auto farm_view = registry.view<
        components::FarmTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::Health,
        components::FarmFood>();
    core::GridPos best{-1, -1};
    int best_distance = std::numeric_limits<int>::max();
    for (const entt::entity farm : farm_view) {
        if (components::entity_player_slot(registry, farm) != player_slot) {
            continue;
        }

        if (registry.any_of<components::UnderConstructionTag>(farm)) {
            continue;
        }

        if (farm_view.get<components::Health>(farm).current.raw() <= 0) {
            continue;
        }

        if (farm_view.get<components::FarmFood>(farm).remaining <= 0) {
            continue;
        }

        const core::GridPos cell = farm_view.get<components::GridPosition>(farm).cell;
        const int distance = core::chebyshev_distance(from, cell);
        if (distance < best_distance) {
            best_distance = distance;
            best = cell;
        }
    }

    return best;
}

core::GridPos scout_waypoint(
    const components::MapGrid& map,
    const core::GridPos from,
    const std::uint64_t execute_tick,
    const std::uint8_t player_slot,
    const components::FogOfWarState* fog)
{
    if (fog != nullptr) {
        const int max_radius = map.width + map.height;
        for (int radius = 1; radius <= max_radius; ++radius) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const int dy = radius - std::abs(dx);
                const core::GridPos a{from.x + dx, from.y + dy};
                if (core::is_inside_grid(a, map.width, map.height)
                    && is_tile_walkable(map, a, false)
                    && !is_cell_explored_to_slot(*fog, a, player_slot)) {
                    return a;
                }

                if (dy == 0) {
                    continue;
                }

                const core::GridPos b{from.x + dx, from.y - dy};
                if (core::is_inside_grid(b, map.width, map.height)
                    && is_tile_walkable(map, b, false)
                    && !is_cell_explored_to_slot(*fog, b, player_slot)) {
                    return b;
                }
            }
        }
    }

    const std::size_t corner_index = static_cast<std::size_t>(
        (execute_tick / static_cast<std::uint64_t>(constants::AI_SCOUT_WAYPOINT_PERIOD_TICKS))
        + player_slot)
        % static_cast<std::size_t>(constants::AI_SCOUT_WAYPOINT_CORNER_COUNT);
    const int inset = constants::AI_SCOUT_WAYPOINT_INSET;
    core::GridPos waypoint{inset, inset};
    if (corner_index == 1U) {
        waypoint = {map.width - inset - 1, inset};
    }
    else if (corner_index == 2U) {
        waypoint = {map.width - inset - 1, map.height - inset - 1};
    }
    else if (corner_index == 3U) {
        waypoint = {inset, map.height - inset - 1};
    }

    waypoint.x = std::clamp(waypoint.x, 0, map.width - 1);
    waypoint.y = std::clamp(waypoint.y, 0, map.height - 1);
    return waypoint;
}

player::PlayerCommand make_command(
    const player::PlayerCommandType type,
    const std::uint8_t player_slot,
    const std::uint64_t execute_tick,
    std::uint64_t& next_sequence)
{
    player::PlayerCommand command{};
    command.sequence = next_sequence++;
    command.execute_tick = execute_tick;
    command.player_slot = player_slot;
    command.type = type;
    return command;
}

[[nodiscard]] bool worker_is_idle(
    entt::registry& registry,
    const components::MapGrid& map,
    const entt::entity worker)
{
    if (registry.any_of<components::MoveSegment>(worker)
        || registry.any_of<components::BuildOrder>(worker)) {
        return false;
    }

    if (!registry.any_of<components::GatherTarget>(worker)) {
        return true;
    }

    return !gather_target_still_has_resource(
        map, registry, registry.get<components::GatherTarget>(worker));
}

[[nodiscard]] int builder_priority(
    entt::registry& registry,
    const components::MapGrid& map,
    const entt::entity worker)
{
    if (registry.any_of<components::BuildOrder>(worker)) {
        return 1;
    }

    if (worker_is_idle(registry, map, worker)) {
        return 0;
    }

    if (!registry.any_of<components::GatherTarget>(worker)) {
        return 0;
    }

    const auto type = registry.get<components::GatherTarget>(worker).resource_type;
    if (type == components::TileType::Forest) {
        return 2;
    }

    if (type == components::TileType::GoldMine) {
        return 3;
    }

    return 4;
}

entt::entity pick_builder(
    entt::registry& registry,
    const components::MapGrid& map,
    const std::vector<entt::entity>& workers,
    const bool allow_busy_builders)
{
    entt::entity best = entt::null;
    int best_priority = std::numeric_limits<int>::max();
    for (const entt::entity worker : workers) {
        if (!allow_busy_builders && registry.any_of<components::BuildOrder>(worker)) {
            continue;
        }

        const int priority = builder_priority(registry, map, worker);
        if (priority < best_priority) {
            best_priority = priority;
            best = worker;
        }
    }

    return best;
}

core::GridPos find_food_target(
    entt::registry& registry,
    const components::MapGrid& map,
    const std::uint8_t player_slot,
    const core::GridPos from,
    const bool prefer_natural,
    const components::FogOfWarState* fog)
{
    if (prefer_natural) {
        const core::GridPos bushes = find_nearest_resource_tile(
            map, from, components::TileType::Berries, fog, player_slot);
        if (bushes.x >= 0) {
            return bushes;
        }
    }

    const core::GridPos farm = find_nearest_farm_food(registry, player_slot, from);
    if (farm.x >= 0) {
        return farm;
    }

    return find_nearest_resource_tile(
        map, from, components::TileType::Berries, fog, player_slot);
}

void push_gather(
    std::vector<player::PlayerCommand>& commands,
    const std::uint8_t player_slot,
    const std::uint64_t execute_tick,
    std::uint64_t& next_sequence,
    const entt::entity worker,
    const core::GridPos cell)
{
    player::PlayerCommand command =
        make_command(player::PlayerCommandType::Gather, player_slot, execute_tick, next_sequence);
    command.unit_ids = {worker};
    command.cell = cell;
    commands.push_back(std::move(command));
}

void push_build(
    std::vector<player::PlayerCommand>& commands,
    const player::PlayerCommandType type,
    const std::uint8_t player_slot,
    const std::uint64_t execute_tick,
    std::uint64_t& next_sequence,
    const entt::entity builder,
    const core::GridPos cell)
{
    player::PlayerCommand command = make_command(type, player_slot, execute_tick, next_sequence);
    command.unit_ids = {builder};
    command.cell = cell;
    commands.push_back(std::move(command));
}

} // namespace

std::vector<player::PlayerCommand> generate_ai_commands_for_slot(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const std::uint64_t execute_tick,
    std::uint64_t& next_sequence)
{
    std::vector<player::PlayerCommand> commands{};

    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return commands;
    }

    if (registry.any_of<components::MatchSession>(world)
        && components::player_is_eliminated(
            registry.get<components::MatchSession>(world), player_slot)) {
        return commands;
    }

    const auto& map = registry.get<components::MapGrid>(world);
    const MovementQueryCacheGuard occupancy_cache{registry, map};
    const components::FogOfWarState* fog = nullptr;
    if (registry.any_of<components::FogOfWarState>(world)) {
        fog = &registry.get<components::FogOfWarState>(world);
    }
    const entt::entity town_center = find_town_center_for_slot(registry, player_slot);
    if (town_center == entt::null) {
        return commands;
    }

    const components::Stockpile stockpile = player::sum_player_stockpile(registry, player_slot);
    const constants::PlayerAge age = registry.any_of<components::MatchSession>(world)
        ? components::player_age(registry.get<components::MatchSession>(world), player_slot)
        : constants::PlayerAge::Human;
    const components::AgeAdvanceCost age_cost = components::age_advance_cost(age);
    const bool elemental_or_later = age != constants::PlayerAge::Human;

    const int completed_houses = player::count_completed_houses(registry, player_slot);
    const int total_houses =
        count_owned_buildings<components::HouseTag>(registry, player_slot, true);
    const int constructing_houses = total_houses - completed_houses;
    const int mills = count_owned_buildings<components::MillTag>(registry, player_slot, true);
    const int farms = count_owned_buildings<components::FarmTag>(registry, player_slot, true);
    const int barracks_count =
        count_owned_buildings<components::BarracksTag>(registry, player_slot, true);
    const int extractors =
        count_owned_buildings<components::ExtractorTag>(registry, player_slot, true);
    const int reservoirs =
        count_owned_buildings<components::ReservoirTag>(registry, player_slot, true);
    const int academies =
        count_owned_buildings<components::MageAcademyTag>(registry, player_slot, true);
    const int lumber_camps =
        count_owned_buildings<components::LumberCampTag>(registry, player_slot, true);
    const int mining_camps =
        count_owned_buildings<components::MiningCampTag>(registry, player_slot, true);

    const auto& tc_anchor = registry.get<components::GridPosition>(town_center);
    components::BuildingFootprint tc_footprint{};
    if (registry.any_of<components::BuildingFootprint>(town_center)) {
        tc_footprint = registry.get<components::BuildingFootprint>(town_center);
    }
    tc_footprint = components::effective_building_footprint(tc_footprint, true);

    std::vector<entt::entity> workers{};
    const auto worker_view = registry.view<
        components::WorkerUnitTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::Health>();
    for (const entt::entity entity : worker_view) {
        if (is_alive_player_unit_for_slot(registry, entity, player_slot)) {
            workers.push_back(entity);
        }
    }
    std::sort(workers.begin(), workers.end(), [&registry](const entt::entity left, const entt::entity right) {
        return snapshot::compare_entities_for_deterministic_iteration(registry, left, right);
    });

    std::vector<entt::entity> militias{};
    const auto militia_view = registry.view<
        components::MilitiaUnitTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::Health>();
    for (const entt::entity entity : militia_view) {
        if (is_alive_player_unit_for_slot(registry, entity, player_slot)) {
            militias.push_back(entity);
        }
    }
    std::sort(militias.begin(), militias.end(), [&registry](const entt::entity left, const entt::entity right) {
        return snapshot::compare_entities_for_deterministic_iteration(registry, left, right);
    });

    std::vector<entt::entity> mages{};
    const auto mage_view = registry.view<
        components::MageUnitTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::Health>();
    for (const entt::entity entity : mage_view) {
        if (is_alive_player_unit_for_slot(registry, entity, player_slot)) {
            mages.push_back(entity);
        }
    }

    int wood_workers = 0;
    int food_workers = 0;
    int gold_workers = 0;
    for (const entt::entity worker : workers) {
        if (registry.any_of<components::BuildOrder>(worker)) {
            continue;
        }

        if (worker_is_gathering_food(registry, map, worker)) {
            ++food_workers;
            continue;
        }

        if (!registry.any_of<components::GatherTarget>(worker)) {
            continue;
        }

        const auto type = registry.get<components::GatherTarget>(worker).resource_type;
        if (type == components::TileType::Forest) {
            ++wood_workers;
        }
        else if (type == components::TileType::GoldMine) {
            ++gold_workers;
        }
    }

    const int worker_count = static_cast<int>(workers.size());
    const int military_count = static_cast<int>(militias.size() + mages.size());
    const int unit_count = player::count_player_units(registry, player_slot);
    const int pop_cap = player::player_civil_cap_max(registry, player_slot);
    const int natural_food = count_natural_food_near(
        map,
        tc_anchor.cell,
        constants::AI_NATURAL_FOOD_SEARCH_RADIUS,
        fog,
        player_slot);
    const bool natural_food_low = natural_food <= constants::AI_NATURAL_FOOD_LOW_AMOUNT;
    const core::GridPos nearest_berries = find_nearest_resource_tile(
        map, tc_anchor.cell, components::TileType::Berries, fog, player_slot);
    const bool berries_available = nearest_berries.x >= 0;
    const bool need_farm_food = natural_food_low || !berries_available;
    const bool preparing_age = worker_count >= constants::AI_AGE_PREP_WORKER_MIN
        && age_cost.can_advance;
    const bool ratio_phase = elemental_or_later;
    const int desired_workers = ratio_phase
        ? std::max(
              constants::AI_AGE_PREP_WORKER_MIN,
              (unit_count * constants::AI_ECONOMY_RATIO_PERCENT)
                  / constants::AI_RATIO_PERCENT_BASE)
        : constants::AI_AGE_PREP_WORKER_MAX;
    const int desired_military = ratio_phase
        ? std::max(
              constants::AI_SCOUT_COUNT,
              (unit_count * constants::AI_MILITARY_RATIO_PERCENT)
                  / constants::AI_RATIO_PERCENT_BASE)
        : constants::AI_SCOUT_COUNT;

    const int projected_cap = pop_cap
        + constructing_houses * constants::CIVIL_POPULATION_CAP_PER_HOUSE;
    int map_cap = constants::CIVIL_POPULATION_CAP_MAX;
    if (registry.any_of<components::MatchSession>(world)) {
        const int session_cap =
            registry.get<components::MatchSession>(world).civil_population_map_cap;
        if (session_cap > 0) {
            map_cap = std::min(map_cap, session_cap);
        }
    }
    const bool house_needed = projected_cap < map_cap
        && (projected_cap - unit_count) <= constants::AI_POP_HEADROOM_SLOTS;
    const bool wood_urgent = house_needed && stockpile.wood < constants::HOUSE_BUILD_WOOD_COST;

    bool issued_build = false;
    const auto try_issue_build = [&](
                                     const player::PlayerCommandType type,
                                     const int footprint,
                                     const int min_separation,
                                     const int wood_cost,
                                     const int money_cost,
                                     const int mana_cost,
                                     const bool allow_busy_builders) {
        if (issued_build || workers.empty()) {
            return;
        }

        if (stockpile.wood < wood_cost || stockpile.money < money_cost
            || stockpile.mana < mana_cost) {
            return;
        }

        const std::optional<core::GridPos> site = find_build_site_near(
            map,
            registry,
            tc_anchor.cell,
            tc_footprint.width,
            tc_footprint.height,
            footprint,
            std::max(min_separation, constants::AI_BUILDING_MIN_SEPARATION_TILES),
            constants::AI_BUILD_SEARCH_RADIUS_TILES);
        if (!site.has_value()) {
            return;
        }

        const entt::entity builder =
            pick_builder(registry, map, workers, allow_busy_builders);
        if (builder == entt::null) {
            return;
        }

        push_build(commands, type, player_slot, execute_tick, next_sequence, builder, *site);
        issued_build = true;
    };

    const auto try_issue_camp_near_resource = [&](
                                                  const player::PlayerCommandType type,
                                                  const int footprint,
                                                  const int wood_cost,
                                                  const components::TileType resource_type) {
        if (issued_build || workers.empty() || stockpile.wood < wood_cost) {
            return;
        }

        const core::GridPos resource = find_nearest_resource_tile(
            map, tc_anchor.cell, resource_type, fog, player_slot);
        if (resource.x < 0) {
            return;
        }

        const std::optional<core::GridPos> site = find_build_site_near(
            map,
            registry,
            resource,
            1,
            1,
            footprint,
            constants::AI_BUILDING_MIN_SEPARATION_TILES,
            constants::AI_RESOURCE_CAMP_SEARCH_RADIUS_TILES);
        if (!site.has_value()) {
            return;
        }

        const entt::entity builder = pick_builder(registry, map, workers, false);
        if (builder == entt::null) {
            return;
        }

        push_build(commands, type, player_slot, execute_tick, next_sequence, builder, *site);
        issued_build = true;
    };

    // 1) Population cap: house before anything else.
    if (house_needed && constructing_houses <= 0) {
        try_issue_build(
            player::PlayerCommandType::BuildHouse,
            constants::HOUSE_FOOTPRINT_TILES,
            constants::AI_HOUSE_TC_MIN_SEPARATION_TILES,
            constants::HOUSE_BUILD_WOOD_COST,
            0,
            0,
            true);
    }

    // Resume a stranded foundation if nobody is building it.
    if (!issued_build) {
        const auto building_view = registry.view<
            components::BuildingTag,
            components::PlayerOwnedTag,
            components::UnderConstructionTag,
            components::Health>();
        for (const entt::entity building : building_view) {
            if (components::entity_player_slot(registry, building) != player_slot) {
                continue;
            }

            if (building_view.get<components::Health>(building).current.raw() <= 0) {
                continue;
            }

            bool has_builder = false;
            for (const entt::entity worker : workers) {
                if (registry.any_of<components::BuildOrder>(worker)
                    && registry.get<components::BuildOrder>(worker).building == building) {
                    has_builder = true;
                    break;
                }
            }
            if (has_builder) {
                continue;
            }

            const entt::entity builder = pick_builder(registry, map, workers, false);
            if (builder == entt::null) {
                break;
            }

            player::PlayerCommand command = make_command(
                player::PlayerCommandType::ResumeBuild,
                player_slot,
                execute_tick,
                next_sequence);
            command.unit_ids = {builder};
            command.target_entity = building;
            commands.push_back(std::move(command));
            issued_build = true;
            break;
        }
    }

    // 2) Resource drop-offs. Mill must exist before farms; if berries are gone,
    // place the mill near the Town Center instead of waiting on a berry tile.
    if (!issued_build && mills <= 0 && worker_count >= constants::AI_MILL_WORKER_MIN) {
        if (berries_available) {
            try_issue_camp_near_resource(
                player::PlayerCommandType::BuildMill,
                constants::MILL_FOOTPRINT_TILES,
                constants::MILL_BUILD_WOOD_COST,
                components::TileType::Berries);
        }
        if (!issued_build) {
            try_issue_build(
                player::PlayerCommandType::BuildMill,
                constants::MILL_FOOTPRINT_TILES,
                constants::AI_HOUSE_TC_MIN_SEPARATION_TILES,
                constants::MILL_BUILD_WOOD_COST,
                0,
                0,
                false);
        }
    }

    if (!issued_build && lumber_camps <= 0
        && worker_count >= constants::AI_EARLY_WOOD_WORKERS) {
        try_issue_camp_near_resource(
            player::PlayerCommandType::BuildLumberCamp,
            constants::LUMBER_CAMP_FOOTPRINT_TILES,
            constants::LUMBER_CAMP_BUILD_WOOD_COST,
            components::TileType::Forest);
    }

    if (!issued_build && mining_camps <= 0
        && (preparing_age || elemental_or_later || gold_workers > 0)) {
        try_issue_camp_near_resource(
            player::PlayerCommandType::BuildMiningCamp,
            constants::MINING_CAMP_FOOTPRINT_TILES,
            constants::MINING_CAMP_BUILD_WOOD_COST,
            components::TileType::GoldMine);
    }

    const bool mill_ready = player::player_has_completed_mill(registry, player_slot);
    const int farm_target = std::max(
        constants::AI_MIN_FARMS_WHEN_NATURAL_FOOD_LOW,
        food_workers * constants::AI_FARMS_PER_FOOD_WORKER);
    if (!issued_build && mill_ready && need_farm_food && farms < farm_target) {
        try_issue_build(
            player::PlayerCommandType::BuildFarm,
            constants::FARM_FOOTPRINT_TILES,
            constants::AI_HOUSE_TC_MIN_SEPARATION_TILES,
            constants::FARM_BUILD_WOOD_COST,
            0,
            0,
            false);
    }

    // 3) Elemental mana chain.
    if (!issued_build && elemental_or_later && extractors <= 0) {
        const std::optional<core::GridPos> lake =
            find_free_mana_lake_anchor(registry, tc_anchor.cell, fog, player_slot);
        if (lake.has_value()
            && stockpile.wood >= constants::EXTRACTOR_BUILD_WOOD_COST
            && stockpile.money >= constants::EXTRACTOR_BUILD_MONEY_COST) {
            const entt::entity builder = pick_builder(registry, map, workers, false);
            if (builder != entt::null) {
                push_build(
                    commands,
                    player::PlayerCommandType::BuildExtractor,
                    player_slot,
                    execute_tick,
                    next_sequence,
                    builder,
                    *lake);
                issued_build = true;
            }
        }
    }

    if (!issued_build && elemental_or_later
        && player::count_completed_extractors(registry, player_slot) > 0 && reservoirs <= 0) {
        try_issue_build(
            player::PlayerCommandType::BuildReservoir,
            constants::RESERVOIR_FOOTPRINT_TILES,
            constants::AI_HOUSE_TC_MIN_SEPARATION_TILES,
            constants::RESERVOIR_BUILD_WOOD_COST,
            constants::RESERVOIR_BUILD_MONEY_COST,
            0,
            false);
    }

    if (!issued_build && elemental_or_later
        && player::count_completed_reservoirs(registry, player_slot) > 0 && academies <= 0) {
        try_issue_build(
            player::PlayerCommandType::BuildMageAcademy,
            constants::MAGE_ACADEMY_FOOTPRINT_TILES,
            constants::AI_HOUSE_TC_MIN_SEPARATION_TILES,
            constants::MAGE_ACADEMY_BUILD_WOOD_COST,
            constants::MAGE_ACADEMY_BUILD_MONEY_COST,
            constants::MAGE_ACADEMY_BUILD_MANA_COST,
            false);
    }

    // 4) Barracks for the first scout and later military.
    if (!issued_build && barracks_count <= 0
        && worker_count >= constants::AI_BARRACKS_WORKER_MIN
        && completed_houses >= 1) {
        try_issue_build(
            player::PlayerCommandType::BuildBarracks,
            constants::BARRACKS_FOOTPRINT_TILES,
            constants::AI_HOUSE_TC_MIN_SEPARATION_TILES,
            constants::BARRACKS_BUILD_WOOD_COST,
            0,
            0,
            false);
    }

    // 5) Villager jobs: idle first, then reassign toward the urgent need.
    int wood_target = elemental_or_later
        ? constants::AI_WOOD_WORKERS_STABLE
        : constants::AI_EARLY_WOOD_WORKERS;
    int gold_target = preparing_age || elemental_or_later
        ? constants::AI_GOLD_WORKERS_STABLE
        : 0;
    if (wood_urgent) {
        wood_target = std::max(wood_target, constants::AI_WOOD_WORKERS_STABLE);
    }
    if (preparing_age && age_cost.money > stockpile.money) {
        gold_target = std::max(
            gold_target, worker_count / constants::AI_AGE_GOLD_WORKER_DIVISOR);
    }
    if (preparing_age && age_cost.food > stockpile.food) {
        wood_target = std::min(wood_target, constants::AI_EARLY_WOOD_WORKERS);
        gold_target = std::min(gold_target, constants::AI_EARLY_WOOD_WORKERS);
    }

    const auto assign_worker = [&](const entt::entity worker, const GatherNeed need) {
        const core::GridPos from = registry.get<components::GridPosition>(worker).cell;
        core::GridPos target{-1, -1};
        if (need == GatherNeed::Wood) {
            target = find_nearest_resource_tile(
                map, from, components::TileType::Forest, fog, player_slot);
        }
        else if (need == GatherNeed::Gold) {
            target = find_nearest_resource_tile(
                map, from, components::TileType::GoldMine, fog, player_slot);
        }
        else {
            target = find_food_target(
                registry,
                map,
                player_slot,
                from,
                berries_available && (!need_farm_food || farms <= 0),
                fog);
        }

        if (target.x < 0) {
            target = find_food_target(registry, map, player_slot, from, true, fog);
        }
        if (target.x < 0) {
            target = find_nearest_resource_tile(
                map, from, components::TileType::Forest, fog, player_slot);
        }
        if (target.x < 0) {
            return false;
        }

        registry.remove<components::ManualControlTag>(worker);
        push_gather(commands, player_slot, execute_tick, next_sequence, worker, target);
        if (need == GatherNeed::Wood) {
            ++wood_workers;
        }
        else if (need == GatherNeed::Gold) {
            ++gold_workers;
        }
        else {
            ++food_workers;
        }
        return true;
    };

    const auto next_need = [&]() {
        if (wood_workers < wood_target) {
            return GatherNeed::Wood;
        }

        if (gold_target > 0 && gold_workers < gold_target) {
            return GatherNeed::Gold;
        }

        return GatherNeed::Food;
    };

    for (const entt::entity worker : workers) {
        if (registry.any_of<components::BuildOrder>(worker)) {
            continue;
        }

        if (!worker_is_idle(registry, map, worker)) {
            continue;
        }

        (void)assign_worker(worker, next_need());
    }

    if (wood_urgent && wood_workers < wood_target) {
        for (const entt::entity worker : workers) {
            if (wood_workers >= wood_target) {
                break;
            }

            if (registry.any_of<components::BuildOrder>(worker)
                || worker_is_idle(registry, map, worker)) {
                continue;
            }

            if (!registry.any_of<components::GatherTarget>(worker)) {
                continue;
            }

            const auto type = registry.get<components::GatherTarget>(worker).resource_type;
            if (type == components::TileType::Forest) {
                continue;
            }

            const bool was_food = worker_is_gathering_food(registry, map, worker);
            if (was_food && gold_workers > 0) {
                continue;
            }

            if (!assign_worker(worker, GatherNeed::Wood)) {
                continue;
            }

            if (type == components::TileType::GoldMine) {
                --gold_workers;
            }
            else if (was_food) {
                --food_workers;
            }
        }
    }

    if (preparing_age && age_cost.money > stockpile.money && gold_workers < gold_target) {
        for (const entt::entity worker : workers) {
            if (gold_workers >= gold_target) {
                break;
            }

            if (registry.any_of<components::BuildOrder>(worker)
                || worker_is_idle(registry, map, worker)) {
                continue;
            }

            if (!registry.any_of<components::GatherTarget>(worker)) {
                continue;
            }

            const auto type = registry.get<components::GatherTarget>(worker).resource_type;
            if (type == components::TileType::GoldMine) {
                continue;
            }

            const bool was_food = worker_is_gathering_food(registry, map, worker);
            if (type == components::TileType::Forest && wood_workers <= wood_target) {
                continue;
            }

            if (was_food
                && (wood_workers > wood_target
                    || food_workers
                        <= worker_count / constants::AI_AGE_FOOD_WORKER_DIVISOR)) {
                continue;
            }

            if (!assign_worker(worker, GatherNeed::Gold)) {
                continue;
            }

            if (type == components::TileType::Forest) {
                --wood_workers;
            }
            else if (was_food) {
                --food_workers;
            }
        }
    }

    if (preparing_age && age_cost.food > stockpile.food
        && food_workers < worker_count / constants::AI_AGE_FOOD_WORKER_DIVISOR) {
        for (const entt::entity worker : workers) {
            if (food_workers >= worker_count / constants::AI_AGE_FOOD_WORKER_DIVISOR) {
                break;
            }

            if (registry.any_of<components::BuildOrder>(worker)
                || worker_is_gathering_food(registry, map, worker)
                || worker_is_idle(registry, map, worker)) {
                continue;
            }

            if (!registry.any_of<components::GatherTarget>(worker)) {
                continue;
            }

            const auto type = registry.get<components::GatherTarget>(worker).resource_type;
            if (type == components::TileType::Forest && wood_workers <= wood_target) {
                continue;
            }

            if (assign_worker(worker, GatherNeed::Food)) {
                if (type == components::TileType::Forest) {
                    --wood_workers;
                }
                else if (type == components::TileType::GoldMine) {
                    --gold_workers;
                }
            }
        }
    }

    // 6) Produce villagers while the economy is still growing.
    const bool tc_busy = components::building_has_active_process(registry, town_center);
    int spawn_worker_cost = constants::WORKER_FOOD_COST;
    if (registry.any_of<components::ContentPack>(world)
        && registry.any_of<components::DefinitionRef>(town_center)) {
        const auto& content = registry.get<components::ContentPack>(world).content;
        const auto* town_center_archetype = data::find_structure_archetype(
            content, registry.get<components::DefinitionRef>(town_center).id);
        if (town_center_archetype != nullptr && town_center_archetype->spawn_worker_food_cost > 0) {
            spawn_worker_cost = town_center_archetype->spawn_worker_food_cost;
        }
    }

    bool issued_town_center_order = false;
    if (!tc_busy && worker_count < desired_workers
        && player::player_can_spawn_units(registry, player_slot)
        && stockpile.food >= spawn_worker_cost) {
        player::PlayerCommand command = make_command(
            player::PlayerCommandType::SpawnWorker, player_slot, execute_tick, next_sequence);
        command.target_entity = town_center;
        commands.push_back(std::move(command));
        issued_town_center_order = true;
    }

    // 7) Age up once the worker base is in place and the stockpile is ready.
    if (!issued_town_center_order && !tc_busy && preparing_age && age_cost.can_advance
        && stockpile.food >= age_cost.food
        && stockpile.money >= age_cost.money
        && stockpile.mana >= age_cost.mana) {
        player::PlayerCommand command = make_command(
            player::PlayerCommandType::AdvanceAge, player_slot, execute_tick, next_sequence);
        command.target_entity = town_center;
        commands.push_back(std::move(command));
    }

    // 8) Military production toward the current balance, plus one early scout.
    const entt::entity barracks =
        find_owned_completed_building<components::BarracksTag>(registry, player_slot);
    if (barracks != entt::null
        && !components::building_has_active_process(registry, barracks)
        && player::player_can_spawn_units(registry, player_slot)
        && military_count < desired_military
        && stockpile.food >= constants::MILITIA_FOOD_COST
        && stockpile.money >= constants::MILITIA_MONEY_COST) {
        player::PlayerCommand command = make_command(
            player::PlayerCommandType::SpawnMilitia, player_slot, execute_tick, next_sequence);
        command.target_entity = barracks;
        commands.push_back(std::move(command));
    }

    const entt::entity academy =
        find_owned_completed_building<components::MageAcademyTag>(registry, player_slot);
    const int mana_cap = player::player_mana_cap_max(registry, player_slot);
    if (academy != entt::null
        && !components::building_has_active_process(registry, academy)
        && player::player_can_spawn_units(registry, player_slot)
        && military_count < desired_military
        && mana_cap > 0
        && stockpile.mana >= constants::MAGE_MANA_COST
        && stockpile.money >= constants::MAGE_MONEY_COST) {
        player::PlayerCommand command = make_command(
            player::PlayerCommandType::SpawnMage, player_slot, execute_tick, next_sequence);
        command.target_entity = academy;
        commands.push_back(std::move(command));
    }

    // 9) Scout with the first militia; attack when the army has an edge.
    if (!militias.empty()) {
        const entt::entity scout = militias.front();
        if (!unit_has_live_attack_target(registry, scout)
            && !registry.any_of<components::MovePath>(scout)
            && !registry.any_of<components::MoveSegment>(scout)) {
            player::PlayerCommand command =
                make_command(player::PlayerCommandType::Move, player_slot, execute_tick, next_sequence);
            command.unit_ids = {scout};
            command.cell = scout_waypoint(
                map,
                registry.get<components::GridPosition>(scout).cell,
                execute_tick,
                player_slot,
                fog);
            commands.push_back(std::move(command));
        }
    }

    const int visible_enemies = count_visible_enemy_units(registry, player_slot, fog);
    const bool attack_ready = military_count >= constants::AI_ATTACK_MIN_ARMY
        && military_count >= visible_enemies + constants::AI_ATTACK_ADVANTAGE;
    if (attack_ready) {
        entt::entity wave_target = entt::null;
        for (const entt::entity soldier : militias) {
            const core::GridPos pos = registry.get<components::GridPosition>(soldier).cell;
            wave_target = find_nearest_visible_opponent_target(registry, pos, player_slot, fog);
            if (wave_target != entt::null) {
                break;
            }
        }
        if (wave_target == entt::null && !mages.empty()) {
            const core::GridPos pos = registry.get<components::GridPosition>(mages.front()).cell;
            wave_target = find_nearest_visible_opponent_target(registry, pos, player_slot, fog);
        }

        if (wave_target != entt::null) {
            const std::size_t first_combat = militias.size() > 1U ? 1U : 0U;
            for (std::size_t index = first_combat; index < militias.size(); ++index) {
                const entt::entity militia = militias[index];
                if (unit_has_live_attack_target(registry, militia)) {
                    continue;
                }

                player::PlayerCommand command = make_command(
                    player::PlayerCommandType::Attack, player_slot, execute_tick, next_sequence);
                command.unit_ids = {militia};
                command.target_entity = wave_target;
                commands.push_back(std::move(command));
            }

            for (const entt::entity mage : mages) {
                if (unit_has_live_attack_target(registry, mage)) {
                    continue;
                }

                player::PlayerCommand command = make_command(
                    player::PlayerCommandType::Attack, player_slot, execute_tick, next_sequence);
                command.unit_ids = {mage};
                command.target_entity = wave_target;
                commands.push_back(std::move(command));
            }
        }
    }

    return commands;
}

} // namespace aoa::sim::systems
