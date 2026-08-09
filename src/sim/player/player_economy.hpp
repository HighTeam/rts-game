#pragma once

#include "sim/components/resources.hpp"

#include <cstdint>
#include <entt/entt.hpp>

namespace aoa::sim::player {

[[nodiscard]] int count_player_units(const entt::registry& registry, std::uint8_t player_slot);

[[nodiscard]] int count_completed_town_centers(
    const entt::registry& registry,
    std::uint8_t player_slot);

[[nodiscard]] int count_completed_houses(const entt::registry& registry, std::uint8_t player_slot);

[[nodiscard]] int player_civil_cap_max(const entt::registry& registry, std::uint8_t player_slot);

[[nodiscard]] bool player_can_spawn_units(const entt::registry& registry, std::uint8_t player_slot);

[[nodiscard]] components::Stockpile sum_player_stockpile(
    const entt::registry& registry,
    std::uint8_t player_slot);

[[nodiscard]] int player_mana_total(const entt::registry& registry, std::uint8_t player_slot);

[[nodiscard]] bool try_deduct_player_wood(
    entt::registry& registry,
    std::uint8_t player_slot,
    int amount);

[[nodiscard]] bool can_afford_player_wood(
    const entt::registry& registry,
    std::uint8_t player_slot,
    int amount);

[[nodiscard]] bool try_deduct_player_food(
    entt::registry& registry,
    std::uint8_t player_slot,
    int amount);

[[nodiscard]] bool can_afford_player_food(
    const entt::registry& registry,
    std::uint8_t player_slot,
    int amount);

void add_player_wood(entt::registry& registry, std::uint8_t player_slot, int amount);

void add_player_mana(entt::registry& registry, std::uint8_t player_slot, int amount);

} // namespace aoa::sim::player
