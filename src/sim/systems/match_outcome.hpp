#pragma once

#include "sim/components/match_session.hpp"

#include <cstdint>
#include <entt/entt.hpp>

namespace aoa::sim::systems {

void note_unit_created(entt::registry& registry, std::uint8_t player_slot);
void note_building_created(entt::registry& registry, std::uint8_t player_slot);
void note_resources_collected(
    entt::registry& registry,
    std::uint8_t player_slot,
    int wood,
    int food,
    int money);
void note_mana_collected(entt::registry& registry, std::uint8_t player_slot, int amount);
void note_entity_killed(entt::registry& registry, entt::entity victim, entt::entity killer);
void update_match_outcome(entt::registry& registry, std::uint64_t tick_count);
[[nodiscard]] bool match_is_finished(const entt::registry& registry);
[[nodiscard]] components::MatchSession* match_session(entt::registry& registry);
[[nodiscard]] const components::MatchSession* match_session(const entt::registry& registry);

} // namespace aoa::sim::systems
