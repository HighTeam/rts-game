#include "sim/map/test_map.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"

namespace aoa::sim::map {

components::MapGrid create_test_map(const int forest_patch_wood)
{
    components::MapGrid map{};
    map.width = constants::MAP_TEST_WIDTH;
    map.height = constants::MAP_TEST_HEIGHT;
    map.tiles.assign(static_cast<std::size_t>(map.width * map.height), components::TileType::Grass);
    map.forest_wood.assign(map.tiles.size(), 0);

    const auto mark_forest = [&](const core::GridPos pos) {
        if (!core::is_inside_grid(pos, map.width, map.height)) {
            return;
        }

        const int index = core::grid_index(pos, map.width);
        map.tiles[static_cast<std::size_t>(index)] = components::TileType::Forest;
        map.forest_wood[static_cast<std::size_t>(index)] = forest_patch_wood;
    };

    for (int y = 12; y <= 18; ++y) {
        for (int x = 20; x <= 28; ++x) {
            mark_forest({x, y});
        }
    }

    for (int y = 30; y <= 36; ++y) {
        for (int x = 40; x <= 48; ++x) {
            mark_forest({x, y});
        }
    }

    return map;
}

} // namespace aoa::sim::map
