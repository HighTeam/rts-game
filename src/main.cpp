#include "app/application.hpp"

#include "harness/regression_harness.hpp"
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
