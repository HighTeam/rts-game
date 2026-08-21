#include "sim/map/map_generator.hpp"

#include "core/constants.hpp"
#include "sim/map/test_map.hpp"
#include "sim/scenario/scenario_layouts.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace aoa::sim::map {

[[nodiscard]] float start_ring_radius(const int map_width, const int map_height)
{
    return static_cast<float>(std::min(map_width, map_height))
        * static_cast<float>(constants::PATTERN_START_RING_RADIUS_PERCENT) / 100.0F;
}

std::vector<core::GridPos> start_ring_positions(
    const std::uint8_t player_count,
    const int map_width,
    const int map_height)
{
    std::vector<core::GridPos> starts{};
    if (player_count == 0U) {
        return starts;
    }

    const float center_x = static_cast<float>(map_width - 1) * 0.5F;
    const float center_y = static_cast<float>(map_height - 1) * 0.5F;
    const float radius = start_ring_radius(map_width, map_height);
    const float base_angle = constants::PATTERN_START_RING_BASE_ANGLE;
    const float step = constants::PATTERN_TAU / static_cast<float>(player_count);
    starts.reserve(player_count);
    for (std::uint8_t index = 0; index < player_count; ++index) {
        const float angle = base_angle + step * static_cast<float>(index);
        starts.push_back(core::GridPos{
            std::clamp(
                static_cast<int>(std::lround(center_x + std::cos(angle) * radius)),
                0,
                map_width - 1),
            std::clamp(
                static_cast<int>(std::lround(center_y + std::sin(angle) * radius)),
                0,
                map_height - 1),
        });
    }

    return starts;
}

core::GridPos snap_to_start_ring(
    const core::GridPos cell,
    const int map_width,
    const int map_height)
{
    const float center_x = static_cast<float>(map_width - 1) * 0.5F;
    const float center_y = static_cast<float>(map_height - 1) * 0.5F;
    const float radius = start_ring_radius(map_width, map_height);
    float dx = static_cast<float>(cell.x) - center_x;
    float dy = static_cast<float>(cell.y) - center_y;
    if (dx == 0.0F && dy == 0.0F) {
        dx = 1.0F;
    }
    const float length = std::sqrt(dx * dx + dy * dy);
    return {
        std::clamp(static_cast<int>(std::lround(center_x + dx / length * radius)), 0, map_width - 1),
        std::clamp(static_cast<int>(std::lround(center_y + dy / length * radius)), 0, map_height - 1),
    };
}

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

[[nodiscard]] std::uint32_t make_rng(const MapGenerationConfig& config, const std::uint32_t salt)
{
    std::uint32_t rng = static_cast<std::uint32_t>(config.seed)
        ^ static_cast<std::uint32_t>(config.seed >> 32U)
        ^ (static_cast<std::uint32_t>(config.player_count) * 0x9E3779B9U)
        ^ constants::MAP_GEN_LAYOUT_SALT
        ^ salt;
    if (rng == 0U) {
        rng = 1U;
    }
    return rng;
}

[[nodiscard]] int rng_range(std::uint32_t& rng, const int min_value, const int max_value)
{
    if (max_value <= min_value) {
        return min_value;
    }

    const int span = max_value - min_value + 1;
    return min_value + static_cast<int>(xorshift32(rng) % static_cast<std::uint32_t>(span));
}

[[nodiscard]] float rng_angle(std::uint32_t& rng)
{
    return (static_cast<float>(xorshift32(rng))
        / static_cast<float>(std::numeric_limits<std::uint32_t>::max()))
        * constants::PATTERN_TAU;
}

void init_empty_map(components::MapGrid& map, const int width, const int height)
{
    map.width = width;
    map.height = height;
    const std::size_t cell_count = static_cast<std::size_t>(width * height);
    map.tiles.assign(cell_count, components::TileType::Grass);
    map.ground.assign(cell_count, components::GroundType::Grass);
    map.forest_wood.assign(cell_count, 0);
    map.bush_food.assign(cell_count, 0);
    map.mine_money.assign(cell_count, 0);
}

[[nodiscard]] bool in_map(const components::MapGrid& map, const core::GridPos cell)
{
    return core::is_inside_grid(cell, map.width, map.height);
}

[[nodiscard]] std::size_t cell_index(const components::MapGrid& map, const core::GridPos cell)
{
    return static_cast<std::size_t>(core::grid_index(cell, map.width));
}

void fill_ground_rect(
    components::MapGrid& map,
    const MapPiece& piece,
    const components::GroundType ground)
{
    for (int y = piece.y; y < piece.y + piece.height; ++y) {
        for (int x = piece.x; x < piece.x + piece.width; ++x) {
            const core::GridPos cell{x, y};
            if (!in_map(map, cell)) {
                continue;
            }
            map.ground[cell_index(map, cell)] = ground;
        }
    }
}

void paint_blob(
    components::MapGrid& map,
    std::uint32_t& rng,
    const core::GridPos center,
    const int radius,
    const components::GroundType ground)
{
    for (int y = center.y - radius; y <= center.y + radius; ++y) {
        for (int x = center.x - radius; x <= center.x + radius; ++x) {
            const core::GridPos cell{x, y};
            if (!in_map(map, cell)) {
                continue;
            }

            const int dx = x - center.x;
            const int dy = y - center.y;
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }

            map.ground[cell_index(map, cell)] = ground;
        }
    }
    (void)rng;
}

void place_forest_patch(
    components::MapGrid& map,
    const core::GridPos cell,
    const int wood)
{
    if (!in_map(map, cell)) {
        return;
    }

    const std::size_t index = cell_index(map, cell);
    if (map.tiles[index] != components::TileType::Grass) {
        return;
    }

    map.tiles[index] = components::TileType::Forest;
    map.forest_wood[index] = wood;
}

void place_berries(components::MapGrid& map, const core::GridPos cell, const int food)
{
    if (!in_map(map, cell)) {
        return;
    }

    const std::size_t index = cell_index(map, cell);
    if (map.tiles[index] != components::TileType::Grass) {
        return;
    }

    map.tiles[index] = components::TileType::Berries;
    map.bush_food[index] = food;
}

void place_gold(components::MapGrid& map, const core::GridPos cell, const int money)
{
    if (!in_map(map, cell)) {
        return;
    }

    const std::size_t index = cell_index(map, cell);
    if (map.tiles[index] != components::TileType::Grass) {
        return;
    }

    map.tiles[index] = components::TileType::GoldMine;
    map.mine_money[index] = money;
}

