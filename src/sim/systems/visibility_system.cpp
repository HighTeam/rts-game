#include "sim/systems/visibility_system.hpp"



#include "core/constants.hpp"

#include "core/grid.hpp"

#include "data/content_types.hpp"

#include "sim/components/definition_ref.hpp"

#include "sim/components/content_pack.hpp"

#include "sim/components/fog_of_war.hpp"

#include "sim/components/grid_position.hpp"

#include "sim/components/health.hpp"

#include "sim/components/map_grid.hpp"

#include "sim/components/player_slot.hpp"

#include "sim/components/tags.hpp"

#include "sim/components/world_position.hpp"

#include "sim/snapshot/entity_snapshot_key.hpp"

#include "math/fixed.hpp"

#include <cmath>

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



[[nodiscard]] std::size_t fog_cell_index(

    const components::FogOfWarState& fog,

    const int x,

    const int y,

    const std::uint8_t player_slot)

{

    return static_cast<std::size_t>(player_slot * fog.width * fog.height + y * fog.width + x);

}



[[nodiscard]] bool fog_cell_in_bounds(

    const components::FogOfWarState& fog,

    const int x,

    const int y)

{

    return x >= 0 && y >= 0 && x < fog.width && y < fog.height;

}



void sync_fog_memory_from_map(

    components::FogOfWarState& fog,

    const components::MapGrid& map,

    const int x,

    const int y,

    const std::uint8_t player_slot)

{

    if (!fog_cell_in_bounds(fog, x, y) || !core::is_inside_grid({x, y}, map.width, map.height)) {

        return;

    }



    const std::size_t fog_index = fog_cell_index(fog, x, y, player_slot);

    const int map_index = core::grid_index({x, y}, map.width);

    fog.memory_tiles[fog_index] = static_cast<std::uint8_t>(map.tiles[static_cast<std::size_t>(map_index)]);

    fog.memory_forest_wood[fog_index] = map.forest_wood[static_cast<std::size_t>(map_index)];

}



void set_fog_cell(

    components::FogOfWarState& fog,

    const components::MapGrid& map,

    const int x,

    const int y,

    const std::uint8_t player_slot,

    const bool mark_visible)

{

    if (!fog_cell_in_bounds(fog, x, y)) {

        return;

    }



    const std::size_t index = fog_cell_index(fog, x, y, player_slot);

    fog.explored[index] = 1U;

    if (mark_visible) {

        fog.visible[index] = 1U;

        sync_fog_memory_from_map(fog, map, x, y, player_slot);

    }

}



void set_fog_visible_cell(

    components::FogOfWarState& fog,

    const int x,

    const int y,

    const std::uint8_t player_slot)

{

    if (!fog_cell_in_bounds(fog, x, y)) {

        return;

    }



    fog.visible[fog_cell_index(fog, x, y, player_slot)] = 1U;

}



void reveal_vision_circle(
    components::FogOfWarState& fog,
    const components::MapGrid& map,
    const math::Fixed center_x,
    const math::Fixed center_y,
    const int vision_range,
    const std::uint8_t player_slot,
    const bool mark_explored)
{
    const float radius =
        static_cast<float>(vision_range) + aoa::constants::FOG_VISION_RADIUS_TILE_PADDING;
    const float radius_squared = radius * radius;
    const float origin_x = center_x.to_float();
    const float origin_y = center_y.to_float();
    const int origin_cell_x = static_cast<int>(std::floor(origin_x));
    const int origin_cell_y = static_cast<int>(std::floor(origin_y));
    const int scan_radius = vision_range + 1;

    for (int delta_y = -scan_radius; delta_y <= scan_radius; ++delta_y) {
        for (int delta_x = -scan_radius; delta_x <= scan_radius; ++delta_x) {
            const int x = origin_cell_x + delta_x;
            const int y = origin_cell_y + delta_y;
            if (!fog_cell_in_bounds(fog, x, y)) {
                continue;
            }

            const float tile_center_x = static_cast<float>(x) + 0.5F;
            const float tile_center_y = static_cast<float>(y) + 0.5F;
            const float offset_x = tile_center_x - origin_x;
            const float offset_y = tile_center_y - origin_y;
            if ((offset_x * offset_x) + (offset_y * offset_y) > radius_squared) {
                continue;
            }

            if (mark_explored) {
                set_fog_cell(fog, map, x, y, player_slot, true);
            }
            else {
                set_fog_visible_cell(fog, x, y, player_slot);
            }
        }
    }
}



[[nodiscard]] const data::ArchetypeDefinition* find_archetype_for_entity(

    const components::ContentPack& content_pack,

    const components::DefinitionRef& definition_ref)

