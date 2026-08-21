#pragma once

#include <cstddef>
#include <cstdint>

namespace aoa::net::constants {

inline constexpr std::uint16_t DEFAULT_PORT = 27000U;
inline constexpr std::size_t MAX_PEERS = 8U;
inline constexpr int LOCKSTEP_MAX_PLAYER_SLOTS = 8;
inline constexpr std::uint8_t LOCKSTEP_DEFAULT_MAX_CLIENTS = 1U;
inline constexpr std::uint8_t LOCKSTEP_4_PLAYER_COUNT = 4U;
inline constexpr std::uint8_t LOCKSTEP_4_MAX_CLIENTS = 3U;
inline constexpr std::size_t CHANNEL_COUNT = 2U;
inline constexpr std::uint8_t CHANNEL_RELIABLE = 0U;
inline constexpr std::uint8_t CHANNEL_UNRELIABLE = 1U;
inline constexpr int NET_SMOKE_CONNECT_ATTEMPTS = 5000;
inline constexpr int NET_SMOKE_RECEIVE_ATTEMPTS = 5000;
inline constexpr std::uint32_t NET_POLL_TIMEOUT_MS = 0U;
inline constexpr std::uint32_t ENET_NETWORK_THREAD_SERVICE_TIMEOUT_MS = 1U;
inline constexpr std::uint32_t ENET_NETWORK_THREAD_IDLE_WAIT_MS = 1U;
inline constexpr std::uint32_t ENET_PEER_TIMEOUT_MINIMUM_MS = 1000U;
inline constexpr std::uint32_t ENET_PEER_TIMEOUT_MAXIMUM_MS = 15000U;
inline constexpr std::uint32_t ENET_PEER_TIMEOUT_LIMIT = 32U;
inline constexpr std::uint32_t LOCKSTEP_PEER_SILENCE_MS = 3000U;
inline constexpr std::uint32_t LOCKSTEP_BATCH_RESEND_MS = 100U;
inline constexpr std::uint32_t LOCKSTEP_RECONNECT_GRACE_MS = 30000U;
inline constexpr std::uint32_t LOCKSTEP_TICK_LOOP_SLEEP_MS = 0U;
inline constexpr std::uint32_t LOCKSTEP_RTT_SAMPLE_MAX_MS = 100U;
inline constexpr std::uint32_t LOCKSTEP_LATENCY_PROBE_INTERVAL_MS = 200U;
inline constexpr int LOCKSTEP_MAX_ACCUMULATOR_TICKS = 2;
inline constexpr int LOCKSTEP_MAX_TICKS_PER_LOOP = 2;
inline constexpr std::uint32_t LOCKSTEP_TICK_IDLE_SLEEP_MS = 1U;
inline constexpr int LOCKSTEP_PING_DISPLAY_WINDOW = 8;
inline constexpr int LOCKSTEP_HASH_VERIFY_WARMUP_TICKS = 30;
inline constexpr std::uint32_t LOCKSTEP_JOIN_HANDSHAKE_GRACE_MS = 10000U;
inline constexpr std::uint32_t LOCKSTEP_RECONNECT_INTERVAL_MS = 500U;
inline constexpr std::uint32_t LOCKSTEP_RECONNECT_REQUEST_RETRY_MS = 2000U;
inline constexpr std::uint32_t LOCKSTEP_RECONNECT_SNAPSHOT_DEBOUNCE_MS = 500U;
inline constexpr std::uint32_t LOCKSTEP_RESYNC_READY_RETRY_MS = 500U;
inline constexpr std::uint32_t LOCKSTEP_RESYNC_READY_RETRY_WINDOW_MS = 10000U;
inline constexpr int LOCKSTEP_RECONNECT_MAX_ATTEMPTS = 20;
inline constexpr int LOCKSTEP_COMMAND_DELAY_TICKS = 2;
inline constexpr float LOCKSTEP_RTT_SMOOTHING_ALPHA = 0.35F;
inline constexpr float LOCKSTEP_MAX_RENDER_EXTRAPOLATION_ALPHA = 0.2F;
inline constexpr int LOCKSTEP_PLAYER_COUNT = 2;
inline constexpr std::uint8_t LOCKSTEP_HOST_PLAYER_SLOT = 0U;
inline constexpr std::uint8_t LOCKSTEP_CLIENT_PLAYER_SLOT = 1U;
inline constexpr std::uint8_t LOCKSTEP_INVALID_PLAYER_SLOT = 255U;
inline constexpr int LOCKSTEP_CONNECT_ATTEMPTS = 10000;
inline constexpr int LOCKSTEP_ADVANCE_ATTEMPTS = 100000;
inline constexpr std::uint64_t LOCKSTEP_DEFAULT_TICK_COUNT = 100U;
inline constexpr std::uint16_t LOCKSTEP_SMOKE_PORT = 27001U;
inline constexpr std::uint16_t LOCKSTEP_DISCONNECT_SMOKE_PORT = 27200U;
inline constexpr std::uint16_t LOCKSTEP_RECONNECT_SMOKE_PORT = 27201U;
inline constexpr int LOCKSTEP_RECONNECT_SMOKE_CYCLES = 3;
inline constexpr std::uint64_t LOCKSTEP_RECONNECT_SMOKE_WARMUP_TICKS = 25U;
inline constexpr std::uint64_t LOCKSTEP_RECONNECT_SMOKE_AI_TICKS = 15U;
inline constexpr std::uint64_t LOCKSTEP_RECONNECT_SMOKE_LIVE_TICKS = 20U;
inline constexpr std::uint16_t LOCKSTEP_4_SMOKE_PORT = 27202U;
inline constexpr std::uint16_t LOCKSTEP_4_DISCONNECT_SMOKE_PORT = 27203U;
inline constexpr std::uint64_t LOCKSTEP_4_SMOKE_TICKS = 40U;
inline constexpr std::uint64_t LOCKSTEP_4_DISCONNECT_WARMUP_TICKS = 15U;
inline constexpr std::uint64_t LOCKSTEP_4_DISCONNECT_LIVE_TICKS = 15U;
inline constexpr std::uint16_t LOCKSTEP_PEER_SILENCE_SMOKE_PORT = 27204U;
inline constexpr std::uint64_t LOCKSTEP_PEER_SILENCE_SMOKE_WARMUP_TICKS = 35U;
inline constexpr std::uint32_t LOCKSTEP_PEER_SILENCE_SMOKE_WAIT_MS = LOCKSTEP_PEER_SILENCE_MS + 500U;
inline constexpr std::uint16_t LOCKSTEP_4_RECONNECT_SMOKE_PORT = 27205U;
inline constexpr std::uint64_t LOCKSTEP_4_RECONNECT_WARMUP_TICKS = 15U;
inline constexpr std::uint64_t LOCKSTEP_4_RECONNECT_DURING_AI_TICKS = 10U;
inline constexpr std::uint64_t LOCKSTEP_4_RECONNECT_LIVE_TICKS = 15U;

} // namespace aoa::net::constants
