#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace aoa::net {

struct ReconnectRequestMessage {
    std::uint8_t player_slot{0U};
    // Per-slot secret issued at match start; strangers cannot reclaim a seat.
    std::uint64_t claim_token{0U};
};

[[nodiscard]] std::vector<std::byte> encode_reconnect_request(const ReconnectRequestMessage& message);

[[nodiscard]] std::optional<ReconnectRequestMessage> decode_reconnect_request(
    std::span<const std::byte> bytes);

} // namespace aoa::net
