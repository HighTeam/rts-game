#include "sim/simulation.hpp"

#include "sim/components/motion_state.hpp"
#include "sim/components/tags.hpp"
#include "sim/systems/sim_systems.hpp"

namespace aoa::sim {

Simulation::Simulation()
{
    world_entity_ = registry_.create();
    registry_.emplace<components::WorldTag>(world_entity_);
    registry_.emplace<components::MotionState>(world_entity_);
}

void Simulation::tick()
{
    ++tick_count_;
    systems::run_sim_systems(registry_);
}

math::Fixed Simulation::motion_sample() const
{
    if (world_entity_ == entt::null || !registry_.all_of<components::MotionState>(world_entity_)) {
        return math::Fixed::from_int(0);
    }

    return registry_.get<components::MotionState>(world_entity_).value;
}

} // namespace aoa::sim
