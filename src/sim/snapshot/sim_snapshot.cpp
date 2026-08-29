#include "sim/snapshot/sim_snapshot.hpp"



#include "core/constants.hpp"

#include "data/content_loader.hpp"

#include "data/content_types.hpp"

#include "sim/components/building_process.hpp"

#include "sim/components/combat.hpp"

#include "sim/components/content_pack.hpp"

#include "sim/components/entity_snapshot_identity.hpp"

#include "sim/components/fog_of_war.hpp"
#include "sim/components/grid_position.hpp"

#include "sim/components/health.hpp"

#include "sim/components/map_grid.hpp"

#include "sim/components/map_pings.hpp"

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

constexpr std::uint16_t SNAPSHOT_VERSION = 27U;



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

    GatherCooldown = 1U << 9,

    ManaGenerationCooldown = 1U << 10,

    UnderConstruction = 1U << 11,

    BuildOrder = 1U << 12,

    CarriedFood = 1U << 13,

    CarriedMoney = 1U << 14,

    Garrisoned = 1U << 15,

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

    int carried_food{0};

    int carried_money{0};

    int stockpile_wood{0};

    int stockpile_food{0};

    int stockpile_money{0};

    int stockpile_mana{0};

    int mana_generation_ticks_remaining{0};

    snapshot::EntitySnapshotKey build_target_key{};

    int build_hit_cooldown_ticks{0};

    snapshot::EntitySnapshotKey attack_target_key{};

    core::GridPos attack_last_known_cell{-1, -1};

    int attack_cooldown_ticks{0};

    int gather_cooldown_ticks{0};

    core::GridPos gather_cell{-1, -1};

    std::uint8_t gather_resource_type{0U};

    core::GridPos gather_stand_cell{-1, -1};

    int gather_stand_block_ticks{0};

    components::WorkerState worker_state{components::WorkerState::Idle};

    components::MovePath move_path{};

    components::MoveSegment move_segment{};

    std::uint8_t process_kind{0U};

    std::int32_t process_ticks_remaining{0};

    std::int32_t process_ticks_total{0};

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



    if (category_raw > static_cast<std::uint8_t>(snapshot::EntitySnapshotCategory::Farm)) {

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
        if (index < map.bush_food.size() && map.bush_food[index] > 0) {
            continue;
        }

        if (index < map.mine_money.size() && map.mine_money[index] > 0) {
            continue;
        }

        if (map.forest_wood[index] > 0) {
            map.tiles[index] = components::TileType::Forest;
        }
    }
    map.layer_hash_valid = false;
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



    if (!registry.all_of<components::GridPosition>(entity)) {

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

    // Mana lakes are indestructible nature entities and carry no Health component.
    if (const auto* health = registry.try_get<components::Health>(entity); health != nullptr) {

        record.health_current = health->current;

        record.health_max = health->max;

    }



    if (registry.any_of<components::CarriedWood>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::CarriedWood);

        record.carried_wood = registry.get<components::CarriedWood>(entity).amount;

    }



    if (registry.any_of<components::CarriedFood>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::CarriedFood);

        record.carried_food = registry.get<components::CarriedFood>(entity).amount;

    }
    else if (registry.any_of<components::FarmFood>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::CarriedFood);

        record.carried_food = registry.get<components::FarmFood>(entity).remaining;

    }



    if (registry.any_of<components::CarriedMoney>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::CarriedMoney);

        record.carried_money = registry.get<components::CarriedMoney>(entity).amount;

    }



    if (registry.any_of<components::Stockpile>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::Stockpile);

        const auto& stockpile = registry.get<components::Stockpile>(entity);

        record.stockpile_wood = stockpile.wood;

        record.stockpile_food = stockpile.food;

        record.stockpile_money = stockpile.money;

        record.stockpile_mana = stockpile.mana;

    }



    if (registry.any_of<components::ManaGenerationCooldown>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::ManaGenerationCooldown);

        record.mana_generation_ticks_remaining =
            registry.get<components::ManaGenerationCooldown>(entity).ticks_remaining;

    }



    if (registry.any_of<components::UnderConstructionTag>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction);

    }



    if (registry.any_of<components::BuildOrder>(entity)) {

        const entt::entity target = registry.get<components::BuildOrder>(entity).building;

        const auto target_key = snapshot::compute_entity_snapshot_key(registry, target);

        if (target_key.has_value()) {

            record.flags |= static_cast<std::uint16_t>(EntityStateFlags::BuildOrder);

            record.build_target_key = *target_key;

            record.build_hit_cooldown_ticks =
                registry.get<components::BuildOrder>(entity).hit_cooldown_ticks;

        }

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

    if (registry.any_of<components::Projectile>(entity)) {
        const auto& projectile = registry.get<components::Projectile>(entity);
        record.carried_wood = projectile.pierce_damage;
        record.carried_food = projectile.is_arrow ? 1 : 0;
        record.carried_money = static_cast<int>(projectile.reveal_to_slot);
        if (projectile.target != entt::null) {
            const auto target_key = snapshot::compute_entity_snapshot_key(registry, projectile.target);
            if (target_key.has_value()) {
                record.flags |= static_cast<std::uint16_t>(EntityStateFlags::AttackOrder);
                record.attack_target_key = *target_key;
            }
        }
    }

    if (registry.any_of<components::GarrisonedTag>(entity)) {
        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::Garrisoned);
        const entt::entity building = registry.get<components::GarrisonedTag>(entity).building;
        const auto building_key = snapshot::compute_entity_snapshot_key(registry, building);
        if (building_key.has_value()) {
            record.attack_target_key = *building_key;
        }
    }



    if (registry.any_of<components::GatherCooldown>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::GatherCooldown);

        record.gather_cooldown_ticks = registry.get<components::GatherCooldown>(entity).ticks_remaining;

    }



    if (registry.any_of<components::GatherTarget>(entity)) {

        record.flags |= static_cast<std::uint16_t>(EntityStateFlags::GatherTarget);

        const auto& gather_target = registry.get<components::GatherTarget>(entity);

        record.gather_cell = gather_target.cell;

        record.gather_resource_type = static_cast<std::uint8_t>(gather_target.resource_type);

        record.gather_stand_cell = gather_target.stand_cell;

        record.gather_stand_block_ticks = gather_target.stand_block_ticks;

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



    if (registry.any_of<components::BuildingProcess>(entity)) {

        const auto& process = registry.get<components::BuildingProcess>(entity);

        record.process_kind = static_cast<std::uint8_t>(process.kind);

        record.process_ticks_remaining = process.ticks_remaining;

        record.process_ticks_total = process.ticks_total;

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



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::CarriedFood)) != 0U) {

        append_pod(out, record.carried_food);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::CarriedMoney)) != 0U) {

        append_pod(out, record.carried_money);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::Stockpile)) != 0U) {

        append_pod(out, record.stockpile_wood);

        append_pod(out, record.stockpile_food);

        append_pod(out, record.stockpile_money);

        append_pod(out, record.stockpile_mana);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::ManaGenerationCooldown)) != 0U) {

        append_pod(out, record.mana_generation_ticks_remaining);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::BuildOrder)) != 0U) {

        append_entity_snapshot_key(out, record.build_target_key);

        append_pod(out, record.build_hit_cooldown_ticks);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::AttackOrder)) != 0U) {

        append_entity_snapshot_key(out, record.attack_target_key);

        append_pod(out, record.attack_last_known_cell.x);

        append_pod(out, record.attack_last_known_cell.y);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::AttackCooldown)) != 0U) {

        append_pod(out, record.attack_cooldown_ticks);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::GatherCooldown)) != 0U) {

        append_pod(out, record.gather_cooldown_ticks);

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::GatherTarget)) != 0U) {

        append_pod(out, record.gather_cell.x);

        append_pod(out, record.gather_cell.y);

        append_pod(out, record.gather_resource_type);

        append_pod(out, record.gather_stand_cell.x);

        append_pod(out, record.gather_stand_cell.y);

        append_pod(out, record.gather_stand_block_ticks);

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

        append_pod(out, record.move_segment.blocked_ticks);

    }

    append_pod(out, record.process_kind);

    append_pod(out, record.process_ticks_remaining);

    append_pod(out, record.process_ticks_total);

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



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::CarriedFood)) != 0U

        && !read_pod(cursor, record.carried_food)) {

        return std::nullopt;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::CarriedMoney)) != 0U

        && !read_pod(cursor, record.carried_money)) {

        return std::nullopt;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::Stockpile)) != 0U

        && (!read_pod(cursor, record.stockpile_wood) || !read_pod(cursor, record.stockpile_food)
            || !read_pod(cursor, record.stockpile_money) || !read_pod(cursor, record.stockpile_mana))) {

        return std::nullopt;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::ManaGenerationCooldown)) != 0U

        && !read_pod(cursor, record.mana_generation_ticks_remaining)) {

        return std::nullopt;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::BuildOrder)) != 0U

        && !read_entity_snapshot_key(cursor, record.build_target_key)) {

        return std::nullopt;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::BuildOrder)) != 0U

        && !read_pod(cursor, record.build_hit_cooldown_ticks)) {

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



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::GatherCooldown)) != 0U

        && !read_pod(cursor, record.gather_cooldown_ticks)) {

        return std::nullopt;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::GatherTarget)) != 0U

        && (!read_pod(cursor, record.gather_cell.x) || !read_pod(cursor, record.gather_cell.y)

            || !read_pod(cursor, record.gather_resource_type)

            || !read_pod(cursor, record.gather_stand_cell.x)

            || !read_pod(cursor, record.gather_stand_cell.y)

            || !read_pod(cursor, record.gather_stand_block_ticks))) {

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

        if (record.move_path.next_index < 0
            || record.move_path.next_index > static_cast<int>(cell_count)) {

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

            || !read_pod(cursor, record.move_segment.ticks_total)

            || !read_pod(cursor, record.move_segment.blocked_ticks)) {

            return std::nullopt;

        }

        record.move_segment.from_x = math::Fixed::from_raw(from_x_raw);
        record.move_segment.from_y = math::Fixed::from_raw(from_y_raw);
        record.move_segment.to_x = math::Fixed::from_raw(to_x_raw);
        record.move_segment.to_y = math::Fixed::from_raw(to_y_raw);

    }



    if (!read_pod(cursor, record.process_kind)
        || !read_pod(cursor, record.process_ticks_remaining)
        || !read_pod(cursor, record.process_ticks_total)) {

        return std::nullopt;

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



        const bool under_construction =
            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U;

        return spawn::spawn_player_town_center(
            registry,
            *town_center_archetype,
            record.grid_cell,
            record.key.player_slot,
            components::Stockpile{},
            under_construction);

    }

    case snapshot::EntitySnapshotCategory::House: {

        const auto* house_archetype = data::find_structure_archetype(

            content_pack.content,

            std::string(constants::HOUSE_BUILDING_ID));

        if (house_archetype == nullptr) {

            return entt::null;

        }



        const bool under_construction =
            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U;

        return spawn::spawn_player_house(

            registry,

            *house_archetype,

            record.grid_cell,

            record.key.player_slot,

            under_construction);

    }

    case snapshot::EntitySnapshotCategory::LumberCamp: {

        const auto* lumber_camp_archetype = data::find_structure_archetype(

            content_pack.content,

            std::string(constants::LUMBER_CAMP_BUILDING_ID));

        if (lumber_camp_archetype == nullptr) {

            return entt::null;

        }



        const bool under_construction =
            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U;

        return spawn::spawn_player_lumber_camp(

            registry,

            *lumber_camp_archetype,

            record.grid_cell,

            record.key.player_slot,

            under_construction);

    }

    case snapshot::EntitySnapshotCategory::Extractor: {

        const auto* extractor_archetype = data::find_structure_archetype(

            content_pack.content,

            std::string(constants::EXTRACTOR_BUILDING_ID));

        if (extractor_archetype == nullptr) {

            return entt::null;

        }



        const bool under_construction =
            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U;

        return spawn::spawn_player_extractor(

            registry,

            *extractor_archetype,

            record.grid_cell,

            record.key.player_slot,

            under_construction);

    }

    case snapshot::EntitySnapshotCategory::Mill: {
        const auto* mill_archetype = data::find_structure_archetype(
            content_pack.content,
            std::string(constants::MILL_BUILDING_ID));
        if (mill_archetype == nullptr) {
            return entt::null;
        }

        const bool under_construction =
            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U;
        return spawn::spawn_player_mill(
            registry,
            *mill_archetype,
            record.grid_cell,
            record.key.player_slot,
            under_construction);
    }
    case snapshot::EntitySnapshotCategory::MiningCamp: {
        const auto* mining_camp_archetype = data::find_structure_archetype(
            content_pack.content,
            std::string(constants::MINING_CAMP_BUILDING_ID));
        if (mining_camp_archetype == nullptr) {
            return entt::null;
        }

        const bool under_construction =
            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U;
        return spawn::spawn_player_mining_camp(
            registry,
            *mining_camp_archetype,
            record.grid_cell,
            record.key.player_slot,
            under_construction);
    }
    case snapshot::EntitySnapshotCategory::Barracks: {
        const auto* barracks_archetype = data::find_structure_archetype(
            content_pack.content,
            std::string(constants::BARRACKS_BUILDING_ID));
        if (barracks_archetype == nullptr) {
            return entt::null;
        }

        const bool under_construction =
            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U;
        return spawn::spawn_player_barracks(
            registry,
            *barracks_archetype,
            record.grid_cell,
            record.key.player_slot,
            under_construction);
    }
    case snapshot::EntitySnapshotCategory::MageAcademy: {
        const auto* mage_academy_archetype = data::find_structure_archetype(
            content_pack.content,
            std::string(constants::MAGE_ACADEMY_BUILDING_ID));
        if (mage_academy_archetype == nullptr) {
            return entt::null;
        }

        const bool under_construction =
            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U;
        return spawn::spawn_player_mage_academy(
            registry,
            *mage_academy_archetype,
            record.grid_cell,
            record.key.player_slot,
            under_construction);
    }
    case snapshot::EntitySnapshotCategory::Tower: {
        const auto* tower_archetype = data::find_structure_archetype(
            content_pack.content,
            std::string(constants::TOWER_BUILDING_ID));
        if (tower_archetype == nullptr) {
            return entt::null;
        }

        const bool under_construction =
            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U;
        return spawn::spawn_player_tower(
            registry,
            *tower_archetype,
            record.grid_cell,
            record.key.player_slot,
            under_construction);
    }
    case snapshot::EntitySnapshotCategory::Market: {
        const auto* market_archetype = data::find_structure_archetype(
            content_pack.content,
            std::string(constants::MARKET_BUILDING_ID));
        if (market_archetype == nullptr) {
            return entt::null;
        }

        const bool under_construction =
            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U;
        return spawn::spawn_player_market(
            registry,
            *market_archetype,
            record.grid_cell,
            record.key.player_slot,
            under_construction);
    }
    case snapshot::EntitySnapshotCategory::Garden: {
        const auto* garden_archetype = data::find_structure_archetype(
            content_pack.content,
            std::string(constants::GARDEN_BUILDING_ID));
        if (garden_archetype == nullptr) {
            return entt::null;
        }

        const bool under_construction =
            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U;
        return spawn::spawn_player_garden(
            registry,
            *garden_archetype,
            record.grid_cell,
            record.key.player_slot,
            under_construction);
    }
    case snapshot::EntitySnapshotCategory::Reservoir: {
        const auto* reservoir_archetype = data::find_structure_archetype(
            content_pack.content,
            std::string(constants::RESERVOIR_BUILDING_ID));
        if (reservoir_archetype == nullptr) {
            return entt::null;
        }

        const bool under_construction =
            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U;
        return spawn::spawn_player_reservoir(
            registry,
            *reservoir_archetype,
            record.grid_cell,
            record.key.player_slot,
            under_construction);
    }
    case snapshot::EntitySnapshotCategory::Farm: {
        const auto* farm_archetype = data::find_structure_archetype(
            content_pack.content,
            std::string(constants::FARM_BUILDING_ID));
        if (farm_archetype == nullptr) {
            return entt::null;
        }

        const bool under_construction =
            (record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U;
        return spawn::spawn_player_farm(
            registry,
            *farm_archetype,
            record.grid_cell,
            record.key.player_slot,
            under_construction);
    }
    case snapshot::EntitySnapshotCategory::Mage: {
        const auto* mage_archetype = data::find_unit_archetype(
            content_pack.content,
            std::string(constants::MAGE_UNIT_ID));
        if (mage_archetype == nullptr) {
            return entt::null;
        }

        return spawn::spawn_player_mage(
            registry,
            *mage_archetype,
            record.grid_cell,
            record.key.player_slot);
    }
    case snapshot::EntitySnapshotCategory::Projectile: {
        return spawn::spawn_rock_projectile(
            registry,
            record.world_x,
            record.world_y,
            entt::null,
            record.key.player_slot,
            record.carried_wood);
    }
    case snapshot::EntitySnapshotCategory::ManaLake: {

        const auto* mana_lake_archetype = data::find_structure_archetype(

            content_pack.content,

            std::string(constants::MANA_LAKE_BUILDING_ID));

        if (mana_lake_archetype == nullptr) {

            return entt::null;

        }



        return spawn::spawn_mana_lake(

            registry,

            *mana_lake_archetype,

            record.grid_cell,

            record.key.player_slot);

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

    registry.remove<components::CarriedFood>(entity);

    registry.remove<components::FarmFood>(entity);

    registry.remove<components::CarriedMoney>(entity);

    registry.remove<components::Stockpile>(entity);

    registry.remove<components::ManaGenerationCooldown>(entity);

    registry.remove<components::UnderConstructionTag>(entity);

    registry.remove<components::BuildOrder>(entity);

    registry.remove<components::AttackOrder>(entity);

    registry.remove<components::AttackCooldown>(entity);

    registry.remove<components::GatherCooldown>(entity);

    registry.remove<components::GatherTarget>(entity);

    registry.remove<components::WorkerBrain>(entity);

    registry.remove<components::ManualControlTag>(entity);

    registry.remove<components::GarrisonedTag>(entity);

    registry.remove<components::GarrisonOrder>(entity);

    registry.remove<components::MovePath>(entity);

    registry.remove<components::MoveSegment>(entity);

    registry.remove<components::BuildingProcess>(entity);

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

    if (record.key.category != snapshot::EntitySnapshotCategory::TownCenter
        && record.key.category != snapshot::EntitySnapshotCategory::House
        && record.key.category != snapshot::EntitySnapshotCategory::LumberCamp
        && record.key.category != snapshot::EntitySnapshotCategory::Extractor
        && record.key.category != snapshot::EntitySnapshotCategory::ManaLake
        && record.key.category != snapshot::EntitySnapshotCategory::Mill
        && record.key.category != snapshot::EntitySnapshotCategory::MiningCamp
        && record.key.category != snapshot::EntitySnapshotCategory::Barracks
        && record.key.category != snapshot::EntitySnapshotCategory::MageAcademy
        && record.key.category != snapshot::EntitySnapshotCategory::Tower
        && record.key.category != snapshot::EntitySnapshotCategory::Market
        && record.key.category != snapshot::EntitySnapshotCategory::Garden
        && record.key.category != snapshot::EntitySnapshotCategory::Reservoir
        && record.key.category != snapshot::EntitySnapshotCategory::Farm) {

        auto& world = registry.get_or_emplace<components::WorldPosition>(entity);

        world.x = record.world_x;

        world.y = record.world_y;

        auto& previous = registry.get_or_emplace<components::PreviousWorldPosition>(entity);

        previous.x = record.world_x;

        previous.y = record.world_y;

    }



    if (auto* health = registry.try_get<components::Health>(entity); health != nullptr) {

        health->current = record.health_current;

        health->max = record.health_max;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::CarriedWood)) != 0U) {

        registry.get_or_emplace<components::CarriedWood>(entity).amount = record.carried_wood;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::CarriedFood)) != 0U) {
        if (record.key.category == snapshot::EntitySnapshotCategory::Farm) {
            auto& farm_food = registry.get_or_emplace<components::FarmFood>(entity);
            farm_food.remaining = record.carried_food;
            farm_food.max = constants::FARM_FOOD_AMOUNT;
        }
        else {
            registry.get_or_emplace<components::CarriedFood>(entity).amount = record.carried_food;
        }
    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::CarriedMoney)) != 0U) {

        registry.get_or_emplace<components::CarriedMoney>(entity).amount = record.carried_money;

    }
    else if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::CarriedWood)) != 0U
        || (record.flags & static_cast<std::uint16_t>(EntityStateFlags::CarriedFood)) != 0U) {

        registry.get_or_emplace<components::CarriedMoney>(entity).amount = 0;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::Stockpile)) != 0U) {

        auto& stockpile = registry.get_or_emplace<components::Stockpile>(entity);

        stockpile.wood = record.stockpile_wood;

        stockpile.food = record.stockpile_food;

        stockpile.money = record.stockpile_money;

        stockpile.mana = record.stockpile_mana;

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::ManaGenerationCooldown)) != 0U) {

        registry.emplace<components::ManaGenerationCooldown>(

            entity,

            components::ManaGenerationCooldown{record.mana_generation_ticks_remaining});

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::UnderConstruction)) != 0U) {

        registry.emplace<components::UnderConstructionTag>(entity);

    }



    // BuildOrder targets (House/TC) are restored after Workers — apply in a later pass.

    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::AttackCooldown)) != 0U) {

        registry.emplace<components::AttackCooldown>(

            entity,

            components::AttackCooldown{record.attack_cooldown_ticks});

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::GatherCooldown)) != 0U) {

        registry.emplace<components::GatherCooldown>(

            entity,

            components::GatherCooldown{record.gather_cooldown_ticks});

    }



    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::GatherTarget)) != 0U) {

        registry.emplace<components::GatherTarget>(

            entity,

            components::GatherTarget{

                record.gather_cell,

                static_cast<components::TileType>(record.gather_resource_type),

                record.gather_stand_cell,

                record.gather_stand_block_ticks});

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



    if (record.process_kind != 0U && record.process_ticks_total > 0) {

        registry.emplace<components::BuildingProcess>(
            entity,
            components::BuildingProcess{
                static_cast<components::BuildingProcessKind>(record.process_kind),
                record.process_ticks_remaining,
                record.process_ticks_total});

    }



    snapshot::set_entity_snapshot_identity(registry, entity, record.key);

}



