#pragma once

#include "core/grid.hpp"

#include <cstdint>
#include <vector>

namespace aoa::sim::components {

struct MapPing {
    core::GridPos cell{};
    std::uint8_t player_slot{0U};
    int ticks_remaining{0};
};

struct MapPingList {
    std::vector<MapPing> pings{};
};

} // namespace aoa::sim::components
