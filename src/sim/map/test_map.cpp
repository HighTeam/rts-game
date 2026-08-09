#include "sim/map/test_map.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "sim/scenario/scenario_layouts.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace aoa::sim::map {

namespace {

[[nodiscard]] std::uint32_t xorshift32(std::uint32_t& state)
{
    std::uint32_t x = state;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    state = x;
    return x;
}

[[nodiscard]] bool is_grass_empty(const components::MapGrid& map, const core::GridPos pos)
{
    if (!core::is_inside_grid(pos, map.width, map.height)) {
        return false;
    }

    const int index = core::grid_index(pos, map.width);
    return map.tiles[static_cast<std::size_t>(index)] == components::TileType::Grass;
}

[[nodiscard]] bool inside_tc_footprint(const core::GridPos cell, const int tc_x, const int tc_y)
{
    return cell.x >= tc_x && cell.x < tc_x + constants::TOWN_CENTER_FOOTPRINT_TILES
        && cell.y >= tc_y && cell.y < tc_y + constants::TOWN_CENTER_FOOTPRINT_TILES;
}

void place_berry_patch(
    components::MapGrid& map,
    const int tc_x,
    const int tc_y,
    const int bush_food_capacity)
{
    const int footprint = constants::TOWN_CENTER_FOOTPRINT_TILES;
    std::vector<core::GridPos> candidates{};
    candidates.reserve(64U);

    for (int ring = 1; ring <= 4; ++ring) {
        for (int y = tc_y - ring; y <= tc_y + footprint - 1 + ring; ++y) {
            for (int x = tc_x - ring; x <= tc_x + footprint - 1 + ring; ++x) {
                const core::GridPos cell{x, y};
                if (inside_tc_footprint(cell, tc_x, tc_y)) {
                    continue;
                }

                const int dx = std::max(
                    0,
                    std::max(tc_x - x, x - (tc_x + footprint - 1)));
                const int dy = std::max(
                    0,
                    std::max(tc_y - y, y - (tc_y + footprint - 1)));
                if (std::max(dx, dy) != ring) {
                    continue;
                }

                if (!is_grass_empty(map, cell)) {
                    continue;
                }

                candidates.push_back(cell);
            }
        }
    }

    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [tc_x, tc_y, footprint](const core::GridPos left, const core::GridPos right) {
            const int left_south_east =
                (left.y >= tc_y + footprint ? 2 : 0) + (left.x >= tc_x + footprint ? 1 : 0);
            const int right_south_east =
                (right.y >= tc_y + footprint ? 2 : 0) + (right.x >= tc_x + footprint ? 1 : 0);
            if (left_south_east != right_south_east) {
                return left_south_east > right_south_east;
            }

            if (left.y != right.y) {
                return left.y > right.y;
            }

            return left.x > right.x;
        });

    int placed = 0;
    for (const core::GridPos cell : candidates) {
        if (placed >= constants::BERRY_PATCH_TILE_COUNT) {
            break;
        }

        if (!is_grass_empty(map, cell)) {
            continue;
        }

        const int index = core::grid_index(cell, map.width);
        map.tiles[static_cast<std::size_t>(index)] = components::TileType::Berries;
        map.bush_food[static_cast<std::size_t>(index)] = bush_food_capacity;
        ++placed;
    }
}

