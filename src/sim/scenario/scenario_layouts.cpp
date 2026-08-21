#include "sim/scenario/scenario_layouts.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace aoa::sim::scenario {

namespace {

constexpr PlayerBaseLayout k_two_player_layouts[] = {
    {
        8,
        8,
        11,
        9,
        7,
        10,
        20,
        12,
        28,
        18,
    },
    {
        52,
        52,
        55,
        53,
        51,
        55,
        36,
        46,
        44,
        52,
    },
};

constexpr PlayerBaseLayout k_four_player_layouts[] = {
    {
        8,
        8,
        11,
        9,
        7,
        10,
        20,
        12,
        28,
        18,
    },
    {
        55,
        8,
        58,
        9,
        52,
        10,
        47,
        12,
        55,
        18,
    },
    {
        8,
        55,
        11,
        54,
        7,
        52,
        12,
        46,
        20,
        52,
    },
    {
        55,
        55,
        58,
        54,
        52,
        52,
        47,
        46,
        55,
        52,
    },
};

constexpr PlayerBaseLayout k_eight_player_layouts[] = {
    {8, 8, 11, 9, 7, 10, 20, 12, 28, 18},
    {31, 6, 34, 7, 28, 8, 26, 10, 34, 16},
    {55, 8, 58, 9, 52, 10, 47, 12, 55, 18},
    {57, 31, 54, 32, 58, 33, 51, 26, 57, 34},
    {55, 55, 58, 54, 52, 52, 47, 46, 55, 52},
    {31, 57, 34, 56, 28, 58, 26, 48, 34, 55},
    {8, 55, 11, 54, 7, 52, 12, 46, 20, 52},
    {6, 31, 9, 32, 3, 33, 10, 26, 18, 34},
};

} // namespace

std::span<const PlayerBaseLayout> base_layouts_for_player_count(const std::uint8_t player_count)
{
    if (player_count == 0U || player_count > 8U) {
        throw std::invalid_argument("Unsupported scenario player count: " + std::to_string(player_count));
    }

    if (player_count <= 2U) {
        return std::span<const PlayerBaseLayout>(k_two_player_layouts, player_count);
    }

    if (player_count <= 4U) {
        return std::span<const PlayerBaseLayout>(k_four_player_layouts, player_count);
    }

    return std::span<const PlayerBaseLayout>(k_eight_player_layouts, player_count);
}

} // namespace aoa::sim::scenario
