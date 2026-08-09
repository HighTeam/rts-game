#pragma once

#include "data/content_types.hpp"

#include <entt/entt.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace aoa::sim::scenario {

void load_test_scenario(
    entt::registry& registry,
    const data::ContentDatabase& content,
    std::uint8_t player_count = 2U);

[[nodiscard]] entt::entity find_scenario_entity(entt::registry& registry, std::string_view role);

} // namespace aoa::sim::scenario
