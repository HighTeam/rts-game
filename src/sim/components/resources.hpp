#pragma once

#include "core/grid.hpp"
#include "sim/components/map_grid.hpp"

#include <cstdint>

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

struct CarriedFood {
    int amount{0};
};

struct CarriedMoney {
    int amount{0};
};

struct Stockpile {
    int wood{0};
    int food{0};
    int money{0};
    int mana{0};
};

struct ManaGenerationCooldown {
    int ticks_remaining{0};
};

struct ForestResource {
    int wood_remaining{0};
};

struct GatherTarget {
    core::GridPos cell{-1, -1};
    TileType resource_type{TileType::Grass};
};

struct GatherCooldown {
    int ticks_remaining{0};
};

} // namespace aoa::sim::components
