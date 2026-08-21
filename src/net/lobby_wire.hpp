#pragma once

#include "core/constants.hpp"
#include "net/net_constants.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace aoa::net {

enum class LobbySlotKind : std::uint8_t {
    Host = 0,
    Ai = 1,
    Disabled = 2,
    Spectator = 3,
    Enabled = 4,
};

// Match configuration chosen by the host before the lockstep match starts.
struct LobbySettings {
    std::uint8_t player_count{2U};
    std::uint8_t civil_population_map_cap{15U};
    bool fog_of_war_enabled{true};
    std::uint8_t fog_mode{0U};
    bool cheats_enabled{false};
    std::uint8_t map_pattern{static_cast<std::uint8_t>(aoa::constants::MAP_PATTERN_COMMONS_INDEX)};
    std::uint8_t game_style{0U};
    std::uint8_t required_player_count{0U};
    std::uint8_t victory_condition{0U};
    std::string scenario_name{};
    std::uint64_t map_seed{0U};
    std::string pattern_name{};
    std::string pattern_payload{};
    std::uint8_t pattern_min_players{
        static_cast<std::uint8_t>(aoa::constants::PATTERN_DEFAULT_MIN_PLAYERS)};
    std::uint8_t pattern_max_players{
        static_cast<std::uint8_t>(aoa::constants::PATTERN_DEFAULT_MAX_PLAYERS)};
    std::int16_t map_width{static_cast<std::int16_t>(aoa::constants::MAP_TEST_WIDTH)};
    std::int16_t map_height{static_cast<std::int16_t>(aoa::constants::MAP_TEST_HEIGHT)};
    bool map_size_locked{false};
    bool block_team_changes{false};
    bool allow_spectators{false};
};

struct LobbySlotInfo {
    bool occupied{false};
    bool ready{false};
    std::uint16_t ping_ms{0U};
    std::string name{};
    LobbySlotKind kind{LobbySlotKind::Disabled};
    std::uint8_t color{0U};
    std::uint8_t team{0U};
};

struct LobbyStateMessage {
    std::uint8_t host_slot{0U};
    std::uint8_t recipient_slot{0U};
    LobbySettings settings{};
    std::array<LobbySlotInfo, constants::LOCKSTEP_MAX_PLAYER_SLOTS> slots{};
};

struct LobbyJoinMessage {
    std::string name{};
    std::string version{};
};

struct LobbyRejectMessage {
    std::string reason{};
};

struct LobbyReadyMessage {
    std::uint8_t player_slot{0U};
    bool ready{false};
};

struct LobbyColorMessage {
    std::uint8_t player_slot{0U};
    std::uint8_t color{0U};
};

struct LobbyTeamMessage {
    std::uint8_t player_slot{0U};
    std::uint8_t team{0U};
};

[[nodiscard]] std::vector<std::byte> encode_lobby_join(const LobbyJoinMessage& message);
[[nodiscard]] std::optional<LobbyJoinMessage> decode_lobby_join(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_lobby_reject(const LobbyRejectMessage& message);
[[nodiscard]] std::optional<LobbyRejectMessage> decode_lobby_reject(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_lobby_state(const LobbyStateMessage& message);
[[nodiscard]] std::optional<LobbyStateMessage> decode_lobby_state(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_lobby_ready(const LobbyReadyMessage& message);
[[nodiscard]] std::optional<LobbyReadyMessage> decode_lobby_ready(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_lobby_color(const LobbyColorMessage& message);
[[nodiscard]] std::optional<LobbyColorMessage> decode_lobby_color(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_lobby_team(const LobbyTeamMessage& message);
[[nodiscard]] std::optional<LobbyTeamMessage> decode_lobby_team(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_lobby_settings(const LobbySettings& settings);
[[nodiscard]] std::optional<LobbySettings> decode_lobby_settings(std::span<const std::byte> bytes);

[[nodiscard]] bool lobby_slot_is_playing(LobbySlotKind kind);
[[nodiscard]] bool lobby_slot_accepts_join(const LobbySlotInfo& slot);
[[nodiscard]] std::uint8_t lobby_playing_slot_count(const LobbyStateMessage& state);
[[nodiscard]] std::uint8_t lobby_configured_slot_count(const LobbyStateMessage& state);
[[nodiscard]] std::uint8_t lobby_connected_human_count(const LobbyStateMessage& state);
[[nodiscard]] std::optional<std::uint8_t> first_joinable_lobby_slot(const LobbyStateMessage& state);
[[nodiscard]] bool lobby_color_taken(
    const LobbyStateMessage& state,
    std::uint8_t color,
    std::uint8_t except_slot);
[[nodiscard]] std::uint8_t next_free_lobby_color(
    const LobbyStateMessage& state,
    std::uint8_t preferred,
    std::uint8_t except_slot);

} // namespace aoa::net