void apply_entity_attack_orders(entt::registry& registry, const EntityStateRecord& record)

{

    const entt::entity entity = snapshot::resolve_entity_snapshot_key(registry, record.key);

    if (entity == entt::null) {

        return;

    }

    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::Garrisoned)) != 0U) {
        const entt::entity building =
            snapshot::resolve_entity_snapshot_key(registry, record.attack_target_key);
        if (building != entt::null) {
            registry.emplace_or_replace<components::GarrisonedTag>(
                entity,
                components::GarrisonedTag{building});
            if (registry.any_of<components::GarrisonHold>(building)) {
                auto& hold = registry.get<components::GarrisonHold>(building);
                if (std::find(hold.units.begin(), hold.units.end(), entity) == hold.units.end()) {
                    hold.units.push_back(entity);
                }
            }
        }
    }

    if (registry.any_of<components::Projectile>(entity)) {
        auto& projectile = registry.get<components::Projectile>(entity);
        projectile.pierce_damage = record.carried_wood;
        projectile.is_arrow = record.carried_food != 0;
        projectile.reveal_to_slot = static_cast<std::uint8_t>(
            std::clamp(record.carried_money, 0, 255));
        if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::AttackOrder)) != 0U) {
            projectile.target =
                snapshot::resolve_entity_snapshot_key(registry, record.attack_target_key);
        }
        return;
    }

    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::AttackOrder)) == 0U) {

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



void apply_entity_build_orders(entt::registry& registry, const EntityStateRecord& record)

{

    if ((record.flags & static_cast<std::uint16_t>(EntityStateFlags::BuildOrder)) == 0U) {

        return;

    }



    const entt::entity entity = snapshot::resolve_entity_snapshot_key(registry, record.key);

    if (entity == entt::null) {

        return;

    }



    const entt::entity target = snapshot::resolve_entity_snapshot_key(registry, record.build_target_key);

    if (target == entt::null) {

        return;

    }



    registry.emplace<components::BuildOrder>(
        entity,
        components::BuildOrder{target, record.build_hit_cooldown_ticks});

}



struct DecodedSnapshot {

    SimSnapshot metadata{};

    int map_width{0};

    int map_height{0};

    std::vector<int> forest_wood{};

    std::vector<int> bush_food{};

    std::vector<int> mine_money{};

    std::vector<components::TileType> map_tiles{};

    std::vector<components::GroundType> map_ground{};

    std::vector<std::uint8_t> fog_explored{};

    std::vector<std::uint8_t> fog_memory_tiles{};

    std::vector<std::int32_t> fog_memory_forest_wood{};

    std::vector<std::int32_t> fog_memory_bush_food{};

    std::vector<std::int32_t> fog_memory_mine_money{};

    std::vector<components::MapPing> map_pings{};

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

        || !read_pod(cursor, decoded.metadata.ai_controlled_since_tick)

        || !read_pod(cursor, decoded.metadata.civil_population_map_cap)

        || !read_pod(cursor, decoded.metadata.fog_of_war_enabled)) {

        return std::nullopt;

    }

    for (std::uint8_t& age : decoded.metadata.player_ages) {
        if (!read_pod(cursor, age)) {
            return std::nullopt;
        }
    }

    for (std::uint8_t& civ : decoded.metadata.player_civilizations) {
        if (!read_pod(cursor, civ)) {
            return std::nullopt;
        }
    }

    for (std::uint8_t& side : decoded.metadata.player_side_indices) {
        if (!read_pod(cursor, side)) {
            return std::nullopt;
        }
    }

    for (std::uint8_t& cartography : decoded.metadata.player_cartography) {
        if (!read_pod(cursor, cartography)) {
            return std::nullopt;
        }
    }

    for (std::uint8_t& spy : decoded.metadata.player_spy) {
        if (!read_pod(cursor, spy)) {
            return std::nullopt;
        }
    }

    for (std::uint8_t& trades : decoded.metadata.player_trades) {
        if (!read_pod(cursor, trades)) {
            return std::nullopt;
        }
    }

    for (std::uint8_t& built_mill : decoded.metadata.player_built_mill) {
        if (!read_pod(cursor, built_mill)) {
            return std::nullopt;
        }
    }

    for (std::uint8_t& mask : decoded.metadata.player_ally_mask) {
        if (!read_pod(cursor, mask)) {
            return std::nullopt;
        }
    }

    for (std::uint8_t& victory : decoded.metadata.player_ally_victory) {
        if (!read_pod(cursor, victory)) {
            return std::nullopt;
        }
    }

    if (!read_pod(cursor, decoded.metadata.block_team_changes)) {
        return std::nullopt;
    }

    if (!read_pod(cursor, decoded.metadata.playing_slots_mask)
        || !read_pod(cursor, decoded.metadata.eliminated_slots_mask)
        || !read_pod(cursor, decoded.metadata.match_finished)
        || !read_pod(cursor, decoded.metadata.winner_slot)
        || !read_pod(cursor, decoded.metadata.last_eliminating_slot)
        || !read_pod(cursor, decoded.metadata.finished_tick)) {
        return std::nullopt;
    }

    for (auto& stockpile : decoded.metadata.player_stockpiles) {
        if (!read_pod(cursor, stockpile.wood) || !read_pod(cursor, stockpile.food)
            || !read_pod(cursor, stockpile.money) || !read_pod(cursor, stockpile.mana)) {
            return std::nullopt;
        }
    }

    for (auto& stats : decoded.metadata.player_stats) {
        if (!read_pod(cursor, stats.units_created) || !read_pod(cursor, stats.units_lost)
            || !read_pod(cursor, stats.units_killed) || !read_pod(cursor, stats.buildings_created)
            || !read_pod(cursor, stats.buildings_lost)
            || !read_pod(cursor, stats.buildings_destroyed) || !read_pod(cursor, stats.wood_collected)
            || !read_pod(cursor, stats.food_collected) || !read_pod(cursor, stats.money_collected)
            || !read_pod(cursor, stats.mana_collected) || !read_pod(cursor, stats.trades_sent)
            || !read_pod(cursor, stats.trades_received)) {
            return std::nullopt;
        }
    }

    std::uint32_t flare_count = 0U;
    if (!read_pod(cursor, flare_count)) {
        return std::nullopt;
    }
    decoded.metadata.attack_reveal_flares.reserve(flare_count);
    for (std::uint32_t index = 0U; index < flare_count; ++index) {
        components::AttackRevealFlare flare{};
        if (!read_pod(cursor, flare.x) || !read_pod(cursor, flare.y) || !read_pod(cursor, flare.width)
            || !read_pod(cursor, flare.height) || !read_pod(cursor, flare.viewer_slot)
            || !read_pod(cursor, flare.ticks_remaining)) {
            return std::nullopt;
        }
        decoded.metadata.attack_reveal_flares.push_back(flare);
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



    std::int32_t map_width = 0;

    std::int32_t map_height = 0;

    if (!read_pod(cursor, map_width) || !read_pod(cursor, map_height) || map_width <= 0

        || map_height <= 0) {

        return std::nullopt;

    }

    decoded.map_width = map_width;

    decoded.map_height = map_height;



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



    std::uint32_t bush_count = 0U;

    if (!read_pod(cursor, bush_count)) {

        return std::nullopt;

    }



    decoded.bush_food.resize(bush_count);

    for (std::uint32_t index = 0U; index < bush_count; ++index) {

        if (!read_pod(cursor, decoded.bush_food[static_cast<std::size_t>(index)])) {

            return std::nullopt;

        }

    }



    std::uint32_t mine_count = 0U;

    if (!read_pod(cursor, mine_count)) {

        return std::nullopt;

    }



    decoded.mine_money.resize(mine_count);

    for (std::uint32_t index = 0U; index < mine_count; ++index) {

        if (!read_pod(cursor, decoded.mine_money[static_cast<std::size_t>(index)])) {

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



    std::uint32_t ground_count = 0U;

    if (!read_pod(cursor, ground_count)) {

        return std::nullopt;

    }



    decoded.map_ground.resize(ground_count);

    for (std::uint32_t index = 0U; index < ground_count; ++index) {

        std::uint8_t ground_raw = 0U;

        if (!read_pod(cursor, ground_raw)) {

            return std::nullopt;

        }



        decoded.map_ground[static_cast<std::size_t>(index)] =

            static_cast<components::GroundType>(ground_raw);

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



    std::uint32_t fog_memory_bush_count = 0U;

    if (!read_pod(cursor, fog_memory_bush_count)) {

        return std::nullopt;

    }



    decoded.fog_memory_bush_food.resize(fog_memory_bush_count);

    for (std::uint32_t index = 0U; index < fog_memory_bush_count; ++index) {

        std::int32_t food = 0;

        if (!read_pod(cursor, food)) {

            return std::nullopt;

        }

        decoded.fog_memory_bush_food[static_cast<std::size_t>(index)] = food;

    }



    std::uint32_t fog_memory_mine_count = 0U;

    if (!read_pod(cursor, fog_memory_mine_count)) {

        return std::nullopt;

    }



    decoded.fog_memory_mine_money.resize(fog_memory_mine_count);

    for (std::uint32_t index = 0U; index < fog_memory_mine_count; ++index) {

        std::int32_t money = 0;

        if (!read_pod(cursor, money)) {

            return std::nullopt;

        }

        decoded.fog_memory_mine_money[static_cast<std::size_t>(index)] = money;

    }



    std::uint32_t ping_count = 0U;

    if (!read_pod(cursor, ping_count)) {

        return std::nullopt;

    }

    decoded.map_pings.reserve(ping_count);

    for (std::uint32_t index = 0U; index < ping_count; ++index) {

        components::MapPing ping{};

        if (!read_pod(cursor, ping.cell.x) || !read_pod(cursor, ping.cell.y)
            || !read_pod(cursor, ping.player_slot) || !read_pod(cursor, ping.ticks_remaining)) {

            return std::nullopt;

        }

        decoded.map_pings.push_back(ping);

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

        snapshot.civil_population_map_cap = session.civil_population_map_cap;

        snapshot.fog_of_war_enabled = session.fog_of_war_enabled ? 1U : 0U;

        snapshot.player_ages = session.player_ages;

        snapshot.player_civilizations = session.player_civilizations;

        snapshot.player_side_indices = session.player_side_indices;

        snapshot.player_cartography = session.player_cartography;
        snapshot.player_spy = session.player_spy;
        snapshot.player_trades = session.player_trades;
        snapshot.player_built_mill = session.player_built_mill;
        snapshot.player_ally_mask = session.player_ally_mask;
        snapshot.player_ally_victory = session.player_ally_victory;
        snapshot.block_team_changes = session.block_team_changes ? 1U : 0U;
        snapshot.playing_slots_mask = session.playing_slots_mask;
        snapshot.eliminated_slots_mask = session.eliminated_slots_mask;
        snapshot.match_finished = session.match_finished ? 1U : 0U;
        snapshot.winner_slot = session.winner_slot;
        snapshot.last_eliminating_slot = session.last_eliminating_slot;
        snapshot.finished_tick = session.finished_tick;
        snapshot.player_stockpiles = session.player_stockpiles;
        snapshot.player_stats = session.player_stats;
        snapshot.attack_reveal_flares = session.attack_reveal_flares;

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



std::vector<std::byte> encode_sim_snapshot(
    const Simulation& simulation,
    const bool include_input_log,
    const bool include_pending_commands)

{

    entt::registry& registry = const_cast<entt::registry&>(simulation.registry());

    const entt::entity world = find_world_entity(registry);

    if (world != entt::null) {

        auto& map = registry.get<components::MapGrid>(world);

        sync_map_tiles_with_forest_wood(map);

        map.layer_hash_valid = false;

        if (registry.any_of<components::FogOfWarState>(world)) {

            registry.get<components::FogOfWarState>(world).hash_valid = false;

        }

    }

    systems::compute_state_hash(registry);



    SimSnapshot metadata = simulation.export_snapshot();
    bool write_commands = include_input_log;
    if (include_pending_commands) {
        metadata.input_log = simulation.command_queue().unapplied_commands(simulation.tick_count());
        std::uint64_t max_sequence = 0U;
        for (const player::PlayerCommand& command : metadata.input_log) {
            max_sequence = std::max(max_sequence, command.sequence);
        }
        metadata.next_command_sequence = max_sequence + 1U;
        write_commands = true;
    }

    if (world == entt::null) {

        return {};

    }



    const auto& map = registry.get<components::MapGrid>(world);

    const std::vector<EntityStateRecord> entity_states = capture_entity_states(registry);



    std::vector<std::byte> out{};

    out.reserve(1024U + metadata.input_log.size() * 64U + entity_states.size() * 96U

        + map.forest_wood.size() * sizeof(int) + map.bush_food.size() * sizeof(int)
        + map.mine_money.size() * sizeof(int));



    append_pod(out, SNAPSHOT_MAGIC);

    append_pod(out, SNAPSHOT_VERSION);

    append_pod(out, metadata.tick_count);

    append_pod(out, metadata.state_hash);

    append_pod(out, metadata.next_command_sequence);

    append_pod(out, metadata.ai_controlled_slots);

    append_pod(out, metadata.ai_controlled_since_tick);

    append_pod(out, metadata.civil_population_map_cap);

    append_pod(out, metadata.fog_of_war_enabled);

    for (const std::uint8_t age : metadata.player_ages) {
        append_pod(out, age);
    }

    for (const std::uint8_t civ : metadata.player_civilizations) {
        append_pod(out, civ);
    }

    for (const std::uint8_t side : metadata.player_side_indices) {
        append_pod(out, side);
    }

    for (const std::uint8_t cartography : metadata.player_cartography) {
        append_pod(out, cartography);
    }

    for (const std::uint8_t spy : metadata.player_spy) {
        append_pod(out, spy);
    }

    for (const std::uint8_t trades : metadata.player_trades) {
        append_pod(out, trades);
    }

    for (const std::uint8_t built_mill : metadata.player_built_mill) {
        append_pod(out, built_mill);
    }

    for (const std::uint8_t mask : metadata.player_ally_mask) {
        append_pod(out, mask);
    }

    for (const std::uint8_t victory : metadata.player_ally_victory) {
        append_pod(out, victory);
    }

    append_pod(out, metadata.block_team_changes);
    append_pod(out, metadata.playing_slots_mask);
    append_pod(out, metadata.eliminated_slots_mask);
    append_pod(out, metadata.match_finished);
    append_pod(out, metadata.winner_slot);
    append_pod(out, metadata.last_eliminating_slot);
    append_pod(out, metadata.finished_tick);

    for (const auto& stockpile : metadata.player_stockpiles) {
        append_pod(out, stockpile.wood);
        append_pod(out, stockpile.food);
        append_pod(out, stockpile.money);
        append_pod(out, stockpile.mana);
    }

    for (const auto& stats : metadata.player_stats) {
        append_pod(out, stats.units_created);
        append_pod(out, stats.units_lost);
        append_pod(out, stats.units_killed);
        append_pod(out, stats.buildings_created);
        append_pod(out, stats.buildings_lost);
        append_pod(out, stats.buildings_destroyed);
        append_pod(out, stats.wood_collected);
        append_pod(out, stats.food_collected);
        append_pod(out, stats.money_collected);
        append_pod(out, stats.mana_collected);
        append_pod(out, stats.trades_sent);
        append_pod(out, stats.trades_received);
    }

    const auto flare_count = static_cast<std::uint32_t>(metadata.attack_reveal_flares.size());
    append_pod(out, flare_count);
    for (const auto& flare : metadata.attack_reveal_flares) {
        append_pod(out, flare.x);
        append_pod(out, flare.y);
        append_pod(out, flare.width);
        append_pod(out, flare.height);
        append_pod(out, flare.viewer_slot);
        append_pod(out, flare.ticks_remaining);
    }



    const auto transition_count =

        static_cast<std::uint32_t>(metadata.ai_control_transitions.size());

    append_pod(out, transition_count);

    for (const components::AiControlTransition& transition : metadata.ai_control_transitions) {

        append_pod(out, transition.tick);

        append_pod(out, transition.player_slot);

        const std::uint8_t enabled_flag = transition.enabled ? 1U : 0U;

        append_pod(out, enabled_flag);

    }



    const auto command_count = write_commands
        ? static_cast<std::uint32_t>(metadata.input_log.size())
        : 0U;

    append_pod(out, command_count);

    if (write_commands) {
    for (const player::PlayerCommand& command : metadata.input_log) {
        player::PlayerCommand snapshot_command = command;
        bool needs_key_annotation = snapshot_command.unit_keys.size() != snapshot_command.unit_ids.size();
        if (!needs_key_annotation
            && (snapshot_command.type == player::PlayerCommandType::Attack
                || snapshot_command.type == player::PlayerCommandType::SpawnWorker
                || snapshot_command.type == player::PlayerCommandType::SpawnMilitia)
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
                    || snapshot_command.type == player::PlayerCommandType::SpawnWorker
                || snapshot_command.type == player::PlayerCommandType::SpawnMilitia)
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
    }



    const auto forest_count = static_cast<std::uint32_t>(map.forest_wood.size());

    append_pod(out, static_cast<std::int32_t>(map.width));

    append_pod(out, static_cast<std::int32_t>(map.height));

    append_pod(out, forest_count);

    for (const int wood : map.forest_wood) {

        append_pod(out, wood);

    }



    const auto bush_count = static_cast<std::uint32_t>(map.bush_food.size());

    append_pod(out, bush_count);

    for (const int food : map.bush_food) {

        append_pod(out, food);

    }



    const auto mine_count = static_cast<std::uint32_t>(map.mine_money.size());

    append_pod(out, mine_count);

    for (const int money : map.mine_money) {

        append_pod(out, money);

    }



    const auto tile_count = static_cast<std::uint32_t>(map.tiles.size());

    append_pod(out, tile_count);

    for (const components::TileType tile : map.tiles) {

        append_pod(out, static_cast<std::uint8_t>(tile));

    }



    const auto ground_count = static_cast<std::uint32_t>(map.ground.size());

    append_pod(out, ground_count);

    for (const components::GroundType ground : map.ground) {

        append_pod(out, static_cast<std::uint8_t>(ground));

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

        const auto fog_memory_bush_count = static_cast<std::uint32_t>(fog.memory_bush_food.size());

        append_pod(out, fog_memory_bush_count);

        for (const int food : fog.memory_bush_food) {

            append_pod(out, static_cast<std::int32_t>(food));

        }

        const auto fog_memory_mine_count = static_cast<std::uint32_t>(fog.memory_mine_money.size());

        append_pod(out, fog_memory_mine_count);

        for (const int money : fog.memory_mine_money) {

            append_pod(out, static_cast<std::int32_t>(money));

        }

    }

    else {

        append_pod(out, static_cast<std::uint32_t>(0U));

        append_pod(out, static_cast<std::uint32_t>(0U));

        append_pod(out, static_cast<std::uint32_t>(0U));

        append_pod(out, static_cast<std::uint32_t>(0U));

        append_pod(out, static_cast<std::uint32_t>(0U));

    }



    const auto ping_count = registry.any_of<components::MapPingList>(world)
        ? static_cast<std::uint32_t>(registry.get<components::MapPingList>(world).pings.size())
        : 0U;
    append_pod(out, ping_count);
    if (ping_count > 0U) {
        for (const components::MapPing& ping : registry.get<components::MapPingList>(world).pings) {
            append_pod(out, ping.cell.x);
            append_pod(out, ping.cell.y);
            append_pod(out, ping.player_slot);
            append_pod(out, ping.ticks_remaining);
        }
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

    const entt::entity world = find_world_entity(simulation.registry());

    if (world == entt::null) {

        return false;

    }



    auto& map = simulation.registry().get<components::MapGrid>(world);

    const std::size_t cell_count = static_cast<std::size_t>(decoded->map_width)
        * static_cast<std::size_t>(decoded->map_height);
    if (decoded->map_width <= 0 || decoded->map_height <= 0
        || decoded->forest_wood.size() != cell_count || decoded->map_tiles.size() != cell_count) {
        std::cerr << "snapshot restore: map dimension mismatch\n";
        return false;
    }

    if (decoded->bush_food.size() != cell_count || decoded->mine_money.size() != cell_count
        || decoded->map_ground.size() != cell_count) {
        std::cerr << "snapshot restore: map layer size mismatch\n";
        return false;
    }

    map.width = decoded->map_width;
    map.height = decoded->map_height;
    map.forest_wood = decoded->forest_wood;
    map.bush_food = decoded->bush_food;
    map.mine_money = decoded->mine_money;
    map.tiles = decoded->map_tiles;
    map.ground = decoded->map_ground;
    map.layer_hash_valid = false;

    systems::initialize_fog_of_war(simulation.registry());

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

            if (!decoded->fog_memory_bush_food.empty()) {
                if (decoded->fog_memory_bush_food.size() != fog.memory_bush_food.size()) {
                    std::cerr << "snapshot restore: fog memory bush size mismatch\n";
                    return false;
                }

                fog.memory_bush_food.assign(
                    decoded->fog_memory_bush_food.begin(),
                    decoded->fog_memory_bush_food.end());
            }

            if (fog.memory_mine_money.size() != fog.memory_bush_food.size()) {
                fog.memory_mine_money.assign(fog.memory_bush_food.size(), 0);
            }

            if (!decoded->fog_memory_mine_money.empty()) {
                if (decoded->fog_memory_mine_money.size() != fog.memory_mine_money.size()) {
                    std::cerr << "snapshot restore: fog memory mine size mismatch\n";
                    return false;
                }

                fog.memory_mine_money.assign(
                    decoded->fog_memory_mine_money.begin(),
                    decoded->fog_memory_mine_money.end());
            }
        }

        fog.hash_valid = false;
    }

    auto& session = simulation.registry().get_or_emplace<components::MatchSession>(world);

    session.ai_controlled_slots = decoded->metadata.ai_controlled_slots;

    session.ai_controlled_since_tick = decoded->metadata.ai_controlled_since_tick;

    session.civil_population_map_cap = decoded->metadata.civil_population_map_cap;

    session.fog_of_war_enabled = decoded->metadata.fog_of_war_enabled != 0U;

    session.player_ages = decoded->metadata.player_ages;

    session.player_civilizations = decoded->metadata.player_civilizations;

    session.player_side_indices = decoded->metadata.player_side_indices;

    session.player_cartography = decoded->metadata.player_cartography;
    session.player_spy = decoded->metadata.player_spy;
    session.player_trades = decoded->metadata.player_trades;
    session.player_built_mill = decoded->metadata.player_built_mill;
    session.player_ally_mask = decoded->metadata.player_ally_mask;
    session.player_ally_victory = decoded->metadata.player_ally_victory;
    session.block_team_changes = decoded->metadata.block_team_changes != 0U;
    session.playing_slots_mask = decoded->metadata.playing_slots_mask;
    session.eliminated_slots_mask = decoded->metadata.eliminated_slots_mask;
    session.match_finished = decoded->metadata.match_finished != 0U;
    session.winner_slot = decoded->metadata.winner_slot;
    session.last_eliminating_slot = decoded->metadata.last_eliminating_slot;
    session.finished_tick = decoded->metadata.finished_tick;
    session.player_stockpiles = decoded->metadata.player_stockpiles;
    session.player_stats = decoded->metadata.player_stats;
    session.attack_reveal_flares = decoded->metadata.attack_reveal_flares;

    session.ai_control_transitions = decoded->metadata.ai_control_transitions;

    auto& map_pings = simulation.registry().get_or_emplace<components::MapPingList>(world);
    map_pings.pings = decoded->map_pings;



    destroy_all_grid_entities(simulation.registry());



    for (const EntityStateRecord& record : decoded->entity_states) {

        apply_entity_state_record(simulation.registry(), record);

    }



    for (const EntityStateRecord& record : decoded->entity_states) {

        apply_entity_attack_orders(simulation.registry(), record);

    }



    for (const EntityStateRecord& record : decoded->entity_states) {

        apply_entity_build_orders(simulation.registry(), record);

    }

    // Lakes restore after extractors, so extractor lake refs are only valid now.
    spawn::relink_extractor_mana_lakes(simulation.registry());

    normalize_registry_for_snapshot(simulation.registry());

    // spawn_* used above calls note_unit/building_created. Restore the snapshot
    // counters so reconnect hash matches the host that encoded this state.
    session.player_stats = decoded->metadata.player_stats;
    session.player_stockpiles = decoded->metadata.player_stockpiles;
    session.player_built_mill = decoded->metadata.player_built_mill;

    simulation.set_tick_count(decoded->metadata.tick_count);

    simulation.restore_command_log(
        decoded->metadata.input_log,
        decoded->metadata.next_command_sequence);

    systems::compute_state_hash(simulation.registry());



    if (simulation.tick_count() != decoded->metadata.tick_count) {

        return false;

    }



    if (simulation.state_hash() != decoded->metadata.state_hash) {

        std::cerr << "snapshot restore: hash mismatch expected=0x" << std::hex

                  << decoded->metadata.state_hash << " actual=0x" << simulation.state_hash() << std::dec

                  << " entities=" << decoded->entity_states.size()

                  << " — keeping restored host state\n";

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


