#pragma once

#include "sim/components/match_session.hpp"
#include "sim/player/player_command.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace aoa::sim {

class Simulation;

struct SimSnapshot {
    std::uint64_t tick_count{0U};
    std::uint64_t state_hash{0U};
    std::uint64_t next_command_sequence{1U};
    std::uint8_t ai_controlled_slots{0U};
    std::uint64_t ai_controlled_since_tick{0U};
    std::int32_t civil_population_map_cap{15};
    std::uint8_t fog_of_war_enabled{1U};
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
    std::vector<components::AiControlTransition> ai_control_transitions{};
    std::vector<player::PlayerCommand> input_log{};
};

[[nodiscard]] std::vector<std::byte> encode_sim_snapshot(const Simulation& simulation);

[[nodiscard]] std::optional<SimSnapshot> decode_sim_snapshot_metadata(std::span<const std::byte> bytes);

[[nodiscard]] bool apply_sim_snapshot(Simulation& simulation, std::span<const std::byte> bytes);

void diagnose_snapshot_roundtrip_failure(

    const Simulation& source,

    const Simulation& restored,

    std::span<const std::byte> snapshot_bytes);

[[nodiscard]] bool validate_snapshot_input_replay(std::span<const std::byte> snapshot_bytes);

} // namespace aoa::sim
