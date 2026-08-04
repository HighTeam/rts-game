#include "net/net_message.hpp"

#include <cstring>

namespace aoa::net {

namespace {

[[nodiscard]] bool is_known_kind(const std::uint8_t kind_raw)
{
    return kind_raw == static_cast<std::uint8_t>(NetMessageKind::PlayerCommand)
        || kind_raw == static_cast<std::uint8_t>(NetMessageKind::TickStateHash);
}

} // namespace

std::vector<std::byte> encode_net_message(
    const NetMessageKind kind,
    const std::span<const std::byte> payload)
{
    std::vector<std::byte> out{};
    out.reserve(1U + payload.size());
    out.push_back(static_cast<std::byte>(kind));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::optional<std::pair<NetMessageKind, std::vector<std::byte>>> decode_net_message(
    const std::span<const std::byte> bytes)
{
    if (bytes.empty()) {
        return std::nullopt;
    }

    const auto kind_raw = static_cast<std::uint8_t>(bytes.front());
    if (!is_known_kind(kind_raw)) {
        return std::nullopt;
    }

    std::vector<std::byte> payload{};
    if (bytes.size() > 1U) {
        payload.resize(bytes.size() - 1U);
        std::memcpy(payload.data(), bytes.data() + 1U, payload.size());
    }

    return std::pair{
        static_cast<NetMessageKind>(kind_raw),
        std::move(payload),
    };
}

} // namespace aoa::net
