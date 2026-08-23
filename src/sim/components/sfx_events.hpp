#pragma once

#include "core/grid.hpp"

#include <entt/entt.hpp>

#include <cstdint>
#include <mutex>
#include <vector>

namespace aoa::sim::components {

enum class SfxEventKind : std::uint8_t {
    MilitiaMeleeHit = 0,
    WorkerMeleeHit = 1,
    UnitDeath = 2,
    LookHere = 3,
};

struct SfxEvent {
    SfxEventKind kind{SfxEventKind::MilitiaMeleeHit};
    core::GridPos cell{};
    std::uint8_t player_slot{0xFFU};
};

struct SfxEventQueue {
    std::mutex mutex{};
    std::vector<SfxEvent> events{};
};

inline void push_sfx_event(
    entt::registry& registry,
    const SfxEventKind kind,
    const core::GridPos cell,
    const std::uint8_t player_slot = 0xFFU)
{
    if (!registry.ctx().contains<SfxEventQueue>()) {
        registry.ctx().emplace<SfxEventQueue>();
    }

    auto& queue = registry.ctx().get<SfxEventQueue>();
    const std::lock_guard<std::mutex> lock(queue.mutex);
    queue.events.push_back(SfxEvent{kind, cell, player_slot});
}

inline std::vector<SfxEvent> drain_sfx_events(entt::registry& registry)
{
    if (!registry.ctx().contains<SfxEventQueue>()) {
        return {};
    }

    auto& queue = registry.ctx().get<SfxEventQueue>();
    const std::lock_guard<std::mutex> lock(queue.mutex);
    std::vector<SfxEvent> drained = std::move(queue.events);
    queue.events.clear();
    return drained;
}

} // namespace aoa::sim::components
