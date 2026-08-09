#pragma once

#include <cstdint>
#include <vector>

namespace aoa::sim::components {

enum class TileType : std::uint8_t {
    Grass = 0,
    Forest = 1,
    Berries = 2,
    Blueberries = 3,
    GoldMine = 4,
};

struct MapGrid {
    int width{0};
    int height{0};
    std::vector<TileType> tiles{};
    std::vector<int> forest_wood{};
    std::vector<int> bush_food{};
    std::vector<int> mine_money{};
};

struct SimState {
    std::uint64_t state_hash{0U};
};

} // namespace aoa::sim::components
