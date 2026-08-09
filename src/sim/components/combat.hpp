#pragma once

#include "core/grid.hpp"

#include <entt/entt.hpp>

namespace aoa::sim::components {

struct AttackOrder {
    entt::entity target{entt::null};
    core::GridPos last_known_cell{-1, -1};
};

struct AttackCooldown {
    int ticks_remaining{0};
};

struct BuildOrder {
    entt::entity building{entt::null};
    int hit_cooldown_ticks{0};
};

} // namespace aoa::sim::components
