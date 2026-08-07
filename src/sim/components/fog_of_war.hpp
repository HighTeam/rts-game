#pragma once

#include "sim/components/map_grid.hpp"

#include <cstdint>
#include <vector>

namespace aoa::sim::components {

struct FogOfWarState {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> explored{};
    std::vector<std::uint8_t> visible{};
    std::vector<std::uint8_t> memory_tiles{};
    std::vector<int> memory_forest_wood{};
};

[[nodiscard]] inline TileType fog_memory_tile_type(
    const FogOfWarState& fog,
    const MapGrid& map,
    const int x,
    const int y,
    const std::uint8_t player_slot)
{
    const std::size_t cells_per_player = static_cast<std::size_t>(fog.width * fog.height);
    const std::size_t index =
        static_cast<std::size_t>(player_slot) * cells_per_player
        + static_cast<std::size_t>(y * fog.width + x);
    if (index >= fog.memory_tiles.size()) {
        const int map_index = y * map.width + x;
        return map.tiles[static_cast<std::size_t>(map_index)];
    }

    return static_cast<TileType>(fog.memory_tiles[index]);
}

[[nodiscard]] inline int fog_memory_forest_wood(
    const FogOfWarState& fog,
    const MapGrid& map,
    const int x,
    const int y,
    const std::uint8_t player_slot)
{
    const std::size_t cells_per_player = static_cast<std::size_t>(fog.width * fog.height);
    const std::size_t index =
        static_cast<std::size_t>(player_slot) * cells_per_player
        + static_cast<std::size_t>(y * fog.width + x);
    if (index >= fog.memory_forest_wood.size()) {
        const int map_index = y * map.width + x;
        return map.forest_wood[static_cast<std::size_t>(map_index)];
    }

    return fog.memory_forest_wood[index];
}

} // namespace aoa::sim::components
