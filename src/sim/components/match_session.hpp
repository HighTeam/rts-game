#pragma once

#include "core/constants.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
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
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> player_ages{};
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> player_side_indices{
        0U,
        1U,
        2U,
        3U,
        4U,
        5U,
        6U,
        7U};
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> player_cartography{};
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> player_spy{};
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> player_built_mill{};
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

[[nodiscard]] inline const std::array<float, 3>& player_slot_rgb(
    const std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS>& color_indices,
    const std::uint8_t player_slot)
{
    std::uint8_t color_index = 0U;
    if (player_slot < color_indices.size()) {
        color_index = color_indices[player_slot];
    }
    if (color_index >= constants::PLAYER_SLOT_COLOR_RGB.size()) {
        color_index = 0U;
    }
    return constants::PLAYER_SLOT_COLOR_RGB[color_index];
}

[[nodiscard]] inline const std::array<float, 3>& player_slot_rgb(
    const MatchSession& session,
    const std::uint8_t player_slot)
{
    return player_slot_rgb(session.player_color_indices, player_slot);
}

[[nodiscard]] inline constants::PlayerAge player_age(
    const MatchSession& session,
    const std::uint8_t player_slot)
{
    if (player_slot >= session.player_ages.size()) {
        return constants::PlayerAge::Human;
    }

    const std::uint8_t raw = session.player_ages[player_slot];
    if (raw > static_cast<std::uint8_t>(constants::PlayerAge::Spirit)) {
        return constants::PlayerAge::Human;
    }

    return static_cast<constants::PlayerAge>(raw);
}

struct AgeAdvanceCost {
    int food{0};
    int money{0};
    int mana{0};
    bool can_advance{false};
    std::string_view next_name{};
};

[[nodiscard]] inline AgeAdvanceCost age_advance_cost(const constants::PlayerAge current)
{
    switch (current) {
    case constants::PlayerAge::Human:
        return AgeAdvanceCost{
            constants::AGE_MAGIC_FOOD_COST,
            constants::AGE_MAGIC_MONEY_COST,
            constants::AGE_MAGIC_MANA_COST,
            true,
            constants::PLAYER_AGE_NAMES[static_cast<std::size_t>(constants::PlayerAge::Magic)],
        };
    case constants::PlayerAge::Magic:
        return AgeAdvanceCost{
            constants::AGE_TECHNOLOGY_FOOD_COST,
            constants::AGE_TECHNOLOGY_MONEY_COST,
            constants::AGE_TECHNOLOGY_MANA_COST,
            true,
            constants::PLAYER_AGE_NAMES[static_cast<std::size_t>(constants::PlayerAge::Technology)],
        };
    case constants::PlayerAge::Technology:
        return AgeAdvanceCost{
            constants::AGE_SPIRIT_FOOD_COST,
            constants::AGE_SPIRIT_MONEY_COST,
            constants::AGE_SPIRIT_MANA_COST,
            true,
            constants::PLAYER_AGE_NAMES[static_cast<std::size_t>(constants::PlayerAge::Spirit)],
        };
    case constants::PlayerAge::Spirit:
        break;
    }

    return {};
}

[[nodiscard]] inline std::string_view player_age_name(const constants::PlayerAge age)
{
    const auto index = static_cast<std::size_t>(age);
    if (index >= constants::PLAYER_AGE_NAMES.size()) {
        return constants::PLAYER_AGE_NAMES[0];
    }

    return constants::PLAYER_AGE_NAMES[index];
}

[[nodiscard]] inline std::uint8_t player_side_index(
    const MatchSession& session,
    const std::uint8_t player_slot)
{
    if (player_slot >= session.player_side_indices.size()) {
        return player_slot;
    }

    return session.player_side_indices[player_slot];
}

[[nodiscard]] inline std::uint8_t next_lobby_team(const std::uint8_t team)
{
    return static_cast<std::uint8_t>((team + 1U) % constants::LOBBY_TEAM_OPTION_COUNT);
}

[[nodiscard]] inline std::string_view lobby_team_label(const std::uint8_t team)
{
    if (team == constants::LOBBY_TEAM_RANDOM) {
        return constants::LOBBY_TEAM_RANDOM_LABEL;
    }
    if (team == constants::LOBBY_TEAM_ONE) {
        return "1";
    }
    if (team == 2U) {
        return "2";
    }
    if (team == 3U) {
        return "3";
    }
    if (team == constants::LOBBY_TEAM_FOUR) {
        return "4";
    }
    return constants::LOBBY_TEAM_NONE_LABEL;
}

