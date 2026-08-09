#include "net/lockstep_auto_input.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "net/net_constants.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"
#include "sim/player/player_command.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"

#include <cstdint>
#include <vector>

namespace aoa::net {

namespace {

std::uint32_t mix_seed(const std::uint64_t tick_count, const std::uint8_t player_slot, const int salt)
{
    return static_cast<std::uint32_t>(
        tick_count * 7919U + static_cast<std::uint64_t>(player_slot) * 104729U
        + static_cast<std::uint64_t>(salt) * 1543U);
}

std::uint32_t next_rand(std::uint32_t& seed)
{
    seed = seed * 1103515245U + 12345U;
    return seed;
}

entt::entity find_player_town_center(entt::registry& registry, const std::uint8_t player_slot)
{
    const auto view = registry.view<
        sim::components::TownCenterTag,
        sim::components::PlayerOwnedTag>();
    for (const entt::entity entity : view) {
        if (sim::components::entity_player_slot(registry, entity) == player_slot) {
            return entity;
        }
    }

    return entt::null;
}

entt::entity find_player_worker(entt::registry& registry, const std::uint8_t player_slot)
{
    const auto view = registry.view<
        sim::components::WorkerUnitTag,
        sim::components::PlayerOwnedTag,
        sim::components::Health>();
    for (const entt::entity entity : view) {
        if (sim::components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        if (registry.get<sim::components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        return entity;
    }

    return entt::null;
}

entt::entity find_player_militia(entt::registry& registry, const std::uint8_t player_slot)
{
    const auto view = registry.view<
        sim::components::MilitiaUnitTag,
        sim::components::PlayerOwnedTag,
        sim::components::Health>();
    for (const entt::entity entity : view) {
        if (sim::components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        if (registry.get<sim::components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        return entity;
    }

    return entt::null;
}

std::optional<core::GridPos> find_any_forest_cell(entt::registry& registry)
{
    const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
    if (world_view.begin() == world_view.end()) {
        return std::nullopt;
    }

    const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const int index = core::grid_index({x, y}, map.width);
            if (map.tiles[static_cast<std::size_t>(index)] == sim::components::TileType::Forest
                && map.forest_wood[static_cast<std::size_t>(index)] > 0) {
                return core::GridPos{x, y};
            }
        }
    }

    return std::nullopt;
}

core::GridPos random_walkable_cell(
    entt::registry& registry,
    std::uint32_t& seed,
    const core::GridPos fallback)
{
    const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
    if (world_view.begin() == world_view.end()) {
        return fallback;
    }

    const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
    for (int attempt = 0; attempt < 64; ++attempt) {
        const int x = static_cast<int>(next_rand(seed) % static_cast<std::uint32_t>(map.width));
        const int y = static_cast<int>(next_rand(seed) % static_cast<std::uint32_t>(map.height));
        const core::GridPos cell{x, y};
        if (!core::is_inside_grid(cell, map.width, map.height)) {
            continue;
        }

        const int index = core::grid_index(cell, map.width);
        if (map.tiles[static_cast<std::size_t>(index)] != sim::components::TileType::Grass) {
            continue;
        }

        return cell;
    }

    return fallback;
}

} // namespace

void maybe_inject_lockstep_auto_input(LockstepSession& session, sim::Simulation& simulation)
{
    if (!session.is_auto_input_enabled() || !session.is_session_ready() || session.is_desynced()) {
        return;
    }

    const std::uint64_t tick_count = simulation.tick_count();
    if (tick_count < 5U || tick_count % 10U != 5U || tick_count == session.last_auto_input_tick()) {
        return;
    }

    session.set_last_auto_input_tick(tick_count);

    const std::uint8_t player_slot = session.player_slot();
    entt::registry& registry = simulation.registry();

    std::uint32_t seed = mix_seed(tick_count, player_slot, 17);
    const int action = static_cast<int>(next_rand(seed) % 5U);

    sim::player::PlayerCommand command{};
    command.execute_tick =
        tick_count + static_cast<std::uint64_t>(constants::LOCKSTEP_COMMAND_DELAY_TICKS);
    command.player_slot = player_slot;

    const entt::entity town_center = find_player_town_center(registry, player_slot);
    const entt::entity worker = find_player_worker(registry, player_slot);
    const entt::entity militia = find_player_militia(registry, player_slot);

    switch (action) {
    case 0:
        if (worker == entt::null) {
            return;
        }

        command.type = sim::player::PlayerCommandType::Move;
        command.unit_ids = {worker};
        command.cell = random_walkable_cell(registry, seed, {8, 8});
        break;
    case 1:
        if (worker == entt::null) {
            return;
        }

        {
            const std::optional<core::GridPos> forest_cell = find_any_forest_cell(registry);
            if (!forest_cell.has_value()) {
                return;
            }

            command.type = sim::player::PlayerCommandType::Gather;
            command.unit_ids = {worker};
            command.cell = *forest_cell;
        }
        break;
    case 2:
        if (town_center == entt::null) {
            return;
        }

        command.type = sim::player::PlayerCommandType::SpawnWorker;
        command.target_entity = town_center;
        break;
    case 3:
        if (town_center == entt::null) {
            return;
        }

        command.type = sim::player::PlayerCommandType::SpawnMilitia;
        command.target_entity = town_center;
        break;
    default:
        if (militia == entt::null) {
            return;
        }

        command.type = sim::player::PlayerCommandType::Move;
        command.unit_ids = {militia};
        command.cell = random_walkable_cell(registry, seed, {10, 8});
        break;
    }

    sim::snapshot::annotate_command_entity_keys(registry, command);
    session.submit_local_command(std::move(command));
}

} // namespace aoa::net
