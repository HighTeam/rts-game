#pragma once

#include "math/fixed.hpp"

namespace aoa::sim::components {

struct WorldPosition {
    math::Fixed x{};
    math::Fixed y{};
};

struct PreviousWorldPosition {
    math::Fixed x{};
    math::Fixed y{};
};

struct MoveSegment {
    math::Fixed from_x{};
    math::Fixed from_y{};
    math::Fixed to_x{};
    math::Fixed to_y{};
    int ticks_elapsed{0};
    int ticks_total{0};
};

} // namespace aoa::sim::components
