#include "sim/systems/disconnected_player_ai.hpp"

#include "core/constants.hpp"
#include "data/content_types.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/content_pack.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
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

#include <algorithm>
#include <cmath>
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

core::GridPos find_nearest_resource_tile(const components::MapGrid& map, const core::GridPos from)
{
    core::GridPos best{-1, -1};
    int best_distance = std::numeric_limits<int>::max();

    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const int index = core::grid_index({x, y}, map.width);
            const bool has_wood = map.forest_wood[static_cast<std::size_t>(index)] > 0;
            const bool has_food = map.bush_food[static_cast<std::size_t>(index)] > 0;
            const bool has_money =
                static_cast<std::size_t>(index) < map.mine_money.size()
                && map.mine_money[static_cast<std::size_t>(index)] > 0;
            if (!has_wood && !has_food && !has_money) {
                continue;
            }

            const int distance = std::abs(from.x - x) + std::abs(from.y - y);
            if (distance < best_distance) {
                best_distance = distance;
                best = {x, y};
            }
        }
    }

    return best;
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

            if (is_movement_blocked(registry, cell)) {
                return false;
            }
        }
    }

    return true;
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

            bool overlaps_tc = false;
            for (int y = 0; y < house_footprint && !overlaps_tc; ++y) {
                for (int x = 0; x < house_footprint; ++x) {
                    if (components::building_contains_cell(
                            components::GridPosition{tc_anchor},
                            tc_footprint,
                            {anchor.x + x, anchor.y + y})) {
                        overlaps_tc = true;
                        break;
                    }
                }
            }
            if (overlaps_tc) {
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

    const auto& map = registry.get<components::MapGrid>(world);
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

    // 1) Idle workers gather nearest wood or berries (ignore ManualControlTag so AI retargets).
    for (const entt::entity worker : workers) {
        if (registry.any_of<components::MovePath>(worker)
            || registry.any_of<components::MoveSegment>(worker)
            || registry.any_of<components::GatherTarget>(worker)
            || registry.any_of<components::BuildOrder>(worker)) {
            continue;
        }

        registry.remove<components::ManualControlTag>(worker);

        const core::GridPos worker_pos = registry.get<components::GridPosition>(worker).cell;
        const core::GridPos resource = find_nearest_resource_tile(map, worker_pos);
        if (resource.x < 0) {
            continue;
        }

        player::PlayerCommand command =
            make_command(player::PlayerCommandType::Gather, player_slot, execute_tick, next_sequence);
        command.unit_ids = {worker};
        command.cell = resource;
        commands.push_back(std::move(command));
    }

    // 2) Build up to 2 houses near TC when wood allows.
    if (stockpile.wood >= constants::HOUSE_BUILD_WOOD_COST && total_houses < 2
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
            for (const entt::entity worker : workers) {
                if (registry.any_of<components::BuildOrder>(worker)) {
                    continue;
                }

                builder = worker;
                break;
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

    // 4) After 2 houses and 3 workers: spawn militias; one scout patrols corners.
    const bool late_game = completed_houses >= 2 && static_cast<int>(workers.size()) >= 3;
    if (late_game) {
        if (player::player_can_spawn_units(registry, player_slot)
            && !registry.any_of<components::UnderConstructionTag>(town_center)) {
            int spawn_militia_cost = 0;
            if (registry.any_of<components::ContentPack>(world)
                && registry.any_of<components::DefinitionRef>(town_center)) {
                const auto& content = registry.get<components::ContentPack>(world).content;
                const auto& definition_ref = registry.get<components::DefinitionRef>(town_center);
                const auto* town_center_archetype =
                    data::find_structure_archetype(content, definition_ref.id);
                if (town_center_archetype != nullptr) {
                    spawn_militia_cost = town_center_archetype->spawn_militia_food_cost;
                }
            }

            if (spawn_militia_cost > 0 && stockpile.food >= spawn_militia_cost) {
                player::PlayerCommand command = make_command(
                    player::PlayerCommandType::SpawnMilitia,
                    player_slot,
                    execute_tick,
                    next_sequence);
                command.target_entity = town_center;
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

    // 5) Non-scout militias: attack any visible enemy (idle or mid-move).
    const std::size_t first_combat_index = late_game && !militias.empty() ? 1U : 0U;
    for (std::size_t index = first_combat_index; index < militias.size(); ++index) {
        const entt::entity militia = militias[index];
        if (militia_has_live_attack_target(registry, militia)) {
            continue;
        }

        const core::GridPos militia_pos = registry.get<components::GridPosition>(militia).cell;
        const entt::entity target =
            find_nearest_visible_opponent_target(registry, militia_pos, player_slot, fog);
        if (target == entt::null) {
            continue;
        }

        player::PlayerCommand command =
            make_command(player::PlayerCommandType::Attack, player_slot, execute_tick, next_sequence);
        command.unit_ids = {militia};
        command.target_entity = target;
        commands.push_back(std::move(command));
    }

    return commands;
}

} // namespace aoa::sim::systems
