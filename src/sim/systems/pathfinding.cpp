#include "sim/systems/pathfinding.hpp"

#include "core/constants.hpp"
#include "math/fixed.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <queue>
#include <unordered_map>
#include <limits>
#include <vector>

namespace aoa::sim::systems {

namespace {

struct NodeKey {
    int x{0};
    int y{0};

    auto operator==(const NodeKey& other) const -> bool = default;
};

struct NodeKeyHash {
    std::size_t operator()(const NodeKey key) const
    {
        return static_cast<std::size_t>(key.y) * 10000U + static_cast<std::size_t>(key.x);
    }
};

struct PathStepOffset {
    core::GridPos offset{};
    int cost{0};
};

constexpr std::array<PathStepOffset, 8> grid8_step_offsets = {{
    PathStepOffset{{1, 0}, constants::PATHFIND_CARDINAL_STEP_COST},
    PathStepOffset{{1, 1}, constants::PATHFIND_DIAGONAL_STEP_COST},
    PathStepOffset{{0, 1}, constants::PATHFIND_CARDINAL_STEP_COST},
    PathStepOffset{{-1, 1}, constants::PATHFIND_DIAGONAL_STEP_COST},
    PathStepOffset{{-1, 0}, constants::PATHFIND_CARDINAL_STEP_COST},
    PathStepOffset{{-1, -1}, constants::PATHFIND_DIAGONAL_STEP_COST},
    PathStepOffset{{0, -1}, constants::PATHFIND_CARDINAL_STEP_COST},
    PathStepOffset{{1, -1}, constants::PATHFIND_DIAGONAL_STEP_COST},
}};
constexpr std::array<PathStepOffset, 16> path_step_offsets = {{
    PathStepOffset{{1, 0}, constants::PATHFIND_CARDINAL_STEP_COST},
    PathStepOffset{{2, 1}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{1, 1}, constants::PATHFIND_DIAGONAL_STEP_COST},
    PathStepOffset{{1, 2}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{0, 1}, constants::PATHFIND_CARDINAL_STEP_COST},
    PathStepOffset{{-1, 2}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{-1, 1}, constants::PATHFIND_DIAGONAL_STEP_COST},
    PathStepOffset{{-2, 1}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{-1, 0}, constants::PATHFIND_CARDINAL_STEP_COST},
    PathStepOffset{{-2, -1}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{-1, -1}, constants::PATHFIND_DIAGONAL_STEP_COST},
    PathStepOffset{{-1, -2}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{0, -1}, constants::PATHFIND_CARDINAL_STEP_COST},
    PathStepOffset{{1, -2}, constants::PATHFIND_KNIGHT_STEP_COST},
    PathStepOffset{{1, -1}, constants::PATHFIND_DIAGONAL_STEP_COST},
    PathStepOffset{{2, -1}, constants::PATHFIND_KNIGHT_STEP_COST},
}};

bool is_single_tile_diagonal_step(const core::GridPos offset)
{
    return std::abs(offset.x) == 1 && std::abs(offset.y) == 1;
}

std::vector<core::GridPos> cells_along_step(const core::GridPos from, const core::GridPos to)
{
    std::vector<core::GridPos> cells{};

    int x0 = from.x;
    int y0 = from.y;
    const int x1 = to.x;
    const int y1 = to.y;

    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);
    const int step_x = x0 < x1 ? 1 : -1;
    const int step_y = y0 < y1 ? 1 : -1;

    int err = dx - dy;

    while (true) {
        cells.push_back({x0, y0});

        if (x0 == x1 && y0 == y1) {
            break;
        }

        const int err2 = err * 2;
        if (err2 > -dy) {
            err -= dy;
            x0 += step_x;
        }

        if (err2 < dx) {
            err += dx;
            y0 += step_y;
        }
    }

    return cells;
}

core::GridPos unit_effective_cell(
    entt::registry& registry,
    const entt::entity entity,
    const core::GridPos fallback)
{
    if (registry.any_of<components::MoveSegment>(entity)
        && registry.any_of<components::GridPosition>(entity)) {
        return registry.get<components::GridPosition>(entity).cell;
    }

    if (registry.all_of<components::WorldPosition>(entity)) {
        const auto& world = registry.get<components::WorldPosition>(entity);
        return {world.x.to_int(), world.y.to_int()};
    }

    return fallback;
}

} // namespace

