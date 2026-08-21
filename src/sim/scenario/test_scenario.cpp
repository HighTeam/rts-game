#include "sim/scenario/test_scenario.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/content_pack.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/match_session.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/map/map_generator.hpp"
#include "sim/map/test_map.hpp"
#include "sim/scenario/scenario_layouts.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"
#include "sim/spawn/building_unit_spawn.hpp"
#include "sim/spawn/unit_spawn.hpp"
#include "sim/systems/visibility_system.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <vector>

namespace aoa::sim::scenario {

namespace {

entt::entity find_entity_with_slot(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const auto& view)
{
    for (const entt::entity entity : view) {
        if (components::entity_player_slot(registry, entity) == player_slot) {
            return entity;
        }
    }

    return entt::null;
}

[[nodiscard]] bool is_free_mana_lake_footprint(
    entt::registry& registry,
    const components::MapGrid& map,
    const core::GridPos anchor)
{
    for (int y = 0; y < constants::MANA_LAKE_FOOTPRINT_TILES; ++y) {
        for (int x = 0; x < constants::MANA_LAKE_FOOTPRINT_TILES; ++x) {
            const core::GridPos cell{anchor.x + x, anchor.y + y};
            if (!core::is_inside_grid(cell, map.width, map.height)) {
                return false;
            }

            const std::size_t index = static_cast<std::size_t>(core::grid_index(cell, map.width));
            if (map.tiles[index] != components::TileType::Grass) {
                return false;
            }

            if (components::ground_at(map, index) != components::GroundType::Grass) {
                return false;
            }

            for (int ky = -constants::MANA_LAKE_RESOURCE_KEEP_TILES;
                 ky <= constants::MANA_LAKE_RESOURCE_KEEP_TILES + constants::MANA_LAKE_FOOTPRINT_TILES - 1;
                 ++ky) {
                for (int kx = -constants::MANA_LAKE_RESOURCE_KEEP_TILES;
                     kx <= constants::MANA_LAKE_RESOURCE_KEEP_TILES + constants::MANA_LAKE_FOOTPRINT_TILES - 1;
                     ++kx) {
                    const core::GridPos keep{anchor.x + kx, anchor.y + ky};
                    if (!core::is_inside_grid(keep, map.width, map.height)) {
                        continue;
                    }

                    const std::size_t keep_index =
                        static_cast<std::size_t>(core::grid_index(keep, map.width));
                    const auto keep_tile = map.tiles[keep_index];
                    if (keep_tile == components::TileType::Berries
                        || keep_tile == components::TileType::Blueberries) {
                        return false;
                    }
                }
            }

            const auto building_view =
                registry.view<components::BuildingTag, components::GridPosition>();
            for (const entt::entity building : building_view) {
                components::BuildingFootprint footprint{};
                if (registry.any_of<components::BuildingFootprint>(building)) {
                    footprint = registry.get<components::BuildingFootprint>(building);
                }
                footprint = components::effective_building_footprint(
                    footprint,
                    registry.any_of<components::TownCenterTag>(building));
                if (components::building_contains_cell(
                        building_view.get<components::GridPosition>(building),
                        footprint,
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

            const auto unit_view = registry.view<components::UnitTag, components::GridPosition>();
            for (const entt::entity unit : unit_view) {
                if (unit_view.get<components::GridPosition>(unit).cell == cell) {
                    return false;
                }
            }
        }
    }

    return true;
}

[[nodiscard]] std::uint32_t xorshift32(std::uint32_t& state)
{
    std::uint32_t x = state;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    state = x;
    return x;
}

// Walks the Chebyshev bands around the town center and returns a random free 2x2 grass patch.
[[nodiscard]] std::optional<core::GridPos> find_mana_lake_anchor(
    entt::registry& registry,
    const components::MapGrid& map,
    const core::GridPos town_center_cell,
    std::uint32_t& rng)
{
    std::vector<core::GridPos> candidates{};
    for (int ring = constants::MANA_LAKE_MIN_RING_FROM_TOWN_CENTER;
         ring <= constants::MANA_LAKE_MAX_RING_FROM_TOWN_CENTER;
         ++ring) {
        for (int dy = -ring; dy <= ring; ++dy) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != ring) {
                    continue;
                }

                const core::GridPos anchor{town_center_cell.x + dx, town_center_cell.y + dy};
                if (!is_free_mana_lake_footprint(registry, map, anchor)) {
                    continue;
                }

                candidates.push_back(anchor);
            }
        }
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    const std::size_t pick =
        static_cast<std::size_t>(xorshift32(rng) % static_cast<std::uint32_t>(candidates.size()));
    return candidates[pick];
}

} // namespace

void load_test_scenario(
    entt::registry& registry,
    const data::ContentDatabase& content,
    const std::uint8_t player_count)
{
    map::MapGenerationConfig generation{};
    generation.player_count = player_count;
    load_test_scenario(registry, content, generation);
}

void load_test_scenario(
    entt::registry& registry,
    const data::ContentDatabase& content,
    const map::MapGenerationConfig& generation)
{
    const std::uint8_t player_count = generation.player_count;
    const std::span<const PlayerBaseLayout> layouts = base_layouts_for_player_count(player_count);

    const auto* forest_patch = data::find_resource_node_archetype(
        content,
        std::string(constants::FOREST_PATCH_RESOURCE_ID));
    const int forest_patch_wood = forest_patch != nullptr ? forest_patch->wood_capacity : 100;

    const auto* berry_bush = data::find_resource_node_archetype(
        content,
        std::string(constants::BERRY_BUSH_RESOURCE_ID));
    const int bush_food_capacity = berry_bush != nullptr && berry_bush->food_capacity > 0
        ? berry_bush->food_capacity
        : constants::BERRY_BUSH_FOOD_CAPACITY;

    const auto* gold_mine = data::find_resource_node_archetype(
        content,
        std::string(constants::GOLD_MINE_RESOURCE_ID));
    const int mine_money_capacity = gold_mine != nullptr && gold_mine->money_capacity > 0
        ? gold_mine->money_capacity
        : constants::GOLD_MINE_MONEY_CAPACITY;

    const auto* town_center_archetype = data::find_structure_archetype(
        content,
        std::string(constants::TOWN_CENTER_BUILDING_ID));
    const auto* worker_archetype =
        data::find_unit_archetype(content, std::string(constants::WORKER_UNIT_ID));
    const auto* mana_lake_archetype = data::find_structure_archetype(
        content,
        std::string(constants::MANA_LAKE_BUILDING_ID));

    if (town_center_archetype == nullptr || worker_archetype == nullptr) {
        throw std::runtime_error("Missing required archetypes for test scenario");
    }

    const entt::entity world = registry.create();
    registry.emplace<components::WorldTag>(world);
    registry.emplace<components::ContentPack>(world, components::ContentPack{content});
    map::MapGenerationConfig map_config = generation;
    map_config.forest_patch_wood = forest_patch_wood;
    map_config.bush_food_capacity = bush_food_capacity;
    map_config.mine_money_capacity = mine_money_capacity;
    const map::GeneratedMap generated = map::generate_map(map_config);
    registry.emplace<components::MapGrid>(world, generated.grid);
    registry.emplace<components::SimState>(world);
    auto& session = registry.emplace<components::MatchSession>(world);
    if (player_count > 0 && player_count <= constants::MAX_PLAYER_SLOTS) {
        session.playing_slots_mask = static_cast<std::uint8_t>((1U << player_count) - 1U);
    }

    for (std::size_t layout_index = 0U; layout_index < layouts.size(); ++layout_index) {
        const PlayerBaseLayout& layout = layouts[layout_index];
        const std::uint8_t player_slot = static_cast<std::uint8_t>(layout_index);
        const core::GridPos tc_cell = layout_index < generated.start_anchors.size()
            ? generated.start_anchors[layout_index]
            : core::GridPos{layout.tc_x, layout.tc_y};

        const entt::entity town_center = spawn::spawn_player_town_center(
            registry,
            *town_center_archetype,
            tc_cell,
            player_slot,
            components::Stockpile{
                content.civ.starting_stockpile_wood,
                content.civ.starting_stockpile_food,
                content.civ.starting_stockpile_money,
                content.civ.starting_stockpile_mana,
            });

        const auto& map = registry.get<components::MapGrid>(world);
        const auto& depot_anchor = registry.get<components::GridPosition>(town_center);
        const components::BuildingFootprint depot_footprint =
            components::effective_building_footprint(
                registry.any_of<components::BuildingFootprint>(town_center)
                    ? registry.get<components::BuildingFootprint>(town_center)
                    : components::BuildingFootprint{},
                true);
        const auto worker_spawn =
            spawn::find_building_unit_spawn_cell(map, registry, depot_anchor, depot_footprint);
        if (!worker_spawn.has_value()) {
            throw std::runtime_error("No free spawn tile for starting worker");
        }

        (void)spawn::spawn_player_worker(
            registry,
            *worker_archetype,
            *worker_spawn,
            player_slot);

        if (mana_lake_archetype == nullptr) {
            continue;
        }

        if (!generated.mana_lake_anchors.empty()) {
            continue;
        }

        for (int lake_index = 0; lake_index < constants::MANA_LAKE_PER_PLAYER_COUNT; ++lake_index) {
            const auto& current_map = registry.get<components::MapGrid>(world);
            std::uint32_t lake_rng = static_cast<std::uint32_t>(current_map.width)
                ^ (static_cast<std::uint32_t>(current_map.height) << 16U)
                ^ (static_cast<std::uint32_t>(player_slot + 1U) * 0x9E3779B9U)
                ^ constants::MAP_GEN_LAYOUT_SALT
                ^ static_cast<std::uint32_t>(lake_index + 1);
            if (lake_rng == 0U) {
                lake_rng = 1U;
            }

            const std::optional<core::GridPos> lake_anchor = find_mana_lake_anchor(
                registry,
                current_map,
                tc_cell,
                lake_rng);
            if (!lake_anchor.has_value()) {
                break;
            }

            (void)spawn::spawn_mana_lake(
                registry,
                *mana_lake_archetype,
                *lake_anchor,
                player_slot);
        }
    }

    if (mana_lake_archetype != nullptr) {
        for (std::size_t lake_index = 0; lake_index < generated.mana_lake_anchors.size(); ++lake_index) {
            const core::GridPos lake_anchor = generated.mana_lake_anchors[lake_index];
            const auto& current_map = registry.get<components::MapGrid>(world);
            if (!is_free_mana_lake_footprint(registry, current_map, lake_anchor)) {
                continue;
            }

            const std::uint8_t lake_slot = player_count == 0U
                ? 0U
                : static_cast<std::uint8_t>(lake_index % static_cast<std::size_t>(player_count));
            (void)spawn::spawn_mana_lake(
                registry,
                *mana_lake_archetype,
                lake_anchor,
                lake_slot);
        }
    }

    snapshot::assign_snapshot_identities(registry);
    systems::initialize_fog_of_war(registry);
}

entt::entity find_scenario_entity(entt::registry& registry, const std::string_view role)
{
    if (role == "player_worker") {
        const auto view = registry.view<components::WorkerUnitTag, components::PlayerOwnedTag>();
        return find_entity_with_slot(registry, 0U, view);
    }

    if (role == "player2_worker") {
        const auto view = registry.view<components::WorkerUnitTag, components::PlayerOwnedTag>();
        return find_entity_with_slot(registry, 1U, view);
    }

    if (role == "player_militia") {
        const auto view = registry.view<components::MilitiaUnitTag, components::PlayerOwnedTag>();
        return find_entity_with_slot(registry, 0U, view);
    }

    if (role == "player2_militia" || role == "enemy_militia") {
        const auto view = registry.view<components::MilitiaUnitTag, components::PlayerOwnedTag>();
        return find_entity_with_slot(registry, 1U, view);
    }

    if (role == "town_center" || role == "player1_town_center") {
        const auto view = registry.view<components::TownCenterTag, components::PlayerOwnedTag>();
        return find_entity_with_slot(registry, 0U, view);
    }

    if (role == "player2_town_center") {
        const auto view = registry.view<components::TownCenterTag, components::PlayerOwnedTag>();
        return find_entity_with_slot(registry, 1U, view);
    }

    return entt::null;
}

} // namespace aoa::sim::scenario
