#include "app/application.hpp"

#include "sim/simulation.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv)
{
    try {
        aoa::sim::Simulation simulation{};
        const aoa::app::LaunchOptions options = aoa::app::parse_launch_options(argc, argv);

        if (options.headless) {
            return aoa::app::run_headless(simulation, options.headless_ticks);
        }

        return aoa::app::run_graphical(simulation);
    }
    catch (const std::exception& exception) {
        std::cerr << "Fatal error: " << exception.what() << '\n';
        return 1;
    }
}
