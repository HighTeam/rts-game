#pragma once

#include "core/grid.hpp"
#include "sim/components/map_grid.hpp"

#include <vector>

namespace aoa::sim::systems {

std::vector<core::GridPos> find_path(
    const components::MapGrid& map,
    core::GridPos start,
    core::GridPos goal);

} // namespace aoa::sim::systems
