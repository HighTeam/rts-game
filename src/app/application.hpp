#pragma once

#include "sim/simulation.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace aoa::app {

struct LaunchOptions {
    bool headless{false};
    bool run_harness{false};
    bool print_state_hash{false};
    std::uint64_t headless_ticks{0U};
    std::optional<std::uint64_t> expect_state_hash{};
};

LaunchOptions parse_launch_options(int argc, char** argv);

int run_headless(sim::Simulation& simulation, const LaunchOptions& options);
int run_graphical(sim::Simulation& simulation);

} // namespace aoa::app
