#pragma once

#include <cstdint>
#include <span>

namespace aoa::sim::scenario {

struct PlayerBaseLayout {
    int tc_x;
    int tc_y;
    int worker_x;
    int worker_y;
    int militia_x;
    int militia_y;
    int forest_min_x;
    int forest_min_y;
    int forest_max_x;
    int forest_max_y;
};

[[nodiscard]] std::span<const PlayerBaseLayout> base_layouts_for_player_count(std::uint8_t player_count);

} // namespace aoa::sim::scenario
