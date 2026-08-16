#include "sim/spawn/building_unit_spawn.hpp"

#include "core/constants.hpp"
#include "sim/systems/pathfinding.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace aoa::sim::spawn {
namespace {

[[nodiscard]] float spawn_angle_from_south_clockwise(
    const core::GridPos cell,
    const components::GridPosition& building_anchor,
    const components::BuildingFootprint& building_footprint)
{
    const float center_x = static_cast<float>(building_anchor.cell.x)
        + static_cast<float>(building_footprint.width) * 0.5F - 0.5F;
    const float center_y = static_cast<float>(building_anchor.cell.y)
        + static_cast<float>(building_footprint.height) * 0.5F - 0.5F;
    const float delta_x = static_cast<float>(cell.x) - center_x;
    const float delta_y = static_cast<float>(cell.y) - center_y;
    // atan2(-x, y): 0 at south (+y), increasing clockwise through west/north/east.
    float angle = std::atan2(-delta_x, delta_y);
    constexpr float k_two_pi = 6.28318530718F;
    if (angle < 0.0F) {
        angle += k_two_pi;
    }
    return angle;
}

} // namespace

std::optional<core::GridPos> find_building_unit_spawn_cell(
    const components::MapGrid& map,
    entt::registry& registry,
    const components::GridPosition& building_anchor,
    const components::BuildingFootprint& building_footprint,
    const std::vector<core::GridPos>& reserved_cells)
{
    for (int ring = 1; ring <= constants::BUILDING_UNIT_SPAWN_MAX_RING; ++ring) {
        std::vector<core::GridPos> ring_cells{};
        const int min_x = building_anchor.cell.x - ring;
        const int max_x = building_anchor.cell.x + building_footprint.width - 1 + ring;
        const int min_y = building_anchor.cell.y - ring;
        const int max_y = building_anchor.cell.y + building_footprint.height - 1 + ring;
        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                const core::GridPos candidate{x, y};
                if (components::building_contains_cell(
                        building_anchor, building_footprint, candidate)) {
                    continue;
                }

                if (components::chebyshev_distance_to_footprint(
                        candidate, building_anchor, building_footprint)
                    != ring) {
                    continue;
                }

                ring_cells.push_back(candidate);
            }
        }

        std::sort(
            ring_cells.begin(),
            ring_cells.end(),
            [&](const core::GridPos left, const core::GridPos right) {
                const float left_angle = spawn_angle_from_south_clockwise(
                    left, building_anchor, building_footprint);
                const float right_angle = spawn_angle_from_south_clockwise(
                    right, building_anchor, building_footprint);
                if (left_angle != right_angle) {
                    return left_angle < right_angle;
                }
                if (left.y != right.y) {
                    return left.y < right.y;
                }
                return left.x < right.x;
            });

        for (const core::GridPos candidate : ring_cells) {
            if (!systems::is_tile_walkable(map, candidate, false)) {
                continue;
            }

            bool reserved = false;
            for (const core::GridPos taken : reserved_cells) {
                if (taken == candidate) {
                    reserved = true;
                    break;
                }
            }
            if (reserved) {
                continue;
            }

            if (!systems::is_movement_blocked(registry, candidate, entt::null)) {
                return candidate;
            }
        }
    }

    return std::nullopt;
}

} // namespace aoa::sim::spawn
