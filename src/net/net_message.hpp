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
};

[[nodiscard]] std::vector<std::byte> encode_net_message(
    NetMessageKind kind,
    std::span<const std::byte> payload);

[[nodiscard]] std::optional<std::pair<NetMessageKind, std::vector<std::byte>>> decode_net_message(
    std::span<const std::byte> bytes);

} // namespace aoa::net
