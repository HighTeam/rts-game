#include "sim/snapshot/sim_snapshot.hpp"



#include "core/constants.hpp"

#include "data/content_loader.hpp"

#include "data/content_types.hpp"

#include "sim/components/combat.hpp"

#include "sim/components/content_pack.hpp"

#include "sim/components/entity_snapshot_identity.hpp"

#include "sim/components/fog_of_war.hpp"
#include "sim/components/grid_position.hpp"

#include "sim/components/health.hpp"

#include "sim/components/map_grid.hpp"

#include "sim/components/match_session.hpp"

#include "sim/components/movement.hpp"

#include "sim/components/player_slot.hpp"

#include "sim/components/resources.hpp"

#include "sim/components/tags.hpp"

#include "sim/components/world_position.hpp"

#include "sim/scenario/test_scenario.hpp"

#include "sim/simulation.hpp"

#include "sim/spawn/unit_spawn.hpp"

#include "sim/snapshot/entity_snapshot_key.hpp"

#include "sim/systems/gameplay_systems.hpp"
#include "sim/systems/visibility_system.hpp"

#include "math/fixed.hpp"

#include <algorithm>

#include <cstring>

#include <iostream>

#include <limits>



namespace aoa::sim {



namespace {



constexpr std::uint32_t SNAPSHOT_MAGIC = 0x414F4153U; // AOAS

constexpr std::uint16_t SNAPSHOT_VERSION = 9U;



enum class EntityStateFlags : std::uint16_t {

    CarriedWood = 1U << 0,

    Stockpile = 1U << 1,

    AttackOrder = 1U << 2,

    AttackCooldown = 1U << 3,

    GatherTarget = 1U << 4,

    WorkerBrain = 1U << 5,

    ManualControl = 1U << 6,

    MovePath = 1U << 7,

    MoveSegment = 1U << 8,

};



struct EntityStateRecord {

    snapshot::EntitySnapshotKey key{};

    core::GridPos grid_cell{};

    math::Fixed world_x{};

    math::Fixed world_y{};

    math::Fixed health_current{};

    math::Fixed health_max{};

    std::uint16_t flags{0U};

    int carried_wood{0};

    int stockpile_wood{0};

    snapshot::EntitySnapshotKey attack_target_key{};

    core::GridPos attack_last_known_cell{-1, -1};

    int attack_cooldown_ticks{0};

    core::GridPos gather_cell{-1, -1};

    components::WorkerState worker_state{components::WorkerState::Idle};

    components::MovePath move_path{};

    components::MoveSegment move_segment{};

};



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



entt::entity find_world_entity(entt::registry& registry)

{

    const auto view = registry.view<components::WorldTag>();

    if (view.begin() == view.end()) {

        return entt::null;

    }



    return *view.begin();

}



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



    if (category_raw > static_cast<std::uint8_t>(snapshot::EntitySnapshotCategory::TownCenter)) {

        return false;

    }



    key.category = static_cast<snapshot::EntitySnapshotCategory>(category_raw);

    return true;

}



std::vector<std::byte> encode_snapshot_command(const player::PlayerCommand& command)

{

    return player::encode_player_command_with_keys(command);

}



[[nodiscard]] std::optional<player::PlayerCommand> decode_snapshot_command(

    const std::span<const std::byte> bytes)

{

    return player::decode_player_command_with_keys(bytes);

}



void sync_map_tiles_with_forest_wood(components::MapGrid& map)
{
    for (std::size_t index = 0U; index < map.forest_wood.size(); ++index) {
        if (map.forest_wood[index] > 0) {
            map.tiles[index] = components::TileType::Forest;
            continue;
        }

        if (map.tiles[index] == components::TileType::Forest) {
            map.tiles[index] = components::TileType::Grass;
        }
    }
}

void normalize_registry_for_snapshot(entt::registry& registry)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return;
    }

    sync_map_tiles_with_forest_wood(registry.get<components::MapGrid>(world));

    std::vector<entt::entity> strip_attack_orders{};
    const auto attack_view = registry.view<components::AttackOrder>();
    for (const entt::entity entity : attack_view) {
        const entt::entity target = attack_view.get<components::AttackOrder>(entity).target;
        if (target == entt::null || !registry.valid(target)) {
            strip_attack_orders.push_back(entity);
            continue;
        }

        if (!snapshot::compute_entity_snapshot_key(registry, target).has_value()) {
            strip_attack_orders.push_back(entity);
        }
    }

    for (const entt::entity entity : strip_attack_orders) {
        registry.remove<components::AttackOrder>(entity);
    }
}

void reset_simulation_to_default_scenario(Simulation& simulation)
{
    const data::ContentDatabase content = data::load_content_database(data::default_data_directory());

    simulation.registry() = entt::registry{};
    simulation.set_tick_count(0U);
    simulation.restore_command_log({}, 1U);

    scenario::load_test_scenario(simulation.registry(), content);
}

[[nodiscard]] std::optional<EntityStateRecord> capture_entity_state(

    entt::registry& registry,

    const entt::entity entity)

