#pragma once

#include "core/constants.hpp"

#include <entt/entt.hpp>

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

namespace aoa::sim::components {

enum class MatchAnnouncementKind : std::uint8_t {
    AgeAdvanced = 0,
    AttackedBy = 1,
    RelationshipChanged = 2,
};

struct MatchAnnouncement {
    MatchAnnouncementKind kind{MatchAnnouncementKind::AgeAdvanced};
    std::uint8_t player_slot{0U};
    std::uint8_t other_slot{0U};
    std::uint8_t age{0U};
};

struct MatchAnnouncementQueue {
    std::mutex mutex{};
    std::vector<MatchAnnouncement> events{};
    std::array<int, constants::MAX_PLAYER_SLOTS> attacked_cooldown_ticks{};
};

inline MatchAnnouncementQueue& match_announcement_queue(entt::registry& registry)
{
    if (!registry.ctx().contains<MatchAnnouncementQueue>()) {
        registry.ctx().emplace<MatchAnnouncementQueue>();
    }

    return registry.ctx().get<MatchAnnouncementQueue>();
}

inline void tick_match_announcement_cooldowns(entt::registry& registry)
{
    if (!registry.ctx().contains<MatchAnnouncementQueue>()) {
        return;
    }

    auto& queue = registry.ctx().get<MatchAnnouncementQueue>();
    const std::lock_guard<std::mutex> lock(queue.mutex);
    for (int& remaining : queue.attacked_cooldown_ticks) {
        if (remaining > 0) {
            --remaining;
        }
    }
}

inline void push_age_advanced_announcement(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const std::uint8_t age)
{
    auto& queue = match_announcement_queue(registry);
    const std::lock_guard<std::mutex> lock(queue.mutex);
    queue.events.push_back(MatchAnnouncement{
        MatchAnnouncementKind::AgeAdvanced,
        player_slot,
        0U,
        age,
    });
}

inline void note_player_attacked(
    entt::registry& registry,
    const std::uint8_t victim_slot,
    const std::uint8_t attacker_slot)
{
    if (victim_slot == attacker_slot
        || victim_slot >= static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)
        || attacker_slot >= static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)) {
        return;
    }

    auto& queue = match_announcement_queue(registry);
    const std::lock_guard<std::mutex> lock(queue.mutex);
    if (queue.attacked_cooldown_ticks[victim_slot] > 0) {
        return;
    }

    queue.attacked_cooldown_ticks[victim_slot] = constants::ATTACKED_ANNOUNCE_COOLDOWN_TICKS;
    queue.events.push_back(MatchAnnouncement{
        MatchAnnouncementKind::AttackedBy,
        victim_slot,
        attacker_slot,
        0U,
    });
}

inline void push_relationship_changed_announcement(
    entt::registry& registry,
    const std::uint8_t target_slot,
    const std::uint8_t actor_slot,
    const bool allied)
{
    if (target_slot == actor_slot
        || target_slot >= static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)
        || actor_slot >= static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)) {
        return;
    }

    auto& queue = match_announcement_queue(registry);
    const std::lock_guard<std::mutex> lock(queue.mutex);
    queue.events.push_back(MatchAnnouncement{
        MatchAnnouncementKind::RelationshipChanged,
        target_slot,
        actor_slot,
        allied ? 1U : 0U,
    });
}

inline std::vector<MatchAnnouncement> drain_match_announcements(entt::registry& registry)
{
    if (!registry.ctx().contains<MatchAnnouncementQueue>()) {
        return {};
    }

    auto& queue = registry.ctx().get<MatchAnnouncementQueue>();
    const std::lock_guard<std::mutex> lock(queue.mutex);
    std::vector<MatchAnnouncement> drained = std::move(queue.events);
    queue.events.clear();
    return drained;
}

} // namespace aoa::sim::components
