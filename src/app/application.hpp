#pragma once

#include "sim/simulation.hpp"

#include <cstdint>
#include <string>

namespace aoa::app {

struct LaunchOptions {
    bool headless{false};
    std::uint64_t headless_ticks{0U};
};

LaunchOptions parse_launch_options(int argc, char** argv);

int run_headless(sim::Simulation& simulation, std::uint64_t tick_count);
int run_graphical(sim::Simulation& simulation);

} // namespace aoa::app