core::GridPos move_segment_destination_cell(const math::Fixed to_x, const math::Fixed to_y)
{
    const math::Fixed half_tile = math::Fixed::from_int(1) / math::Fixed::from_int(2);
    return {
        (to_x - half_tile).to_int(),
        (to_y - half_tile).to_int(),
    };
}

core::GridPos first_grid_step_toward(const core::GridPos from, const core::GridPos to)
{
    if (from == to) {
        return to;
    }

    const std::vector<core::GridPos> steps = cells_along_step(from, to);
    if (steps.size() >= 2U) {
        return steps[1U];
    }

    return to;
}

core::GridPos world_position_to_grid_cell(const components::WorldPosition& world)
{
    return {world.x.to_int(), world.y.to_int()};
}

core::GridPos unit_movement_grid_cell(entt::registry& registry, const entt::entity entity)
{
    if (registry.all_of<components::WorldPosition>(entity)) {
        return world_position_to_grid_cell(registry.get<components::WorldPosition>(entity));
    }

    if (registry.all_of<components::GridPosition>(entity)) {
        return registry.get<components::GridPosition>(entity).cell;
    }

    return {0, 0};
}

core::GridPos unit_occupancy_grid_cell(entt::registry& registry, const entt::entity entity)
{
    if (registry.all_of<components::GridPosition>(entity)) {
        return registry.get<components::GridPosition>(entity).cell;
    }

    if (registry.all_of<components::WorldPosition>(entity)) {
        return world_position_to_grid_cell(registry.get<components::WorldPosition>(entity));
    }

    return {0, 0};
}

bool unit_grid_adjacent(
    entt::registry& registry,
    const entt::entity from,
    const entt::entity to)
{
    if (!registry.valid(from) || !registry.valid(to)) {
        return false;
    }

    return core::chebyshev_distance(
               unit_occupancy_grid_cell(registry, from),
               unit_occupancy_grid_cell(registry, to))
        == 1;
}

bool try_unit_world_position(
    entt::registry& registry,
    const entt::entity entity,
    float& world_x,
    float& world_z)
{
    if (registry.all_of<components::WorldPosition>(entity)) {
        const auto& world = registry.get<components::WorldPosition>(entity);
        world_x = world.x.to_float();
        world_z = world.y.to_float();
        return true;
    }

    if (registry.all_of<components::GridPosition>(entity)) {
        const core::GridPos cell = registry.get<components::GridPosition>(entity).cell;
        world_x = static_cast<float>(cell.x) + 0.5F;
        world_z = static_cast<float>(cell.y) + 0.5F;
        return true;
    }

    return false;
}

namespace {

float melee_max_center_distance_sq_for_cells(
    const core::GridPos from_cell,
    const core::GridPos to_cell)
{
    (void)from_cell;
    (void)to_cell;
    return constants::MELEE_STRIKE_MAX_CENTER_DISTANCE_SQ;
}

bool is_entity_ignored_for_movement(
    const entt::entity entity,
    const entt::entity ignore,
    const entt::entity also_ignore)
{
    return entity == ignore || entity == also_ignore;
}

} // namespace

bool unit_in_melee_range(
    entt::registry& registry,
    const entt::entity from,
    const entt::entity to)
{
    if (!registry.valid(from) || !registry.valid(to)) {
        return false;
    }

    if (!unit_grid_adjacent(registry, from, to)) {
        return false;
    }

    float from_x = 0.0F;
    float from_z = 0.0F;
    float to_x = 0.0F;
    float to_z = 0.0F;
    if (!try_unit_world_position(registry, from, from_x, from_z)
        || !try_unit_world_position(registry, to, to_x, to_z)) {
        return false;
    }

    const float delta_x = from_x - to_x;
    const float delta_z = from_z - to_z;
    const float distance_sq = delta_x * delta_x + delta_z * delta_z;
    return distance_sq <= melee_max_center_distance_sq_for_cells(
        unit_occupancy_grid_cell(registry, from),
        unit_occupancy_grid_cell(registry, to));
}

