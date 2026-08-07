#pragma once

#include "core/grid.hpp"
#include "math/fixed.hpp"

#include <vector>

namespace aoa::sim::components {

struct MovePath {
    std::vector<core::GridPos> cells{};
    int next_index{0};
    bool has_goal_world{false};
    math::Fixed goal_world_x{};
    math::Fixed goal_world_y{};
};

struct MoveCooldown {
    int ticks_remaining{0};
};

} // namespace aoa::sim::components
