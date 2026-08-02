#pragma once

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

} // namespace aoa::sim::components