[[nodiscard]] bool too_close(
    const core::GridPos cell,
    const std::vector<core::GridPos>& starts,
    const int min_separation)
{
    for (const core::GridPos& start : starts) {
        const int dx = cell.x - start.x;
        const int dy = cell.y - start.y;
        if (dx * dx + dy * dy < min_separation * min_separation) {
            return true;
        }
    }

    return false;
}

void scatter_in_rect(
    components::MapGrid& map,
    std::uint32_t& rng,
    const MapPiece& piece,
    const int count,
    const auto& place)
{
    for (int attempt = 0; attempt < count * constants::MAP_GEN_SCATTER_MAX_ATTEMPTS; ++attempt) {
        if (count <= 0) {
            return;
        }

        const int x = rng_range(rng, piece.x, piece.x + std::max(0, piece.width - 1));
        const int y = rng_range(rng, piece.y, piece.y + std::max(0, piece.height - 1));
        const core::GridPos cell{x, y};
        if (!in_map(map, cell)) {
            continue;
        }

        place(map, cell);
    }
}

[[nodiscard]] bool in_start_clearance(
    const core::GridPos cell,
    const std::vector<core::GridPos>& starts,
    const int clear_radius)
{
    return too_close(cell, starts, clear_radius);
}

void grow_organic_forest(
    components::MapGrid& map,
    std::uint32_t& rng,
    const core::GridPos seed,
    const int target_count,
    const int wood,
    const std::vector<core::GridPos>& starts)
{
    if (!in_map(map, seed) || target_count <= 0) {
        return;
    }

    std::vector<core::GridPos> blob{};
    blob.push_back(seed);
    std::vector<core::GridPos> frontier{};
    const auto try_add_frontier = [&](const core::GridPos cell) {
        if (!in_map(map, cell) || map.tiles[cell_index(map, cell)] != components::TileType::Grass) {
            return;
        }
        if (in_start_clearance(cell, starts, constants::PATTERN_START_CLEAR_RADIUS)) {
            return;
        }
        for (const core::GridPos& existing : blob) {
            if (existing.x == cell.x && existing.y == cell.y) {
                return;
            }
        }
        for (const core::GridPos& existing : frontier) {
            if (existing.x == cell.x && existing.y == cell.y) {
                return;
            }
        }
        frontier.push_back(cell);
    };

    const std::array<core::GridPos, 4> neighbors{{
        {1, 0}, {0, 1}, {-1, 0}, {0, -1},
    }};
    for (const core::GridPos& offset : neighbors) {
        try_add_frontier({seed.x + offset.x, seed.y + offset.y});
    }

    while (static_cast<int>(blob.size()) < target_count && !frontier.empty()) {
        const std::size_t pick =
            static_cast<std::size_t>(xorshift32(rng) % static_cast<std::uint32_t>(frontier.size()));
        const core::GridPos next = frontier[pick];
        frontier[pick] = frontier.back();
        frontier.pop_back();
        blob.push_back(next);
        for (const core::GridPos& offset : neighbors) {
            try_add_frontier({next.x + offset.x, next.y + offset.y});
        }
    }

    for (const core::GridPos& cell : blob) {
        place_forest_patch(map, cell, wood);
    }
}

void apply_forest_piece(
    components::MapGrid& map,
    std::uint32_t& rng,
    const MapPiece& piece,
    const int wood,
    const std::vector<core::GridPos>& starts)
{
    const int cells = std::max(1, piece.width * piece.height);
    const int target = std::max(
        constants::PATTERN_FOREST_BLOB_BASE_TILES / 2,
        cells * std::clamp(piece.density, 1, 100) / 100);
    const core::GridPos seed{
        piece.x + std::max(0, piece.width / 2),
        piece.y + std::max(0, piece.height / 2),
    };
    grow_organic_forest(map, rng, seed, target, wood, starts);
}

void clear_start_territory(
    components::MapGrid& map,
    const std::vector<core::GridPos>& starts)
{
    const int radius = constants::PATTERN_START_CLEAR_RADIUS;
    for (const core::GridPos& start : starts) {
        for (int y = start.y - radius; y <= start.y + radius; ++y) {
            for (int x = start.x - radius; x <= start.x + radius; ++x) {
                const core::GridPos cell{x, y};
                if (!in_map(map, cell)) {
                    continue;
                }
                const int dx = x - start.x;
                const int dy = y - start.y;
                if (dx * dx + dy * dy > radius * radius) {
                    continue;
                }

                const std::size_t index = cell_index(map, cell);
                map.tiles[index] = components::TileType::Grass;
                map.forest_wood[index] = 0;
                map.bush_food[index] = 0;
                map.mine_money[index] = 0;
            }
        }
    }
}

void carve_road(
    components::MapGrid& map,
    const core::GridPos from,
    const core::GridPos to,
    const int half_width)
{
    int x = from.x;
    int y = from.y;
    const int dx = std::abs(to.x - from.x);
    const int dy = std::abs(to.y - from.y);
    const int sx = from.x < to.x ? 1 : -1;
    const int sy = from.y < to.y ? 1 : -1;
    int error = dx - dy;
    while (true) {
        for (int oy = -half_width; oy <= half_width; ++oy) {
            for (int ox = -half_width; ox <= half_width; ++ox) {
                const core::GridPos cell{x + ox, y + oy};
                if (!in_map(map, cell)) {
                    continue;
                }
                const std::size_t index = cell_index(map, cell);
                const components::TileType tile = map.tiles[index];
                if (tile == components::TileType::GoldMine || tile == components::TileType::Berries
                    || tile == components::TileType::Blueberries) {
                    continue;
                }
                map.tiles[index] = components::TileType::Grass;
                map.forest_wood[index] = 0;
            }
        }

        if (x == to.x && y == to.y) {
            break;
        }

        const int doubled = error * 2;
        if (doubled > -dy) {
            error -= dy;
            x += sx;
        }
        if (doubled < dx) {
            error += dx;
            y += sy;
        }
    }
}

[[nodiscard]] core::GridPos piece_center(const MapPiece& piece)
{
    return {
        piece.x + std::max(0, piece.width / 2),
        piece.y + std::max(0, piece.height / 2),
    };
}

[[nodiscard]] const MapPiece* find_piece(const MapPattern& pattern, const std::uint32_t id)
{
    for (const MapPiece& piece : pattern.pieces) {
        if (piece.id == id) {
            return &piece;
        }
    }

    return nullptr;
}

