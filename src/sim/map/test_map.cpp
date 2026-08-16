#include "sim/map/test_map.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "sim/scenario/scenario_layouts.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace aoa::sim::map {

namespace {

constexpr std::array<core::GridPos, 8> k_neighbor_offsets{{
    {1, 0},
    {1, 1},
    {0, 1},
    {-1, 1},
    {-1, 0},
    {-1, -1},
    {0, -1},
    {1, -1},
}};

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

[[nodiscard]] bool is_grass_biome(const components::MapGrid& map, const core::GridPos pos)
{
    if (!core::is_inside_grid(pos, map.width, map.height)) {
        return false;
    }

    const int index = core::grid_index(pos, map.width);
    return components::ground_at(map, static_cast<std::size_t>(index)) == components::GroundType::Grass;
}

[[nodiscard]] bool inside_tc_footprint(const core::GridPos cell, const int tc_x, const int tc_y)
{
    return cell.x >= tc_x && cell.x < tc_x + constants::TOWN_CENTER_FOOTPRINT_TILES
        && cell.y >= tc_y && cell.y < tc_y + constants::TOWN_CENTER_FOOTPRINT_TILES;
}

[[nodiscard]] int chebyshev_to_tc_footprint(
    const core::GridPos cell,
    const int tc_x,
    const int tc_y)
{
    const int footprint = constants::TOWN_CENTER_FOOTPRINT_TILES;
    const int dx = std::max(0, std::max(tc_x - cell.x, cell.x - (tc_x + footprint - 1)));
    const int dy = std::max(0, std::max(tc_y - cell.y, cell.y - (tc_y + footprint - 1)));
    return std::max(dx, dy);
}

[[nodiscard]] int rng_range(std::uint32_t& rng, const int min_value, const int max_value)
{
    if (max_value <= min_value) {
        return min_value;
    }

    const int span = max_value - min_value + 1;
    return min_value + static_cast<int>(xorshift32(rng) % static_cast<std::uint32_t>(span));
}

void collect_ring_candidates(
    const components::MapGrid& map,
    const int tc_x,
    const int tc_y,
    const int min_ring,
    const int max_ring,
    std::vector<core::GridPos>& out)
{
    out.clear();
    const int footprint = constants::TOWN_CENTER_FOOTPRINT_TILES;
    for (int ring = min_ring; ring <= max_ring; ++ring) {
        for (int y = tc_y - ring; y <= tc_y + footprint - 1 + ring; ++y) {
            for (int x = tc_x - ring; x <= tc_x + footprint - 1 + ring; ++x) {
                const core::GridPos cell{x, y};
                if (inside_tc_footprint(cell, tc_x, tc_y)) {
                    continue;
                }

                if (chebyshev_to_tc_footprint(cell, tc_x, tc_y) != ring) {
                    continue;
                }

                if (!is_grass_empty(map, cell)) {
                    continue;
                }

                out.push_back(cell);
            }
        }
    }
}

[[nodiscard]] core::GridPos pick_candidate(
    std::uint32_t& rng,
    const std::vector<core::GridPos>& candidates)
{
    if (candidates.empty()) {
        return {-1, -1};
    }

    const std::size_t index =
        static_cast<std::size_t>(xorshift32(rng) % static_cast<std::uint32_t>(candidates.size()));
    return candidates[index];
}

[[nodiscard]] bool blob_contains(const std::vector<core::GridPos>& blob, const core::GridPos cell)
{
    for (const core::GridPos existing : blob) {
        if (existing == cell) {
            return true;
        }
    }

    return false;
}

void grow_organic_blob(
    std::uint32_t& rng,
    const core::GridPos seed,
    const int target_count,
    const int min_ring,
    const int max_ring,
    const int tc_x,
    const int tc_y,
    const components::MapGrid& map,
    std::vector<core::GridPos>& out,
    const bool require_snow = false,
    const bool require_grass_ground = false)
{
    out.clear();
    if (target_count <= 0 || !is_grass_empty(map, seed)) {
        return;
    }

    const auto cell_allowed = [&](const core::GridPos cell) {
        if (!is_grass_empty(map, cell)) {
            return false;
        }

        if (require_snow) {
            const int index = core::grid_index(cell, map.width);
            if (components::ground_at(map, static_cast<std::size_t>(index))
                != components::GroundType::Snow) {
                return false;
            }
        }

        if (require_grass_ground && !is_grass_biome(map, cell)) {
            return false;
        }

        if (min_ring >= 0) {
            const int ring = chebyshev_to_tc_footprint(cell, tc_x, tc_y);
            if (ring < min_ring || ring > max_ring) {
                return false;
            }
        }

        return true;
    };

    out.push_back(seed);
    std::vector<core::GridPos> frontier{};
    const auto enqueue_neighbors = [&](const core::GridPos base) {
        for (const core::GridPos offset : k_neighbor_offsets) {
            const core::GridPos candidate{base.x + offset.x, base.y + offset.y};
            if (!cell_allowed(candidate) || blob_contains(out, candidate)
                || blob_contains(frontier, candidate)) {
                continue;
            }

            frontier.push_back(candidate);
        }
    };
    enqueue_neighbors(seed);

    while (static_cast<int>(out.size()) < target_count && !frontier.empty()) {
        const std::size_t pick =
            static_cast<std::size_t>(xorshift32(rng) % static_cast<std::uint32_t>(frontier.size()));
        const core::GridPos next = frontier[pick];
        frontier[pick] = frontier.back();
        frontier.pop_back();
        if (!cell_allowed(next) || blob_contains(out, next)) {
            continue;
        }

        out.push_back(next);
        enqueue_neighbors(next);
    }
}

void mark_forest(
    components::MapGrid& map,
    const core::GridPos pos,
    const int forest_patch_wood)
{
    if (!core::is_inside_grid(pos, map.width, map.height)) {
        return;
    }

    const int index = core::grid_index(pos, map.width);
    map.tiles[static_cast<std::size_t>(index)] = components::TileType::Forest;
    map.forest_wood[static_cast<std::size_t>(index)] = forest_patch_wood;
}

struct BiomeSeed {
    core::GridPos cell{};
    components::GroundType ground{components::GroundType::Grass};
};

[[nodiscard]] bool in_player_start_keep(
    const core::GridPos cell,
    const std::span<const scenario::PlayerBaseLayout> layouts)
{
    for (const scenario::PlayerBaseLayout& layout : layouts) {
        if (chebyshev_to_tc_footprint(cell, layout.tc_x, layout.tc_y)
            <= constants::PLAYER_START_GRASS_KEEP_RING) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] float biome_hash01(const int x, const int y, const std::uint32_t salt)
{
    std::uint32_t hash = static_cast<std::uint32_t>(x + 1) * 73856093U
        ^ static_cast<std::uint32_t>(y + 1) * 19349663U
        ^ salt;
    if (hash == 0U) {
        hash = 1U;
    }

    hash ^= hash << 13U;
    hash ^= hash >> 17U;
    hash ^= hash << 5U;
    return static_cast<float>(hash & constants::BIOME_NOISE_FRACTION_MASK)
        / static_cast<float>(constants::BIOME_NOISE_FRACTION_MASK);
}

[[nodiscard]] float biome_value_noise(const float fx, const float fy, const std::uint32_t salt)
{
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const float sx = tx * tx * (constants::BIOME_SMOOTHSTEP_ADD - constants::BIOME_SMOOTHSTEP_MUL * tx);
    const float sy = ty * ty * (constants::BIOME_SMOOTHSTEP_ADD - constants::BIOME_SMOOTHSTEP_MUL * ty);
    const float v00 = biome_hash01(x0, y0, salt);
    const float v10 = biome_hash01(x0 + 1, y0, salt);
    const float v01 = biome_hash01(x0, y0 + 1, salt);
    const float v11 = biome_hash01(x0 + 1, y0 + 1, salt);
    const float x_bottom = v00 + (v10 - v00) * sx;
    const float x_top = v01 + (v11 - v01) * sx;
    return x_bottom + (x_top - x_bottom) * sy;
}

void smooth_biome_islands(
    components::MapGrid& map,
    const std::span<const scenario::PlayerBaseLayout> layouts)
{
    std::vector<components::GroundType> next = map.ground;
    for (int pass = 0; pass < constants::BIOME_SMOOTH_PASSES; ++pass) {
        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                const core::GridPos cell{x, y};
                const int index = core::grid_index(cell, map.width);
                if (in_player_start_keep(cell, layouts)) {
                    next[static_cast<std::size_t>(index)] =
                        map.ground[static_cast<std::size_t>(index)];
                    continue;
                }

                std::array<int, constants::GROUND_TYPE_COUNT> counts{};
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const core::GridPos neighbor{x + dx, y + dy};
                        if (!core::is_inside_grid(neighbor, map.width, map.height)) {
                            continue;
                        }

                        const int neighbor_index = core::grid_index(neighbor, map.width);
                        const auto ground = map.ground[static_cast<std::size_t>(neighbor_index)];
                        const int type_index = static_cast<int>(ground);
                        if (type_index >= 0 && type_index < constants::GROUND_TYPE_COUNT) {
                            counts[static_cast<std::size_t>(type_index)] += 1;
                        }
                    }
                }

                int best_count = -1;
                auto best_ground = map.ground[static_cast<std::size_t>(index)];
                for (int type_index = 0; type_index < constants::GROUND_TYPE_COUNT; ++type_index) {
                    if (counts[static_cast<std::size_t>(type_index)] > best_count) {
                        best_count = counts[static_cast<std::size_t>(type_index)];
                        best_ground = static_cast<components::GroundType>(type_index);
                    }
                }

                next[static_cast<std::size_t>(index)] = best_ground;
            }
        }

        map.ground.swap(next);
        next = map.ground;
    }
}

