#pragma once

#include "math/fixed.hpp"

#include <entt/entt.hpp>

#include <cstdint>

namespace aoa::sim {

class Simulation {
public:
    Simulation();

    void tick();

    [[nodiscard]] std::uint64_t tick_count() const { return tick_count_; }

    [[nodiscard]] math::Fixed motion_sample() const;

    [[nodiscard]] entt::registry& registry() { return registry_; }
    [[nodiscard]] const entt::registry& registry() const { return registry_; }

private:
    entt::registry registry_;
    entt::entity world_entity_{entt::null};
    std::uint64_t tick_count_{0U};
};

} // namespace aoa::sim