[[nodiscard]] bool far_enough_from_bases(
    const core::GridPos cell,
    const std::span<const scenario::PlayerBaseLayout> layouts)
{
    for (const scenario::PlayerBaseLayout& layout : layouts) {
        const core::GridPos tc{layout.tc_x, layout.tc_y};
        if (core::chebyshev_distance(cell, tc) < constants::BLUEBERRY_MIN_DISTANCE_FROM_BASE) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool try_place_blueberry_cluster(
    components::MapGrid& map,
    const core::GridPos seed,
    const int bush_food_capacity,
    const std::span<const scenario::PlayerBaseLayout> layouts)
{
    if (!is_grass_empty(map, seed) || !far_enough_from_bases(seed, layouts)) {
        return false;
    }

    constexpr std::array<core::GridPos, 4> cardinals{{
        {1, 0},
        {0, 1},
        {-1, 0},
        {0, -1},
    }};

    std::vector<core::GridPos> cluster{seed};
    for (int grow = 1; grow < constants::BLUEBERRY_CLUSTER_SIZE; ++grow) {
        bool extended = false;
        for (const core::GridPos base : cluster) {
            for (const core::GridPos offset : cardinals) {
                const core::GridPos candidate{base.x + offset.x, base.y + offset.y};
                if (!is_grass_empty(map, candidate) || !far_enough_from_bases(candidate, layouts)) {
                    continue;
                }

                bool already = false;
                for (const core::GridPos existing : cluster) {
                    if (existing == candidate) {
                        already = true;
                        break;
                    }
                }
                if (already) {
                    continue;
                }

                cluster.push_back(candidate);
                extended = true;
                break;
            }
            if (extended) {
                break;
            }
        }

        if (!extended) {
            return false;
        }
    }

    if (static_cast<int>(cluster.size()) < constants::BLUEBERRY_CLUSTER_SIZE) {
        return false;
    }

    for (const core::GridPos cell : cluster) {
        const int index = core::grid_index(cell, map.width);
        map.tiles[static_cast<std::size_t>(index)] = components::TileType::Blueberries;
        map.bush_food[static_cast<std::size_t>(index)] = bush_food_capacity;
    }

    return true;
}

void place_gold_mines_near_tc(
    components::MapGrid& map,
    const int tc_x,
    const int tc_y,
    const int mine_money_capacity)
{
    const int footprint = constants::TOWN_CENTER_FOOTPRINT_TILES;
    std::vector<core::GridPos> candidates{};
    candidates.reserve(64U);

    for (int ring = 2; ring <= 5; ++ring) {
        for (int y = tc_y - ring; y <= tc_y + footprint - 1 + ring; ++y) {
            for (int x = tc_x - ring; x <= tc_x + footprint - 1 + ring; ++x) {
                const core::GridPos cell{x, y};
                if (inside_tc_footprint(cell, tc_x, tc_y)) {
                    continue;
                }

                const int dx = std::max(
                    0,
                    std::max(tc_x - x, x - (tc_x + footprint - 1)));
                const int dy = std::max(
                    0,
                    std::max(tc_y - y, y - (tc_y + footprint - 1)));
                if (std::max(dx, dy) != ring) {
                    continue;
                }

                if (!is_grass_empty(map, cell)) {
                    continue;
                }

                candidates.push_back(cell);
            }
        }
    }

    // Prefer north-west of the TC so berry patches (south-east bias) stay clear.
    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [tc_x, tc_y](const core::GridPos left, const core::GridPos right) {
            const int left_north_west =
                (left.y < tc_y ? 2 : 0) + (left.x < tc_x ? 1 : 0);
            const int right_north_west =
                (right.y < tc_y ? 2 : 0) + (right.x < tc_x ? 1 : 0);
            if (left_north_west != right_north_west) {
                return left_north_west > right_north_west;
            }

            if (left.y != right.y) {
                return left.y < right.y;
            }

            return left.x < right.x;
        });

    int placed = 0;
    for (const core::GridPos cell : candidates) {
        if (placed >= constants::GOLD_MINE_NEAR_BASE_COUNT) {
            break;
        }

        if (!is_grass_empty(map, cell)) {
            continue;
        }

        const int index = core::grid_index(cell, map.width);
        map.tiles[static_cast<std::size_t>(index)] = components::TileType::GoldMine;
        map.mine_money[static_cast<std::size_t>(index)] = mine_money_capacity;
        ++placed;
    }
}

void place_blueberry_clusters(
    components::MapGrid& map,
    const std::span<const scenario::PlayerBaseLayout> layouts,
    const int bush_food_capacity,
    const std::uint8_t player_count)
{
    std::uint32_t rng_state = static_cast<std::uint32_t>(map.width)
        ^ (static_cast<std::uint32_t>(map.height) << 16U)
        ^ (static_cast<std::uint32_t>(player_count) * 0x9E3779B9U);
    if (rng_state == 0U) {
        rng_state = 1U;
    }

    int placed_clusters = 0;
    for (int attempt = 0;
         attempt < constants::BLUEBERRY_PLACEMENT_MAX_ATTEMPTS
             && placed_clusters < constants::BLUEBERRY_CLUSTER_COUNT;
         ++attempt) {
        const int x = static_cast<int>(xorshift32(rng_state) % static_cast<std::uint32_t>(map.width));
        const int y = static_cast<int>(xorshift32(rng_state) % static_cast<std::uint32_t>(map.height));
        const core::GridPos seed{x, y};
        if (!far_enough_from_bases(seed, layouts)) {
            continue;
        }

        if (!try_place_blueberry_cluster(map, seed, bush_food_capacity, layouts)) {
            continue;
        }

        ++placed_clusters;
    }
}

} // namespace

components::MapGrid create_test_map(
    const int forest_patch_wood,
    const int bush_food_capacity,
    const int mine_money_capacity,
    const std::uint8_t player_count)
{
    components::MapGrid map{};
    map.width = constants::MAP_TEST_WIDTH;
    map.height = constants::MAP_TEST_HEIGHT;
    map.tiles.assign(static_cast<std::size_t>(map.width * map.height), components::TileType::Grass);
    map.forest_wood.assign(map.tiles.size(), 0);
    map.bush_food.assign(map.tiles.size(), 0);
    map.mine_money.assign(map.tiles.size(), 0);

    const auto mark_forest = [&](const core::GridPos pos) {
        if (!core::is_inside_grid(pos, map.width, map.height)) {
            return;
        }

        const int index = core::grid_index(pos, map.width);
        map.tiles[static_cast<std::size_t>(index)] = components::TileType::Forest;
        map.forest_wood[static_cast<std::size_t>(index)] = forest_patch_wood;
    };

    const std::span<const scenario::PlayerBaseLayout> layouts =
        scenario::base_layouts_for_player_count(player_count);

    for (const scenario::PlayerBaseLayout& layout : layouts) {
        for (int y = layout.forest_min_y; y <= layout.forest_max_y; ++y) {
            for (int x = layout.forest_min_x; x <= layout.forest_max_x; ++x) {
                mark_forest({x, y});
            }
        }
    }

    for (const scenario::PlayerBaseLayout& layout : layouts) {
        place_berry_patch(map, layout.tc_x, layout.tc_y, bush_food_capacity);
    }

    for (const scenario::PlayerBaseLayout& layout : layouts) {
        place_gold_mines_near_tc(map, layout.tc_x, layout.tc_y, mine_money_capacity);
    }

    place_blueberry_clusters(map, layouts, bush_food_capacity, player_count);

    return map;
}

} // namespace aoa::sim::map
