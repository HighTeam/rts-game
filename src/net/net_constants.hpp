#pragma once

#include <cstdint>

namespace aoa::net::constants {

inline constexpr std::uint16_t DEFAULT_PORT = 27000U;
inline constexpr std::size_t MAX_PEERS = 2U;
inline constexpr std::size_t CHANNEL_COUNT = 2U;
inline constexpr std::uint8_t CHANNEL_RELIABLE = 0U;
inline constexpr int NET_SMOKE_CONNECT_ATTEMPTS = 5000;
inline constexpr int NET_SMOKE_RECEIVE_ATTEMPTS = 5000;
inline constexpr std::uint32_t NET_POLL_TIMEOUT_MS = 1U;

} // namespace aoa::net::constants