void paint_biomes(
    components::MapGrid& map,
    std::uint32_t& rng,
    const std::span<const scenario::PlayerBaseLayout> layouts,
    const std::uint8_t player_count)
{
    std::vector<BiomeSeed> seeds{};
    if (player_count <= 2U) {
        seeds.push_back({{constants::GRASS_BIOME_2P_SEED_A_X, constants::GRASS_BIOME_2P_SEED_A_Y},
                         components::GroundType::Grass});
        seeds.push_back({{constants::GRASS_BIOME_2P_SEED_B_X, constants::GRASS_BIOME_2P_SEED_B_Y},
                         components::GroundType::Grass});
        seeds.push_back({{constants::SNOW_BIOME_2P_SEED_A_X, constants::SNOW_BIOME_2P_SEED_A_Y},
                         components::GroundType::Snow});
        seeds.push_back({{constants::SNOW_BIOME_2P_SEED_B_X, constants::SNOW_BIOME_2P_SEED_B_Y},
                         components::GroundType::Snow});
        seeds.push_back({{constants::SAND_BIOME_2P_SEED_A_X, constants::SAND_BIOME_2P_SEED_A_Y},
                         components::GroundType::Sand});
        seeds.push_back({{constants::SAND_BIOME_2P_SEED_B_X, constants::SAND_BIOME_2P_SEED_B_Y},
                         components::GroundType::Sand});
    }
    else {
        for (const scenario::PlayerBaseLayout& layout : layouts) {
            seeds.push_back({{layout.tc_x, layout.tc_y}, components::GroundType::Grass});
        }

        seeds.push_back({{constants::SNOW_BIOME_MULTI_SEED_X, constants::SNOW_BIOME_MULTI_SEED_Y},
                         components::GroundType::Snow});
        seeds.push_back({{constants::SNOW_BIOME_MULTI_SEED_B_X, constants::SNOW_BIOME_MULTI_SEED_B_Y},
                         components::GroundType::Snow});
        seeds.push_back({{constants::SAND_BIOME_MULTI_SEED_X, constants::SAND_BIOME_MULTI_SEED_Y},
                         components::GroundType::Sand});
        seeds.push_back({{constants::SAND_BIOME_MULTI_SEED_B_X, constants::SAND_BIOME_MULTI_SEED_B_Y},
                         components::GroundType::Sand});
    }

    const std::uint32_t noise_salt = xorshift32(rng);
    std::vector<BiomeSeed> placed_seeds = seeds;
    for (BiomeSeed& seed : placed_seeds) {
        seed.cell.x = std::clamp(
            seed.cell.x
                + rng_range(
                    rng,
                    -constants::BIOME_SEED_JITTER_TILES,
                    constants::BIOME_SEED_JITTER_TILES),
            0,
            map.width - 1);
        seed.cell.y = std::clamp(
            seed.cell.y
                + rng_range(
                    rng,
                    -constants::BIOME_SEED_JITTER_TILES,
                    constants::BIOME_SEED_JITTER_TILES),
            0,
            map.height - 1);
    }

    const float noise_cell = static_cast<float>(constants::BIOME_NOISE_CELL_SIZE);
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const core::GridPos cell{x, y};
            if (in_player_start_keep(cell, layouts)) {
                continue;
            }

            const float border_wobble =
                (biome_value_noise(
                     static_cast<float>(x) / noise_cell,
                     static_cast<float>(y) / noise_cell,
                     noise_salt)
                    * constants::BIOME_NOISE_SIGNED_SCALE
                    - constants::BIOME_NOISE_SIGNED_BIAS)
                * constants::BIOME_BORDER_WOBBLE;
            float best_distance = 0.0F;
            components::GroundType best_ground = components::GroundType::Grass;
            bool have_best = false;
            for (const BiomeSeed& seed : placed_seeds) {
                const float dx = static_cast<float>(x - seed.cell.x);
                const float dy = static_cast<float>(y - seed.cell.y);
                const float distance = dx * dx + dy * dy + border_wobble;
                if (!have_best || distance < best_distance) {
                    best_distance = distance;
                    best_ground = seed.ground;
                    have_best = true;
                }
            }

            const int index = core::grid_index(cell, map.width);
            map.ground[static_cast<std::size_t>(index)] = best_ground;
        }
    }

    smooth_biome_islands(map, layouts);
}

