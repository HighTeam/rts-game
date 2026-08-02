#pragma once

#include "data/content_types.hpp"

#include <entt/entt.hpp>

namespace aoa::sim::scenario {

void load_test_scenario(entt::registry& registry, const data::CivDefinition& civ);

} // namespace aoa::sim::scenario
