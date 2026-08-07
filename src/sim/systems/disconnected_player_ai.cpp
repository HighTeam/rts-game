#include "sim/systems/disconnected_player_ai.hpp"

#include "sim/components/combat.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"

#include <algorithm>
#include <limits>
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

core::GridPos find_nearest_forest_with_wood(const components::MapGrid& map, const core::GridPos from)
{
    core::GridPos best{-1, -1};
    int best_distance = std::numeric_limits<int>::max();

    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const int index = core::grid_index({x, y}, map.width);
            if (map.forest_wood[static_cast<std::size_t>(index)] <= 0) {
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

entt::entity find_nearest_opponent_unit(
    entt::registry& registry,
    const core::GridPos from,
    const std::uint8_t player_slot,
    const entt::entity ignore)
{
    entt::entity best = entt::null;
    int best_distance = std::numeric_limits<int>::max();

    const auto view = registry.view<components::UnitTag, components::GridPosition, components::Health>();
    for (const entt::entity candidate : view) {
        if (candidate == ignore) {
            continue;
        }

        if (!components::is_opponent_entity(registry, candidate, player_slot)) {
            continue;
        }

        const auto& health = view.get<components::Health>(candidate);
        if (health.current.raw() <= 0) {
            continue;
        }

        const core::GridPos opponent_pos = view.get<components::GridPosition>(candidate).cell;
        const int distance = core::chebyshev_distance(from, opponent_pos);
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    }

    return best;
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

    std::vector<entt::entity> militia_units{};
    const auto militia_view =
        registry.view<components::MilitiaUnitTag, components::PlayerOwnedTag, components::GridPosition, components::Health>();
    for (const entt::entity entity : militia_view) {
        if (!is_alive_player_unit_for_slot(registry, entity, player_slot)) {
            continue;
        }

        militia_units.push_back(entity);
    }

    std::sort(militia_units.begin(), militia_units.end(), [&registry](const entt::entity left, const entt::entity right) {
        return snapshot::compare_entities_for_deterministic_iteration(registry, left, right);
    });

    for (const entt::entity militia : militia_units) {
        if (registry.any_of<components::MovePath>(militia) || registry.any_of<components::MoveSegment>(militia)) {
            continue;
        }

        if (registry.any_of<components::AttackOrder>(militia)) {
            const entt::entity current_target = registry.get<components::AttackOrder>(militia).target;
            if (current_target != entt::null && registry.valid(current_target)) {
                const auto& target_health = registry.get<components::Health>(current_target);
                if (target_health.current.raw() > 0) {
                    continue;
                }
            }
        }

        const core::GridPos militia_pos = registry.get<components::GridPosition>(militia).cell;
        const entt::entity target = find_nearest_opponent_unit(registry, militia_pos, player_slot, militia);
        if (target == entt::null) {
            continue;
        }

        player::PlayerCommand command =
            make_command(player::PlayerCommandType::Attack, player_slot, execute_tick, next_sequence);
        command.unit_ids = {militia};
        command.target_entity = target;
        commands.push_back(std::move(command));
    }

    std::vector<entt::entity> worker_units{};
    const auto worker_view = registry.view<
        components::WorkerUnitTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::Health>();
    for (const entt::entity entity : worker_view) {
        if (!is_alive_player_unit_for_slot(registry, entity, player_slot)) {
            continue;
        }

        if (registry.any_of<components::ManualControlTag>(entity)) {
            continue;
        }

        worker_units.push_back(entity);
    }

    std::sort(worker_units.begin(), worker_units.end(), [&registry](const entt::entity left, const entt::entity right) {
        return snapshot::compare_entities_for_deterministic_iteration(registry, left, right);
    });

    for (const entt::entity worker : worker_units) {
        if (registry.any_of<components::MovePath>(worker) || registry.any_of<components::MoveSegment>(worker)) {
            continue;
        }

        const core::GridPos worker_pos = registry.get<components::GridPosition>(worker).cell;
        const core::GridPos forest = find_nearest_forest_with_wood(map, worker_pos);
        if (forest.x < 0) {
            continue;
        }

        player::PlayerCommand command =
            make_command(player::PlayerCommandType::Gather, player_slot, execute_tick, next_sequence);
        command.unit_ids = {worker};
        command.cell = forest;
        commands.push_back(std::move(command));
    }

    return commands;
}

} // namespace aoa::sim::systems