void place_near_tc_trees(
    components::MapGrid& map,
    std::uint32_t& rng,
    const int tc_x,
    const int tc_y,
    const int forest_patch_wood)
{
    std::vector<core::GridPos> candidates{};
    collect_ring_candidates(
        map,
        tc_x,
        tc_y,
        constants::FOREST_NEAR_TC_MIN_RING,
        constants::FOREST_NEAR_TC_MAX_RING,
        candidates);
    const int target_count = rng_range(
        rng,
        constants::FOREST_NEAR_TC_MIN_COUNT,
        constants::FOREST_NEAR_TC_MAX_COUNT);
    int placed = 0;
    for (int attempt = 0;
         attempt < constants::MAP_GEN_SCATTER_MAX_ATTEMPTS && placed < target_count;
         ++attempt) {
        const core::GridPos cell = pick_candidate(rng, candidates);
        if (cell.x < 0 || !is_grass_empty(map, cell)) {
            continue;
        }

        mark_forest(map, cell, forest_patch_wood);
        ++placed;
    }
}

void place_player_forest_grove(
    components::MapGrid& map,
    std::uint32_t& rng,
    const int tc_x,
    const int tc_y,
    const int forest_patch_wood)
{
    std::vector<core::GridPos> candidates{};
    collect_ring_candidates(
        map,
        tc_x,
        tc_y,
        constants::FOREST_GROVE_MIN_RING,
        constants::FOREST_GROVE_MAX_RING,
        candidates);
    const core::GridPos seed = pick_candidate(rng, candidates);
    if (seed.x < 0) {
        return;
    }

    std::vector<core::GridPos> grove{};
    grow_organic_blob(
        rng,
        seed,
        constants::FOREST_GROVE_TILE_COUNT,
        constants::FOREST_GROVE_MIN_RING,
        constants::FOREST_GROVE_MAX_RING + constants::FOREST_GROVE_GROW_RING_SLACK,
        tc_x,
        tc_y,
        map,
        grove);
    for (const core::GridPos cell : grove) {
        mark_forest(map, cell, forest_patch_wood);
    }
}

