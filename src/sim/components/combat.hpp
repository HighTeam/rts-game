#pragma once

#include <entt/entt.hpp>

namespace aoa::sim::components {

struct AttackOrder {
    entt::entity target{entt::null};
};

struct AttackCooldown {
    int ticks_remaining{0};
};

} // namespace aoa::sim::components
