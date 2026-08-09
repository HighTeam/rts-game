#include "sim/scenario/test_scenario.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
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
#include "sim/map/test_map.hpp"
#include "sim/scenario/scenario_layouts.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"
#include "sim/spawn/unit_spawn.hpp"
#include "sim/systems/visibility_system.hpp"

#include <stdexcept>

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

} // namespace

void load_test_scenario(
    entt::registry& registry,
    const data::ContentDatabase& content,
    const std::uint8_t player_count)
{
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
    const auto* militia_archetype =
        data::find_unit_archetype(content, std::string(constants::MILITIA_UNIT_ID));

    if (town_center_archetype == nullptr || worker_archetype == nullptr || militia_archetype == nullptr) {
        throw std::runtime_error("Missing required archetypes for test scenario");
    }

    const entt::entity world = registry.create();
    registry.emplace<components::WorldTag>(world);
    registry.emplace<components::ContentPack>(world, components::ContentPack{content});
    registry.emplace<components::MapGrid>(
        world,
        map::create_test_map(
            forest_patch_wood,
            bush_food_capacity,
            mine_money_capacity,
            player_count));
    registry.emplace<components::SimState>(world);
    registry.emplace<components::MatchSession>(world);

    for (std::size_t layout_index = 0U; layout_index < layouts.size(); ++layout_index) {
        const PlayerBaseLayout& layout = layouts[layout_index];
        const std::uint8_t player_slot = static_cast<std::uint8_t>(layout_index);

        (void)spawn::spawn_player_town_center(
            registry,
            *town_center_archetype,
            core::GridPos{layout.tc_x, layout.tc_y},
            player_slot,
            components::Stockpile{
                content.civ.starting_stockpile_wood,
                content.civ.starting_stockpile_food,
                content.civ.starting_stockpile_money,
                content.civ.starting_stockpile_mana,
            });

        (void)spawn::spawn_player_worker(
            registry,
            *worker_archetype,
            core::GridPos{layout.worker_x, layout.worker_y},
            player_slot);

        (void)spawn::spawn_player_militia(
            registry,
            *militia_archetype,
            core::GridPos{layout.militia_x, layout.militia_y},
            player_slot);
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