{

    const auto key = snapshot::compute_entity_snapshot_key(registry, entity);

    if (!key.has_value()) {

        return std::nullopt;

    }



    if (!registry.all_of<components::GridPosition, components::Health>(entity)) {

        return std::nullopt;

    }



    EntityStateRecord record{};

    record.key = *key;

    record.grid_cell = registry.get<components::GridPosition>(entity).cell;

    if (registry.all_of<components::WorldPosition>(entity)) {

        const auto& world = registry.get<components::WorldPosition>(entity);

        record.world_x = world.x;

        record.world_y = world.y;

    }

    else {

        record.world_x = math::tile_center_coord(record.grid_cell.x);

        record.world_y = math::tile_center_coord(record.grid_cell.y);

    }

    const auto& health = registry.get<components::Health>(entity);

    record.health_current = health.current;

    record.health_max = health.max;



    if (registry.any_of<components::CarriedWood>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::CarriedWood);

        record.carried_wood = registry.get<components::CarriedWood>(entity).amount;

    }



    if (registry.any_of<components::Stockpile>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::Stockpile);

        record.stockpile_wood = registry.get<components::Stockpile>(entity).wood;

    }



    if (registry.any_of<components::AttackOrder>(entity)) {

        const auto& attack_order = registry.get<components::AttackOrder>(entity);

        const entt::entity target = attack_order.target;

        const auto target_key = snapshot::compute_entity_snapshot_key(registry, target);

        if (target_key.has_value()) {

            record.flags |= static_cast<std::uint16_t>(EntityStateFlags::AttackOrder);

            record.attack_target_key = *target_key;

            record.attack_last_known_cell = attack_order.last_known_cell;

        }

    }



    if (registry.any_of<components::AttackCooldown>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::AttackCooldown);

        record.attack_cooldown_ticks = registry.get<components::AttackCooldown>(entity).ticks_remaining;

    }



    if (registry.any_of<components::GatherTarget>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::GatherTarget);

        record.gather_cell = registry.get<components::GatherTarget>(entity).cell;

    }



    if (registry.any_of<components::WorkerBrain>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::WorkerBrain);

        record.worker_state = registry.get<components::WorkerBrain>(entity).state;

    }



    if (registry.any_of<components::ManualControlTag>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::ManualControl);

    }



    if (registry.any_of<components::MovePath>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::MovePath);

        record.move_path = registry.get<components::MovePath>(entity);

    }



    if (registry.any_of<components::MoveSegment>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::MoveSegment);

        record.move_segment = registry.get<components::MoveSegment>(entity);

    }



    return record;

}



void append_entity_state_record(std::vector<std::byte>& out, const EntityStateRecord& record)

{

    append_entity_snapshot_key(out, record.key);

    append_pod(out, record.grid_cell.x);

    append_pod(out, record.grid_cell.y);

    append_pod(out, record.world_x.raw());

    append_pod(out, record.world_y.raw());

    append_pod(out, record.health_current.raw());

    append_pod(out, record.health_max.raw());

    append_pod(out, record.flags);



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::CarriedWood)) != 0U) {

        append_pod(out, record.carried_wood);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::Stockpile)) != 0U) {

        append_pod(out, record.stockpile_wood);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::AttackOrder)) != 0U) {

        append_entity_snapshot_key(out, record.attack_target_key);

        append_pod(out, record.attack_last_known_cell.x);

        append_pod(out, record.attack_last_known_cell.y);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::AttackCooldown)) != 0U) {

        append_pod(out, record.attack_cooldown_ticks);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::GatherTarget)) != 0U) {

        append_pod(out, record.gather_cell.x);

        append_pod(out, record.gather_cell.y);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::WorkerBrain)) != 0U) {

        append_pod(out, static_cast<std::uint8_t>(record.worker_state));

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::MovePath)) != 0U) {

        append_pod(out, record.move_path.next_index);

        const auto cell_count = static_cast<std::uint16_t>(record.move_path.cells.size());

        append_pod(out, cell_count);

        for (const core::GridPos cell : record.move_path.cells) {

            append_pod(out, cell.x);

            append_pod(out, cell.y);

        }

        append_pod(out, static_cast<std::uint8_t>(record.move_path.has_goal_world ? 1U : 0U));

        if (record.move_path.has_goal_world) {

            append_pod(out, record.move_path.goal_world_x.raw());

            append_pod(out, record.move_path.goal_world_y.raw());

        }

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::MoveSegment)) != 0U) {

        append_pod(out, record.move_segment.from_x.raw());

        append_pod(out, record.move_segment.from_y.raw());

        append_pod(out, record.move_segment.to_x.raw());

        append_pod(out, record.move_segment.to_y.raw());

        append_pod(out, record.move_segment.ticks_elapsed);

        append_pod(out, record.move_segment.ticks_total);

    }

}



[[nodiscard]] std::optional<EntityStateRecord> read_entity_state_record(std::span<const std::byte>& cursor)

