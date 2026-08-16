#pragma once

#include "sim/components/building_footprint.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/map_grid.hpp"

#include <entt/entt.hpp>

#include <optional>
#include <vector>

namespace aoa::sim::spawn {

// Finds the next free tile for a unit leaving a building.
// Order per ring: S, then SW/SE alternating, W, E, NW/NE alternating, N.
[[nodiscard]] std::optional<core::GridPos> find_building_unit_spawn_cell(
    const components::MapGrid& map,
    entt::registry& registry,
    const components::GridPosition& building_anchor,
    const components::BuildingFootprint& building_footprint,
    const std::vector<core::GridPos>& reserved_cells = {});

} // namespace aoa::sim::spawn
