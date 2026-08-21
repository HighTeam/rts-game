#pragma once

#include "sim/components/map_grid.hpp"

#include <cstdint>

namespace aoa::sim::map {

components::MapGrid create_test_map(
    int forest_patch_wood,
    int bush_food_capacity,
    int mine_money_capacity,
    std::uint8_t player_count,
    std::uint64_t seed = 0U);

} // namespace aoa::sim::map
