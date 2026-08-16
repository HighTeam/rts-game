#pragma once

#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/match_session.hpp"
#include "sim/components/tags.hpp"

#include <cstdint>
#include <entt/entt.hpp>

namespace aoa::sim::components {

struct PlayerSlot {
    std::uint8_t value{0U};
};

[[nodiscard]] inline std::uint8_t entity_player_slot(
    const entt::registry& registry,
    const entt::entity entity)
{
    if (registry.any_of<PlayerSlot>(entity)) {
        return registry.get<PlayerSlot>(entity).value;
    }

    return 0U;
}

[[nodiscard]] inline bool is_opponent_entity(
    const entt::registry& registry,
    const entt::entity entity,
    const std::uint8_t local_player_slot)
{
    if (registry.any_of<EnemyTag>(entity)) {
        return true;
    }

    if (!registry.any_of<PlayerOwnedTag>(entity)) {
        return false;
    }

    const std::uint8_t owner_slot = entity_player_slot(registry, entity);
    const auto session_view = registry.view<WorldTag, MatchSession>();
    if (session_view.begin() != session_view.end()) {
        return !slots_are_allied(
            session_view.get<MatchSession>(*session_view.begin()),
            owner_slot,
            local_player_slot);
    }

    return owner_slot != local_player_slot;
}

[[nodiscard]] inline bool is_valid_attack_target(
    const entt::registry& registry,
    const entt::entity attacker,
    const entt::entity target)
{
    if (!registry.valid(target) || !registry.all_of<Health, GridPosition>(target)) {
        return false;
    }

    if (registry.get<Health>(target).current.raw() <= 0) {
        return false;
    }

    if (registry.any_of<EnemyTag>(target)) {
        return true;
    }

    if (!registry.any_of<PlayerOwnedTag>(target)) {
        return false;
    }

    if (!registry.any_of<PlayerOwnedTag>(attacker)) {
        return false;
    }

    const std::uint8_t attacker_slot = entity_player_slot(registry, attacker);
    const std::uint8_t target_slot = entity_player_slot(registry, target);
    const auto session_view = registry.view<WorldTag, MatchSession>();
    if (session_view.begin() != session_view.end()) {
        return !slots_are_allied(
            session_view.get<MatchSession>(*session_view.begin()),
            attacker_slot,
            target_slot);
    }

    return attacker_slot != target_slot;
}

} // namespace aoa::sim::components
