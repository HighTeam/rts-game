#pragma once

#include "core/grid.hpp"
#include "data/content_types.hpp"

#include <entt/entt.hpp>
#include <string>

namespace aoa::sim::spawn {

[[nodiscard]] entt::entity spawn_player_worker(
    entt::registry& registry,
    const data::ArchetypeDefinition& worker_archetype,
    core::GridPos spawn_cell);

} // namespace aoa::sim::spawn
