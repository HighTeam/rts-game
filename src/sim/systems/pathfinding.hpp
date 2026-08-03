#pragma once

#include "core/grid.hpp"
#include "sim/components/map_grid.hpp"

#include <entt/entt.hpp>
#include <vector>

namespace aoa::sim::systems {

[[nodiscard]] bool is_tile_walkable(
    const components::MapGrid& map,
    core::GridPos pos,
    bool allow_forest);

[[nodiscard]] bool is_movement_blocked(
    entt::registry& registry,
    core::GridPos cell,
    entt::entity ignore = entt::null);

[[nodiscard]] std::vector<core::GridPos> find_path(
    const components::MapGrid& map,
    core::GridPos start,
    core::GridPos goal,
    entt::registry& registry,
    entt::entity ignore = entt::null,
    bool allow_forest = false);

} // namespace aoa::sim::systems
