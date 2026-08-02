#include "sim/systems/pathfinding.hpp"

#include <algorithm>
#include <array>
#include <queue>
#include <unordered_map>

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

bool is_diagonal_step(const core::GridPos offset)
{
    return offset.x != 0 && offset.y != 0;
}

bool can_step_to(
    const components::MapGrid& map,
    const core::GridPos current,
    const core::GridPos offset,
    const bool allow_forest)
{
    const core::GridPos neighbor{current.x + offset.x, current.y + offset.y};
    if (!is_tile_walkable(map, neighbor, allow_forest)) {
        return false;
    }

    if (!is_diagonal_step(offset)) {
        return true;
    }

    const core::GridPos cardinal_a{current.x + offset.x, current.y};
    const core::GridPos cardinal_b{current.x, current.y + offset.y};
    return is_tile_walkable(map, cardinal_a, allow_forest)
        && is_tile_walkable(map, cardinal_b, allow_forest);
}

} // namespace

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
    const bool allow_forest)
{
    if (start == goal) {
        return {};
    }

    if (!is_tile_walkable(map, goal, allow_forest)) {
        return {};
    }

    const auto heuristic = [](const core::GridPos from, const core::GridPos to) {
        return std::max(std::abs(from.x - to.x), std::abs(from.y - to.y));
    };

    const std::array<core::GridPos, 8> neighbor_offsets = {
        core::GridPos{0, -1},
        core::GridPos{1, 0},
        core::GridPos{0, 1},
        core::GridPos{-1, 0},
        core::GridPos{1, -1},
        core::GridPos{1, 1},
        core::GridPos{-1, 1},
        core::GridPos{-1, -1},
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

        for (const core::GridPos offset : neighbor_offsets) {
            const core::GridPos neighbor{current.x + offset.x, current.y + offset.y};

            if (!can_step_to(map, current, offset, allow_forest)) {
                continue;
            }

            const NodeKey neighbor_key{neighbor.x, neighbor.y};
            const int step_cost = is_diagonal_step(offset) ? 14 : 10;
            const int tentative_g = g_score.at(NodeKey{current.x, current.y}) + step_cost;

            const auto existing = g_score.find(neighbor_key);
            if (existing != g_score.end() && tentative_g >= existing->second) {
                continue;
            }

            came_from.insert_or_assign(neighbor_key, NodeKey{current.x, current.y});
            g_score.insert_or_assign(neighbor_key, tentative_g);
            open.push(QueueNode{neighbor, tentative_g + heuristic(neighbor, goal) * 10});
        }
    }

    return {};
}

} // namespace aoa::sim::systems
