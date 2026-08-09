#include "app/application.hpp"

#include "core/asset_store.hpp"
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