{

    EntityStateRecord record{};

    if (!read_entity_snapshot_key(cursor, record.key)) {

        return std::nullopt;

    }



    std::int32_t world_x_raw = 0;
    std::int32_t world_y_raw = 0;
    std::int32_t health_current_raw = 0;
    std::int32_t health_max_raw = 0;

    if (!read_pod(cursor, record.grid_cell.x) || !read_pod(cursor, record.grid_cell.y)

        || !read_pod(cursor, world_x_raw) || !read_pod(cursor, world_y_raw)

        || !read_pod(cursor, health_current_raw) || !read_pod(cursor, health_max_raw)

        || !read_pod(cursor, record.flags)) {

        return std::nullopt;

    }

    record.world_x = math::Fixed::from_raw(world_x_raw);
    record.world_y = math::Fixed::from_raw(world_y_raw);
    record.health_current = math::Fixed::from_raw(health_current_raw);
    record.health_max = math::Fixed::from_raw(health_max_raw);



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::CarriedWood)) != 0U

        && !read_pod(cursor, record.carried_wood)) {

        return std::nullopt;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::Stockpile)) != 0U

        && !read_pod(cursor, record.stockpile_wood)) {

        return std::nullopt;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::AttackOrder)) != 0U

        && !read_entity_snapshot_key(cursor, record.attack_target_key)) {

        return std::nullopt;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::AttackOrder)) != 0U

        && (!read_pod(cursor, record.attack_last_known_cell.x)

            || !read_pod(cursor, record.attack_last_known_cell.y))) {

        return std::nullopt;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::AttackCooldown)) != 0U

        && !read_pod(cursor, record.attack_cooldown_ticks)) {

        return std::nullopt;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::GatherTarget)) != 0U

        && (!read_pod(cursor, record.gather_cell.x) || !read_pod(cursor, record.gather_cell.y))) {

        return std::nullopt;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::WorkerBrain)) != 0U) {

        std::uint8_t worker_state_raw = 0U;

        if (!read_pod(cursor, worker_state_raw)) {

            return std::nullopt;

        }



        record.worker_state = static_cast<components::WorkerState>(worker_state_raw);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::MovePath)) != 0U) {

        std::uint16_t cell_count = 0U;

        if (!read_pod(cursor, record.move_path.next_index) || !read_pod(cursor, cell_count)) {

            return std::nullopt;

        }



        record.move_path.cells.clear();

        record.move_path.cells.reserve(cell_count);

        for (std::uint16_t index = 0U; index < cell_count; ++index) {

            core::GridPos cell{};

            if (!read_pod(cursor, cell.x) || !read_pod(cursor, cell.y)) {

                return std::nullopt;

            }



            record.move_path.cells.push_back(cell);

        }

        std::uint8_t has_goal_world = 0U;

        if (!read_pod(cursor, has_goal_world)) {

            return std::nullopt;

        }

        record.move_path.has_goal_world = has_goal_world != 0U;

        if (record.move_path.has_goal_world) {

            std::int32_t goal_world_x_raw = 0;

            std::int32_t goal_world_y_raw = 0;

            if (!read_pod(cursor, goal_world_x_raw) || !read_pod(cursor, goal_world_y_raw)) {

                return std::nullopt;

            }

            record.move_path.goal_world_x = math::Fixed::from_raw(goal_world_x_raw);

            record.move_path.goal_world_y = math::Fixed::from_raw(goal_world_y_raw);

        }

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::MoveSegment)) != 0U) {

        std::int32_t from_x_raw = 0;
        std::int32_t from_y_raw = 0;
        std::int32_t to_x_raw = 0;
        std::int32_t to_y_raw = 0;

        if (!read_pod(cursor, from_x_raw) || !read_pod(cursor, from_y_raw) || !read_pod(cursor, to_x_raw)

            || !read_pod(cursor, to_y_raw) || !read_pod(cursor, record.move_segment.ticks_elapsed)

            || !read_pod(cursor, record.move_segment.ticks_total)) {

            return std::nullopt;

        }

        record.move_segment.from_x = math::Fixed::from_raw(from_x_raw);
        record.move_segment.from_y = math::Fixed::from_raw(from_y_raw);
        record.move_segment.to_x = math::Fixed::from_raw(to_x_raw);
        record.move_segment.to_y = math::Fixed::from_raw(to_y_raw);

    }



    return record;

}



[[nodiscard]] std::vector<EntityStateRecord> capture_entity_states(entt::registry& registry)

{

    std::vector<entt::entity> entities{};

    for (const entt::entity entity : registry.view<components::GridPosition>()) {

        if (registry.all_of<components::WorldTag>(entity)) {

            continue;

        }



        entities.push_back(entity);

    }



    std::sort(entities.begin(), entities.end());



    std::vector<EntityStateRecord> records{};

    records.reserve(entities.size());

    for (const entt::entity entity : entities) {

        const auto record = capture_entity_state(registry, entity);

        if (record.has_value()) {

            records.push_back(*record);

        }

    }



    std::sort(records.begin(), records.end(), [](const EntityStateRecord& left, const EntityStateRecord& right) {

        if (left.key.player_slot != right.key.player_slot) {

            return left.key.player_slot < right.key.player_slot;

        }



        if (left.key.category != right.key.category) {

            return left.key.category < right.key.category;

        }



        return left.key.ordinal < right.key.ordinal;

    });



    return records;

}



entt::entity ensure_entity_for_record(

    entt::registry& registry,

    const EntityStateRecord& record)

