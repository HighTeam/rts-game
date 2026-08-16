#include "sim/player/player_command.hpp"

#include "math/fixed.hpp"

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
        PLAYER_COMMAND_HEADER_BYTES + command.unit_ids.size() * PLAYER_COMMAND_UNIT_ID_BYTES + 12U);

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
        append_pod(out, static_cast<std::int16_t>(command.cell.x));
        append_pod(out, static_cast<std::int16_t>(command.cell.y));
        append_pod(out, static_cast<std::uint8_t>(command.has_goal_world ? 1U : 0U));
        append_pod(out, command.goal_world_x.raw());
        append_pod(out, command.goal_world_y.raw());
        break;
    case PlayerCommandType::Gather:
        append_pod(out, static_cast<std::int16_t>(command.cell.x));
        append_pod(out, static_cast<std::int16_t>(command.cell.y));
        break;
    case PlayerCommandType::Attack:
    case PlayerCommandType::SpawnWorker:
    case PlayerCommandType::SpawnMilitia:
    case PlayerCommandType::ResumeBuild:
        append_pod(out, entity_to_wire(command.target_entity));
        break;
    case PlayerCommandType::BuildTownCenter:
    case PlayerCommandType::BuildHouse:
    case PlayerCommandType::BuildLumberjack:
    case PlayerCommandType::BuildExtractor:
        append_pod(out, static_cast<std::int16_t>(command.cell.x));
        append_pod(out, static_cast<std::int16_t>(command.cell.y));
        break;
    case PlayerCommandType::DestroyBuilding:
        append_pod(out, entity_to_wire(command.target_entity));
        break;
    case PlayerCommandType::Deposit:
    case PlayerCommandType::KillUnits:
    case PlayerCommandType::Stop:
        break;
    }

    return out;
}

std::optional<PlayerCommand> decode_player_command_body(std::span<const std::byte>& cursor)
{
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

    if (type_raw > static_cast<std::uint8_t>(PlayerCommandType::BuildExtractor)) {
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
    case PlayerCommandType::Move: {
        std::int16_t cell_x = 0;
        std::int16_t cell_y = 0;
        std::uint8_t has_goal_world_raw = 0U;
        std::int32_t goal_world_x_raw = 0;
        std::int32_t goal_world_y_raw = 0;
        if (!read_pod(cursor, cell_x) || !read_pod(cursor, cell_y) || !read_pod(cursor, has_goal_world_raw)
            || !read_pod(cursor, goal_world_x_raw) || !read_pod(cursor, goal_world_y_raw)) {
            return std::nullopt;
        }

        command.cell = core::GridPos{static_cast<int>(cell_x), static_cast<int>(cell_y)};
        command.has_goal_world = has_goal_world_raw != 0U;
        command.goal_world_x = math::Fixed::from_raw(goal_world_x_raw);
        command.goal_world_y = math::Fixed::from_raw(goal_world_y_raw);
        break;
    }
    case PlayerCommandType::Gather: {
        std::int16_t cell_x = 0;
        std::int16_t cell_y = 0;
        if (!read_pod(cursor, cell_x) || !read_pod(cursor, cell_y)) {
            return std::nullopt;
        }

        command.cell = core::GridPos{static_cast<int>(cell_x), static_cast<int>(cell_y)};
        break;
    }
    case PlayerCommandType::Attack:
    case PlayerCommandType::SpawnWorker:
    case PlayerCommandType::SpawnMilitia:
    case PlayerCommandType::ResumeBuild: {
        std::uint32_t wire_id = 0U;
        if (!read_pod(cursor, wire_id)) {
            return std::nullopt;
        }

        command.target_entity = entity_from_wire(wire_id);
        break;
    }
    case PlayerCommandType::BuildTownCenter:
    case PlayerCommandType::BuildHouse:
    case PlayerCommandType::BuildLumberjack:
    case PlayerCommandType::BuildExtractor: {
        std::int16_t cell_x = 0;
        std::int16_t cell_y = 0;
        if (!read_pod(cursor, cell_x) || !read_pod(cursor, cell_y)) {
            return std::nullopt;
        }

        command.cell = core::GridPos{static_cast<int>(cell_x), static_cast<int>(cell_y)};
        break;
    }
    case PlayerCommandType::DestroyBuilding: {
        std::uint32_t wire_id = 0U;
        if (!read_pod(cursor, wire_id)) {
            return std::nullopt;
        }

        command.target_entity = entity_from_wire(wire_id);
        break;
    }
    case PlayerCommandType::Deposit:
    case PlayerCommandType::KillUnits:
    case PlayerCommandType::Stop:
        break;
    }

    return command;
}