core::GridPos find_best_melee_stand_tile(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos target_cell,
    const entt::entity mover,
    const entt::entity target_entity)
{
    (void)target_entity;

    const core::GridPos mover_cell = unit_movement_grid_cell(registry, mover);
    const int toward_mover_x = mover_cell.x - target_cell.x;
    const int toward_mover_y = mover_cell.y - target_cell.y;

    const std::array<core::GridPos, 8> neighbor_offsets = {{
        core::GridPos{0, -1},
        core::GridPos{1, 0},
        core::GridPos{0, 1},
        core::GridPos{-1, 0},
        core::GridPos{1, -1},
        core::GridPos{1, 1},
        core::GridPos{-1, 1},
        core::GridPos{-1, -1},
    }};

    core::GridPos best = target_cell;
    bool found = false;
    bool best_between = false;
    int best_travel_distance = std::numeric_limits<int>::max();
    int best_approach_alignment = std::numeric_limits<int>::min();

    const bool horizontal_approach = toward_mover_x != 0 && toward_mover_y == 0;
    const bool vertical_approach = toward_mover_x == 0 && toward_mover_y != 0;

    const auto approach_alignment = [&](const core::GridPos offset, const bool is_cardinal) {
        if (horizontal_approach) {
            return is_cardinal && offset.y == 0 ? 2 : (is_cardinal ? 1 : 0);
        }

        if (vertical_approach) {
            return is_cardinal && offset.x == 0 ? 2 : (is_cardinal ? 1 : 0);
        }

        return !is_cardinal ? 2 : 1;
    };

    const auto consider = [&](const core::GridPos offset) {
        const core::GridPos candidate{target_cell.x + offset.x, target_cell.y + offset.y};
        if (!is_tile_walkable(map, candidate, false)) {
            return;
        }

        if (is_movement_blocked(registry, candidate, mover)) {
            return;
        }

        const bool between = (offset.x * toward_mover_x + offset.y * toward_mover_y) > 0;
        const bool is_cardinal = offset.x == 0 || offset.y == 0;
        const int travel_distance = core::chebyshev_distance(mover_cell, candidate);
        const int alignment = approach_alignment(offset, is_cardinal);

        if (found) {
            if (between != best_between) {
                if (!between) {
                    return;
                }
            }
            else if (travel_distance != best_travel_distance) {
                if (travel_distance > best_travel_distance) {
                    return;
                }
            }
            else if (alignment <= best_approach_alignment) {
                return;
            }
        }

        found = true;
        best_between = between;
        best_travel_distance = travel_distance;
        best_approach_alignment = alignment;
        best = candidate;
    };

    for (const core::GridPos offset : neighbor_offsets) {
        consider(offset);
    }

    return best;
}

bool is_movement_blocked(
    entt::registry& registry,
    const core::GridPos cell,
    const entt::entity ignore,
    const entt::entity also_ignore)
{
    const auto unit_view = registry.view<components::UnitTag, components::GridPosition, components::Health>();
    for (const entt::entity entity : unit_view) {
        if (is_entity_ignored_for_movement(entity, ignore, also_ignore)) {
            continue;
        }

        const auto& health = unit_view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        const core::GridPos unit_cell =
            unit_effective_cell(registry, entity, unit_view.get<components::GridPosition>(entity).cell);
        if (unit_cell == cell) {
            return true;
        }
    }

    const auto building_view = registry.view<components::BuildingTag, components::GridPosition, components::Health>();
    for (const entt::entity entity : building_view) {
        if (is_entity_ignored_for_movement(entity, ignore, also_ignore)) {
            continue;
        }

        const auto& health = building_view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        if (building_view.get<components::GridPosition>(entity).cell == cell) {
            return true;
        }
    }

    const auto segment_view = registry.view<components::UnitTag, components::GridPosition, components::MoveSegment>();
    for (const entt::entity entity : segment_view) {
        if (is_entity_ignored_for_movement(entity, ignore, also_ignore)) {
            continue;
        }

        const core::GridPos occupied_cell = segment_view.get<components::GridPosition>(entity).cell;
        if (occupied_cell == cell) {
            return true;
        }
    }

    return false;
}