{

    entt::entity entity = snapshot::resolve_entity_snapshot_key(registry, record.key);

    if (entity != entt::null) {

        return entity;

    }



    const entt::entity world = find_world_entity(registry);

    if (world == entt::null) {

        return entt::null;

    }



    const auto& content_pack = registry.get<components::ContentPack>(world);



    switch (record.key.category) {

    case snapshot::EntitySnapshotCategory::Worker: {

        const auto* worker_archetype = data::find_unit_archetype(

            content_pack.content,

            std::string(constants::WORKER_UNIT_ID));

        if (worker_archetype == nullptr) {

            return entt::null;

        }



        return spawn::spawn_player_worker(

            registry,

            *worker_archetype,

            record.grid_cell,

            record.key.player_slot);

    }

    case snapshot::EntitySnapshotCategory::Militia: {

        const auto* militia_archetype = data::find_unit_archetype(

            content_pack.content,

            std::string(constants::MILITIA_UNIT_ID));

        if (militia_archetype == nullptr) {

            return entt::null;

        }



        return spawn::spawn_player_militia(

            registry,

            *militia_archetype,

            record.grid_cell,

            record.key.player_slot);

    }

    case snapshot::EntitySnapshotCategory::TownCenter: {

        const auto* town_center_archetype = data::find_structure_archetype(

            content_pack.content,

            std::string(constants::TOWN_CENTER_BUILDING_ID));

        if (town_center_archetype == nullptr) {

            return entt::null;

        }



        const int starting_wood =

            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::Stockpile)) != 0U

            ? record.stockpile_wood

            : 0;



        return spawn::spawn_player_town_center(

            registry,

            *town_center_archetype,

            record.grid_cell,

            record.key.player_slot,

            starting_wood);

    }

    }



    return entt::null;

}



void destroy_all_grid_entities(entt::registry& registry)

{

    std::vector<entt::entity> entities{};

    for (const entt::entity entity : registry.view<components::GridPosition>()) {

        entities.push_back(entity);

    }



    for (const entt::entity entity : entities) {

        registry.destroy(entity);

    }

}



void clear_optional_components(entt::registry& registry, const entt::entity entity)

{

    registry.remove<components::CarriedWood>(entity);

    registry.remove<components::Stockpile>(entity);

    registry.remove<components::AttackOrder>(entity);

    registry.remove<components::AttackCooldown>(entity);

    registry.remove<components::GatherTarget>(entity);

    registry.remove<components::WorkerBrain>(entity);

    registry.remove<components::ManualControlTag>(entity);

    registry.remove<components::MovePath>(entity);

    registry.remove<components::MoveSegment>(entity);

    registry.remove<components::EntitySnapshotIdentity>(entity);

}



void apply_entity_state_record(entt::registry& registry, const EntityStateRecord& record)

{

    entt::entity entity = ensure_entity_for_record(registry, record);

    if (entity == entt::null) {

        return;

    }



    clear_optional_components(registry, entity);



    registry.get<components::GridPosition>(entity).cell = record.grid_cell;

    if (record.key.category != snapshot::EntitySnapshotCategory::TownCenter) {

        auto& world = registry.get_or_emplace<components::WorldPosition>(entity);

        world.x = record.world_x;

        world.y = record.world_y;

        auto& previous = registry.get_or_emplace<components::PreviousWorldPosition>(entity);

        previous.x = record.world_x;

        previous.y = record.world_y;

    }



    auto& health = registry.get<components::Health>(entity);

    health.current = record.health_current;

    health.max = record.health_max;



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::CarriedWood)) != 0U) {

        registry.get_or_emplace<components::CarriedWood>(entity).amount = record.carried_wood;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::Stockpile)) != 0U) {

        registry.get_or_emplace<components::Stockpile>(entity).wood = record.stockpile_wood;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::AttackCooldown)) != 0U) {

        registry.emplace<components::AttackCooldown>(

            entity,

            components::AttackCooldown{record.attack_cooldown_ticks});

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::GatherTarget)) != 0U) {

        registry.emplace<components::GatherTarget>(entity, components::GatherTarget{record.gather_cell});

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::WorkerBrain)) != 0U) {

        registry.get_or_emplace<components::WorkerBrain>(entity).state = record.worker_state;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::ManualControl)) != 0U) {

        registry.emplace<components::ManualControlTag>(entity);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::MovePath)) != 0U) {

        registry.emplace<components::MovePath>(entity, record.move_path);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::MoveSegment)) != 0U) {

        registry.emplace<components::MoveSegment>(entity, record.move_segment);

    }



    snapshot::set_entity_snapshot_identity(registry, entity, record.key);

}



void apply_entity_attack_orders(entt::registry& registry, const EntityStateRecord& record)

{

    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::AttackOrder)) == 0U) {

        return;

    }



    const entt::entity entity = snapshot::resolve_entity_snapshot_key(registry, record.key);

    if (entity == entt::null) {

        return;

    }



    const entt::entity target = snapshot::resolve_entity_snapshot_key(registry, record.attack_target_key);

    if (target == entt::null) {

        return;

    }



    registry.emplace<components::AttackOrder>(
        entity,
        components::AttackOrder{target, record.attack_last_known_cell});

}



struct DecodedSnapshot {

    SimSnapshot metadata{};

    std::vector<int> forest_wood{};

    std::vector<components::TileType> map_tiles{};