{

    if (const data::ArchetypeDefinition* unit = data::find_unit_archetype(content_pack.content, definition_ref.id)) {

        return unit;

    }



    if (const data::ArchetypeDefinition* structure =

            data::find_structure_archetype(content_pack.content, definition_ref.id)) {

        return structure;

    }



    return data::find_archetype(content_pack.content, definition_ref.id);

}



[[nodiscard]] int vision_range_for_entity(

    entt::registry& registry,

    const components::ContentPack& content_pack,

    const entt::entity entity)

{

    if (registry.any_of<components::WorkerUnitTag>(entity)) {

        return aoa::constants::DEFAULT_WORKER_VISION_RANGE;

    }



    if (!registry.any_of<components::DefinitionRef>(entity)) {

        return aoa::constants::DEFAULT_UNIT_VISION_RANGE;

    }



    const auto& definition_ref = registry.get<components::DefinitionRef>(entity);

    const data::ArchetypeDefinition* definition = find_archetype_for_entity(content_pack, definition_ref);

    if (definition == nullptr) {

        return aoa::constants::DEFAULT_UNIT_VISION_RANGE;

    }



    if (definition->vision_range > 0) {

        return definition->vision_range;

    }



    if (registry.any_of<components::TownCenterTag>(entity)) {

        return aoa::constants::DEFAULT_TOWN_CENTER_VISION_RANGE;

    }



    if (registry.any_of<components::UnitTag>(entity)) {

        return aoa::constants::DEFAULT_UNIT_VISION_RANGE;

    }



    return aoa::constants::DEFAULT_STRUCTURE_VISION_RANGE;

}



} // namespace



void initialize_fog_of_war(entt::registry& registry)

{

    const entt::entity world = find_world_entity(registry);

    if (world == entt::null || !registry.any_of<components::MapGrid>(world)) {

        return;

    }



    const auto& map = registry.get<components::MapGrid>(world);

    components::FogOfWarState fog{};

    fog.width = map.width;

    fog.height = map.height;

    const std::size_t cells_per_player =

        static_cast<std::size_t>(map.width * map.height);

    const std::size_t total_cells =

        cells_per_player * static_cast<std::size_t>(aoa::constants::MAX_PLAYER_SLOTS);

    fog.explored.assign(total_cells, 0U);

    fog.visible.assign(total_cells, 0U);

    fog.memory_tiles.assign(total_cells, static_cast<std::uint8_t>(components::TileType::Grass));

    fog.memory_forest_wood.assign(total_cells, 0);



    if (registry.any_of<components::FogOfWarState>(world)) {

        registry.replace<components::FogOfWarState>(world, std::move(fog));

    }

    else {

        registry.emplace<components::FogOfWarState>(world, std::move(fog));

    }



    run_visibility_system(registry);

}



void run_visibility_system(entt::registry& registry)

{

    const entt::entity world = find_world_entity(registry);

    if (world == entt::null || !registry.all_of<components::MapGrid, components::FogOfWarState, components::ContentPack>(world)) {

        return;

    }



    auto& fog = registry.get<components::FogOfWarState>(world);

    const auto& map = registry.get<components::MapGrid>(world);

    const auto& content_pack = registry.get<components::ContentPack>(world);

    std::fill(fog.visible.begin(), fog.visible.end(), 0U);



    const auto vision_view = registry.view<

        components::PlayerOwnedTag,

        components::GridPosition,

        components::Health,

        components::DefinitionRef>();



    std::vector<entt::entity> providers(vision_view.begin(), vision_view.end());

    providers = snapshot::sort_entities_by_snapshot_key(registry, std::move(providers));



    for (const entt::entity entity : providers) {

        if (vision_view.get<components::Health>(entity).current.raw() <= 0) {

            continue;

        }



        const std::uint8_t player_slot = components::entity_player_slot(registry, entity);

        math::Fixed origin_x = math::tile_center_coord(
            vision_view.get<components::GridPosition>(entity).cell.x);
        math::Fixed origin_y = math::tile_center_coord(
            vision_view.get<components::GridPosition>(entity).cell.y);
        if (registry.all_of<components::WorldPosition>(entity)) {
            const auto& world_position = registry.get<components::WorldPosition>(entity);
            origin_x = world_position.x;
            origin_y = world_position.y;
        }

        const int vision_range = vision_range_for_entity(registry, content_pack, entity);

        reveal_vision_circle(fog, map, origin_x, origin_y, vision_range, player_slot, true);

    }

}



void rebuild_fog_visibility(entt::registry& registry)