[[nodiscard]] float piece_angle_from_center(const MapPiece& piece, const float center_x, const float center_y)
{
    const core::GridPos center = piece_center(piece);
    return std::atan2(static_cast<float>(center.y) - center_y, static_cast<float>(center.x) - center_x);
}

[[nodiscard]] core::GridPos map_relative_cell(
    const core::GridPos relative,
    const int relative_width,
    const int relative_height,
    const int world_width,
    const int world_height)
{
    const float rel_cx = static_cast<float>(relative_width - 1) * 0.5F;
    const float rel_cy = static_cast<float>(relative_height - 1) * 0.5F;
    const float world_cx = static_cast<float>(world_width - 1) * 0.5F;
    const float world_cy = static_cast<float>(world_height - 1) * 0.5F;
    const float scale_x = static_cast<float>(world_width) / static_cast<float>(std::max(1, relative_width));
    const float scale_y = static_cast<float>(world_height) / static_cast<float>(std::max(1, relative_height));
    return {
        std::clamp(
            static_cast<int>(std::lround(world_cx + (static_cast<float>(relative.x) - rel_cx) * scale_x)),
            0,
            world_width - 1),
        std::clamp(
            static_cast<int>(std::lround(world_cy + (static_cast<float>(relative.y) - rel_cy) * scale_y)),
            0,
            world_height - 1),
    };
}

[[nodiscard]] bool piece_instances_per_start(const MapPieceKind kind)
{
    return kind == MapPieceKind::Forest
        || kind == MapPieceKind::Gold
        || kind == MapPieceKind::Berries
        || kind == MapPieceKind::Terrain
        || kind == MapPieceKind::ManaLake;
}

[[nodiscard]] float cell_angle_from_center(const core::GridPos cell, const core::GridPos center)
{
    return std::atan2(
        static_cast<float>(cell.y - center.y),
        static_cast<float>(cell.x - center.x));
}

[[nodiscard]] core::GridPos rotate_grid_offset(const int dx, const int dy, const float radians)
{
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {
        static_cast<int>(std::lround(static_cast<float>(dx) * cosine - static_cast<float>(dy) * sine)),
        static_cast<int>(std::lround(static_cast<float>(dx) * sine + static_cast<float>(dy) * cosine)),
    };
}

[[nodiscard]] core::GridPos clamp_world_cell(
    const core::GridPos cell,
    const int world_width,
    const int world_height)
{
    return {
        std::clamp(cell.x, 0, std::max(0, world_width - 1)),
        std::clamp(cell.y, 0, std::max(0, world_height - 1)),
    };
}

void place_connected_cluster(
    components::MapGrid& map,
    std::uint32_t& rng,
    const core::GridPos seed,
    const int count,
    const std::vector<core::GridPos>& starts,
    const int keep_radius,
    const auto& place)
{
    if (!in_map(map, seed) || count <= 0) {
        return;
    }

    std::vector<core::GridPos> blob{};
    std::vector<core::GridPos> frontier{};
    const std::array<core::GridPos, 4> neighbors{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
    const auto try_add = [&](const core::GridPos cell) {
        if (!in_map(map, cell) || map.tiles[cell_index(map, cell)] != components::TileType::Grass) {
            return;
        }
        if (in_start_clearance(cell, starts, keep_radius)) {
            return;
        }
        for (const core::GridPos& existing : blob) {
            if (existing.x == cell.x && existing.y == cell.y) {
                return;
            }
        }
        for (const core::GridPos& existing : frontier) {
            if (existing.x == cell.x && existing.y == cell.y) {
                return;
            }
        }
        frontier.push_back(cell);
    };

    if (!in_start_clearance(seed, starts, keep_radius)
        && map.tiles[cell_index(map, seed)] == components::TileType::Grass) {
        blob.push_back(seed);
        for (const core::GridPos& offset : neighbors) {
            try_add({seed.x + offset.x, seed.y + offset.y});
        }
    }

    while (static_cast<int>(blob.size()) < count && !frontier.empty()) {
        const std::size_t pick =
            static_cast<std::size_t>(xorshift32(rng) % static_cast<std::uint32_t>(frontier.size()));
        const core::GridPos next = frontier[pick];
        frontier[pick] = frontier.back();
        frontier.pop_back();
        blob.push_back(next);
        for (const core::GridPos& offset : neighbors) {
            try_add({next.x + offset.x, next.y + offset.y});
        }
    }

    for (const core::GridPos& cell : blob) {
        place(map, cell);
    }
}

[[nodiscard]] std::optional<core::GridPos> pick_ring_cell(
    const components::MapGrid& map,
    std::uint32_t& rng,
    const core::GridPos start,
    const int min_ring,
    const int max_ring,
    const float angle,
    const std::vector<core::GridPos>& starts,
    const int keep_radius,
    const std::vector<core::GridPos>& reserved = {})
{
    const int hint_x = start.x + static_cast<int>(std::lround(std::cos(angle) * static_cast<float>(min_ring)));
    const int hint_y = start.y + static_cast<int>(std::lround(std::sin(angle) * static_cast<float>(min_ring)));
    std::vector<core::GridPos> candidates{};
    for (int ring = min_ring; ring <= max_ring; ++ring) {
        for (int dy = -ring; dy <= ring; ++dy) {
            for (int dx = -ring; dx <= ring; ++dx) {
                const int dist2 = dx * dx + dy * dy;
                if (dist2 < min_ring * min_ring || dist2 > max_ring * max_ring) {
                    continue;
                }
                const core::GridPos cell{start.x + dx, start.y + dy};
                if (!in_map(map, cell)
                    || map.tiles[cell_index(map, cell)] != components::TileType::Grass
                    || in_start_clearance(cell, starts, keep_radius)
                    || too_close(cell, reserved, constants::PATTERN_START_RESOURCE_SEPARATION)) {
                    continue;
                }
                candidates.push_back(cell);
            }
        }
        if (!candidates.empty()) {
            break;
        }
    }
    if (candidates.empty()) {
        return std::nullopt;
    }

    core::GridPos best = candidates.front();
    int best_dist = std::numeric_limits<int>::max();
    for (const core::GridPos& cell : candidates) {
        const int dx = cell.x - hint_x;
        const int dy = cell.y - hint_y;
        const int dist = dx * dx + dy * dy;
        if (dist < best_dist) {
            best_dist = dist;
            best = cell;
        }
    }
    (void)rng;
    return best;
}

[[nodiscard]] std::optional<core::GridPos> snap_clear_cell(
    const components::MapGrid& map,
    const core::GridPos hint,
    const std::vector<core::GridPos>& starts,
    const std::vector<core::GridPos>& reserved,
    const int keep_radius)
{
    std::optional<core::GridPos> best{};
    int best_dist = std::numeric_limits<int>::max();
    const int radius = constants::PATTERN_RESOURCE_SNAP_RADIUS;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const core::GridPos cell{hint.x + dx, hint.y + dy};
            if (!in_map(map, cell)
                || map.tiles[cell_index(map, cell)] != components::TileType::Grass
                || in_start_clearance(cell, starts, keep_radius)
                || too_close(cell, reserved, constants::PATTERN_START_RESOURCE_SEPARATION)) {
                continue;
            }

            const int dist = dx * dx + dy * dy;
            if (dist < best_dist) {
                best_dist = dist;
                best = cell;
            }
        }
    }

    return best;
}