    std::vector<std::uint8_t> fog_explored{};

    std::vector<std::uint8_t> fog_memory_tiles{};

    std::vector<std::int32_t> fog_memory_forest_wood{};

    std::vector<EntityStateRecord> entity_states{};

};



[[nodiscard]] std::optional<DecodedSnapshot> decode_full_snapshot(const std::span<const std::byte> bytes)

{

    std::span<const std::byte> cursor = bytes;



    std::uint32_t magic = 0U;

    std::uint16_t version = 0U;

    if (!read_pod(cursor, magic) || magic != SNAPSHOT_MAGIC || !read_pod(cursor, version)

        || version != SNAPSHOT_VERSION) {

        return std::nullopt;

    }



    DecodedSnapshot decoded{};

    if (!read_pod(cursor, decoded.metadata.tick_count) || !read_pod(cursor, decoded.metadata.state_hash)

        || !read_pod(cursor, decoded.metadata.next_command_sequence)

        || !read_pod(cursor, decoded.metadata.ai_controlled_slots)

        || !read_pod(cursor, decoded.metadata.ai_controlled_since_tick)) {

        return std::nullopt;

    }



    std::uint32_t transition_count = 0U;

    if (!read_pod(cursor, transition_count)) {

        return std::nullopt;

    }



    decoded.metadata.ai_control_transitions.reserve(transition_count);

    for (std::uint32_t index = 0U; index < transition_count; ++index) {

        components::AiControlTransition transition{};

        std::uint8_t enabled_flag = 0U;

        if (!read_pod(cursor, transition.tick) || !read_pod(cursor, transition.player_slot)

            || !read_pod(cursor, enabled_flag)) {

            return std::nullopt;

        }



        transition.enabled = enabled_flag != 0U;

        decoded.metadata.ai_control_transitions.push_back(transition);

    }



    std::uint32_t command_count = 0U;

    if (!read_pod(cursor, command_count)) {

        return std::nullopt;

    }



    decoded.metadata.input_log.reserve(command_count);

    for (std::uint32_t index = 0U; index < command_count; ++index) {

        std::uint32_t command_size = 0U;

        if (!read_pod(cursor, command_size) || cursor.size() < command_size) {

            return std::nullopt;

        }



        const auto decoded_command = decode_snapshot_command(cursor.subspan(0U, command_size));

        if (!decoded_command.has_value()) {

            return std::nullopt;

        }



        decoded.metadata.input_log.push_back(*decoded_command);

        cursor = cursor.subspan(command_size);

    }



    std::uint32_t forest_count = 0U;

    if (!read_pod(cursor, forest_count)) {

        return std::nullopt;

    }



    decoded.forest_wood.resize(forest_count);

    for (std::uint32_t index = 0U; index < forest_count; ++index) {

        if (!read_pod(cursor, decoded.forest_wood[static_cast<std::size_t>(index)])) {

            return std::nullopt;

        }

    }



    std::uint32_t tile_count = 0U;

    if (!read_pod(cursor, tile_count)) {

        return std::nullopt;

    }



    decoded.map_tiles.resize(tile_count);

    for (std::uint32_t index = 0U; index < tile_count; ++index) {

        std::uint8_t tile_raw = 0U;

        if (!read_pod(cursor, tile_raw)) {

            return std::nullopt;

        }



        decoded.map_tiles[static_cast<std::size_t>(index)] =

            static_cast<components::TileType>(tile_raw);

    }



    std::uint32_t fog_explored_count = 0U;

    if (!read_pod(cursor, fog_explored_count)) {

        return std::nullopt;

    }



    decoded.fog_explored.resize(fog_explored_count);

    for (std::uint32_t index = 0U; index < fog_explored_count; ++index) {

        if (!read_pod(cursor, decoded.fog_explored[static_cast<std::size_t>(index)])) {

            return std::nullopt;

        }

    }



    std::uint32_t fog_memory_tile_count = 0U;

    if (!read_pod(cursor, fog_memory_tile_count)) {

        return std::nullopt;

    }



    decoded.fog_memory_tiles.resize(fog_memory_tile_count);

    for (std::uint32_t index = 0U; index < fog_memory_tile_count; ++index) {

        if (!read_pod(cursor, decoded.fog_memory_tiles[static_cast<std::size_t>(index)])) {

            return std::nullopt;

        }

    }



    std::uint32_t fog_memory_forest_count = 0U;

    if (!read_pod(cursor, fog_memory_forest_count)) {

        return std::nullopt;

    }



    decoded.fog_memory_forest_wood.resize(fog_memory_forest_count);

    for (std::uint32_t index = 0U; index < fog_memory_forest_count; ++index) {

        std::int32_t wood = 0;

        if (!read_pod(cursor, wood)) {

            return std::nullopt;

        }

        decoded.fog_memory_forest_wood[static_cast<std::size_t>(index)] = wood;

    }



    std::uint32_t entity_count = 0U;

    if (!read_pod(cursor, entity_count)) {

        return std::nullopt;

    }



    decoded.entity_states.reserve(entity_count);

    for (std::uint32_t index = 0U; index < entity_count; ++index) {

        const auto record = read_entity_state_record(cursor);

        if (!record.has_value()) {

            return std::nullopt;

        }



        decoded.entity_states.push_back(*record);

    }



    if (!cursor.empty()) {

        return std::nullopt;

    }



    return decoded;

}



} // namespace



