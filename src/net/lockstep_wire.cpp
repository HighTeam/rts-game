#include "net/lockstep_wire.hpp"

#include <cstring>
#include <limits>

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

std::vector<std::byte> encode_tick_input_batch(const TickInputBatch& batch)
{
    if (batch.commands.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
        return {};
    }

    std::vector<std::byte> out{};
    out.reserve(11U + batch.commands.size() * 64U);

    append_pod(out, batch.execute_tick);
    append_pod(out, batch.player_slot);

    const auto command_count = static_cast<std::uint16_t>(batch.commands.size());
    append_pod(out, command_count);

    for (const sim::player::PlayerCommand& command : batch.commands) {
        const std::vector<std::byte> command_bytes = sim::player::encode_player_command(command);
        if (command_bytes.empty()) {
            return {};
        }

        if (command_bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
            return {};
        }

        const auto command_length = static_cast<std::uint16_t>(command_bytes.size());
        append_pod(out, command_length);
        out.insert(out.end(), command_bytes.begin(), command_bytes.end());
    }

    return out;
}

std::optional<TickInputBatch> decode_tick_input_batch(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    TickInputBatch batch{};

    if (!read_pod(cursor, batch.execute_tick)) {
        return std::nullopt;
    }

    if (!read_pod(cursor, batch.player_slot)) {
        return std::nullopt;
    }

    std::uint16_t command_count = 0U;
    if (!read_pod(cursor, command_count)) {
        return std::nullopt;
    }

    batch.commands.reserve(command_count);
    for (std::uint16_t command_index = 0U; command_index < command_count; ++command_index) {
        std::uint16_t command_length = 0U;
        if (!read_pod(cursor, command_length)) {
            return std::nullopt;
        }

        if (cursor.size() < command_length) {
            return std::nullopt;
        }

        const std::span<const std::byte> command_bytes = cursor.subspan(0U, command_length);
        cursor = cursor.subspan(command_length);

        const auto decoded_command = sim::player::decode_player_command(command_bytes);
        if (!decoded_command.has_value()) {
            return std::nullopt;
        }

        batch.commands.push_back(*decoded_command);
    }

    if (!cursor.empty()) {
        return std::nullopt;
    }

    return batch;
}

std::vector<std::byte> encode_tick_state_hash(const TickStateHashMessage& message)
{
    std::vector<std::byte> out{};
    out.reserve(17U);
    append_pod(out, message.execute_tick);
    append_pod(out, message.player_slot);
    append_pod(out, message.state_hash);
    return out;
}

std::optional<TickStateHashMessage> decode_tick_state_hash(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    TickStateHashMessage message{};

    if (!read_pod(cursor, message.execute_tick)) {
        return std::nullopt;
    }

    if (!read_pod(cursor, message.player_slot)) {
        return std::nullopt;
    }

    if (!read_pod(cursor, message.state_hash)) {
        return std::nullopt;
    }

    if (!cursor.empty()) {
        return std::nullopt;
    }

    return message;
}

} // namespace aoa::net