void place_berry_patch(
    components::MapGrid& map,
    std::uint32_t& rng,
    const int tc_x,
    const int tc_y,
    const int bush_food_capacity)
{
    std::vector<core::GridPos> candidates{};
    collect_ring_candidates(
        map,
        tc_x,
        tc_y,
        constants::BERRY_PATCH_MIN_RING,
        constants::BERRY_PATCH_MAX_RING,
        candidates);
    candidates.erase(
        std::remove_if(
            candidates.begin(),
            candidates.end(),
            [&](const core::GridPos cell) { return !is_grass_biome(map, cell); }),
        candidates.end());
    const core::GridPos seed = pick_candidate(rng, candidates);
    if (seed.x < 0) {
        return;
    }

    std::vector<core::GridPos> patch{};
    grow_organic_blob(
        rng,
        seed,
        constants::BERRY_PATCH_TILE_COUNT,
        constants::BERRY_PATCH_MIN_RING,
        constants::BERRY_PATCH_MAX_RING,
        tc_x,
        tc_y,
        map,
        patch,
        false,
        true);
    for (const core::GridPos cell : patch) {
        if (!is_grass_empty(map, cell)) {
            continue;
        }

        const int index = core::grid_index(cell, map.width);
        map.tiles[static_cast<std::size_t>(index)] = components::TileType::Berries;
        map.bush_food[static_cast<std::size_t>(index)] = bush_food_capacity;
    }
}

