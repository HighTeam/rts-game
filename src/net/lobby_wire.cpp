#include "net/lobby_wire.hpp"

#include "core/constants.hpp"

#include <cstring>
#include <type_traits>

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

void append_blob(std::vector<std::byte>& out, const std::string& text)
{
    const std::uint32_t max_bytes = static_cast<std::uint32_t>(aoa::constants::PATTERN_MAX_PAYLOAD_BYTES);
    const std::uint32_t length = static_cast<std::uint32_t>(
        text.size() > max_bytes ? max_bytes : text.size());
    append_pod(out, length);
    const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
    out.insert(out.end(), bytes, bytes + length);
}

[[nodiscard]] bool read_blob(std::span<const std::byte>& bytes, std::string& out)
{
    std::uint32_t length = 0U;
    if (!read_pod(bytes, length)) {
        return false;
    }

    if (length > static_cast<std::uint32_t>(aoa::constants::PATTERN_MAX_PAYLOAD_BYTES)
        || bytes.size() < length) {
        return false;
    }

    out.assign(reinterpret_cast<const char*>(bytes.data()), length);
    bytes = bytes.subspan(length);
    return true;
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
    append_pod(out, settings.fog_mode);
    append_pod(out, static_cast<std::uint8_t>(settings.cheats_enabled ? 1U : 0U));
    append_pod(out, settings.map_pattern);
    append_pod(out, settings.game_style);
    append_pod(out, settings.required_player_count);
    append_pod(out, settings.victory_condition);
    append_text(out, settings.scenario_name);
    append_pod(out, settings.map_seed);
    append_text(out, settings.pattern_name);
    append_blob(out, settings.pattern_payload);
    append_pod(out, settings.pattern_min_players);
    append_pod(out, settings.pattern_max_players);
    append_pod(out, settings.map_width);
    append_pod(out, settings.map_height);
    append_pod(out, static_cast<std::uint8_t>(settings.map_size_locked ? 1U : 0U));
    append_pod(out, static_cast<std::uint8_t>(settings.block_team_changes ? 1U : 0U));
    append_pod(out, static_cast<std::uint8_t>(settings.allow_spectators ? 1U : 0U));
}

[[nodiscard]] bool read_settings(std::span<const std::byte>& bytes, LobbySettings& settings)
{
    std::uint8_t fog_raw = 0U;
    std::uint8_t cheats_raw = 0U;
    std::uint8_t size_locked_raw = 0U;
    if (!read_pod(bytes, settings.player_count)
        || !read_pod(bytes, settings.civil_population_map_cap) || !read_pod(bytes, fog_raw)
        || !read_pod(bytes, settings.fog_mode) || !read_pod(bytes, cheats_raw)
        || !read_pod(bytes, settings.map_pattern) || !read_pod(bytes, settings.game_style)
        || !read_pod(bytes, settings.required_player_count)
        || !read_pod(bytes, settings.victory_condition)
        || !read_text(bytes, settings.scenario_name)
        || !read_pod(bytes, settings.map_seed)
        || !read_text(bytes, settings.pattern_name)
        || !read_blob(bytes, settings.pattern_payload)
        || !read_pod(bytes, settings.pattern_min_players)
        || !read_pod(bytes, settings.pattern_max_players)
        || !read_pod(bytes, settings.map_width)
        || !read_pod(bytes, settings.map_height)
        || !read_pod(bytes, size_locked_raw)) {
        return false;
    }

    settings.fog_of_war_enabled = fog_raw != 0U;
    settings.cheats_enabled = cheats_raw != 0U;
    settings.map_size_locked = size_locked_raw != 0U;
    settings.block_team_changes = false;
    settings.allow_spectators = false;
    if (!bytes.empty()) {
        std::uint8_t block_raw = 0U;
        if (!read_pod(bytes, block_raw)) {
            return false;
        }

        settings.block_team_changes = block_raw != 0U;
    }

    if (!bytes.empty()) {
        std::uint8_t spectators_raw = 0U;
        if (!read_pod(bytes, spectators_raw)) {
            return false;
        }

        settings.allow_spectators = spectators_raw != 0U;
    }

    return true;
}

} // namespace

bool lobby_slot_is_playing(const LobbySlotKind kind)
{
    return kind != LobbySlotKind::Disabled && kind != LobbySlotKind::Spectator;
}

bool lobby_slot_accepts_join(const LobbySlotInfo& slot)
{
    return slot.kind == LobbySlotKind::Enabled && !slot.occupied;
}

std::uint8_t lobby_playing_slot_count(const LobbyStateMessage& state)
{
    std::uint8_t count = 0U;
    for (const LobbySlotInfo& slot : state.slots) {
        if (lobby_slot_is_playing(slot.kind) && (slot.occupied || slot.kind == LobbySlotKind::Ai)) {
            ++count;
        }
    }

    return count;
}

