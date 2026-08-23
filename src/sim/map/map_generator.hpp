#pragma once

#include "sim/components/map_grid.hpp"
#include "sim/map/map_pattern.hpp"
#include "core/grid.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aoa::sim::map {

struct MapGenerationConfig {
    std::uint8_t player_count{2U};
    std::uint8_t spectator_slots_mask{0U};
    std::uint64_t seed{0U};
    MapPattern pattern{};
    int forest_patch_wood{100};
    int bush_food_capacity{constants::BERRY_BUSH_FOOD_CAPACITY};
    int mine_money_capacity{constants::GOLD_MINE_MONEY_CAPACITY};
    std::uint8_t biome_preset{constants::MAP_BIOME_PRESET_MIXED};
};

struct GeneratedMap {
    components::MapGrid grid{};
    std::vector<core::GridPos> start_anchors{};
    std::vector<core::GridPos> mana_lake_anchors{};
};

[[nodiscard]] GeneratedMap generate_map(const MapGenerationConfig& config);

[[nodiscard]] std::uint64_t generate_map_seed();

[[nodiscard]] std::vector<core::GridPos> start_ring_positions(
    std::uint8_t player_count,
    int map_width,
    int map_height);

[[nodiscard]] core::GridPos snap_to_start_ring(
    core::GridPos cell,
    int map_width,
    int map_height);

[[nodiscard]] float start_ring_radius(int map_width, int map_height);

[[nodiscard]] inline MapGenerationConfig make_generation_config(
    const std::uint8_t player_count,
    const std::uint64_t seed,
    const std::uint8_t pattern_index,
    const std::string& pattern_payload,
    const std::uint8_t biome_preset = constants::MAP_BIOME_PRESET_MIXED)
{
    MapGenerationConfig config{};
    config.player_count = player_count;
    config.seed = seed;
    config.pattern = resolve_map_pattern(pattern_index, pattern_payload);
    config.biome_preset = biome_preset;
    return config;
}

} // namespace aoa::sim::map
