#include "app/application.hpp"

#include "core/asset_store.hpp"
#include "core/constants.hpp"
#include "harness/regression_harness.hpp"
#include "net/lockstep_runner.hpp"
#include "net/net_smoke.hpp"
#include "sim/simulation.hpp"

#include <exception>
#include <iostream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <timeapi.h>
#include <cstdio>
#include <string_view>

namespace {

class MultimediaTimerPeriod {
public:
    MultimediaTimerPeriod()
    {
        timeBeginPeriod(1U);
    }

    ~MultimediaTimerPeriod()
    {
        timeEndPeriod(1U);
    }
};

} // namespace
#endif

int main(int argc, char** argv)
{
#if defined(_WIN32)
    // 1ms timer resolution so lockstep idle waits are not quantized to ~15ms.
    const MultimediaTimerPeriod multimedia_timer_period{};

    bool want_console = false;
    for (int arg_index = 1; arg_index < argc; ++arg_index) {
        const std::string_view arg = argv[arg_index];
        if (arg == aoa::constants::CONSOLE_DEBUG_ARG || arg == "--headless" || arg == "--harness"
            || arg == "--net-smoke" || arg == "--lockstep-smoke"
            || arg == "--lockstep-disconnect-smoke" || arg == "--lockstep-reconnect-smoke"
            || arg == "--lockstep-4-smoke" || arg == "--lockstep-2h2ai-smoke"
            || arg == "--sim-8ai-bench" || arg == "--lockstep-4-stress-smoke"
            || arg == "--lockstep-4-reconnect-smoke" || arg == "--snapshot-smoke"
            || arg == "--snapshot-double-spawn-smoke" || arg == "--snapshot-reconnect-smoke"
            || arg == "--snapshot-resign-smoke" || arg == "--snapshot-heavy-smoke" || arg == "--print-hash" || arg == "--help"
            || arg == "-h") {
            want_console = true;
            break;
        }
    }
    if (want_console) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            AllocConsole();
        }
        FILE* stream = nullptr;
        (void)freopen_s(&stream, "CONOUT$", "w", stdout);
        (void)freopen_s(&stream, "CONOUT$", "w", stderr);
        (void)freopen_s(&stream, "CONIN$", "r", stdin);
    }
#endif

    try {
        const aoa::app::LaunchOptions options = aoa::app::parse_launch_options(argc, argv);
        if (!aoa::core::init_asset_store(options.prefer_loose_assets)) {
            return 1;
        }

        const std::uint8_t scenario_player_count =
            (options.lockstep_host || options.lockstep_join) ? options.lockstep_player_count : 2U;
        aoa::sim::Simulation simulation{scenario_player_count};

        if (options.run_harness) {
            return aoa::harness::run_all_scenarios(aoa::harness::default_scenarios_directory());
        }

        if (options.run_net_smoke) {
            return aoa::net::run_net_smoke();
        }

        if (options.run_lockstep_smoke) {
            return aoa::net::run_lockstep_smoke();
        }

        if (options.run_lockstep_disconnect_smoke) {
            return aoa::net::run_lockstep_disconnect_smoke();
        }

        if (options.run_lockstep_reconnect_smoke) {
            return aoa::net::run_lockstep_reconnect_smoke();
        }

        if (options.run_lockstep_4_smoke) {
            return aoa::net::run_lockstep_4_smoke();
        }

        if (options.run_lockstep_4_disconnect_smoke) {
            return aoa::net::run_lockstep_4_disconnect_smoke();
        }

        if (options.run_lockstep_peer_silence_smoke) {
            return aoa::net::run_lockstep_peer_silence_smoke();
        }

        if (options.run_lockstep_2h2ai_smoke) {
            return aoa::net::run_lockstep_2h2ai_smoke();
        }

        if (options.run_sim_8ai_bench) {
            return aoa::net::run_sim_8ai_bench();
        }

        if (options.run_lockstep_4_stress_smoke) {
            return aoa::net::run_lockstep_4_stress_smoke();
        }

        if (options.run_lockstep_4_reconnect_smoke) {
            return aoa::net::run_lockstep_4_reconnect_smoke();
        }

        if (options.run_snapshot_smoke) {
            return aoa::net::run_snapshot_smoke();
        }

        if (options.run_snapshot_double_spawn_smoke) {
            return aoa::net::run_snapshot_double_spawn_smoke();
        }

        if (options.run_snapshot_reconnect_smoke) {
            return aoa::net::run_snapshot_reconnect_smoke();
        }

        if (options.run_snapshot_resign_smoke) {
            return aoa::net::run_snapshot_resign_smoke();
        }

        if (options.run_snapshot_heavy_smoke) {
            return aoa::net::run_snapshot_heavy_smoke();
        }

        if (options.lockstep_host) {
            if (options.headless) {
                aoa::net::LockstepRunOptions lockstep_options{};
                lockstep_options.tick_count = options.lockstep_ticks;
                lockstep_options.port = options.lockstep_port;
                lockstep_options.session_player_count = options.lockstep_player_count;
                lockstep_options.lockstep_debug = options.lockstep_debug;
                lockstep_options.auto_input = options.lockstep_auto_input;
                return aoa::net::run_lockstep_host(lockstep_options);
            }

            return aoa::app::run_graphical_lockstep(simulation, options);
        }

        if (options.lockstep_join) {
            if (options.headless) {
                aoa::net::LockstepRunOptions lockstep_options{};
                lockstep_options.tick_count = options.lockstep_ticks;
                lockstep_options.join_address = options.lockstep_join_address;
                lockstep_options.session_player_count = options.lockstep_player_count;
                lockstep_options.player_slot = aoa::app::resolve_lockstep_join_player_slot(options);
                lockstep_options.lockstep_debug = options.lockstep_debug;
                lockstep_options.auto_input = options.lockstep_auto_input;
                return aoa::net::run_lockstep_join(lockstep_options);
            }

            return aoa::app::run_graphical_lockstep(simulation, options);
        }

        if (options.headless) {
            return aoa::app::run_headless(simulation, options);
        }

        return aoa::app::run_app_shell();
    }
    catch (const std::exception& exception) {
        std::cerr << "Fatal error: " << exception.what() << '\n';
        return 1;
    }
}