SimSnapshot Simulation::export_snapshot() const

{

    SimSnapshot snapshot{};

    snapshot.tick_count = tick_count_;

    snapshot.state_hash = state_hash();

    snapshot.input_log = command_queue_.input_log();



    const entt::entity world = find_world_entity(const_cast<entt::registry&>(registry_));

    if (world != entt::null && registry_.any_of<components::MatchSession>(world)) {

        const auto& session = registry_.get<components::MatchSession>(world);

        snapshot.ai_control_transitions = session.ai_control_transitions;

        snapshot.ai_controlled_slots = session.ai_controlled_slots;

        snapshot.ai_controlled_since_tick = session.ai_controlled_since_tick;

    }



    std::uint64_t max_sequence = 0U;

    for (const player::PlayerCommand& command : snapshot.input_log) {

        max_sequence = std::max(max_sequence, command.sequence);

    }



    snapshot.next_command_sequence = max_sequence + 1U;

    return snapshot;

}



bool Simulation::apply_snapshot(const std::span<const std::byte> snapshot_bytes)

{

    return apply_sim_snapshot(*this, snapshot_bytes);

}



std::vector<std::byte> encode_sim_snapshot(const Simulation& simulation)

{

    entt::registry& registry = const_cast<entt::registry&>(simulation.registry());

    const entt::entity world = find_world_entity(registry);

    if (world != entt::null) {

        sync_map_tiles_with_forest_wood(registry.get<components::MapGrid>(world));

    }

    systems::compute_state_hash(registry);



    const SimSnapshot metadata = simulation.export_snapshot();

    if (world == entt::null) {

        return {};

    }



    const auto& map = registry.get<components::MapGrid>(world);

    const std::vector<EntityStateRecord> entity_states = capture_entity_states(registry);



    std::vector<std::byte> out{};

    out.reserve(1024U + metadata.input_log.size() * 64U + entity_states.size() * 96U

        + map.forest_wood.size() * sizeof(int));



    append_pod(out, SNAPSHOT_MAGIC);

    append_pod(out, SNAPSHOT_VERSION);

    append_pod(out, metadata.tick_count);

    append_pod(out, metadata.state_hash);

    append_pod(out, metadata.next_command_sequence);

    append_pod(out, metadata.ai_controlled_slots);

    append_pod(out, metadata.ai_controlled_since_tick);



    const auto transition_count =

        static_cast<std::uint32_t>(metadata.ai_control_transitions.size());

    append_pod(out, transition_count);

    for (const components::AiControlTransition& transition : metadata.ai_control_transitions) {

        append_pod(out, transition.tick);

        append_pod(out, transition.player_slot);

        const std::uint8_t enabled_flag = transition.enabled ? 1U : 0U;

        append_pod(out, enabled_flag);

    }



    const auto command_count = static_cast<std::uint32_t>(metadata.input_log.size());

    append_pod(out, command_count);

    for (const player::PlayerCommand& command : metadata.input_log) {
        player::PlayerCommand snapshot_command = command;
        bool needs_key_annotation = snapshot_command.unit_keys.size() != snapshot_command.unit_ids.size();
        if (!needs_key_annotation
            && (snapshot_command.type == player::PlayerCommandType::Attack
                || snapshot_command.type == player::PlayerCommandType::SpawnWorker)
            && snapshot_command.target_entity != entt::null
            && !snapshot_command.target_entity_key.has_value()) {
            needs_key_annotation = true;
        }

        if (needs_key_annotation) {
            snapshot::annotate_command_entity_keys(
                const_cast<entt::registry&>(simulation.registry()),
                snapshot_command);

            if (snapshot_command.unit_keys.size() != snapshot_command.unit_ids.size()) {
                std::cerr << "snapshot encode: missing entity keys for command sequence "
                          << snapshot_command.sequence << " execute_tick "
                          << snapshot_command.execute_tick << '\n';
                return {};
            }

            if ((snapshot_command.type == player::PlayerCommandType::Attack
                    || snapshot_command.type == player::PlayerCommandType::SpawnWorker)
                && snapshot_command.target_entity != entt::null
                && !snapshot_command.target_entity_key.has_value()) {
                std::cerr << "snapshot encode: missing target entity key for command sequence "
                          << snapshot_command.sequence << " execute_tick "
                          << snapshot_command.execute_tick << '\n';
                return {};
            }

            const std::vector<std::byte> command_bytes = encode_snapshot_command(snapshot_command);
            if (command_bytes.empty()) {
                return {};
            }

            const auto command_size = static_cast<std::uint32_t>(command_bytes.size());
            append_pod(out, command_size);
            out.insert(out.end(), command_bytes.begin(), command_bytes.end());
            continue;
        }

        const std::vector<std::byte> command_bytes = encode_snapshot_command(command);

        if (command_bytes.empty()) {

            std::cerr << "snapshot encode: failed to serialize command sequence " << command.sequence

                      << '\n';

            return {};

        }



        const auto command_size = static_cast<std::uint32_t>(command_bytes.size());

        append_pod(out, command_size);

        out.insert(out.end(), command_bytes.begin(), command_bytes.end());

    }



    const auto forest_count = static_cast<std::uint32_t>(map.forest_wood.size());

    append_pod(out, forest_count);

    for (const int wood : map.forest_wood) {

        append_pod(out, wood);

    }



    const auto tile_count = static_cast<std::uint32_t>(map.tiles.size());

    append_pod(out, tile_count);

    for (const components::TileType tile : map.tiles) {

        append_pod(out, static_cast<std::uint8_t>(tile));

    }



    if (registry.any_of<components::FogOfWarState>(world)) {

        const auto& fog = registry.get<components::FogOfWarState>(world);

        const auto fog_explored_count = static_cast<std::uint32_t>(fog.explored.size());

        append_pod(out, fog_explored_count);

        for (const std::uint8_t explored : fog.explored) {

            append_pod(out, explored);

        }

        const auto fog_memory_tile_count = static_cast<std::uint32_t>(fog.memory_tiles.size());

        append_pod(out, fog_memory_tile_count);

        for (const std::uint8_t tile : fog.memory_tiles) {

            append_pod(out, tile);

        }

        const auto fog_memory_forest_count = static_cast<std::uint32_t>(fog.memory_forest_wood.size());

        append_pod(out, fog_memory_forest_count);

        for (const int wood : fog.memory_forest_wood) {

            append_pod(out, static_cast<std::int32_t>(wood));

        }

    }

    else {

        append_pod(out, static_cast<std::uint32_t>(0U));

        append_pod(out, static_cast<std::uint32_t>(0U));

        append_pod(out, static_cast<std::uint32_t>(0U));

    }



    const auto entity_count = static_cast<std::uint32_t>(entity_states.size());

    append_pod(out, entity_count);

    for (const EntityStateRecord& record : entity_states) {

        append_entity_state_record(out, record);

    }



    return out;

}



