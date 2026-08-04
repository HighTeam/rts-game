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
inline constexpr int LOCKSTEP_COMMAND_DELAY_TICKS = 2;
inline constexpr int LOCKSTEP_PLAYER_COUNT = 2;
inline constexpr std::uint8_t LOCKSTEP_HOST_PLAYER_SLOT = 0U;
inline constexpr std::uint8_t LOCKSTEP_CLIENT_PLAYER_SLOT = 1U;
inline constexpr int LOCKSTEP_CONNECT_ATTEMPTS = 10000;
inline constexpr int LOCKSTEP_ADVANCE_ATTEMPTS = 100000;
inline constexpr std::uint64_t LOCKSTEP_DEFAULT_TICK_COUNT = 100U;
inline constexpr std::uint16_t LOCKSTEP_SMOKE_PORT = 27001U;

} // namespace aoa::net::constants
