#pragma once

#include <cstdint>
#include <vector>

namespace aoa::sim::components {

struct AiControlTransition {
    std::uint64_t tick{0U};
    std::uint8_t player_slot{0U};
    bool enabled{false};
};

struct MatchSession {
    std::uint8_t ai_controlled_slots{0U};
    std::uint64_t ai_controlled_since_tick{0U};
    std::vector<AiControlTransition> ai_control_transitions{};
};

[[nodiscard]] inline bool is_slot_ai_controlled(const MatchSession& session, const std::uint8_t player_slot)
{
    const std::uint8_t mask = static_cast<std::uint8_t>(1U << player_slot);
    return (session.ai_controlled_slots & mask) != 0U;
}

inline void set_slot_ai_controlled(MatchSession& session, const std::uint8_t player_slot, const bool enabled)
{
    const std::uint8_t mask = static_cast<std::uint8_t>(1U << player_slot);
    if (enabled) {
        session.ai_controlled_slots = static_cast<std::uint8_t>(session.ai_controlled_slots | mask);
        return;
    }

    session.ai_controlled_slots = static_cast<std::uint8_t>(session.ai_controlled_slots & ~mask);
}

} // namespace aoa::sim::components
