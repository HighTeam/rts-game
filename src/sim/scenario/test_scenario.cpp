#include "sim/scenario/test_scenario.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "math/fixed.hpp"
#include "sim/components/combat.hpp"
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
#include "sim/snapshot/entity_snapshot_key.hpp"
#include "sim/spawn/unit_spawn.hpp"
#include "sim/systems/visibility_system.hpp"

#include <stdexcept>

namespace aoa::sim::scenario {

namespace {

entt::entity spawn_town_center(
    entt::registry& registry,
    const data::ArchetypeDefinition& town_center_archetype,
    const core::GridPos pos,
    const std::uint8_t player_slot,
    const int starting_wood)
{
    const entt::entity town_center = registry.create();
    registry.emplace<components::BuildingTag>(town_center);
    registry.emplace<components::TownCenterTag>(town_center);
    registry.emplace<components::PlayerOwnedTag>(town_center);
    registry.emplace<components::PlayerSlot>(town_center, components::PlayerSlot{player_slot});
    registry.emplace<components::GridPosition>(town_center, components::GridPosition{pos});
    registry.emplace<components::DefinitionRef>(
        town_center,
        components::DefinitionRef{std::string(constants::TOWN_CENTER_BUILDING_ID)});
    registry.emplace<components::Health>(
        town_center,
        components::Health{town_center_archetype.max_hp, town_center_archetype.max_hp});
    registry.emplace<components::Stockpile>(town_center, components::Stockpile{starting_wood});
    return town_center;
}

entt::entity spawn_militia(
    entt::registry& registry,
    const data::ArchetypeDefinition& militia_archetype,
    const core::GridPos pos,
    const std::uint8_t player_slot)
{
    const entt::entity entity = registry.create();
    registry.emplace<components::UnitTag>(entity);
    registry.emplace<components::PlayerOwnedTag>(entity);
    registry.emplace<components::PlayerSlot>(entity, components::PlayerSlot{player_slot});
    registry.emplace<components::MilitiaUnitTag>(entity);
    registry.emplace<components::GridPosition>(entity, components::GridPosition{pos});
    registry.emplace<components::DefinitionRef>(
        entity,
        components::DefinitionRef{std::string(constants::MILITIA_UNIT_ID)});
    registry.emplace<components::Health>(
        entity,
        components::Health{militia_archetype.max_hp, militia_archetype.max_hp});
    registry.emplace<components::MoveCooldown>(entity);
    registry.emplace<components::AttackCooldown>(entity);
    registry.emplace<components::WorldPosition>(
        entity,
        components::WorldPosition{
            math::tile_center_coord(pos.x),
            math::tile_center_coord(pos.y)});
    registry.emplace<components::PreviousWorldPosition>(
        entity,
        components::PreviousWorldPosition{
            math::tile_center_coord(pos.x),
            math::tile_center_coord(pos.y)});
    return entity;
}

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

void load_test_scenario(entt::registry& registry, const data::ContentDatabase& content)
{
    const auto* forest_patch = data::find_resource_node_archetype(
        content,
        std::string(constants::FOREST_PATCH_RESOURCE_ID));
    const int forest_patch_wood = forest_patch != nullptr ? forest_patch->wood_capacity : 100;

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
    registry.emplace<components::MapGrid>(world, map::create_test_map(forest_patch_wood));
    registry.emplace<components::SimState>(world);
    registry.emplace<components::MatchSession>(world);

    constexpr std::uint8_t player1_slot = 0U;
    constexpr std::uint8_t player2_slot = 1U;

    (void)spawn_town_center(
        registry,
        *town_center_archetype,
        core::GridPos{
            constants::SCENARIO_PLAYER1_TC_X,
            constants::SCENARIO_PLAYER1_TC_Y},
        player1_slot,
        content.civ.starting_stockpile_wood);

    (void)spawn::spawn_player_worker(
        registry,
        *worker_archetype,
        core::GridPos{
            constants::SCENARIO_PLAYER1_WORKER_X,
            constants::SCENARIO_PLAYER1_WORKER_Y},
        player1_slot);

    const entt::entity player1_militia = spawn_militia(
        registry,
        *militia_archetype,
        core::GridPos{
            constants::SCENARIO_PLAYER1_MILITIA_X,
            constants::SCENARIO_PLAYER1_MILITIA_Y},
        player1_slot);

    (void)spawn_town_center(
        registry,
        *town_center_archetype,
        core::GridPos{
            constants::SCENARIO_PLAYER2_TC_X,
            constants::SCENARIO_PLAYER2_TC_Y},
        player2_slot,
        content.civ.starting_stockpile_wood);

    (void)spawn::spawn_player_worker(
        registry,
        *worker_archetype,
        core::GridPos{
            constants::SCENARIO_PLAYER2_WORKER_X,
            constants::SCENARIO_PLAYER2_WORKER_Y},
        player2_slot);

    const entt::entity player2_militia = spawn_militia(
        registry,
        *militia_archetype,
        core::GridPos{
            constants::SCENARIO_PLAYER2_MILITIA_X,
            constants::SCENARIO_PLAYER2_MILITIA_Y},
        player2_slot);

    components::AttackOrder player1_attack{};
    player1_attack.target = player2_militia;
    player1_attack.last_known_cell = core::GridPos{
        constants::SCENARIO_PLAYER2_MILITIA_X,
        constants::SCENARIO_PLAYER2_MILITIA_Y};
    registry.emplace<components::AttackOrder>(player1_militia, player1_attack);

    components::AttackOrder player2_attack{};
    player2_attack.target = player1_militia;
    player2_attack.last_known_cell = core::GridPos{
        constants::SCENARIO_PLAYER1_MILITIA_X,
        constants::SCENARIO_PLAYER1_MILITIA_Y};
    registry.emplace<components::AttackOrder>(player2_militia, player2_attack);

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