std::uint8_t lobby_configured_slot_count(const LobbyStateMessage& state)
{
    std::uint8_t count = 0U;
    for (const LobbySlotInfo& slot : state.slots) {
        if (slot.kind != LobbySlotKind::Disabled) {
            ++count;
        }
    }

    return count;
}

std::uint8_t lobby_connected_human_count(const LobbyStateMessage& state)
{
    std::uint8_t count = 0U;
    for (const LobbySlotInfo& slot : state.slots) {
        if (!slot.occupied) {
            continue;
        }

        if (slot.kind == LobbySlotKind::Host || slot.kind == LobbySlotKind::Enabled
            || slot.kind == LobbySlotKind::Spectator) {
            ++count;
        }
    }

    return count;
}

std::optional<std::uint8_t> first_joinable_lobby_slot(const LobbyStateMessage& state)
{
    for (std::uint8_t slot = 1U; slot < state.slots.size(); ++slot) {
        if (lobby_slot_accepts_join(state.slots[slot])) {
            return slot;
        }
    }

    if (!state.settings.allow_spectators) {
        return std::nullopt;
    }

    for (std::uint8_t slot = 1U; slot < state.slots.size(); ++slot) {
        if (state.slots[slot].occupied) {
            continue;
        }

        if (state.slots[slot].kind == LobbySlotKind::Disabled
            || state.slots[slot].kind == LobbySlotKind::Spectator) {
            return slot;
        }
    }

    return std::nullopt;
}

bool lobby_color_taken(
    const LobbyStateMessage& state,
    const std::uint8_t color,
    const std::uint8_t except_slot)
{
    for (std::uint8_t slot = 0U; slot < state.slots.size(); ++slot) {
        if (slot == except_slot || state.slots[slot].kind == LobbySlotKind::Disabled) {
            continue;
        }

        if (state.slots[slot].color == color) {
            return true;
        }
    }

    return false;
}

std::uint8_t next_free_lobby_color(
    const LobbyStateMessage& state,
    const std::uint8_t preferred,
    const std::uint8_t except_slot)
{
    if (!lobby_color_taken(state, preferred, except_slot)) {
        return preferred;
    }

    for (int step = 1; step <= aoa::constants::MAX_PLAYER_SLOTS; ++step) {
        const std::uint8_t candidate = static_cast<std::uint8_t>(
            (static_cast<int>(preferred) + step) % aoa::constants::MAX_PLAYER_SLOTS);
        if (!lobby_color_taken(state, candidate, except_slot)) {
            return candidate;
        }
    }

    return preferred;
}

std::vector<std::byte> encode_lobby_join(const LobbyJoinMessage& message)
{
    std::vector<std::byte> out{};
    append_text(out, message.name);
    append_text(out, message.version);
    return out;
}

std::optional<LobbyJoinMessage> decode_lobby_join(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    LobbyJoinMessage message{};
    if (!read_text(cursor, message.name) || !read_text(cursor, message.version)) {
        return std::nullopt;
    }

    return message;
}

std::vector<std::byte> encode_lobby_reject(const LobbyRejectMessage& message)
{
    std::vector<std::byte> out{};
    append_text(out, message.reason);
    return out;
}

std::optional<LobbyRejectMessage> decode_lobby_reject(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    LobbyRejectMessage message{};
    if (!read_text(cursor, message.reason)) {
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
        append_pod(out, static_cast<std::uint8_t>(slot.kind));
        append_pod(out, slot.color);
        append_pod(out, slot.team);
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
        std::uint8_t kind_raw = 0U;
        LobbySlotInfo& slot = message.slots[index];
        if (!read_pod(cursor, occupied_raw) || !read_pod(cursor, ready_raw)
            || !read_pod(cursor, slot.ping_ms) || !read_text(cursor, slot.name)
            || !read_pod(cursor, kind_raw) || !read_pod(cursor, slot.color)
            || !read_pod(cursor, slot.team)) {
            return std::nullopt;
        }

        slot.occupied = occupied_raw != 0U;
        slot.ready = ready_raw != 0U;
        slot.kind = static_cast<LobbySlotKind>(kind_raw);
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

std::vector<std::byte> encode_lobby_color(const LobbyColorMessage& message)
{
    std::vector<std::byte> out{};
    append_pod(out, message.player_slot);
    append_pod(out, message.color);
    return out;
}

std::optional<LobbyColorMessage> decode_lobby_color(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    LobbyColorMessage message{};
    if (!read_pod(cursor, message.player_slot) || !read_pod(cursor, message.color)) {
        return std::nullopt;
    }

    return message;
}

std::vector<std::byte> encode_lobby_team(const LobbyTeamMessage& message)
{
    std::vector<std::byte> out{};
    append_pod(out, message.player_slot);
    append_pod(out, message.team);
    return out;
}

std::optional<LobbyTeamMessage> decode_lobby_team(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    LobbyTeamMessage message{};
    if (!read_pod(cursor, message.player_slot) || !read_pod(cursor, message.team)) {
        return std::nullopt;
    }

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
