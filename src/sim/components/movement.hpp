#pragma once

#include <vector>

#include "core/grid.hpp"

namespace aoa::sim::components {

struct MovePath {
    std::vector<core::GridPos> cells{};
    int next_index{0};
};

struct MoveCooldown {
    int ticks_remaining{0};
};

} // namespace aoa::sim::components