std::optional<SimSnapshot> decode_sim_snapshot_metadata(const std::span<const std::byte> bytes)

{

    const auto decoded = decode_full_snapshot(bytes);

    if (!decoded.has_value()) {

        return std::nullopt;

    }



    return decoded->metadata;

}



bool apply_sim_snapshot(Simulation& simulation, const std::span<const std::byte> bytes)

{

    const auto decoded = decode_full_snapshot(bytes);

    if (!decoded.has_value()) {

        return false;

    }



    reset_simulation_to_default_scenario(simulation);

    simulation.restore_command_log(

        decoded->metadata.input_log,

        decoded->metadata.next_command_sequence);



    const entt::entity world = find_world_entity(simulation.registry());

    if (world == entt::null) {

        return false;

    }



    auto& map = simulation.registry().get<components::MapGrid>(world);

    if (decoded->forest_wood.size() != map.forest_wood.size()) {

        std::cerr << "snapshot restore: forest size mismatch\n";

        return false;

    }



    map.forest_wood = decoded->forest_wood;

    if (decoded->map_tiles.size() != map.tiles.size()) {

        std::cerr << "snapshot restore: map tile size mismatch\n";

        return false;

    }



    map.tiles = decoded->map_tiles;

    if (simulation.registry().any_of<components::FogOfWarState>(world)) {
        auto& fog = simulation.registry().get<components::FogOfWarState>(world);
        if (!decoded->fog_explored.empty()) {
            if (decoded->fog_explored.size() != fog.explored.size()) {
                std::cerr << "snapshot restore: fog explored size mismatch\n";
                return false;
            }

            fog.explored = decoded->fog_explored;
            std::fill(fog.visible.begin(), fog.visible.end(), 0U);

            if (!decoded->fog_memory_tiles.empty()) {
                if (decoded->fog_memory_tiles.size() != fog.memory_tiles.size()) {
                    std::cerr << "snapshot restore: fog memory tile size mismatch\n";
                    return false;
                }

                fog.memory_tiles = decoded->fog_memory_tiles;
            }

            if (!decoded->fog_memory_forest_wood.empty()) {
                if (decoded->fog_memory_forest_wood.size() != fog.memory_forest_wood.size()) {
                    std::cerr << "snapshot restore: fog memory forest size mismatch\n";
                    return false;
                }

                fog.memory_forest_wood.assign(
                    decoded->fog_memory_forest_wood.begin(),
                    decoded->fog_memory_forest_wood.end());
            }
        }
    }

    auto& session = simulation.registry().get_or_emplace<components::MatchSession>(world);

    session.ai_controlled_slots = decoded->metadata.ai_controlled_slots;

    session.ai_controlled_since_tick = decoded->metadata.ai_controlled_since_tick;

    session.ai_control_transitions = decoded->metadata.ai_control_transitions;



    destroy_all_grid_entities(simulation.registry());



    for (const EntityStateRecord& record : decoded->entity_states) {

        apply_entity_state_record(simulation.registry(), record);

    }



    for (const EntityStateRecord& record : decoded->entity_states) {

        apply_entity_attack_orders(simulation.registry(), record);

    }

    normalize_registry_for_snapshot(simulation.registry());

    simulation.set_tick_count(decoded->metadata.tick_count);

    systems::compute_state_hash(simulation.registry());



    if (simulation.tick_count() != decoded->metadata.tick_count) {

        return false;

    }



    if (simulation.state_hash() != decoded->metadata.state_hash) {

        std::cerr << "snapshot restore: hash mismatch expected=0x" << std::hex

                  << decoded->metadata.state_hash << " actual=0x" << simulation.state_hash() << std::dec

                  << " entities=" << decoded->entity_states.size() << '\n';

        return false;

    }

    systems::rebuild_fog_visibility(simulation.registry());

    return true;

}



