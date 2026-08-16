#pragma once

#include "core/constants.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace aoa::sim::components {

enum class FogOfWarMode : std::uint8_t {
    Enabled = 0,
    Explored = 1,
    Disabled = 2,
};

enum class VictoryCondition : std::uint8_t {
    Normal = 0,
};

struct PlayerMatchStats {
    int units_created{0};
    int units_lost{0};
    int units_killed{0};
    int buildings_created{0};
    int buildings_lost{0};
    int buildings_destroyed{0};
    int wood_collected{0};
    int food_collected{0};
    int money_collected{0};
    int mana_collected{0};
};

struct AiControlTransition {
    std::uint64_t tick{0U};
    std::uint8_t player_slot{0U};
    bool enabled{false};
};

struct MatchSession {
    std::uint8_t ai_controlled_slots{0U};
    std::uint64_t ai_controlled_since_tick{0U};
    std::vector<AiControlTransition> ai_control_transitions{};
    // Hard ceiling for civil population per player (map setting).
    int civil_population_map_cap{15};
    bool fog_of_war_enabled{true};
    FogOfWarMode fog_of_war_mode{FogOfWarMode::Enabled};
    bool cheats_enabled{false};
    VictoryCondition victory_condition{VictoryCondition::Normal};
    std::uint8_t playing_slots_mask{0U};
    std::uint8_t eliminated_slots_mask{0U};
    bool match_finished{false};
    std::uint8_t winner_slot{constants::MATCH_WINNER_NONE};
    std::uint8_t last_eliminating_slot{constants::MATCH_WINNER_NONE};
    std::uint64_t finished_tick{0U};
    std::array<PlayerMatchStats, constants::MAX_PLAYER_SLOTS> player_stats{};
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> player_color_indices{
        0U,
        1U,
        2U,
        3U,
        4U,
        5U,
        6U,
        7U};
    // FFA default: each playing slot is its own side. Shared values are a team.
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> player_side_indices{
        0U,
        1U,
        2U,
        3U,
        4U,
        5U,
        6U,
        7U};
};

[[nodiscard]] inline std::uint8_t player_color_index(
    const MatchSession& session,
    const std::uint8_t player_slot)
{
    if (player_slot >= session.player_color_indices.size()) {
        return 0U;
    }

    return session.player_color_indices[player_slot];
}

[[nodiscard]] inline std::uint8_t player_side_index(
    const MatchSession& session,
    const std::uint8_t player_slot)
{
    if (player_slot >= session.player_side_indices.size()) {
        return player_slot;
    }

    const std::uint8_t side = session.player_side_indices[player_slot];
    if (side >= static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)) {
        return player_slot;
    }

    return side;
}

[[nodiscard]] inline bool slot_is_on_winning_side(
    const MatchSession& session,
    const std::uint8_t player_slot)
{
    if (!session.match_finished || session.winner_slot == constants::MATCH_WINNER_NONE) {
        return false;
    }

    return player_side_index(session, player_slot)
        == player_side_index(session, session.winner_slot);
}

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