bool is_step_movement_blocked(
    entt::registry& registry,
    const components::MapGrid& map,
    const core::GridPos from,
    const core::GridPos to,
    const entt::entity ignore,
    const bool allow_forest,
    const entt::entity also_ignore)
{
    const std::vector<core::GridPos> step_cells = cells_along_step(from, to);
    for (std::size_t index = 1U; index < step_cells.size(); ++index) {
        const core::GridPos cell = step_cells[index];
        if (!is_tile_walkable(map, cell, allow_forest)) {
            return true;
        }

        if (is_movement_blocked(registry, cell, ignore, also_ignore)) {
            return true;
        }
    }

    const int delta_x = to.x - from.x;
    const int delta_y = to.y - from.y;
    if (delta_x == 0 || delta_y == 0) {
        return false;
    }

    const int step_x = delta_x > 0 ? 1 : -1;
    const int step_y = delta_y > 0 ? 1 : -1;
    const core::GridPos cardinal_x{from.x + step_x, from.y};
    const core::GridPos cardinal_y{from.x, from.y + step_y};

    if (!is_tile_walkable(map, cardinal_x, allow_forest)
        || is_movement_blocked(registry, cardinal_x, ignore, also_ignore)) {
        return true;
    }

    if (!is_tile_walkable(map, cardinal_y, allow_forest)
        || is_movement_blocked(registry, cardinal_y, ignore, also_ignore)) {
        return true;
    }

    return false;
}

bool is_world_segment_movement_blocked(
    entt::registry& registry,
    const components::MapGrid& map,
    const math::Fixed from_x,
    const math::Fixed from_y,
    const math::Fixed to_x,
    const math::Fixed to_y,
    const entt::entity ignore,
    const bool allow_forest)
{
    const core::GridPos from_cell{from_x.to_int(), from_y.to_int()};
    const core::GridPos to_cell{to_x.to_int(), to_y.to_int()};
    return is_step_movement_blocked(registry, map, from_cell, to_cell, ignore, allow_forest);
}

bool is_world_position_movement_blocked(
    entt::registry& registry,
    const components::MapGrid& map,
    const components::WorldPosition& world,
    const entt::entity ignore,
    const bool allow_forest)
{
    const core::GridPos cell = world_position_to_grid_cell(world);
    if (!is_tile_walkable(map, cell, allow_forest)) {
        return true;
    }

    return is_movement_blocked(registry, cell, ignore);
}

bool is_tile_walkable(
    const components::MapGrid& map,
    const core::GridPos pos,
    const bool allow_forest)
{
    if (!core::is_inside_grid(pos, map.width, map.height)) {
        return false;
    }

    const auto tile = map.tiles[static_cast<std::size_t>(core::grid_index(pos, map.width))];
    if (tile == components::TileType::Grass) {
        return true;
    }

    if (tile == components::TileType::Forest) {
        return allow_forest;
    }

    return false;
}

