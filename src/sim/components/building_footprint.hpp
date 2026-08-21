#pragma once

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "math/fixed.hpp"
#include "sim/components/grid_position.hpp"

#include <algorithm>

namespace aoa::sim::components {

struct BuildingFootprint {
    int width{1};
    int height{1};
};

[[nodiscard]] inline BuildingFootprint effective_building_footprint(
    const BuildingFootprint& footprint,
    const bool is_town_center)
{
    BuildingFootprint result = footprint;
    if (is_town_center) {
        result.width = std::max(result.width, aoa::constants::TOWN_CENTER_FOOTPRINT_TILES);
        result.height = std::max(result.height, aoa::constants::TOWN_CENTER_FOOTPRINT_TILES);
    }

    return result;
}

[[nodiscard]] inline bool building_contains_cell(
    const GridPosition& anchor,
    const BuildingFootprint& footprint,
    const core::GridPos cell)
{
    return cell.x >= anchor.cell.x && cell.x < anchor.cell.x + footprint.width
        && cell.y >= anchor.cell.y && cell.y < anchor.cell.y + footprint.height;
}

[[nodiscard]] inline core::GridPos building_center_cell(
    const GridPosition& anchor,
    const BuildingFootprint& footprint)
{
    return {
        anchor.cell.x + footprint.width / 2,
        anchor.cell.y + footprint.height / 2,
    };
}

[[nodiscard]] inline int chebyshev_distance_to_footprint(
    const core::GridPos cell,
    const GridPosition& anchor,
    const BuildingFootprint& footprint)
{
    const int max_x = anchor.cell.x + footprint.width - 1;
    const int max_y = anchor.cell.y + footprint.height - 1;
    const int dx = cell.x < anchor.cell.x ? anchor.cell.x - cell.x
        : cell.x > max_x ? cell.x - max_x
                         : 0;
    const int dy = cell.y < anchor.cell.y ? anchor.cell.y - cell.y
        : cell.y > max_y ? cell.y - max_y
                         : 0;
    return std::max(dx, dy);
}

[[nodiscard]] inline math::Fixed euclidean_distance_sq_to_footprint_center(
    const math::Fixed world_x,
    const math::Fixed world_y,
    const GridPosition& anchor,
    const BuildingFootprint& footprint)
{
    const math::Fixed center_x = math::Fixed::from_int(anchor.cell.x)
        + math::Fixed::from_int(footprint.width) / math::Fixed::from_int(2);
    const math::Fixed center_y = math::Fixed::from_int(anchor.cell.y)
        + math::Fixed::from_int(footprint.height) / math::Fixed::from_int(2);
    const math::Fixed dx = world_x - center_x;
    const math::Fixed dy = world_y - center_y;
    return dx * dx + dy * dy;
}

[[nodiscard]] inline bool in_circle_attack_range(
    const math::Fixed world_x,
    const math::Fixed world_y,
    const GridPosition& anchor,
    const BuildingFootprint& footprint,
    const int attack_range_tiles,
    const int blind_range_tiles)
{
    if (attack_range_tiles <= 0) {
        return false;
    }

    const math::Fixed dist_sq =
        euclidean_distance_sq_to_footprint_center(world_x, world_y, anchor, footprint);
    if (blind_range_tiles > 0) {
        const math::Fixed blind = math::Fixed::from_int(blind_range_tiles);
        if (dist_sq < blind * blind) {
            return false;
        }
    }

    const math::Fixed range = math::Fixed::from_int(attack_range_tiles);
    return dist_sq <= range * range;
}

} // namespace aoa::sim::components
