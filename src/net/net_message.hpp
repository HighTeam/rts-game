#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace aoa::net {

enum class NetMessageKind : std::uint8_t {
    PlayerCommand = 1,
    TickStateHash = 2,
    TickInputBatch = 3,
    ReconnectRequest = 4,
    ReconnectSnapshot = 5,
    JoinAccepted = 6,
    ResyncReady = 7,
    LatencyProbe = 8,
    LatencyPong = 9,
    SlotAiTakeover = 10,
    SlotAiResume = 11,
    Chat = 12,
    LobbyJoin = 13,
    LobbyState = 14,
    LobbyReady = 15,
    LobbyLeave = 16,
    LobbyMatchStart = 17,
    LobbyColor = 18,
    LobbyReject = 19,
    LobbyTeam = 20,
    MatchPause = 21,
    PlayerResign = 22,
    HostEnded = 23,
};

[[nodiscard]] std::vector<std::byte> encode_net_message(
    NetMessageKind kind,
    std::span<const std::byte> payload);

[[nodiscard]] std::optional<std::pair<NetMessageKind, std::vector<std::byte>>> decode_net_message(
    std::span<const std::byte> bytes);

} // namespace aoa::net
