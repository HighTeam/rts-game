#pragma once

#include "net/net_constants.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace aoa::net {

struct LockstepRunOptions {
    std::uint64_t tick_count{0U};
    std::uint16_t port{0U};
    std::uint8_t session_player_count{
        static_cast<std::uint8_t>(constants::LOCKSTEP_PLAYER_COUNT)};
    std::uint8_t player_slot{constants::LOCKSTEP_CLIENT_PLAYER_SLOT};
    bool lockstep_debug{false};
    bool auto_input{false};
    std::optional<std::string> join_address{};
};

[[nodiscard]] int run_lockstep_4_stress_smoke();

[[nodiscard]] int run_lockstep_4_reconnect_smoke();

[[nodiscard]] int run_lockstep_smoke();

[[nodiscard]] int run_lockstep_disconnect_smoke();

[[nodiscard]] int run_lockstep_reconnect_smoke();

[[nodiscard]] int run_lockstep_4_smoke();

[[nodiscard]] int run_lockstep_4_disconnect_smoke();

[[nodiscard]] int run_lockstep_peer_silence_smoke();

[[nodiscard]] int run_lockstep_2h2ai_smoke();

[[nodiscard]] int run_sim_8ai_bench();

[[nodiscard]] int run_snapshot_smoke();

[[nodiscard]] int run_snapshot_double_spawn_smoke();

int run_snapshot_heavy_smoke();

[[nodiscard]] int run_snapshot_reconnect_smoke();

[[nodiscard]] int run_lockstep_host(const LockstepRunOptions& options);

[[nodiscard]] int run_lockstep_join(const LockstepRunOptions& options);

[[nodiscard]] std::optional<std::pair<std::string, std::uint16_t>> parse_lockstep_join_address(
    const std::string& address_text);

} // namespace aoa::net
