#pragma once

#include "core/grid.hpp"
#include "sim/components/map_grid.hpp"

#include <vector>

namespace aoa::sim::systems {

[[nodiscard]] bool is_tile_walkable(
    const components::MapGrid& map,
    core::GridPos pos,
    bool allow_forest);

[[nodiscard]] std::vector<core::GridPos> find_path(
    const components::MapGrid& map,
    core::GridPos start,
    core::GridPos goal,
    bool allow_forest = false);

} // namespace aoa::sim::systems
