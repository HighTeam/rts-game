#pragma once

#include "core/constants.hpp"

#include <algorithm>
#include <cstdint>
#include <entt/entt.hpp>

namespace aoa::sim::components {

enum class BuildingProcessKind : std::uint8_t {
    None = 0,
    TrainWorker = 1,
    TrainMilitia = 2,
    TrainMage = 3,
    AdvanceAge = 4,
    ResearchCartography = 5,
    ResearchSpy = 6,
    ResearchTrades = 7,
};

struct BuildingProcess {
    BuildingProcessKind kind{BuildingProcessKind::None};
    int ticks_remaining{0};
    int ticks_total{0};
};

[[nodiscard]] inline bool building_has_active_process(
    const entt::registry& registry,
    const entt::entity building)
{
    if (!registry.valid(building) || !registry.any_of<BuildingProcess>(building)) {
        return false;
    }

    const auto& process = registry.get<BuildingProcess>(building);
    return process.kind != BuildingProcessKind::None && process.ticks_total > 0;
}

[[nodiscard]] inline int building_process_percent(const BuildingProcess& process)
{
    if (process.ticks_total <= 0) {
        return 0;
    }

    const int elapsed = process.ticks_total - std::max(0, process.ticks_remaining);
    return std::clamp(
        elapsed * constants::HUD_PROCESS_FULL_PERCENT / process.ticks_total,
        0,
        constants::HUD_PROCESS_FULL_PERCENT);
}

[[nodiscard]] inline bool building_process_is_research(const BuildingProcessKind kind)
{
    return kind == BuildingProcessKind::AdvanceAge
        || kind == BuildingProcessKind::ResearchCartography
        || kind == BuildingProcessKind::ResearchSpy
        || kind == BuildingProcessKind::ResearchTrades;
}

inline void clear_building_process(BuildingProcess& process)
{
    process.kind = BuildingProcessKind::None;
    process.ticks_remaining = 0;
    process.ticks_total = 0;
}

} // namespace aoa::sim::components