void diagnose_snapshot_roundtrip_failure(

    const Simulation& source,

    const Simulation& restored,

    const std::span<const std::byte> snapshot_bytes)

{

    const auto decoded = decode_full_snapshot(snapshot_bytes);

    if (!decoded.has_value()) {

        std::cerr << "snapshot diagnose: decode failed\n";

        return;

    }



    entt::registry& source_registry = const_cast<entt::registry&>(source.registry());

    entt::registry& restored_registry = const_cast<entt::registry&>(restored.registry());



    std::size_t source_entities = 0U;

    std::size_t source_without_key = 0U;

    for (const entt::entity entity : source_registry.view<components::GridPosition>()) {

        if (source_registry.all_of<components::WorldTag>(entity)) {

            continue;

        }



        ++source_entities;

        if (!snapshot::compute_entity_snapshot_key(source_registry, entity).has_value()) {

            ++source_without_key;

        }

    }



    std::size_t restored_entities = 0U;

    for (const entt::entity entity : restored_registry.view<components::GridPosition>()) {

        if (restored_registry.all_of<components::WorldTag>(entity)) {

            continue;

        }



        ++restored_entities;

    }



    std::cerr << "snapshot diagnose: captured=" << decoded->entity_states.size()

              << " source_grid=" << source_entities << " restored_grid=" << restored_entities

              << " source_no_key=" << source_without_key << " tick=" << decoded->metadata.tick_count

              << '\n';



    const entt::entity source_world = find_world_entity(source_registry);

    const entt::entity restored_world = find_world_entity(restored_registry);

    if (source_world != entt::null && restored_world != entt::null) {

        const auto& source_map = source_registry.get<components::MapGrid>(source_world);

        const auto& restored_map = restored_registry.get<components::MapGrid>(restored_world);

        std::size_t tile_diffs = 0U;

        std::size_t wood_diffs = 0U;

        for (std::size_t index = 0U; index < source_map.forest_wood.size(); ++index) {

            if (source_map.forest_wood[index] != restored_map.forest_wood[index]) {

                ++wood_diffs;

            }



            if (source_map.tiles[index] != restored_map.tiles[index]) {

                ++tile_diffs;

            }

        }



        std::cerr << "snapshot diagnose: map wood_diffs=" << wood_diffs << " tile_diffs=" << tile_diffs

                  << '\n';

    }



    for (const EntityStateRecord& record : decoded->entity_states) {

        const entt::entity source_entity =

            snapshot::resolve_entity_snapshot_key(source_registry, record.key);

        const entt::entity restored_entity =

            snapshot::resolve_entity_snapshot_key(restored_registry, record.key);

        if (source_entity == entt::null || restored_entity == entt::null) {

            std::cerr << "snapshot diagnose: missing entity slot="

                      << static_cast<int>(record.key.player_slot) << " category="

                      << static_cast<int>(record.key.category) << " ordinal=" << record.key.ordinal

                      << " source=" << (source_entity != entt::null)

                      << " restored=" << (restored_entity != entt::null) << '\n';

            continue;

        }



        if (!source_registry.all_of<components::GridPosition>(source_entity)

            || !restored_registry.all_of<components::GridPosition>(restored_entity)) {

            continue;

        }



        const core::GridPos source_cell =

            source_registry.get<components::GridPosition>(source_entity).cell;

        const core::GridPos restored_cell =

            restored_registry.get<components::GridPosition>(restored_entity).cell;

        if (source_cell != restored_cell) {

            std::cerr << "snapshot diagnose: cell mismatch slot="

                      << static_cast<int>(record.key.player_slot) << " ordinal=" << record.key.ordinal

                      << " source=(" << source_cell.x << ',' << source_cell.y << ") restored=("

                      << restored_cell.x << ',' << restored_cell.y << ")\n";

        }

    }



    if (validate_snapshot_input_replay(snapshot_bytes)) {

        std::cerr << "snapshot diagnose: input-log replay hash matches metadata (checkpoint capture bug)\n";

    }

    else {

        std::cerr << "snapshot diagnose: input-log replay hash also mismatches metadata\n";

    }

}



bool validate_snapshot_input_replay(const std::span<const std::byte> snapshot_bytes)

{

    const auto decoded = decode_full_snapshot(snapshot_bytes);

    if (!decoded.has_value()) {

        return false;

    }



    Simulation replay{};

    reset_simulation_to_default_scenario(replay);

    replay.restore_command_log(

        decoded->metadata.input_log,

        decoded->metadata.next_command_sequence);



    while (replay.tick_count() < decoded->metadata.tick_count) {

        replay.tick();

    }



    systems::compute_state_hash(replay.registry());

    return replay.state_hash() == decoded->metadata.state_hash;

}



} // namespace aoa::sim


