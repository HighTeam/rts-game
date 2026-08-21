#include "sim/systems/disconnected_player_ai.hpp"

#include "core/constants.hpp"
#include "data/content_types.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/content_pack.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/match_session.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/player/player_command.hpp"
#include "sim/player/player_economy.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"
#include "sim/components/fog_of_war.hpp"
#include "sim/systems/pathfinding.hpp"
#include "sim/systems/visibility_system.hpp"
#include "math/fixed.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <vector>

namespace aoa::sim::systems {

namespace {

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

    if (!registry.all_of<components::UnitTag, components::PlayerOwnedTag, components::GridPosition, components::Health>(
            entity)) {
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
    const components::TileType preferred_type)
{
    const auto tile_matches = [&](const int x, const int y) {
        if (!core::is_inside_grid({x, y}, map.width, map.height)) {
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

[[nodiscard]] components::TileType gather_type_for_worker(
    const entt::registry& registry,
    const entt::entity worker)
{
    if (!registry.any_of<components::GatherTarget>(worker)) {
        return components::TileType::Grass;
    }

    return registry.get<components::GatherTarget>(worker).resource_type;
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

    const auto unit_view = registry.view<components::UnitTag, components::GridPosition, components::Health>();
    for (const entt::entity candidate : unit_view) {
        consider(candidate, unit_view.get<components::GridPosition>(candidate).cell);
    }

    const auto building_view = registry.view<
        components::BuildingTag,
        components::GridPosition,
        components::Health>();
    for (const entt::entity candidate : building_view) {
        consider(candidate, building_view.get<components::GridPosition>(candidate).cell);
    }

    return best;
}

[[nodiscard]] bool militia_has_live_attack_target(entt::registry& registry, const entt::entity militia)
{
    if (!registry.any_of<components::AttackOrder>(militia)) {
        return false;
    }

    const entt::entity current_target = registry.get<components::AttackOrder>(militia).target;
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

[[nodiscard]] bool house_too_close_to_tc(
    const core::GridPos house_anchor,
    const core::GridPos tc_anchor,
    const components::BuildingFootprint& tc_footprint)
{
    const components::GridPosition tc_grid{tc_anchor};
    const int house_footprint = constants::HOUSE_FOOTPRINT_TILES;
    for (int y = 0; y < house_footprint; ++y) {
        for (int x = 0; x < house_footprint; ++x) {
            const core::GridPos cell{house_anchor.x + x, house_anchor.y + y};
            if (components::chebyshev_distance_to_footprint(cell, tc_grid, tc_footprint)
                < constants::AI_HOUSE_TC_MIN_SEPARATION_TILES) {
                return true;
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

[[nodiscard]] std::optional<core::GridPos> find_house_site_near_tc(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos tc_anchor,
    const components::BuildingFootprint& tc_footprint)
{
    const int house_footprint = constants::HOUSE_FOOTPRINT_TILES;
    const int search_radius = 8;
    core::GridPos best{-1, -1};
    int best_distance = std::numeric_limits<int>::max();

    for (int dy = -search_radius; dy <= search_radius + tc_footprint.height; ++dy) {
        for (int dx = -search_radius; dx <= search_radius + tc_footprint.width; ++dx) {
            const core::GridPos anchor{tc_anchor.x + dx, tc_anchor.y + dy};
            if (!can_place_footprint(map, registry, anchor, house_footprint)) {
                continue;
            }

            if (house_too_close_to_tc(anchor, tc_anchor, tc_footprint)) {
                continue;
            }

            if (!footprint_has_walkable_stand(map, registry, anchor, house_footprint)) {
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

[[nodiscard]] core::GridPos scout_waypoint(
    const components::MapGrid& map,
    const std::uint64_t execute_tick,
    const std::uint8_t player_slot)
{
    const std::size_t corner_index =
        static_cast<std::size_t>((execute_tick / 40U) + player_slot) % 4U;
    core::GridPos waypoint{4, 4};
    if (corner_index == 1U) {
        waypoint = {map.width - 5, 4};
    }
    else if (corner_index == 2U) {
        waypoint = {map.width - 5, map.height - 5};
    }
    else if (corner_index == 3U) {
        waypoint = {4, map.height - 5};
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
    const entt::entity town_center = find_town_center_for_slot(registry, player_slot);
    if (town_center == entt::null) {
        return commands;
    }

    const components::Stockpile stockpile = player::sum_player_stockpile(registry, player_slot);
    const int completed_houses = player::count_completed_houses(registry, player_slot);
    int total_houses = completed_houses;
    {
        const auto house_view = registry.view<
            components::HouseTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity house : house_view) {
            if (components::entity_player_slot(registry, house) != player_slot) {
                continue;
            }

            if (house_view.get<components::Health>(house).current.raw() <= 0) {
                continue;
            }

            if (registry.any_of<components::UnderConstructionTag>(house)) {
                ++total_houses;
            }
        }
    }

    std::vector<entt::entity> workers{};
    const auto worker_view = registry.view<
        components::WorkerUnitTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::Health>();
    for (const entt::entity entity : worker_view) {
        if (!is_alive_player_unit_for_slot(registry, entity, player_slot)) {
            continue;
        }

        workers.push_back(entity);
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
        if (!is_alive_player_unit_for_slot(registry, entity, player_slot)) {
            continue;
        }

        militias.push_back(entity);
    }
    std::sort(militias.begin(), militias.end(), [&registry](const entt::entity left, const entt::entity right) {
        return snapshot::compare_entities_for_deterministic_iteration(registry, left, right);
    });

    const components::FogOfWarState* fog = nullptr;
    if (registry.any_of<components::FogOfWarState>(world)) {
        fog = &registry.get<components::FogOfWarState>(world);
    }

    int wood_workers = 0;
    int food_workers = 0;
    int gold_workers = 0;
    for (const entt::entity worker : workers) {
        const components::TileType gather_type = gather_type_for_worker(registry, worker);
        if (gather_type == components::TileType::Forest) {
            ++wood_workers;
        }
        else if (gather_type == components::TileType::Berries
            || gather_type == components::TileType::Blueberries) {
            ++food_workers;
        }
        else if (gather_type == components::TileType::GoldMine) {
            ++gold_workers;
        }
    }

    // 1) Idle workers: keep a wood line, then food, then gold. Re-job after a node depletes.
    for (const entt::entity worker : workers) {
        if (registry.any_of<components::MoveSegment>(worker)
            || registry.any_of<components::BuildOrder>(worker)) {
            continue;
        }

        if (registry.any_of<components::GatherTarget>(worker)) {
            const auto& gather_target = registry.get<components::GatherTarget>(worker);
            if (core::is_inside_grid(gather_target.cell, map.width, map.height)) {
                const int index = core::grid_index(gather_target.cell, map.width);
                const bool still_there =
                    (gather_target.resource_type == components::TileType::Forest
                        && map.forest_wood[static_cast<std::size_t>(index)] > 0)
                    || ((gather_target.resource_type == components::TileType::Berries
                            || gather_target.resource_type == components::TileType::Blueberries)
                        && map.bush_food[static_cast<std::size_t>(index)] > 0)
                    || (gather_target.resource_type == components::TileType::GoldMine
                        && static_cast<std::size_t>(index) < map.mine_money.size()
                        && map.mine_money[static_cast<std::size_t>(index)] > 0);
                if (still_there) {
                    continue;
                }
            }
        }

        registry.remove<components::ManualControlTag>(worker);

        const core::GridPos worker_pos = registry.get<components::GridPosition>(worker).cell;
        components::TileType wanted = components::TileType::Forest;
        if (wood_workers < constants::AI_WOOD_WORKERS_MIN
            || stockpile.wood < constants::AI_WOOD_STOCKPILE_TARGET) {
            wanted = components::TileType::Forest;
        }
        else if (food_workers < constants::AI_FOOD_WORKERS_MIN) {
            wanted = components::TileType::Berries;
        }
        else if (gold_workers < constants::AI_GOLD_WORKERS_MIN) {
            wanted = components::TileType::GoldMine;
        }

        core::GridPos resource = find_nearest_resource_tile(map, worker_pos, wanted);
        if (resource.x < 0 && wanted != components::TileType::Forest) {
            resource = find_nearest_resource_tile(map, worker_pos, components::TileType::Forest);
            wanted = components::TileType::Forest;
        }
        if (resource.x < 0 && wanted != components::TileType::Berries) {
            resource = find_nearest_resource_tile(map, worker_pos, components::TileType::Berries);
            wanted = components::TileType::Berries;
        }
        if (resource.x < 0 && wanted != components::TileType::GoldMine) {
            resource = find_nearest_resource_tile(map, worker_pos, components::TileType::GoldMine);
            wanted = components::TileType::GoldMine;
        }
        if (resource.x < 0) {
            continue;
        }

        if (wanted == components::TileType::Forest) {
            ++wood_workers;
        }
        else if (wanted == components::TileType::GoldMine) {
            ++gold_workers;
        }
        else {
            ++food_workers;
        }

        player::PlayerCommand command =
            make_command(player::PlayerCommandType::Gather, player_slot, execute_tick, next_sequence);
        command.unit_ids = {worker};
        command.cell = resource;
        commands.push_back(std::move(command));
    }

    // 2) Build up to 2 houses near TC when wood allows.
    if (stockpile.wood >= constants::HOUSE_BUILD_WOOD_COST && total_houses < constants::AI_HOUSE_TARGET
        && !workers.empty()) {
        const auto& tc_anchor = registry.get<components::GridPosition>(town_center);
        components::BuildingFootprint tc_footprint{};
        if (registry.any_of<components::BuildingFootprint>(town_center)) {
            tc_footprint = registry.get<components::BuildingFootprint>(town_center);
        }
        tc_footprint = components::effective_building_footprint(tc_footprint, true);

        const std::optional<core::GridPos> house_site =
            find_house_site_near_tc(map, registry, tc_anchor.cell, tc_footprint);
        if (house_site.has_value()) {
            entt::entity builder = entt::null;
            entt::entity fallback_builder = entt::null;
            for (const entt::entity worker : workers) {
                if (registry.any_of<components::BuildOrder>(worker)) {
                    continue;
                }

                if (!registry.any_of<components::GatherTarget>(worker)) {
                    builder = worker;
                    break;
                }

                if (fallback_builder == entt::null) {
                    fallback_builder = worker;
                }
            }

            if (builder == entt::null) {
                builder = fallback_builder;
            }

            if (builder != entt::null) {
                player::PlayerCommand command = make_command(
                    player::PlayerCommandType::BuildHouse,
                    player_slot,
                    execute_tick,
                    next_sequence);
                command.unit_ids = {builder};
                command.cell = *house_site;
                commands.push_back(std::move(command));
            }
        }
    }

    // 3) Spawn workers while under civil cap.
    if (player::player_can_spawn_units(registry, player_slot)
        && !registry.any_of<components::UnderConstructionTag>(town_center)) {
        int spawn_worker_cost = 0;
        if (registry.any_of<components::ContentPack>(world)
            && registry.any_of<components::DefinitionRef>(town_center)) {
            const auto& content = registry.get<components::ContentPack>(world).content;
            const auto& definition_ref = registry.get<components::DefinitionRef>(town_center);
            const auto* town_center_archetype =
                data::find_structure_archetype(content, definition_ref.id);
            if (town_center_archetype != nullptr) {
                spawn_worker_cost = town_center_archetype->spawn_worker_food_cost;
            }
        }

        if (spawn_worker_cost > 0 && stockpile.food >= spawn_worker_cost) {
            player::PlayerCommand command = make_command(
                player::PlayerCommandType::SpawnWorker,
                player_slot,
                execute_tick,
                next_sequence);
            command.target_entity = town_center;
            commands.push_back(std::move(command));
        }
    }

    // 4) After houses + workers: spawn a militia group, scout with one, wave with the rest.
    const bool late_game = completed_houses >= constants::AI_HOUSE_TARGET
        && static_cast<int>(workers.size()) >= 3;
    const int militia_target = constants::AI_MILITIA_WAVE_SIZE + constants::AI_SCOUT_COUNT;
    if (late_game) {
        if (player::player_can_spawn_units(registry, player_slot)
            && !registry.any_of<components::UnderConstructionTag>(town_center)
            && static_cast<int>(militias.size()) < militia_target) {
            entt::entity barracks = entt::null;
            const auto barracks_view = registry.view<
                components::BarracksTag,
                components::PlayerOwnedTag,
                components::Health>();
            for (const entt::entity candidate : barracks_view) {
                if (components::entity_player_slot(registry, candidate) != player_slot) {
                    continue;
                }

                if (registry.any_of<components::UnderConstructionTag>(candidate)) {
                    continue;
                }

                if (barracks_view.get<components::Health>(candidate).current.raw() <= 0) {
                    continue;
                }

                barracks = candidate;
                break;
            }

            int spawn_militia_food_cost = 0;
            int spawn_militia_money_cost = constants::MILITIA_MONEY_COST;
            if (barracks != entt::null
                && registry.any_of<components::ContentPack>(world)
                && registry.any_of<components::DefinitionRef>(barracks)) {
                const auto& content = registry.get<components::ContentPack>(world).content;
                const auto& definition_ref = registry.get<components::DefinitionRef>(barracks);
                const auto* barracks_archetype =
                    data::find_structure_archetype(content, definition_ref.id);
                if (barracks_archetype != nullptr) {
                    spawn_militia_food_cost = barracks_archetype->spawn_militia_food_cost;
                    if (barracks_archetype->spawn_militia_money_cost > 0) {
                        spawn_militia_money_cost = barracks_archetype->spawn_militia_money_cost;
                    }
                }
            }

            if (barracks != entt::null && spawn_militia_food_cost > 0
                && stockpile.food >= spawn_militia_food_cost
                && stockpile.money >= spawn_militia_money_cost) {
                player::PlayerCommand command = make_command(
                    player::PlayerCommandType::SpawnMilitia,
                    player_slot,
                    execute_tick,
                    next_sequence);
                command.target_entity = barracks;
                commands.push_back(std::move(command));
            }
        }

        if (!militias.empty()) {
            const entt::entity scout = militias.front();
            if (!militia_has_live_attack_target(registry, scout)
                && !registry.any_of<components::MovePath>(scout)
                && !registry.any_of<components::MoveSegment>(scout)) {
                player::PlayerCommand command =
                    make_command(player::PlayerCommandType::Move, player_slot, execute_tick, next_sequence);
                command.unit_ids = {scout};
                command.cell = scout_waypoint(map, execute_tick, player_slot);
                commands.push_back(std::move(command));
            }
        }
    }

    // 5) Attack wave: only after the group is ready, and only at a visible enemy.
    const bool wave_ready = late_game
        && static_cast<int>(militias.size()) >= militia_target;
    const std::size_t first_combat_index = late_game && !militias.empty() ? 1U : 0U;
    if (wave_ready) {
        entt::entity wave_target = entt::null;
        for (std::size_t index = 0U; index < militias.size(); ++index) {
            const core::GridPos militia_pos = registry.get<components::GridPosition>(militias[index]).cell;
            wave_target =
                find_nearest_visible_opponent_target(registry, militia_pos, player_slot, fog);
            if (wave_target != entt::null) {
                break;
            }
        }

        if (wave_target != entt::null) {
            for (std::size_t index = first_combat_index; index < militias.size(); ++index) {
                const entt::entity militia = militias[index];
                if (militia_has_live_attack_target(registry, militia)) {
                    continue;
                }

                player::PlayerCommand command = make_command(
                    player::PlayerCommandType::Attack, player_slot, execute_tick, next_sequence);
                command.unit_ids = {militia};
                command.target_entity = wave_target;
                commands.push_back(std::move(command));
            }
        }
    }

    return commands;
}

} // namespace aoa::sim::systems






