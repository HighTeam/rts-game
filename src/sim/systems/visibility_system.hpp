#pragma once



#include "core/grid.hpp"



#include <entt/entt.hpp>

#include <cstdint>



namespace aoa::sim::components {

struct ContentPack;

struct FogOfWarState;

struct MapGrid;

} // namespace aoa::sim::components



namespace aoa::sim::systems {



void initialize_fog_of_war(entt::registry& registry);



void run_visibility_system(entt::registry& registry);



void rebuild_fog_visibility(entt::registry& registry);



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

