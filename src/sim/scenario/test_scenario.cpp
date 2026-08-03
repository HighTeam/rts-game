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

#include "sim/components/movement.hpp"

#include "sim/components/resources.hpp"

#include "sim/components/tags.hpp"

#include "sim/components/world_position.hpp"

#include "sim/map/test_map.hpp"

#include <stdexcept>

namespace aoa::sim::scenario {



namespace {



entt::entity spawn_unit(

    entt::registry& registry,

    const std::string& unit_id,

    const data::ArchetypeDefinition& archetype,

    const core::GridPos pos,

    const bool is_player)

{

    const entt::entity entity = registry.create();

    registry.emplace<components::UnitTag>(entity);

    registry.emplace<components::GridPosition>(entity, components::GridPosition{pos});

    registry.emplace<components::DefinitionRef>(entity, components::DefinitionRef{unit_id});

    registry.emplace<components::Health>(

        entity,

        components::Health{archetype.max_hp, archetype.max_hp});

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



    if (is_player) {

        registry.emplace<components::PlayerOwnedTag>(entity);

    }

    else {

        registry.emplace<components::EnemyTag>(entity);

    }



    return entity;

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



    const entt::entity town_center = registry.create();

    registry.emplace<components::BuildingTag>(town_center);

    registry.emplace<components::TownCenterTag>(town_center);

    registry.emplace<components::PlayerOwnedTag>(town_center);

    registry.emplace<components::GridPosition>(town_center, components::GridPosition{{8, 8}});

    registry.emplace<components::DefinitionRef>(

        town_center,

        components::DefinitionRef{std::string(constants::TOWN_CENTER_BUILDING_ID)});

    registry.emplace<components::Health>(

        town_center,

        components::Health{town_center_archetype->max_hp, town_center_archetype->max_hp});

    registry.emplace<components::Stockpile>(town_center, components::Stockpile{content.civ.starting_stockpile_wood});



    const entt::entity worker = spawn_unit(

        registry,

        std::string(constants::WORKER_UNIT_ID),

        *worker_archetype,

        core::GridPos{9, 8},

        true);

    registry.emplace<components::WorkerUnitTag>(worker);

    registry.emplace<components::WorkerBrain>(worker);

    registry.emplace<components::CarriedWood>(worker);



    const entt::entity player_militia = spawn_unit(

        registry,

        std::string(constants::MILITIA_UNIT_ID),

        *militia_archetype,

        core::GridPos{10, 8},

        true);

    registry.emplace<components::MilitiaUnitTag>(player_militia);



    const entt::entity enemy_militia = spawn_unit(

        registry,

        std::string(constants::MILITIA_UNIT_ID),

        *militia_archetype,

        core::GridPos{45, 45},

        false);

    registry.emplace<components::MilitiaUnitTag>(enemy_militia);

    registry.emplace<components::AttackOrder>(player_militia, components::AttackOrder{enemy_militia});

}



} // namespace aoa::sim::scenario

