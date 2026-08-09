#pragma once

#include "net/net_constants.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace aoa::net {

// Match configuration chosen by the host before the lockstep match starts.
struct LobbySettings {
    std::uint8_t player_count{2U};
    std::uint8_t civil_population_map_cap{15U};
    bool fog_of_war_enabled{true};
};

struct LobbySlotInfo {
    bool occupied{false};
    bool ready{false};
    std::uint16_t ping_ms{0U};
    std::string name{};
};

struct LobbyStateMessage {
    std::uint8_t host_slot{0U};
    std::uint8_t recipient_slot{0U};
    LobbySettings settings{};
    std::array<LobbySlotInfo, constants::LOCKSTEP_MAX_PLAYER_SLOTS> slots{};
};

struct LobbyJoinMessage {
    std::string name{};
};

struct LobbyReadyMessage {
    std::uint8_t player_slot{0U};
    bool ready{false};
};

[[nodiscard]] std::vector<std::byte> encode_lobby_join(const LobbyJoinMessage& message);
[[nodiscard]] std::optional<LobbyJoinMessage> decode_lobby_join(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_lobby_state(const LobbyStateMessage& message);
[[nodiscard]] std::optional<LobbyStateMessage> decode_lobby_state(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_lobby_ready(const LobbyReadyMessage& message);
[[nodiscard]] std::optional<LobbyReadyMessage> decode_lobby_ready(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_lobby_settings(const LobbySettings& settings);
[[nodiscard]] std::optional<LobbySettings> decode_lobby_settings(std::span<const std::byte> bytes);

} // namespace aoa::net
