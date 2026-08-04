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

bool can_step_to(
    const components::MapGrid& map,
    const core::GridPos current,
    const core::GridPos offset,
    const bool allow_forest)
{
    const core::GridPos neighbor{current.x + offset.x, current.y + offset.y};
    const std::vector<core::GridPos> cells = cells_along_step(current, neighbor);

    if (cells.size() < 2U) {
        return false;
    }

    for (std::size_t index = 1U; index < cells.size(); ++index) {
        if (!is_tile_walkable(map, cells[index], allow_forest)) {
            return false;
        }
    }

    if (!is_single_tile_diagonal_step(offset)) {
        return true;
    }

    const core::GridPos cardinal_a{current.x + offset.x, current.y};
    const core::GridPos cardinal_b{current.x, current.y + offset.y};
    return is_tile_walkable(map, cardinal_a, allow_forest)
        && is_tile_walkable(map, cardinal_b, allow_forest);
}

core::GridPos move_segment_destination_cell(const components::MoveSegment& segment)
{
    const math::Fixed half_tile = math::Fixed::from_int(1) / math::Fixed::from_int(2);
    return {
        (segment.to_x - half_tile).to_int(),
        (segment.to_y - half_tile).to_int(),
    };
}

} // namespace

bool is_movement_blocked(
    entt::registry& registry,
    const core::GridPos cell,
    const entt::entity ignore)
{
    const auto unit_view = registry.view<components::UnitTag, components::GridPosition, components::Health>();
    for (const entt::entity entity : unit_view) {
        if (entity == ignore) {
            continue;
        }

        const auto& health = unit_view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        if (unit_view.get<components::GridPosition>(entity).cell == cell) {
            return true;
        }
    }

    const auto building_view = registry.view<components::BuildingTag, components::GridPosition, components::Health>();
    for (const entt::entity entity : building_view) {
        if (entity == ignore) {
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

    const auto segment_view = registry.view<components::UnitTag, components::MoveSegment>();
    for (const entt::entity entity : segment_view) {
        if (entity == ignore) {
            continue;
        }

        if (move_segment_destination_cell(segment_view.get<components::MoveSegment>(entity)) == cell) {
            return true;
        }
    }

    return false;
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
    const bool allow_forest)
{
    if (start == goal) {
        return {};
    }

    if (!is_tile_walkable(map, goal, allow_forest)) {
        return {};
    }

    if (is_movement_blocked(registry, goal, ignore)) {
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

        for (const PathStepOffset& step : path_step_offsets) {
            const core::GridPos offset = step.offset;
            const core::GridPos neighbor{current.x + offset.x, current.y + offset.y};

            if (!can_step_to(map, current, offset, allow_forest)) {
                continue;
            }

            const std::vector<core::GridPos> step_cells = cells_along_step(current, neighbor);
            bool blocked = false;
            for (std::size_t index = 1U; index < step_cells.size(); ++index) {
                const core::GridPos cell = step_cells[index];
                if (cell == goal) {
                    continue;
                }

                if (is_movement_blocked(registry, cell, ignore)) {
                    blocked = true;
                    break;
                }
            }

            if (blocked) {
                continue;
            }

            const NodeKey neighbor_key{neighbor.x, neighbor.y};
            const int tentative_g = g_score.at(NodeKey{current.x, current.y}) + step.cost;

            const auto existing = g_score.find(neighbor_key);
            if (existing != g_score.end() && tentative_g >= existing->second) {
                continue;
            }

            came_from.insert_or_assign(neighbor_key, NodeKey{current.x, current.y});
            g_score.insert_or_assign(neighbor_key, tentative_g);
            open.push(QueueNode{neighbor, tentative_g + heuristic(neighbor, goal)});
        }
    }

    return {};
}

} // namespace aoa::sim::systems
