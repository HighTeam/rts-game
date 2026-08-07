#pragma once

#include "net/net_constants.hpp"
#include "sim/simulation.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace aoa::app {

struct LaunchOptions {
    bool headless{false};
    bool run_harness{false};
    bool run_net_smoke{false};
    bool run_lockstep_smoke{false};
    bool run_lockstep_disconnect_smoke{false};
    bool run_lockstep_reconnect_smoke{false};
    bool run_lockstep_4_smoke{false};
    bool run_snapshot_smoke{false};
    bool run_snapshot_double_spawn_smoke{false};
    bool run_snapshot_reconnect_smoke{false};
    bool run_snapshot_heavy_smoke{false};
    bool lockstep_host{false};
    bool lockstep_join{false};
    bool print_state_hash{false};
    std::uint64_t headless_ticks{0U};
    std::uint64_t lockstep_ticks{0U};
    std::uint16_t lockstep_port{0U};
    bool lockstep_debug{false};
    std::uint8_t lockstep_player_count{
        static_cast<std::uint8_t>(aoa::net::constants::LOCKSTEP_PLAYER_COUNT)};
    std::optional<std::uint8_t> lockstep_player_number{};
    std::optional<std::uint64_t> expect_state_hash{};
    std::optional<std::string> lockstep_join_address{};
};

LaunchOptions parse_launch_options(int argc, char** argv);

[[nodiscard]] std::uint8_t resolve_lockstep_join_player_slot(const LaunchOptions& options);

int run_headless(sim::Simulation& simulation, const LaunchOptions& options);
int run_graphical(sim::Simulation& simulation);
int run_graphical_lockstep(sim::Simulation& simulation, const LaunchOptions& options);

} // namespace aoa::app
