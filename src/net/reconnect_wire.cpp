#include "net/reconnect_wire.hpp"

#include <cstring>

namespace aoa::net {

namespace {

template <typename T>
void append_pod(std::vector<std::byte>& out, const T value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* bytes = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <typename T>
[[nodiscard]] bool read_pod(std::span<const std::byte>& bytes, T& out)
{
    if (bytes.size() < sizeof(T)) {
        return false;
    }

    std::memcpy(&out, bytes.data(), sizeof(T));
    bytes = bytes.subspan(sizeof(T));
    return true;
}

} // namespace

std::vector<std::byte> encode_reconnect_request(const ReconnectRequestMessage& message)
{
    std::vector<std::byte> out{};
    append_pod(out, message.player_slot);
    return out;
}

std::optional<ReconnectRequestMessage> decode_reconnect_request(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    ReconnectRequestMessage message{};
    if (!read_pod(cursor, message.player_slot)) {
        return std::nullopt;
    }

    return message;
}

} // namespace aoa::net
