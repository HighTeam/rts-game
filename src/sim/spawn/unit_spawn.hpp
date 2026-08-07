#pragma once

#include "core/grid.hpp"
#include "data/content_types.hpp"

#include <entt/entt.hpp>
#include <string>

namespace aoa::sim::spawn {

[[nodiscard]] entt::entity spawn_player_worker(
    entt::registry& registry,
    const data::ArchetypeDefinition& worker_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot);

[[nodiscard]] entt::entity spawn_player_militia(
    entt::registry& registry,
    const data::ArchetypeDefinition& militia_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot);

[[nodiscard]] entt::entity spawn_player_town_center(
    entt::registry& registry,
    const data::ArchetypeDefinition& town_center_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    int starting_wood);

} // namespace aoa::sim::spawn
