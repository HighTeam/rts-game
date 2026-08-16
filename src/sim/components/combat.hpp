#pragma once

#include "core/grid.hpp"

#include <cstdint>
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

struct GarrisonOrder {
    entt::entity building{entt::null};
};

struct Projectile {
    entt::entity target{entt::null};
    std::uint8_t owner_slot{0U};
    int pierce_damage{0};
};

} // namespace aoa::sim::components
