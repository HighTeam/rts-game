#include "sim/simulation.hpp"

#include "core/constants.hpp"
#include "data/content_loader.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/tags.hpp"
#include "sim/scenario/test_scenario.hpp"
#include "sim/systems/gameplay_systems.hpp"
#include "sim/systems/sim_systems.hpp"

#include <string>

namespace aoa::sim {

Simulation::Simulation()
{
    const data::CivDefinition civ = data::load_civ_definition(
        data::default_data_directory() / "civs" / (std::string(constants::EARTH_CIV_ID) + ".json"));

    scenario::load_test_scenario(registry_, civ);
}

void Simulation::tick()
{
    ++tick_count_;
    systems::run_sim_systems(registry_);
}

std::uint64_t Simulation::state_hash() const
{
    const auto view = registry_.view<components::WorldTag, components::SimState>();
    for (const entt::entity world : view) {
        return registry_.get<components::SimState>(world).state_hash;
    }

    return 0U;
}

} // namespace aoa::sim