[[nodiscard]] inline std::uint8_t resolve_lobby_team(
    const std::uint8_t team,
    const std::uint8_t match_slot,
    const std::uint64_t map_seed)
{
    if (team >= constants::LOBBY_TEAM_ONE && team <= constants::LOBBY_TEAM_FOUR) {
        return team;
    }
    if (team != constants::LOBBY_TEAM_RANDOM) {
        return constants::LOBBY_TEAM_NONE;
    }

    const std::uint64_t mixed =
        map_seed ^ (0x9E3779B97F4A7C15ULL * (static_cast<std::uint64_t>(match_slot) + 1U));
    return static_cast<std::uint8_t>(
        constants::LOBBY_TEAM_ONE + static_cast<std::uint8_t>(mixed % 4U));
}

[[nodiscard]] inline std::uint8_t match_side_from_team(
    const std::uint8_t resolved_team,
    const std::uint8_t match_slot)
{
    if (resolved_team >= constants::LOBBY_TEAM_ONE && resolved_team <= constants::LOBBY_TEAM_FOUR) {
        return resolved_team;
    }

    return static_cast<std::uint8_t>(constants::MATCH_FFA_SIDE_BASE + match_slot);
}

inline void apply_lobby_teams_to_session(
    MatchSession& session,
    const std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS>& slot_teams,
    const std::uint8_t player_count,
    const std::uint64_t map_seed)
{
    const std::uint8_t count = std::min(player_count, static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS));
    for (std::uint8_t slot = 0U; slot < count; ++slot) {
        const std::uint8_t resolved = resolve_lobby_team(slot_teams[slot], slot, map_seed);
        session.player_side_indices[slot] = match_side_from_team(resolved, slot);
    }
    for (std::uint8_t slot = count; slot < static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS);
         ++slot) {
        session.player_side_indices[slot] =
            static_cast<std::uint8_t>(constants::MATCH_FFA_SIDE_BASE + slot);
    }
}

[[nodiscard]] inline bool slots_are_allied(
    const MatchSession& session,
    const std::uint8_t left_slot,
    const std::uint8_t right_slot)
{
    return player_side_index(session, left_slot) == player_side_index(session, right_slot);
}

[[nodiscard]] inline bool slot_has_cartography(
    const MatchSession& session,
    const std::uint8_t player_slot)
{
    if (player_slot >= session.player_cartography.size()) {
        return false;
    }

    return session.player_cartography[player_slot] != 0U;
}

[[nodiscard]] inline bool slot_has_spy(
    const MatchSession& session,
    const std::uint8_t player_slot)
{
    if (player_slot >= session.player_spy.size()) {
        return false;
    }

    return session.player_spy[player_slot] != 0U;
}

[[nodiscard]] inline bool slot_has_built_mill(
    const MatchSession& session,
    const std::uint8_t player_slot)
{
    if (player_slot >= session.player_built_mill.size()) {
        return false;
    }

    return session.player_built_mill[player_slot] != 0U;
}

[[nodiscard]] inline bool slot_age_at_least(
    const MatchSession& session,
    const std::uint8_t player_slot,
    const constants::PlayerAge required)
{
    return static_cast<std::uint8_t>(player_age(session, player_slot))
        >= static_cast<std::uint8_t>(required);
}

[[nodiscard]] inline std::uint8_t player_slot_bit(const std::uint8_t player_slot)
{
    if (player_slot >= static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)) {
        return 0U;
    }

    return static_cast<std::uint8_t>(1U << player_slot);
}

[[nodiscard]] inline bool player_slot_bit_is_set(
    const std::uint8_t mask,
    const std::uint8_t player_slot)
{
    return (mask & player_slot_bit(player_slot)) != 0U;
}

[[nodiscard]] inline std::uint8_t cartography_vision_slots_mask(
    const MatchSession& session,
    const std::uint8_t viewer_slot)
{
    if (slot_has_spy(session, viewer_slot)) {
        std::uint8_t mask = 0U;
        for (std::uint8_t slot = 0U; slot < static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS);
             ++slot) {
            mask = static_cast<std::uint8_t>(mask | player_slot_bit(slot));
        }
        return mask;
    }

    std::uint8_t mask = player_slot_bit(viewer_slot);
    if (!slot_has_cartography(session, viewer_slot)) {
        return mask;
    }

    for (std::uint8_t slot = 0U; slot < static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS);
         ++slot) {
        if (slots_are_allied(session, viewer_slot, slot)) {
            mask = static_cast<std::uint8_t>(mask | player_slot_bit(slot));
        }
    }

    return mask;
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
