#pragma once

#include "net/lockstep_session.hpp"
#include "sim/simulation.hpp"

namespace aoa::net {

void maybe_inject_lockstep_auto_input(LockstepSession& session, sim::Simulation& simulation);

} // namespace aoa::net
