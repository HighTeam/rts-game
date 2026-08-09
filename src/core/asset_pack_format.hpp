#pragma once

#include <cstdint>
#include <string_view>

namespace aoa::core {

inline constexpr std::string_view ASSET_PACK_FILENAME = "assets.dat";
inline constexpr std::uint32_t ASSET_PACK_MAGIC = 0x31414F41U; // 'AOA1' LE
inline constexpr std::uint16_t ASSET_PACK_VERSION = 1U;
// Game data JSON lives under this key prefix inside the pack (and in loose mode).
inline constexpr std::string_view DATA_PACK_PREFIX = "data/";

} // namespace aoa::core
