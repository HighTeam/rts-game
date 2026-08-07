#include "app/application.hpp"

#include "harness/regression_harness.hpp"
#include "net/lockstep_runner.hpp"
#include "net/net_smoke.hpp"
#include "sim/simulation.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv)
{
    try {
        aoa::sim::Simulation simulation{};
        const aoa::app::LaunchOptions options = aoa::app::parse_launch_options(argc, argv);

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
                return aoa::net::run_lockstep_host(lockstep_options);
            }

            return aoa::app::run_graphical_lockstep(simulation, options);
        }

        if (options.lockstep_join) {
            if (options.headless) {
                aoa::net::LockstepRunOptions lockstep_options{};
                lockstep_options.tick_count = options.lockstep_ticks;
                lockstep_options.join_address = options.lockstep_join_address;
                return aoa::net::run_lockstep_join(lockstep_options);
            }

            return aoa::app::run_graphical_lockstep(simulation, options);
        }

        if (options.headless) {
            return aoa::app::run_headless(simulation, options);
        }

        return aoa::app::run_graphical(simulation);
    }
    catch (const std::exception& exception) {
        std::cerr << "Fatal error: " << exception.what() << '\n';
        return 1;
    }
}
