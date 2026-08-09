#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace aoa::sim::components {

enum class SfxEventKind : std::uint8_t {
    MilitiaMeleeHit = 0,
    WorkerMeleeHit = 1,
    UnitDeath = 2,
};

struct SfxEventQueue {
    std::mutex mutex{};
    std::vector<SfxEventKind> events{};
};

inline void push_sfx_event(entt::registry& registry, const SfxEventKind kind)
{
    if (!registry.ctx().contains<SfxEventQueue>()) {
        registry.ctx().emplace<SfxEventQueue>();
    }

    auto& queue = registry.ctx().get<SfxEventQueue>();
    const std::lock_guard<std::mutex> lock(queue.mutex);
    queue.events.push_back(kind);
}

inline std::vector<SfxEventKind> drain_sfx_events(entt::registry& registry)
{
    if (!registry.ctx().contains<SfxEventQueue>()) {
        return {};
    }

    auto& queue = registry.ctx().get<SfxEventQueue>();
    const std::lock_guard<std::mutex> lock(queue.mutex);
    std::vector<SfxEventKind> drained = std::move(queue.events);
    queue.events.clear();
    return drained;
}

} // namespace aoa::sim::components