{

    const entt::entity world = find_world_entity(registry);

    if (world == entt::null || !registry.all_of<components::MapGrid, components::FogOfWarState, components::ContentPack>(world)) {

        return;

    }



    auto& fog = registry.get<components::FogOfWarState>(world);

    const auto& map = registry.get<components::MapGrid>(world);

    const auto& content_pack = registry.get<components::ContentPack>(world);

    std::fill(fog.visible.begin(), fog.visible.end(), 0U);



    const auto vision_view = registry.view<

        components::PlayerOwnedTag,

        components::GridPosition,

        components::Health,

        components::DefinitionRef>();



    std::vector<entt::entity> providers(vision_view.begin(), vision_view.end());

    providers = snapshot::sort_entities_by_snapshot_key(registry, std::move(providers));



    for (const entt::entity entity : providers) {

        if (vision_view.get<components::Health>(entity).current.raw() <= 0) {

            continue;

        }



        const std::uint8_t player_slot = components::entity_player_slot(registry, entity);

        math::Fixed origin_x = math::tile_center_coord(
            vision_view.get<components::GridPosition>(entity).cell.x);
        math::Fixed origin_y = math::tile_center_coord(
            vision_view.get<components::GridPosition>(entity).cell.y);
        if (registry.all_of<components::WorldPosition>(entity)) {
            const auto& world_position = registry.get<components::WorldPosition>(entity);
            origin_x = world_position.x;
            origin_y = world_position.y;
        }

        const int vision_range = vision_range_for_entity(registry, content_pack, entity);

        reveal_vision_circle(fog, map, origin_x, origin_y, vision_range, player_slot, false);

    }

}



bool is_cell_visible_to_slot(

    const components::FogOfWarState& fog,

    const core::GridPos cell,

    const std::uint8_t player_slot)

{

    if (!fog_cell_in_bounds(fog, cell.x, cell.y) || player_slot >= aoa::constants::MAX_PLAYER_SLOTS) {

        return false;

    }



    return fog.visible[fog_cell_index(fog, cell.x, cell.y, player_slot)] != 0U;

}



bool is_cell_explored_to_slot(

    const components::FogOfWarState& fog,

    const core::GridPos cell,

    const std::uint8_t player_slot)

{

    if (!fog_cell_in_bounds(fog, cell.x, cell.y) || player_slot >= aoa::constants::MAX_PLAYER_SLOTS) {

        return false;

    }



    return fog.explored[fog_cell_index(fog, cell.x, cell.y, player_slot)] != 0U;

}



bool is_cell_in_explored_shroud(

    const components::FogOfWarState& fog,

    const core::GridPos cell,

    const std::uint8_t player_slot)

{

    return is_cell_explored_to_slot(fog, cell, player_slot)

        && !is_cell_visible_to_slot(fog, cell, player_slot);

}



core::GridPos entity_visibility_cell(
    const entt::registry& registry,
    const entt::entity entity)
{
    if (entity == entt::null || !registry.valid(entity)) {
        return {0, 0};
    }

    if (registry.all_of<components::WorldPosition>(entity)) {
        const auto& world = registry.get<components::WorldPosition>(entity);
        return {world.x.to_int(), world.y.to_int()};
    }

    if (registry.all_of<components::GridPosition>(entity)) {
        return registry.get<components::GridPosition>(entity).cell;
    }

    return {0, 0};
}



bool is_entity_visible_to_slot(

    const entt::registry& registry,

    const components::FogOfWarState& fog,

    const entt::entity entity,

    const std::uint8_t player_slot)

{

    if (entity == entt::null || !registry.valid(entity)) {

        return false;

    }



    if (registry.any_of<components::PlayerOwnedTag>(entity)

        && components::entity_player_slot(registry, entity) == player_slot) {

        return true;

    }



    return is_cell_visible_to_slot(fog, entity_visibility_cell(registry, entity), player_slot);

}



bool is_opponent_entity_visible_to_slot(

    const entt::registry& registry,

    const components::FogOfWarState& fog,

    const entt::entity entity,

    const std::uint8_t player_slot)

{

    if (!components::is_opponent_entity(registry, entity, player_slot)

        && !registry.any_of<components::EnemyTag>(entity)) {

        return true;

    }



    return is_entity_visible_to_slot(registry, fog, entity, player_slot);

}



bool is_building_renderable_in_shroud(

    const entt::registry& registry,

    const components::FogOfWarState& fog,

    const entt::entity entity,

    const std::uint8_t player_slot)

{

    if (entity == entt::null || !registry.valid(entity) || !registry.any_of<components::BuildingTag>(entity)

        || !registry.any_of<components::GridPosition>(entity)) {

        return false;

    }



    if (is_entity_visible_to_slot(registry, fog, entity, player_slot)) {

        return false;

    }



    if (!components::is_opponent_entity(registry, entity, player_slot)) {

        return false;

    }



    return is_cell_in_explored_shroud(

        fog,

        registry.get<components::GridPosition>(entity).cell,

        player_slot);

}



} // namespace aoa::sim::systems

