#pragma once

#include <cstdint>
#include <string_view>

namespace aoa::constants {

inline constexpr std::string_view WINDOW_TITLE = "Age of Affinities";
inline constexpr std::uint32_t DEFAULT_WINDOW_WIDTH = 1280U;
inline constexpr std::uint32_t DEFAULT_WINDOW_HEIGHT = 720U;

inline constexpr int SIM_TICKS_PER_SECOND = 20;
inline constexpr int OPENGL_MAJOR_VERSION = 3;
inline constexpr int OPENGL_MINOR_VERSION = 3;

inline constexpr int FIXED_FRACTION_BITS = 16;
inline constexpr std::int32_t FIXED_ONE_RAW = 1 << FIXED_FRACTION_BITS;

inline constexpr std::uint64_t HEADLESS_DEFAULT_TICK_COUNT = 100U;

inline constexpr int MAP_TEST_WIDTH = 64;
inline constexpr int MAP_TEST_HEIGHT = 64;

inline constexpr int MAP_MIN_SIZE = 32;
inline constexpr int MAP_MAX_SIZE = 512;

inline constexpr int RENDER_TILE_PIXELS = 16;

inline constexpr std::string_view EARTH_CIV_ID = "earth";
inline constexpr std::string_view WORKER_UNIT_ID = "worker";
inline constexpr std::string_view MILITIA_UNIT_ID = "militia";
inline constexpr std::string_view TOWN_CENTER_BUILDING_ID = "town_center";

} // namespace aoa::constants
