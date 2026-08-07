#pragma once

#include "core/grid.hpp"

namespace aoa::sim::components {

enum class WorkerState : std::uint8_t {
    Idle,
    MovingToResource,
    Gathering,
    MovingToDeposit,
    Depositing,
};

struct WorkerBrain {
    WorkerState state{WorkerState::Idle};
};

struct CarriedWood {
    int amount{0};
};

struct Stockpile {
    int wood{0};
};

struct ForestResource {
    int wood_remaining{0};
};

struct GatherTarget {
    core::GridPos cell{-1, -1};
};

} // namespace aoa::sim::components