void place_border_trees(
    components::MapGrid& map,
    const int wood,
    const std::vector<core::GridPos>& starts)
{
    const int depth = constants::PATTERN_BORDER_TREE_DEPTH;
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const int edge = std::min(std::min(x, y), std::min(map.width - 1 - x, map.height - 1 - y));
            if (edge >= depth) {
                continue;
            }
            const core::GridPos cell{x, y};
            if (in_start_clearance(cell, starts, constants::PATTERN_START_ECONOMY_RADIUS)) {
                continue;
            }
            place_forest_patch(map, cell, wood);
        }
    }
}

void place_random_stray_trees(
    components::MapGrid& map,
    std::uint32_t& rng,
    const int count,
    const int wood,
    const std::vector<core::GridPos>& starts,
    const std::vector<core::GridPos>& reserved)
{
    int placed = 0;
    for (int attempt = 0; attempt < count * constants::MAP_GEN_SCATTER_MAX_ATTEMPTS && placed < count;
         ++attempt) {
        const core::GridPos cell{
            rng_range(rng, 0, map.width - 1),
            rng_range(rng, 0, map.height - 1),
        };
        if (!in_map(map, cell)
            || map.tiles[cell_index(map, cell)] != components::TileType::Grass
            || in_start_clearance(cell, starts, constants::PATTERN_START_ECONOMY_RADIUS)
            || too_close(cell, reserved, constants::PATTERN_RESOURCE_CLUSTER_RADIUS)) {
            continue;
        }
        place_forest_patch(map, cell, wood);
        ++placed;
    }
}

void place_start_economy(
    components::MapGrid& map,
    std::uint32_t& rng,
    const std::vector<core::GridPos>& starts,
    const MapGenerationConfig& config,
    GeneratedMap& result,
    const bool place_lakes)
{
    const float spin = rng_angle(rng);
    const float gold_angle = spin;
    const float berry_angle = gold_angle + constants::PATTERN_PI * 0.5F;
    const float lake_angle = gold_angle + constants::PATTERN_PI;
    const float wood_angle = gold_angle + constants::PATTERN_PI * 1.5F;
    for (const core::GridPos& start : starts) {
        std::vector<core::GridPos> reserved{};
        if (const auto gold = pick_ring_cell(
                map,
                rng,
                start,
                constants::PATTERN_START_GOLD_MIN_RING,
                constants::PATTERN_START_GOLD_MAX_RING,
                gold_angle,
                starts,
                constants::PATTERN_START_PLAZA_RADIUS,
                reserved)) {
            reserved.push_back(*gold);
            place_connected_cluster(
                map,
                rng,
                *gold,
                constants::PATTERN_START_GOLD_COUNT,
                starts,
                constants::PATTERN_START_PLAZA_RADIUS,
                [&](components::MapGrid& grid, const core::GridPos cell) {
                    place_gold(grid, cell, config.mine_money_capacity);
                });
        }

        if (const auto berries = pick_ring_cell(
                map,
                rng,
                start,
                constants::PATTERN_START_BERRY_MIN_RING,
                constants::PATTERN_START_BERRY_MAX_RING,
                berry_angle,
                starts,
                constants::PATTERN_START_PLAZA_RADIUS,
                reserved)) {
            reserved.push_back(*berries);
            place_connected_cluster(
                map,
                rng,
                *berries,
                constants::PATTERN_START_BERRY_COUNT,
                starts,
                constants::PATTERN_START_PLAZA_RADIUS,
                [&](components::MapGrid& grid, const core::GridPos cell) {
                    place_berries(grid, cell, config.bush_food_capacity);
                });
        }

        for (int tree = 0; tree < constants::PATTERN_START_STRAY_TREE_COUNT; ++tree) {
            const float angle = berry_angle + static_cast<float>(tree) * 0.7F;
            if (const auto cell = pick_ring_cell(
                    map,
                    rng,
                    start,
                    constants::PATTERN_START_STRAY_TREE_MIN_RING,
                    constants::PATTERN_START_STRAY_TREE_MAX_RING,
                    angle,
                    starts,
                    constants::PATTERN_START_PLAZA_RADIUS,
                    reserved)) {
                reserved.push_back(*cell);
                place_forest_patch(map, *cell, config.forest_patch_wood);
            }
        }

        if (const auto wood = pick_ring_cell(
                map,
                rng,
                start,
                constants::PATTERN_START_WOODLINE_MIN_RING,
                constants::PATTERN_START_WOODLINE_MAX_RING,
                wood_angle,
                starts,
                constants::PATTERN_START_ECONOMY_RADIUS,
                reserved)) {
            reserved.push_back(*wood);
            grow_organic_forest(
                map,
                rng,
                *wood,
                constants::PATTERN_START_WOODLINE_TILES,
                config.forest_patch_wood,
                starts);
        }

        if (place_lakes) {
            if (const auto lake = pick_ring_cell(
                    map,
                    rng,
                    start,
                    constants::PATTERN_START_LAKE_MIN_RING,
                    constants::PATTERN_START_LAKE_MAX_RING,
                    lake_angle,
                    starts,
                    constants::PATTERN_START_PLAZA_RADIUS,
                    reserved)) {
                result.mana_lake_anchors.push_back(*lake);
            }
        }
    }
}

