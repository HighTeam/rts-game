#pragma once

#include "math/fixed.hpp"

namespace aoa::sim::components {

// Placeholder sim state stored as ECS data until gameplay components land in M1.
struct MotionState {
    math::Fixed value{math::Fixed::from_int(0)};
};

} // namespace aoa::sim::components
