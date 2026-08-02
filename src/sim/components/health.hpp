#pragma once

#include "math/fixed.hpp"

namespace aoa::sim::components {

struct Health {
    math::Fixed current{};
    math::Fixed max{};
};

} // namespace aoa::sim::components