void place_random_nodes(
    components::MapGrid& map,
    std::uint32_t& rng,
    const int count,
    const std::vector<core::GridPos>& starts,
    const auto& place)
{
    int placed = 0;
    for (int attempt = 0; attempt < count * constants::MAP_GEN_SCATTER_MAX_ATTEMPTS && placed < count;
         ++attempt) {
        const core::GridPos cell{
            rng_range(rng, 0, map.width - 1),
            rng_range(rng, 0, map.height - 1),
        };
        if (!in_map(map, cell)
            || in_start_clearance(cell, starts, constants::PATTERN_START_CLEAR_RADIUS)
            || map.tiles[cell_index(map, cell)] != components::TileType::Grass) {
            continue;
        }

        place(map, cell);
        ++placed;
    }
}

[[nodiscard]] bool piece_is_forest_fill(const MapPiece& piece, const components::MapGrid& map)
{
    const int piece_area = std::max(1, piece.width * piece.height);
    const int map_area = std::max(1, map.width * map.height);
    return piece_area * 100 >= map_area * constants::PATTERN_FOREST_FILL_COVERAGE_PERCENT;
}

[[nodiscard]] core::GridPos nudge_out_of_starts(
    const core::GridPos seed,
    const std::vector<core::GridPos>& starts,
    const int map_width,
    const int map_height)
{
    if (!in_start_clearance(seed, starts, constants::PATTERN_START_CLEAR_RADIUS)) {
        return seed;
    }

    const core::GridPos* nearest = nullptr;
    int nearest_dist = std::numeric_limits<int>::max();
    for (const core::GridPos& start : starts) {
        const int dx = seed.x - start.x;
        const int dy = seed.y - start.y;
        const int dist = dx * dx + dy * dy;
        if (dist < nearest_dist) {
            nearest_dist = dist;
            nearest = &start;
        }
    }
    if (nearest == nullptr) {
        return seed;
    }

    int dx = seed.x - nearest->x;
    int dy = seed.y - nearest->y;
    if (dx == 0 && dy == 0) {
        dx = 1;
    }

    const int ring = constants::FOREST_NEAR_TC_MIN_RING;
    const float length = std::sqrt(static_cast<float>(dx * dx + dy * dy));
    return {
        std::clamp(
            nearest->x + static_cast<int>(std::lround(static_cast<float>(dx) / length * ring)),
            0,
            map_width - 1),
        std::clamp(
            nearest->y + static_cast<int>(std::lround(static_cast<float>(dy) / length * ring)),
            0,
            map_height - 1),
    };
}

void fill_map_forest(
    components::MapGrid& map,
    const int wood,
    const std::vector<core::GridPos>& starts)
{
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const core::GridPos cell{x, y};
            if (in_start_clearance(cell, starts, constants::PATTERN_START_CLEAR_RADIUS)) {
                continue;
            }
            place_forest_patch(map, cell, wood);
        }
    }
}

void apply_resource_piece(
    components::MapGrid& map,
    std::uint32_t& rng,
    const MapPiece& piece,
    const MapGenerationConfig& config)
{
    int gold_left = piece.gold_count;
    int berry_left = piece.berry_count;
    scatter_in_rect(map, rng, piece, gold_left + berry_left, [&](components::MapGrid& grid, const core::GridPos cell) {
        if (gold_left > 0) {
            place_gold(grid, cell, config.mine_money_capacity);
            --gold_left;
            return;
        }
        if (berry_left > 0) {
            place_berries(grid, cell, config.bush_food_capacity);
            --berry_left;
        }
    });
}

[[nodiscard]] std::vector<core::GridPos> default_starts(
    const std::uint8_t player_count,
    const int map_width,
    const int map_height)
{
    std::vector<core::GridPos> starts{};
    const auto layouts = scenario::base_layouts_for_player_count(player_count);
    starts.reserve(layouts.size());
    for (const scenario::PlayerBaseLayout& layout : layouts) {
        starts.push_back(core::GridPos{
            layout.tc_x * map_width / constants::MAP_TEST_WIDTH,
            layout.tc_y * map_height / constants::MAP_TEST_HEIGHT,
        });
    }
    return starts;
}

void place_starts_in_rect(
    const components::MapGrid& map,
    std::uint32_t& rng,
    const MapPiece& piece,
    const int count,
    const int min_separation,
    std::vector<core::GridPos>& starts)
{
    for (int placed = 0; placed < count; ++placed) {
        bool found = false;
        for (int attempt = 0; attempt < constants::MAP_GEN_SCATTER_MAX_ATTEMPTS; ++attempt) {
            const int x = rng_range(rng, piece.x, piece.x + std::max(0, piece.width - 1));
            const int y = rng_range(rng, piece.y, piece.y + std::max(0, piece.height - 1));
            const core::GridPos cell{x, y};
            if (!in_map(map, cell) || too_close(cell, starts, min_separation)) {
                continue;
            }

            starts.push_back(cell);
            found = true;
            break;
        }

        if (!found) {
            starts.push_back(core::GridPos{
                std::clamp(piece.x, 0, map.width - 1),
                std::clamp(piece.y, 0, map.height - 1),
            });
        }
    }
}

void carve_start_resource_access(GeneratedMap& result)
{
    const int half_width = constants::PATTERN_START_ACCESS_ROAD_HALF_WIDTH;
    const int reach = constants::PATTERN_START_ACCESS_REACH;
    const int reach_sq = reach * reach;
    for (const core::GridPos& start : result.start_anchors) {
        std::vector<core::GridPos> targets{};
        for (int y = start.y - reach; y <= start.y + reach; ++y) {
            for (int x = start.x - reach; x <= start.x + reach; ++x) {
                const core::GridPos cell{x, y};
                if (!in_map(result.grid, cell)) {
                    continue;
                }
                const int dx = x - start.x;
                const int dy = y - start.y;
                if (dx * dx + dy * dy > reach_sq) {
                    continue;
                }

                const components::TileType tile = result.grid.tiles[cell_index(result.grid, cell)];
                if (tile != components::TileType::GoldMine && tile != components::TileType::Berries
                    && tile != components::TileType::Blueberries) {
                    continue;
                }
                targets.push_back(cell);
            }
        }

        for (const core::GridPos& lake : result.mana_lake_anchors) {
            const int dx = lake.x - start.x;
            const int dy = lake.y - start.y;
            if (dx * dx + dy * dy > reach_sq) {
                continue;
            }
            targets.push_back(lake);
        }

        for (const core::GridPos& target : targets) {
            carve_road(result.grid, start, target, half_width);
        }
    }
}