void place_pine_groves_on_snow(
    components::MapGrid& map,
    std::uint32_t& rng,
    const int forest_patch_wood,
    const std::span<const scenario::PlayerBaseLayout> layouts)
{
    std::vector<core::GridPos> snow_cells{};
    snow_cells.reserve(map.tiles.size());
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const core::GridPos cell{x, y};
            const int index = core::grid_index(cell, map.width);
            if (components::ground_at(map, static_cast<std::size_t>(index))
                != components::GroundType::Snow) {
                continue;
            }

            if (!is_grass_empty(map, cell)) {
                continue;
            }

            if (in_player_start_keep(cell, layouts)) {
                continue;
            }

            snow_cells.push_back(cell);
        }
    }

    if (snow_cells.empty()) {
        return;
    }

    const core::GridPos seed = pick_candidate(rng, snow_cells);
    std::vector<core::GridPos> grove{};
    grow_organic_blob(
        rng,
        seed,
        constants::PINE_GROVE_TILE_COUNT,
        -1,
        0,
        0,
        0,
        map,
        grove,
        true);
    for (const core::GridPos cell : grove) {
        mark_forest(map, cell, forest_patch_wood);
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
    if (!is_grass_empty(map, seed) || !far_enough_from_bases(seed, layouts)
        || !is_grass_biome(map, seed)) {
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
                if (!is_grass_empty(map, candidate) || !far_enough_from_bases(candidate, layouts)
                    || !is_grass_biome(map, candidate)) {
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
    std::uint32_t& rng,
    const int tc_x,
    const int tc_y,
    const int mine_money_capacity)
{
    std::vector<core::GridPos> candidates{};
    collect_ring_candidates(
        map,
        tc_x,
        tc_y,
        constants::GOLD_MINE_MIN_RING,
        constants::GOLD_MINE_MAX_RING,
        candidates);
    const core::GridPos seed = pick_candidate(rng, candidates);
    if (seed.x < 0) {
        return;
    }

    std::vector<core::GridPos> cluster{};
    grow_organic_blob(
        rng,
        seed,
        constants::GOLD_MINE_NEAR_BASE_COUNT,
        constants::GOLD_MINE_MIN_RING,
        constants::GOLD_MINE_MAX_RING,
        tc_x,
        tc_y,
        map,
        cluster);
    for (const core::GridPos cell : cluster) {
        if (!is_grass_empty(map, cell)) {
            continue;
        }

        const int index = core::grid_index(cell, map.width);
        map.tiles[static_cast<std::size_t>(index)] = components::TileType::GoldMine;
        map.mine_money[static_cast<std::size_t>(index)] = mine_money_capacity;
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
        ^ (static_cast<std::uint32_t>(player_count) * 0x9E3779B9U)
        ^ constants::MAP_GEN_LAYOUT_SALT;
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
    const std::uint8_t player_count,
    const std::uint64_t seed)
{
    components::MapGrid map{};
    map.width = constants::MAP_TEST_WIDTH;
    map.height = constants::MAP_TEST_HEIGHT;
    map.tiles.assign(static_cast<std::size_t>(map.width * map.height), components::TileType::Grass);
    map.ground.assign(map.tiles.size(), components::GroundType::Grass);
    map.forest_wood.assign(map.tiles.size(), 0);
    map.bush_food.assign(map.tiles.size(), 0);
    map.mine_money.assign(map.tiles.size(), 0);

    const std::span<const scenario::PlayerBaseLayout> layouts =
        scenario::base_layouts_for_player_count(player_count);

    std::uint32_t rng_state = static_cast<std::uint32_t>(map.width)
        ^ (static_cast<std::uint32_t>(map.height) << 16U)
        ^ (static_cast<std::uint32_t>(player_count) * 0x9E3779B9U)
        ^ constants::MAP_GEN_LAYOUT_SALT
        ^ static_cast<std::uint32_t>(seed)
        ^ static_cast<std::uint32_t>(seed >> 32U);
    if (rng_state == 0U) {
        rng_state = 1U;
    }

    paint_biomes(map, rng_state, layouts, player_count);

    for (const scenario::PlayerBaseLayout& layout : layouts) {
        place_gold_mines_near_tc(map, rng_state, layout.tc_x, layout.tc_y, mine_money_capacity);
    }

    for (std::size_t layout_index = 0U; layout_index < layouts.size(); ++layout_index) {
        const scenario::PlayerBaseLayout& layout = layouts[layout_index];
        std::uint32_t player_rng = rng_state
            ^ (static_cast<std::uint32_t>(layout.tc_x + 1) * 73856093U)
            ^ (static_cast<std::uint32_t>(layout.tc_y + 1) * 19349663U)
            ^ (static_cast<std::uint32_t>(layout_index + 1U) * 83492791U);
        if (player_rng == 0U) {
            player_rng = 1U;
        }

        place_near_tc_trees(map, player_rng, layout.tc_x, layout.tc_y, forest_patch_wood);
        place_berry_patch(map, player_rng, layout.tc_x, layout.tc_y, bush_food_capacity);
        for (int grove_index = 0; grove_index < constants::FOREST_GROVE_COUNT_PER_PLAYER; ++grove_index) {
            place_player_forest_grove(map, player_rng, layout.tc_x, layout.tc_y, forest_patch_wood);
        }
    }

    place_pine_groves_on_snow(map, rng_state, forest_patch_wood, layouts);
    const int pine_grove_count = player_count <= 2U
        ? constants::PINE_GROVE_COUNT_TWO_PLAYER
        : constants::PINE_GROVE_COUNT_MULTI_PLAYER;
    for (int grove_index = 1; grove_index < pine_grove_count; ++grove_index) {
        place_pine_groves_on_snow(map, rng_state, forest_patch_wood, layouts);
    }
    place_blueberry_clusters(map, layouts, bush_food_capacity, player_count);

    return map;
}

} // namespace aoa::sim::map
