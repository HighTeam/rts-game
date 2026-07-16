#pragma once

#include "math/fixed.hpp"

#include <cstdint>

namespace aoa::sim {

class Simulation {
public:
    void tick();

    [[nodiscard]] std::uint64_t tick_count() const { return tick_count_; }

    // Advances each sim tick; used later to validate fixed-point in the loop.
    [[nodiscard]] math::Fixed motion_sample() const { return motion_sample_; }

private:
    std::uint64_t tick_count_{0U};
    math::Fixed motion_sample_{math::Fixed::from_int(0)};
};

} // namespace aoa::sim