GeneratedMap generate_from_pieces(const MapGenerationConfig& config)
{
    GeneratedMap result{};
    const MapPattern& pattern = config.pattern;
    init_empty_map(result.grid, pattern.map_width, pattern.map_height);
    std::uint32_t rng = make_rng(config, 0xA5A5A5A5U);
    const std::vector<core::GridPos> ring = start_ring_positions(
        config.player_count,
        result.grid.width,
        result.grid.height);
    const core::GridPos world_center{
        result.grid.width / 2,
        result.grid.height / 2,
    };

    std::vector<const MapPiece*> start_pieces{};
    bool has_multi_start = false;
    for (const MapPiece& piece : pattern.pieces) {
        if (piece.kind == MapPieceKind::StartPosition) {
            start_pieces.push_back(&piece);
        }
        if (piece.kind == MapPieceKind::MultiStartPosition) {
            has_multi_start = true;
        }
    }

    int rel_min_x = pattern.map_width;
    int rel_min_y = pattern.map_height;
    int rel_max_x = 0;
    int rel_max_y = 0;
    for (const MapPiece& piece : pattern.pieces) {
        rel_min_x = std::min(rel_min_x, piece.x);
        rel_min_y = std::min(rel_min_y, piece.y);
        rel_max_x = std::max(rel_max_x, piece.x + piece.width);
        rel_max_y = std::max(rel_max_y, piece.y + piece.height);
    }
    if (rel_max_x <= rel_min_x || rel_max_y <= rel_min_y) {
        rel_min_x = 0;
        rel_min_y = 0;
        rel_max_x = pattern.map_width;
        rel_max_y = pattern.map_height;
    }
    const int rel_width = std::max(1, rel_max_x - rel_min_x);
    const int rel_height = std::max(1, rel_max_y - rel_min_y);
    const float rel_cx = static_cast<float>(rel_min_x + rel_max_x - 1) * 0.5F;
    const float rel_cy = static_cast<float>(rel_min_y + rel_max_y - 1) * 0.5F;
    std::sort(start_pieces.begin(), start_pieces.end(), [&](const MapPiece* left, const MapPiece* right) {
        return piece_angle_from_center(*left, rel_cx, rel_cy)
            < piece_angle_from_center(*right, rel_cx, rel_cy);
    });

    if (has_multi_start || start_pieces.empty()) {
        result.start_anchors = ring;
    }
    else {
        std::vector<std::size_t> ring_order(ring.size());
        for (std::size_t index = 0; index < ring_order.size(); ++index) {
            ring_order[index] = index;
        }
        std::sort(ring_order.begin(), ring_order.end(), [&](const std::size_t left, const std::size_t right) {
            const float left_angle = std::atan2(
                static_cast<float>(ring[left].y) - static_cast<float>(world_center.y),
                static_cast<float>(ring[left].x) - static_cast<float>(world_center.x));
            const float right_angle = std::atan2(
                static_cast<float>(ring[right].y) - static_cast<float>(world_center.y),
                static_cast<float>(ring[right].x) - static_cast<float>(world_center.x));
            return left_angle < right_angle;
        });

        const std::size_t count = std::min(start_pieces.size(), ring_order.size());
        for (std::size_t index = 0; index < count; ++index) {
            result.start_anchors.push_back(ring[ring_order[index]]);
        }
    }

    std::vector<std::vector<core::GridPos>> piece_worlds(pattern.pieces.size());
    const auto set_worlds = [&](const std::uint32_t id, std::vector<core::GridPos> cells) {
        for (std::size_t index = 0; index < pattern.pieces.size(); ++index) {
            if (pattern.pieces[index].id == id) {
                piece_worlds[index] = std::move(cells);
                return;
            }
        }
    };
    const auto worlds_of = [&](const std::uint32_t id) -> std::vector<core::GridPos> {
        for (std::size_t index = 0; index < pattern.pieces.size(); ++index) {
            if (pattern.pieces[index].id == id) {
                if (piece_worlds[index].empty()) {
                    return {world_center};
                }
                return piece_worlds[index];
            }
        }
        return {world_center};
    };
    const auto world_of = [&](const std::uint32_t id) {
        return worlds_of(id).front();
    };

    for (std::size_t index = 0; index < start_pieces.size() && index < result.start_anchors.size();
         ++index) {
        set_worlds(start_pieces[index]->id, {result.start_anchors[index]});
    }
    for (const MapPiece& piece : pattern.pieces) {
        if (piece.kind == MapPieceKind::MultiStartPosition) {
            set_worlds(piece.id, {world_center});
        }
    }

    struct RelativeAnchor {
        core::GridPos cell{};
        std::optional<core::GridPos> start{};
    };

    const auto relative_anchor = [&](const MapPiece& piece) -> std::optional<RelativeAnchor> {
        for (const MapPieceLink& link : pattern.links) {
            if (link.kind != MapLinkKind::Relative) {
                continue;
            }
            if (link.from_id != piece.id && link.to_id != piece.id) {
                continue;
            }
            const std::uint32_t other = link.from_id == piece.id ? link.to_id : link.from_id;
            const MapPiece* linked = find_piece(pattern, other);
            if (linked == nullptr) {
                continue;
            }
            if (linked->kind == MapPieceKind::StartPosition) {
                const core::GridPos rel_offset{
                    piece_center(piece).x - piece_center(*linked).x,
                    piece_center(piece).y - piece_center(*linked).y,
                };
                const core::GridPos start = world_of(linked->id);
                return RelativeAnchor{
                    clamp_world_cell(
                        {start.x + rel_offset.x, start.y + rel_offset.y},
                        result.grid.width,
                        result.grid.height),
                    start,
                };
            }
            if (linked->kind == MapPieceKind::MultiStartPosition) {
                const core::GridPos rel_offset{
                    piece_center(piece).x - piece_center(*linked).x,
                    piece_center(piece).y - piece_center(*linked).y,
                };
                return RelativeAnchor{
                    clamp_world_cell(
                        {world_center.x + rel_offset.x, world_center.y + rel_offset.y},
                        result.grid.width,
                        result.grid.height),
                    std::nullopt,
                };
            }
        }
        return std::nullopt;
    };

    const float economy_spin = rng_angle(rng);
    const float ref_angle = result.start_anchors.empty()
        ? 0.0F
        : cell_angle_from_center(result.start_anchors.front(), world_center);

    for (const MapPiece& piece : pattern.pieces) {
        if (piece_is_start(piece.kind)) {
            continue;
        }
        if (piece.kind == MapPieceKind::MiddlePoint) {
            set_worlds(piece.id, {world_center});
            continue;
        }

        core::GridPos base = map_relative_cell(
            {
                piece_center(piece).x - rel_min_x,
                piece_center(piece).y - rel_min_y,
            },
            rel_width,
            rel_height,
            result.grid.width,
            result.grid.height);
        if (const std::optional<RelativeAnchor> anchored = relative_anchor(piece)) {
            base = anchored->cell;
            if (anchored->start.has_value()) {
                const int spin_dx = anchored->cell.x - anchored->start->x;
                const int spin_dy = anchored->cell.y - anchored->start->y;
                const core::GridPos spun = rotate_grid_offset(spin_dx, spin_dy, economy_spin);
                base = clamp_world_cell(
                    {anchored->start->x + spun.x, anchored->start->y + spun.y},
                    result.grid.width,
                    result.grid.height);
            }
        }

        if (!has_multi_start || !piece_instances_per_start(piece.kind)
            || result.start_anchors.empty()) {
            set_worlds(piece.id, {base});
            continue;
        }

        std::vector<core::GridPos> copies{};
        copies.reserve(result.start_anchors.size());
        const int dx = base.x - world_center.x;
        const int dy = base.y - world_center.y;
        for (const core::GridPos& start : result.start_anchors) {
            const float delta = cell_angle_from_center(start, world_center) - ref_angle;
            const core::GridPos rotated = rotate_grid_offset(dx, dy, delta);
            copies.push_back(clamp_world_cell(
                {world_center.x + rotated.x, world_center.y + rotated.y},
                result.grid.width,
                result.grid.height));
        }
        set_worlds(piece.id, std::move(copies));
    }

    for (const MapPiece& piece : pattern.pieces) {
        if (piece.kind != MapPieceKind::Terrain) {
            continue;
        }

        for (const core::GridPos& cell : worlds_of(piece.id)) {
            paint_blob(
                result.grid,
                rng,
                cell,
                std::max(piece.width, piece.height) / 2,
                static_cast<components::GroundType>(piece.ground_type));
        }
    }

    clear_start_territory(result.grid, result.start_anchors);

    bool has_forest_fill = false;
    for (const MapPiece& piece : pattern.pieces) {
        if (piece.kind == MapPieceKind::Forest && piece_is_forest_fill(piece, result.grid)) {
            has_forest_fill = true;
            break;
        }
    }
    const int road_half_width = has_forest_fill
        ? constants::PATTERN_FOREST_FILL_ROAD_HALF_WIDTH
        : constants::PATTERN_ROAD_HALF_WIDTH;

    for (const MapPiece& piece : pattern.pieces) {
        if (piece.kind != MapPieceKind::Forest) {
            continue;
        }
        if (piece_is_forest_fill(piece, result.grid)) {
            fill_map_forest(result.grid, config.forest_patch_wood, result.start_anchors);
            continue;
        }

        const int radius = std::clamp(
            piece.range,
            constants::PATTERN_FOREST_MIN_RANGE,
            constants::PATTERN_FOREST_MAX_RANGE);
        for (const core::GridPos& cell : worlds_of(piece.id)) {
            grow_organic_forest(
                result.grid,
                rng,
                nudge_out_of_starts(
                    cell,
                    result.start_anchors,
                    result.grid.width,
                    result.grid.height),
                std::max(constants::PATTERN_FOREST_BLOB_BASE_TILES, radius * radius),
                config.forest_patch_wood,
                result.start_anchors);
        }
    }

    const auto carve_roads_between = [&](
                                         const std::vector<core::GridPos>& from_cells,
                                         const std::vector<core::GridPos>& to_cells) {
        if (from_cells.size() == to_cells.size()) {
            for (std::size_t index = 0; index < from_cells.size(); ++index) {
                carve_road(result.grid, from_cells[index], to_cells[index], road_half_width);
            }
            return;
        }

        for (const core::GridPos& from_cell : from_cells) {
            for (const core::GridPos& to_cell : to_cells) {
                carve_road(result.grid, from_cell, to_cell, road_half_width);
            }
        }
    };

    for (const MapPieceLink& link : pattern.links) {
        if (link.kind != MapLinkKind::Road) {
            continue;
        }

        const MapPiece* from = find_piece(pattern, link.from_id);
        const MapPiece* to = find_piece(pattern, link.to_id);
        if (from == nullptr || to == nullptr) {
            continue;
        }

        if (from->kind == MapPieceKind::MultiStartPosition || to->kind == MapPieceKind::MultiStartPosition) {
            const MapPiece* other =
                from->kind == MapPieceKind::MultiStartPosition ? to : from;
            carve_roads_between(result.start_anchors, worlds_of(other->id));
            continue;
        }

        carve_roads_between(worlds_of(from->id), worlds_of(to->id));
    }

    std::vector<core::GridPos> placed_resources{};
    const auto snap_piece_resource = [&](const core::GridPos cell) -> std::optional<core::GridPos> {
        const core::GridPos nudged = nudge_out_of_starts(
            cell,
            result.start_anchors,
            result.grid.width,
            result.grid.height);
        return snap_clear_cell(
            result.grid,
            nudged,
            result.start_anchors,
            placed_resources,
            constants::PATTERN_START_PLAZA_RADIUS);
    };

    for (const MapPiece& piece : pattern.pieces) {
        if (piece.kind == MapPieceKind::Gold) {
            for (const core::GridPos& cell : worlds_of(piece.id)) {
                const std::optional<core::GridPos> seed = snap_piece_resource(cell);
                if (!seed.has_value()) {
                    continue;
                }
                placed_resources.push_back(*seed);
                place_connected_cluster(
                    result.grid,
                    rng,
                    *seed,
                    piece.gold_count,
                    result.start_anchors,
                    constants::PATTERN_START_PLAZA_RADIUS,
                    [&](components::MapGrid& map, const core::GridPos gold_cell) {
                        place_gold(map, gold_cell, config.mine_money_capacity);
                    });
            }
            continue;
        }

        if (piece.kind == MapPieceKind::Berries) {
            for (const core::GridPos& cell : worlds_of(piece.id)) {
                const std::optional<core::GridPos> seed = snap_piece_resource(cell);
                if (!seed.has_value()) {
                    continue;
                }
                placed_resources.push_back(*seed);
                place_connected_cluster(
                    result.grid,
                    rng,
                    *seed,
                    piece.berry_count,
                    result.start_anchors,
                    constants::PATTERN_START_PLAZA_RADIUS,
                    [&](components::MapGrid& map, const core::GridPos berry_cell) {
                        place_berries(map, berry_cell, config.bush_food_capacity);
                    });
            }
            continue;
        }

        if (piece.kind == MapPieceKind::ManaLake) {
            for (const core::GridPos& cell : worlds_of(piece.id)) {
                const std::optional<core::GridPos> seed = snap_piece_resource(cell);
                if (!seed.has_value()) {
                    continue;
                }
                placed_resources.push_back(*seed);
                result.mana_lake_anchors.push_back(*seed);
            }
        }
    }

    if (pattern.random_gold) {
        place_random_nodes(
            result.grid,
            rng,
            pattern.random_gold_count,
            result.start_anchors,
            [&](components::MapGrid& map, const core::GridPos cell) {
                place_gold(map, cell, config.mine_money_capacity);
            });
    }
    if (pattern.random_berries) {
        place_random_nodes(
            result.grid,
            rng,
            pattern.random_berry_count,
            result.start_anchors,
            [&](components::MapGrid& map, const core::GridPos cell) {
                place_berries(map, cell, config.bush_food_capacity);
            });
    }

    place_start_economy(
        result.grid,
        rng,
        result.start_anchors,
        config,
        result,
        result.mana_lake_anchors.empty());

    if (pattern.border_trees) {
        place_border_trees(result.grid, config.forest_patch_wood, result.start_anchors);
    }

    if (pattern.random_stray_trees) {
        std::vector<core::GridPos> reserved{};
        for (const std::vector<core::GridPos>& cells : piece_worlds) {
            reserved.insert(reserved.end(), cells.begin(), cells.end());
        }
        place_random_stray_trees(
            result.grid,
            rng,
            pattern.random_stray_tree_count,
            config.forest_patch_wood,
            result.start_anchors,
            reserved);
    }

    clear_start_territory(result.grid, result.start_anchors);
    carve_start_resource_access(result);
    return result;
}

