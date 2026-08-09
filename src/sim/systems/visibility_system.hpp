#pragma once



#include "core/grid.hpp"



#include <entt/entt.hpp>

#include <cstdint>
#include <vector>



namespace aoa::sim::components {

struct ContentPack;

struct FogOfWarState;

struct MapGrid;

} // namespace aoa::sim::components



namespace aoa::sim::systems {



void initialize_fog_of_war(entt::registry& registry);



void run_visibility_system(entt::registry& registry);



void rebuild_fog_visibility(entt::registry& registry);



// A single vision-granting entity's position and reveal radius (in tile
// units, already including FOG_VISION_RADIUS_TILE_PADDING). Rendering uses
// this to build a smooth, distance-accurate fade that never extends past the
// entity's real vision radius, and automatically follows whatever radius
// each unit/building type actually has.
struct VisionSource {
    float origin_x{0.0F};
    float origin_y{0.0F};
    float radius{0.0F};
};

[[nodiscard]] std::vector<VisionSource> collect_vision_sources_for_slot(
    const entt::registry& registry,
    std::uint8_t player_slot);



[[nodiscard]] bool is_cell_visible_to_slot(

    const components::FogOfWarState& fog,

    const core::GridPos cell,

    const std::uint8_t player_slot);



[[nodiscard]] bool is_cell_explored_to_slot(

    const components::FogOfWarState& fog,

    const core::GridPos cell,

    const std::uint8_t player_slot);



[[nodiscard]] bool is_cell_in_explored_shroud(

    const components::FogOfWarState& fog,

    const core::GridPos cell,

    const std::uint8_t player_slot);



[[nodiscard]] core::GridPos entity_visibility_cell(
    const entt::registry& registry,
    entt::entity entity);

[[nodiscard]] bool is_entity_visible_to_slot(

    const entt::registry& registry,

    const components::FogOfWarState& fog,

    const entt::entity entity,

    const std::uint8_t player_slot);



[[nodiscard]] bool is_opponent_entity_visible_to_slot(

    const entt::registry& registry,

    const components::FogOfWarState& fog,

    const entt::entity entity,

    const std::uint8_t player_slot);



[[nodiscard]] bool is_building_renderable_in_shroud(

    const entt::registry& registry,

    const components::FogOfWarState& fog,

    const entt::entity entity,

    const std::uint8_t player_slot);



} // namespace aoa::sim::systems

