#include "sim/simulation.hpp"

namespace aoa::sim {

void Simulation::tick()
{
    ++tick_count_;
    motion_sample_ = motion_sample_ + math::fixed_sim_delta();
}

} // namespace aoa::sim
