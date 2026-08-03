#include "sim/player/player_command.hpp"

#include <cstring>
#include <limits>

namespace aoa::sim::player {

namespace {

constexpr std::size_t PLAYER_COMMAND_HEADER_BYTES = 22U;
constexpr std::size_t PLAYER_COMMAND_UNIT_ID_BYTES = 4U;

void append_bytes(std::vector<std::byte>& out, const void* data, const std::size_t size)
{
    const auto* bytes = static_cast<const std::byte*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

template <typename T>
void append_pod(std::vector<std::byte>& out, const T value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    append_bytes(out, &value, sizeof(T));
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

[[nodiscard]] entt::entity entity_from_wire(const std::uint32_t wire_id)
{
    return entt::entity{wire_id};
}

[[nodiscard]] std::uint32_t entity_to_wire(const entt::entity entity)
{
    return static_cast<std::uint32_t>(entt::to_integral(entity));
}

} // namespace

std::vector<std::byte> encode_player_command(const PlayerCommand& command)
{
    if (command.unit_ids.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
        return {};
    }

    std::vector<std::byte> out{};
    out.reserve(
        PLAYER_COMMAND_HEADER_BYTES + command.unit_ids.size() * PLAYER_COMMAND_UNIT_ID_BYTES + 8U);

    append_pod(out, command.sequence);
    append_pod(out, command.execute_tick);
    append_pod(out, command.player_slot);
    append_pod(out, static_cast<std::uint8_t>(command.type));

    const auto unit_count = static_cast<std::uint16_t>(command.unit_ids.size());
    append_pod(out, unit_count);

    for (const entt::entity unit : command.unit_ids) {
        append_pod(out, entity_to_wire(unit));
    }

    switch (command.type) {
    case PlayerCommandType::Move:
    case PlayerCommandType::Gather:
        append_pod(out, static_cast<std::int16_t>(command.cell.x));
        append_pod(out, static_cast<std::int16_t>(command.cell.y));
        break;
    case PlayerCommandType::Attack:
        append_pod(out, entity_to_wire(command.target_entity));
        break;
    case PlayerCommandType::Deposit:
        break;
    }

    return out;
}

std::optional<PlayerCommand> decode_player_command(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    PlayerCommand command{};

    if (!read_pod(cursor, command.sequence)) {
        return std::nullopt;
    }

    if (!read_pod(cursor, command.execute_tick)) {
        return std::nullopt;
    }

    if (!read_pod(cursor, command.player_slot)) {
        return std::nullopt;
    }

    std::uint8_t type_raw = 0U;
    if (!read_pod(cursor, type_raw)) {
        return std::nullopt;
    }

    if (type_raw > static_cast<std::uint8_t>(PlayerCommandType::Deposit)) {
        return std::nullopt;
    }

    command.type = static_cast<PlayerCommandType>(type_raw);

    std::uint16_t unit_count = 0U;
    if (!read_pod(cursor, unit_count)) {
        return std::nullopt;
    }

    if (cursor.size() < static_cast<std::size_t>(unit_count) * PLAYER_COMMAND_UNIT_ID_BYTES) {
        return std::nullopt;
    }

    command.unit_ids.reserve(unit_count);
    for (std::uint16_t unit_index = 0U; unit_index < unit_count; ++unit_index) {
        std::uint32_t wire_id = 0U;
        if (!read_pod(cursor, wire_id)) {
            return std::nullopt;
        }

        command.unit_ids.push_back(entity_from_wire(wire_id));
    }

    switch (command.type) {
    case PlayerCommandType::Move:
    case PlayerCommandType::Gather: {
        std::int16_t cell_x = 0;
        std::int16_t cell_y = 0;
        if (!read_pod(cursor, cell_x) || !read_pod(cursor, cell_y)) {
            return std::nullopt;
        }

        command.cell = core::GridPos{static_cast<int>(cell_x), static_cast<int>(cell_y)};
        break;
    }
    case PlayerCommandType::Attack: {
        std::uint32_t wire_id = 0U;
        if (!read_pod(cursor, wire_id)) {
            return std::nullopt;
        }

        command.target_entity = entity_from_wire(wire_id);
        break;
    }
    case PlayerCommandType::Deposit:
        break;
    }

    if (!cursor.empty()) {
        return std::nullopt;
    }

    return command;
}

} // namespace aoa::sim::player
