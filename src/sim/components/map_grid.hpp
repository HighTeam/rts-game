#pragma once

#include <cstddef>
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

enum class GroundType : std::uint8_t {
    Grass = 0,
    Snow = 1,
    Sand = 2,
    Dirt = 3,
};

struct MapGrid {
    int width{0};
    int height{0};
    std::vector<TileType> tiles{};
    std::vector<GroundType> ground{};
    std::vector<int> forest_wood{};
    std::vector<int> bush_food{};
    std::vector<int> mine_money{};
    std::uint64_t cached_layer_hash{0U};
    bool layer_hash_valid{false};
};

[[nodiscard]] inline GroundType ground_at(const MapGrid& map, const std::size_t index)
{
    if (index >= map.ground.size()) {
        return GroundType::Grass;
    }

    return map.ground[index];
}

struct SimState {
    std::uint64_t state_hash{0U};
};

} // namespace aoa::sim::components
