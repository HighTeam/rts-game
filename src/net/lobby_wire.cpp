#include "net/lobby_wire.hpp"

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

void append_text(std::vector<std::byte>& out, const std::string& text)
{
    const std::uint8_t length = static_cast<std::uint8_t>(
        text.size() > 0xFFU ? 0xFFU : text.size());
    append_pod(out, length);
    const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
    out.insert(out.end(), bytes, bytes + length);
}

[[nodiscard]] bool read_text(std::span<const std::byte>& bytes, std::string& out)
{
    std::uint8_t length = 0U;
    if (!read_pod(bytes, length)) {
        return false;
    }

    if (bytes.size() < length) {
        return false;
    }

    out.assign(reinterpret_cast<const char*>(bytes.data()), length);
    bytes = bytes.subspan(length);
    return true;
}

void append_settings(std::vector<std::byte>& out, const LobbySettings& settings)
{
    append_pod(out, settings.player_count);
    append_pod(out, settings.civil_population_map_cap);
    append_pod(out, static_cast<std::uint8_t>(settings.fog_of_war_enabled ? 1U : 0U));
}

[[nodiscard]] bool read_settings(std::span<const std::byte>& bytes, LobbySettings& settings)
{
    std::uint8_t fog_raw = 0U;
    if (!read_pod(bytes, settings.player_count)
        || !read_pod(bytes, settings.civil_population_map_cap) || !read_pod(bytes, fog_raw)) {
        return false;
    }

    settings.fog_of_war_enabled = fog_raw != 0U;
    return true;
}

} // namespace

std::vector<std::byte> encode_lobby_join(const LobbyJoinMessage& message)
{
    std::vector<std::byte> out{};
    append_text(out, message.name);
    return out;
}

std::optional<LobbyJoinMessage> decode_lobby_join(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    LobbyJoinMessage message{};
    if (!read_text(cursor, message.name)) {
        return std::nullopt;
    }

    return message;
}

std::vector<std::byte> encode_lobby_state(const LobbyStateMessage& message)
{
    std::vector<std::byte> out{};
    append_pod(out, message.host_slot);
    append_pod(out, message.recipient_slot);
    append_settings(out, message.settings);

    const std::uint8_t slot_count = static_cast<std::uint8_t>(message.slots.size());
    append_pod(out, slot_count);
    for (const LobbySlotInfo& slot : message.slots) {
        append_pod(out, static_cast<std::uint8_t>(slot.occupied ? 1U : 0U));
        append_pod(out, static_cast<std::uint8_t>(slot.ready ? 1U : 0U));
        append_pod(out, slot.ping_ms);
        append_text(out, slot.name);
    }

    return out;
}

std::optional<LobbyStateMessage> decode_lobby_state(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    LobbyStateMessage message{};
    std::uint8_t slot_count = 0U;
    if (!read_pod(cursor, message.host_slot) || !read_pod(cursor, message.recipient_slot)
        || !read_settings(cursor, message.settings) || !read_pod(cursor, slot_count)) {
        return std::nullopt;
    }

    if (slot_count > message.slots.size()) {
        return std::nullopt;
    }

    for (std::uint8_t index = 0U; index < slot_count; ++index) {
        std::uint8_t occupied_raw = 0U;
        std::uint8_t ready_raw = 0U;
        LobbySlotInfo& slot = message.slots[index];
        if (!read_pod(cursor, occupied_raw) || !read_pod(cursor, ready_raw)
            || !read_pod(cursor, slot.ping_ms) || !read_text(cursor, slot.name)) {
            return std::nullopt;
        }

        slot.occupied = occupied_raw != 0U;
        slot.ready = ready_raw != 0U;
    }

    return message;
}

std::vector<std::byte> encode_lobby_ready(const LobbyReadyMessage& message)
{
    std::vector<std::byte> out{};
    append_pod(out, message.player_slot);
    append_pod(out, static_cast<std::uint8_t>(message.ready ? 1U : 0U));
    return out;
}

std::optional<LobbyReadyMessage> decode_lobby_ready(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    LobbyReadyMessage message{};
    std::uint8_t ready_raw = 0U;
    if (!read_pod(cursor, message.player_slot) || !read_pod(cursor, ready_raw)) {
        return std::nullopt;
    }

    message.ready = ready_raw != 0U;
    return message;
}

std::vector<std::byte> encode_lobby_settings(const LobbySettings& settings)
{
    std::vector<std::byte> out{};
    append_settings(out, settings);
    return out;
}

std::optional<LobbySettings> decode_lobby_settings(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    LobbySettings settings{};
    if (!read_settings(cursor, settings)) {
        return std::nullopt;
    }

    return settings;
}

} // namespace aoa::net