GeneratedMap generate_continental(const MapGenerationConfig& config)
{
    GeneratedMap result{};
    init_empty_map(result.grid, config.pattern.map_width, config.pattern.map_height);
    std::uint32_t rng = make_rng(config, 0xC0A1U);
    result.start_anchors = default_starts(
        config.player_count,
        result.grid.width,
        result.grid.height);

    for (std::size_t index = 0; index < result.grid.ground.size(); ++index) {
        result.grid.ground[index] = components::GroundType::Sand;
    }

    for (const core::GridPos& start : result.start_anchors) {
        paint_blob(result.grid, rng, start, 14, components::GroundType::Grass);
    }

    paint_blob(
        result.grid,
        rng,
        {result.grid.width / 2, 6},
        10,
        components::GroundType::Snow);

    MapPiece forest_near{};
    forest_near.width = 8;
    forest_near.height = 8;
    forest_near.density = 40;
    forest_near.patch_size = 3;
    MapPiece resources_near{};
    resources_near.width = 6;
    resources_near.height = 6;
    resources_near.gold_count = 2;
    resources_near.berry_count = 1;
    for (const core::GridPos& start : result.start_anchors) {
        forest_near.x = start.x + 3;
        forest_near.y = start.y + 3;
        resources_near.x = start.x + 4;
        resources_near.y = start.y;
        apply_forest_piece(
            result.grid,
            rng,
            forest_near,
            config.forest_patch_wood,
            result.start_anchors);
        apply_resource_piece(result.grid, rng, resources_near, config);
    }

    carve_start_resource_access(result);
    return result;
}