std::vector<core::GridPos> find_path(
    const components::MapGrid& map,
    core::GridPos start,
    core::GridPos goal,
    entt::registry& registry,
    const entt::entity ignore,
    const bool allow_forest,
    const entt::entity also_ignore,
    const bool allow_knight_steps)
{
    if (start == goal) {
        return {};
    }

    if (!is_tile_walkable(map, goal, allow_forest)) {
        return {};
    }

    if (is_movement_blocked(registry, goal, ignore, also_ignore)) {
        return {};
    }

    const auto heuristic = [](const core::GridPos from, const core::GridPos to) {
        return std::max(std::abs(from.x - to.x), std::abs(from.y - to.y))
            * constants::PATHFIND_CARDINAL_STEP_COST;
    };

    struct QueueNode {
        core::GridPos pos{};
        int f_score{0};
    };

    const auto compare = [](const QueueNode& left, const QueueNode& right) {
        if (left.f_score != right.f_score) {
            return left.f_score > right.f_score;
        }

        if (left.pos.y != right.pos.y) {
            return left.pos.y > right.pos.y;
        }

        return left.pos.x > right.pos.x;
    };

    std::priority_queue<QueueNode, std::vector<QueueNode>, decltype(compare)> open(compare);
    std::unordered_map<NodeKey, NodeKey, NodeKeyHash> came_from;
    std::unordered_map<NodeKey, int, NodeKeyHash> g_score;

    open.push(QueueNode{start, heuristic(start, goal)});
    g_score.insert({NodeKey{start.x, start.y}, 0});

    while (!open.empty()) {
        const core::GridPos current = open.top().pos;
        open.pop();

        if (current == goal) {
            std::vector<core::GridPos> path{};
            core::GridPos cursor = goal;

            while (!(cursor == start)) {
                path.push_back(cursor);
                const NodeKey key{cursor.x, cursor.y};
                const NodeKey previous = came_from.at(key);
                cursor = {previous.x, previous.y};
            }

            std::reverse(path.begin(), path.end());
            return path;
        }

        const auto try_neighbor = [&](const PathStepOffset& step) {
            const core::GridPos offset = step.offset;
            const core::GridPos neighbor{current.x + offset.x, current.y + offset.y};

            if (is_step_movement_blocked(
                    registry, map, current, neighbor, ignore, allow_forest, also_ignore)) {
                return;
            }

            const NodeKey neighbor_key{neighbor.x, neighbor.y};
            const int tentative_g = g_score.at(NodeKey{current.x, current.y}) + step.cost;

            const auto existing = g_score.find(neighbor_key);
            if (existing != g_score.end() && tentative_g >= existing->second) {
                return;
            }

            came_from.insert_or_assign(neighbor_key, NodeKey{current.x, current.y});
            g_score.insert_or_assign(neighbor_key, tentative_g);
            open.push(QueueNode{neighbor, tentative_g + heuristic(neighbor, goal)});
        };

        if (allow_knight_steps) {
            for (const PathStepOffset& step : path_step_offsets) {
                try_neighbor(step);
            }
        }
        else {
            for (const PathStepOffset& step : grid8_step_offsets) {
                try_neighbor(step);
            }
        }
    }

    return {};
}

std::vector<core::GridPos> build_direct_line_path(
    entt::registry& registry,
    const components::MapGrid& map,
    core::GridPos start,
    core::GridPos goal,
    const entt::entity ignore,
    const bool allow_forest,
    const entt::entity also_ignore)
{
    if (start == goal) {
        return {};
    }

    std::vector<core::GridPos> path{};
    core::GridPos current = start;

    for (int step_index = 0;
        step_index < constants::ATTACK_PATH_DIRECT_LINE_MAX_STEPS && current != goal;
        ++step_index) {
        const core::GridPos next = first_grid_step_toward(current, goal);
        if (next == current) {
            return {};
        }

        if (is_step_movement_blocked(registry, map, current, next, ignore, allow_forest, also_ignore)) {
            return {};
        }

        path.push_back(next);
        current = next;
    }

    if (current != goal) {
        return {};
    }

    return path;
}

std::vector<core::GridPos> find_attack_path(
    const components::MapGrid& map,
    core::GridPos start,
    core::GridPos goal,
    entt::registry& registry,
    const entt::entity ignore,
    const bool allow_forest,
    const entt::entity also_ignore)
{
    std::vector<core::GridPos> direct =
        build_direct_line_path(registry, map, start, goal, ignore, allow_forest, also_ignore);
    if (!direct.empty()) {
        return direct;
    }

    std::vector<core::GridPos> path =
        find_path(map, start, goal, registry, ignore, allow_forest, also_ignore, false);
    if (!path.empty()) {
        return path;
    }

    return find_path(map, start, goal, registry, ignore, allow_forest, also_ignore, true);
}

bool attack_path_follows_direct_line(
    const core::GridPos start,
    const core::GridPos goal,
    const std::vector<core::GridPos>& path_cells,
    const int path_start_index)
{
    if (path_start_index < 0 || path_start_index >= static_cast<int>(path_cells.size())) {
        return path_cells.empty() || start == goal;
    }

    core::GridPos cursor = start;
    for (std::size_t index = static_cast<std::size_t>(path_start_index); index < path_cells.size(); ++index) {
        const core::GridPos expected = first_grid_step_toward(cursor, goal);
        if (path_cells[index] != expected) {
            return false;
        }

        cursor = path_cells[index];
    }

    return cursor == goal;
}

} // namespace aoa::sim::systems
