#pragma once

#include <cstdlib>

namespace aoa::core {

struct GridPos {
    int x{0};
    int y{0};

    bool operator==(const GridPos& other) const
    {
        return x == other.x && y == other.y;
    }
};

inline int chebyshev_distance(const GridPos from, const GridPos to)
{
    const int dx = std::abs(from.x - to.x);
    const int dy = std::abs(from.y - to.y);
    return dx > dy ? dx : dy;
}

inline bool is_inside_grid(const GridPos pos, const int width, const int height)
{
    return pos.x >= 0 && pos.y >= 0 && pos.x < width && pos.y < height;
}

inline int grid_index(const GridPos pos, const int width)
{
    return pos.y * width + pos.x;
}

} // namespace aoa::core