GeneratedMap generate_archipelago(const MapGenerationConfig& config)
{
    GeneratedMap result{};
    init_empty_map(result.grid, config.pattern.map_width, config.pattern.map_height);
    std::uint32_t rng = make_rng(config, 0xA4C4U);
    result.start_anchors = default_starts(
        config.player_count,
        result.grid.width,
        result.grid.height);

    for (std::size_t index = 0; index < result.grid.ground.size(); ++index) {
        result.grid.ground[index] = components::GroundType::Sand;
    }

    for (const core::GridPos& start : result.start_anchors) {
        paint_blob(result.grid, rng, start, 8, components::GroundType::Grass);
        MapPiece forest_near{};
        forest_near.x = start.x + 2;
        forest_near.y = start.y + 2;
        forest_near.width = 5;
        forest_near.height = 5;
        forest_near.density = 45;
        forest_near.patch_size = 2;
        apply_forest_piece(
            result.grid,
            rng,
            forest_near,
            config.forest_patch_wood,
            result.start_anchors);
        MapPiece resources_near{};
        resources_near.x = start.x + 1;
        resources_near.y = start.y + 3;
        resources_near.width = 4;
        resources_near.height = 4;
        resources_near.gold_count = 1;
        resources_near.berry_count = 1;
        apply_resource_piece(result.grid, rng, resources_near, config);
    }

    const int extra_islands = 3;
    for (int island = 0; island < extra_islands; ++island) {
        const core::GridPos center{
            rng_range(rng, 8, result.grid.width - 9),
            rng_range(rng, 8, result.grid.height - 9),
        };
        paint_blob(result.grid, rng, center, 5, components::GroundType::Grass);
    }

    carve_start_resource_access(result);
    return result;
}

} // namespace

GeneratedMap generate_map(const MapGenerationConfig& config)
{
    if (config.pattern.builtin == MapPatternBuiltin::Default
        || (config.pattern.builtin == MapPatternBuiltin::Custom && config.pattern.pieces.empty()
            && config.pattern.name.empty())) {
        GeneratedMap result{};
        result.grid = create_test_map(
            config.forest_patch_wood,
            config.bush_food_capacity,
            config.mine_money_capacity,
            config.player_count,
            config.seed);
        result.start_anchors = default_starts(
            config.player_count,
            result.grid.width,
            result.grid.height);
        return result;
    }

    if (config.pattern.builtin == MapPatternBuiltin::Continental) {
        return generate_continental(config);
    }

    if (config.pattern.builtin == MapPatternBuiltin::Archipelago) {
        return generate_archipelago(config);
    }

    return generate_from_pieces(config);
}

std::uint64_t generate_map_seed()
{
    std::random_device device{};
    const std::uint64_t mixed =
        (static_cast<std::uint64_t>(device()) << 32U) ^ static_cast<std::uint64_t>(device())
        ^ static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    return mixed == 0U ? 1U : mixed;
}

} // namespace aoa::sim::map