namespace {

void append_entity_snapshot_key(std::vector<std::byte>& out, const snapshot::EntitySnapshotKey& key)
{
    append_pod(out, key.player_slot);
    append_pod(out, static_cast<std::uint8_t>(key.category));
    append_pod(out, key.ordinal);
}

[[nodiscard]] bool read_entity_snapshot_key(
    std::span<const std::byte>& cursor,
    snapshot::EntitySnapshotKey& key)
{
    std::uint8_t category_raw = 0U;
    if (!read_pod(cursor, key.player_slot) || !read_pod(cursor, category_raw)
        || !read_pod(cursor, key.ordinal)) {
        return false;
    }

    if (category_raw > static_cast<std::uint8_t>(snapshot::EntitySnapshotCategory::House)) {
        return false;
    }

    key.category = static_cast<snapshot::EntitySnapshotCategory>(category_raw);
    return true;
}

} // namespace

std::vector<std::byte> encode_player_command_with_keys(const PlayerCommand& command)
{
    std::vector<std::byte> out = encode_player_command(command);
    if (out.empty()) {
        return out;
    }

    if (command.unit_keys.size() != command.unit_ids.size()) {
        return {};
    }

    const auto unit_key_count = static_cast<std::uint16_t>(command.unit_keys.size());
    append_pod(out, unit_key_count);
    for (const snapshot::EntitySnapshotKey& key : command.unit_keys) {
        append_entity_snapshot_key(out, key);
    }

    const std::uint8_t has_target_key = command.target_entity_key.has_value() ? 1U : 0U;
    append_pod(out, has_target_key);
    if (has_target_key != 0U) {
        append_entity_snapshot_key(out, *command.target_entity_key);
    }

    return out;
}

std::optional<PlayerCommand> decode_player_command_with_keys(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    const auto decoded_body = decode_player_command_body(cursor);
    if (!decoded_body.has_value()) {
        return std::nullopt;
    }

    PlayerCommand command = *decoded_body;

    std::uint16_t unit_key_count = 0U;
    if (!read_pod(cursor, unit_key_count)) {
        return std::nullopt;
    }

    if (unit_key_count != command.unit_ids.size()) {
        return std::nullopt;
    }

    command.unit_keys.clear();
    command.unit_keys.reserve(unit_key_count);
    for (std::uint16_t index = 0U; index < unit_key_count; ++index) {
        snapshot::EntitySnapshotKey key{};
        if (!read_entity_snapshot_key(cursor, key)) {
            return std::nullopt;
        }

        command.unit_keys.push_back(key);
    }

    std::uint8_t has_target_key = 0U;
    if (!read_pod(cursor, has_target_key)) {
        return std::nullopt;
    }

    if (has_target_key != 0U) {
        snapshot::EntitySnapshotKey key{};
        if (!read_entity_snapshot_key(cursor, key)) {
            return std::nullopt;
        }

        command.target_entity_key = key;
    }

    if (!cursor.empty()) {
        return std::nullopt;
    }

    return command;
}

std::optional<PlayerCommand> decode_player_command(const std::span<const std::byte> bytes)
{
    std::span<const std::byte> cursor = bytes;
    const auto command = decode_player_command_body(cursor);
    if (!command.has_value() || !cursor.empty()) {
        return std::nullopt;
    }

    return command;
}

} // namespace aoa::sim::player
