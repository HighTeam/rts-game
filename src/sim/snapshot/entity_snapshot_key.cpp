#include "sim/snapshot/entity_snapshot_key.hpp"

#include "core/constants.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/entity_snapshot_identity.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/tags.hpp"
#include "sim/player/player_command.hpp"

#include <algorithm>
#include <vector>

namespace aoa::sim::snapshot {

namespace {

std::optional<EntitySnapshotCategory> category_for_entity(
    entt::registry& registry,
    const entt::entity entity)
{
    if (registry.any_of<components::WorkerUnitTag>(entity)) {
        return EntitySnapshotCategory::Worker;
    }

    if (registry.any_of<components::MilitiaUnitTag>(entity)) {
        return EntitySnapshotCategory::Militia;
    }

    if (registry.any_of<components::MageUnitTag>(entity)) {
        return EntitySnapshotCategory::Mage;
    }

    if (registry.any_of<components::Projectile>(entity)) {
        return EntitySnapshotCategory::Projectile;
    }

    if (registry.any_of<components::TownCenterTag>(entity)) {
        return EntitySnapshotCategory::TownCenter;
    }

    if (registry.any_of<components::HouseTag>(entity)) {
        return EntitySnapshotCategory::House;
    }

    if (registry.any_of<components::LumberCampTag>(entity)) {
        return EntitySnapshotCategory::LumberCamp;
    }

    if (registry.any_of<components::ExtractorTag>(entity)) {
        return EntitySnapshotCategory::Extractor;
    }

    if (registry.any_of<components::ManaLakeTag>(entity)) {
        return EntitySnapshotCategory::ManaLake;
    }

    if (registry.any_of<components::MillTag>(entity)) {
        return EntitySnapshotCategory::Mill;
    }

    if (registry.any_of<components::MiningCampTag>(entity)) {
        return EntitySnapshotCategory::MiningCamp;
    }

    if (registry.any_of<components::BarracksTag>(entity)) {
        return EntitySnapshotCategory::Barracks;
    }

    if (registry.any_of<components::MageAcademyTag>(entity)) {
        return EntitySnapshotCategory::MageAcademy;
    }

    if (registry.any_of<components::TowerTag>(entity)) {
        return EntitySnapshotCategory::Tower;
    }

    if (registry.any_of<components::MarketTag>(entity)) {
        return EntitySnapshotCategory::Market;
    }

    if (registry.any_of<components::GardenTag>(entity)) {
        return EntitySnapshotCategory::Garden;
    }

    if (registry.any_of<components::ReservoirTag>(entity)) {
        return EntitySnapshotCategory::Reservoir;
    }

    if (registry.any_of<components::FarmTag>(entity)) {
        return EntitySnapshotCategory::Farm;
    }

    return std::nullopt;
}

std::vector<entt::entity> collect_entities_for_key(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const EntitySnapshotCategory category)
{
    std::vector<entt::entity> entities{};

    switch (category) {
    case EntitySnapshotCategory::Worker: {
        const auto view = registry.view<
            components::WorkerUnitTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::Militia: {
        const auto view = registry.view<
            components::MilitiaUnitTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::TownCenter: {
        const auto view = registry.view<
            components::TownCenterTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::House: {
        const auto view = registry.view<
            components::HouseTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::LumberCamp: {
        const auto view = registry.view<
            components::LumberCampTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::Extractor: {
        const auto view = registry.view<
            components::ExtractorTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::ManaLake: {
        const auto view = registry.view<components::ManaLakeTag, components::PlayerSlot>();
        for (const entt::entity entity : view) {
            if (view.get<components::PlayerSlot>(entity).value != player_slot) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::Mill: {
        const auto view = registry.view<
            components::MillTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::MiningCamp: {
        const auto view = registry.view<
            components::MiningCampTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::Barracks: {
        const auto view = registry.view<
            components::BarracksTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::MageAcademy: {
        const auto view = registry.view<
            components::MageAcademyTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::Tower: {
        const auto view = registry.view<
            components::TowerTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::Market: {
        const auto view = registry.view<
            components::MarketTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::Garden: {
        const auto view = registry.view<
            components::GardenTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::Reservoir: {
        const auto view = registry.view<
            components::ReservoirTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::Farm: {
        const auto view = registry.view<
            components::FarmTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::Mage: {
        const auto view = registry.view<
            components::MageUnitTag,
            components::PlayerOwnedTag,
            components::Health>();
        for (const entt::entity entity : view) {
            if (components::entity_player_slot(registry, entity) != player_slot) {
                continue;
            }

            if (view.get<components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    case EntitySnapshotCategory::Projectile: {
        const auto view = registry.view<components::Projectile, components::PlayerSlot>();
        for (const entt::entity entity : view) {
            if (view.get<components::PlayerSlot>(entity).value != player_slot) {
                continue;
            }

            entities.push_back(entity);
        }
        break;
    }
    }

    std::sort(entities.begin(), entities.end(), [](const entt::entity left, const entt::entity right) {
        return static_cast<entt::id_type>(left) < static_cast<entt::id_type>(right);
    });

    return entities;
}

std::optional<EntitySnapshotKey> compute_entity_snapshot_key_from_entt_order(
    entt::registry& registry,
    const entt::entity entity)
{
    if (!registry.valid(entity)) {
        return std::nullopt;
    }

    const auto category = category_for_entity(registry, entity);
    if (!category.has_value()) {
        return std::nullopt;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, entity);
    const std::vector<entt::entity> entities =
        collect_entities_for_key(registry, player_slot, *category);

    for (std::size_t index = 0U; index < entities.size(); ++index) {
        if (entities[index] == entity) {
            return EntitySnapshotKey{
                player_slot,
                *category,
                static_cast<std::uint16_t>(index),
            };
        }
    }

    return std::nullopt;
}

entt::entity resolve_entity_snapshot_key_from_entt_order(
    entt::registry& registry,
    const EntitySnapshotKey key)
{
    const std::vector<entt::entity> entities =
        collect_entities_for_key(registry, key.player_slot, key.category);

    if (key.ordinal >= entities.size()) {
        return entt::null;
    }

    return entities[static_cast<std::size_t>(key.ordinal)];
}

std::optional<EntitySnapshotKey> ensure_entity_snapshot_key(
    entt::registry& registry,
    const entt::entity entity)
{
    const auto existing = compute_entity_snapshot_key(registry, entity);
    if (existing.has_value()) {
        return existing;
    }

    if (!registry.valid(entity)) {
        return std::nullopt;
    }

    const auto category = category_for_entity(registry, entity);
    if (!category.has_value()) {
        return std::nullopt;
    }

    const std::uint8_t player_slot = components::entity_player_slot(registry, entity);
    const EntitySnapshotKey key{
        player_slot,
        *category,
        next_entity_snapshot_ordinal(registry, player_slot, *category),
    };
    set_entity_snapshot_identity(registry, entity, key);
    return key;
}

} // namespace

entt::entity find_owned_building_at_cell(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const core::GridPos cell)
{
    const auto view = registry.view<
        components::BuildingTag,
        components::PlayerOwnedTag,
        components::GridPosition,
        components::Health>();
    for (const entt::entity entity : view) {
        if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        if (view.get<components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        components::BuildingFootprint footprint{1, 1};
        if (registry.any_of<components::BuildingFootprint>(entity)) {
            footprint = registry.get<components::BuildingFootprint>(entity);
        }
        footprint = components::effective_building_footprint(
            footprint, registry.any_of<components::TownCenterTag>(entity));
        if (components::building_contains_cell(
                view.get<components::GridPosition>(entity), footprint, cell)) {
            return entity;
        }
    }

    return entt::null;
}

bool compare_entity_snapshot_keys(const EntitySnapshotKey& left, const EntitySnapshotKey& right)
{
    if (left.player_slot != right.player_slot) {
        return left.player_slot < right.player_slot;
    }

    if (left.category != right.category) {
        return left.category < right.category;
    }

    return left.ordinal < right.ordinal;
}

void mix_entity_snapshot_key(std::uint64_t& hash, const EntitySnapshotKey& key)
{
    const auto mix = [&hash](const std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };

    mix(static_cast<std::uint64_t>(key.player_slot));
    mix(static_cast<std::uint64_t>(static_cast<std::uint8_t>(key.category)));
    mix(static_cast<std::uint64_t>(key.ordinal));
}

std::optional<EntitySnapshotKey> compute_entity_snapshot_key(
    entt::registry& registry,
    const entt::entity entity)
{
    if (!registry.valid(entity)) {
        return std::nullopt;
    }

    if (registry.any_of<components::EntitySnapshotIdentity>(entity)) {
        return registry.get<components::EntitySnapshotIdentity>(entity).key;
    }

    return compute_entity_snapshot_key_from_entt_order(registry, entity);
}

entt::entity resolve_entity_snapshot_key(entt::registry& registry, const EntitySnapshotKey key)
{
    const auto identity_view = registry.view<components::EntitySnapshotIdentity>();
    for (const entt::entity entity : identity_view) {
        if (identity_view.get<components::EntitySnapshotIdentity>(entity).key == key) {
            return entity;
        }
    }

    return resolve_entity_snapshot_key_from_entt_order(registry, key);
}

std::uint16_t next_entity_snapshot_ordinal(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const EntitySnapshotCategory category)
{
    std::uint16_t max_ordinal = 0U;
    bool found = false;

    const auto identity_view = registry.view<components::EntitySnapshotIdentity>();
    for (const entt::entity entity : identity_view) {
        const components::EntitySnapshotIdentity& identity =
            identity_view.get<components::EntitySnapshotIdentity>(entity);
        if (identity.key.player_slot != player_slot || identity.key.category != category) {
            continue;
        }

        found = true;
        max_ordinal = std::max(max_ordinal, identity.key.ordinal);
    }

    if (!found) {
        return 0U;
    }

    return static_cast<std::uint16_t>(max_ordinal + 1U);
}

void set_entity_snapshot_identity(
    entt::registry& registry,
    const entt::entity entity,
    const EntitySnapshotKey key)
{
    registry.emplace_or_replace<components::EntitySnapshotIdentity>(
        entity, components::EntitySnapshotIdentity{key});
    refresh_unit_sex_from_identity(registry, entity);
}

void refresh_unit_sex_from_identity(entt::registry& registry, const entt::entity entity)
{
    if (!registry.any_of<components::UnitTag>(entity)
        || !registry.any_of<components::EntitySnapshotIdentity>(entity)) {
        return;
    }

    const EntitySnapshotKey& key = registry.get<components::EntitySnapshotIdentity>(entity).key;
    std::uint32_t rng = constants::UNIT_SEX_SPAWN_SALT
        ^ (static_cast<std::uint32_t>(key.player_slot) * constants::UNIT_SEX_SPAWN_SLOT_MIX)
        ^ (static_cast<std::uint32_t>(key.category) * constants::UNIT_SEX_SPAWN_CATEGORY_MIX)
        ^ (static_cast<std::uint32_t>(key.ordinal) * constants::UNIT_SEX_SPAWN_ORDINAL_MIX);
    if (rng == 0U) {
        rng = 1U;
    }

    rng ^= rng << 13U;
    rng ^= rng >> 17U;
    rng ^= rng << 5U;
    const constants::UnitSex sex = (rng & 1U) == 0U
        ? constants::UnitSex::Male
        : constants::UnitSex::Female;
    registry.emplace_or_replace<components::UnitSex>(entity, components::UnitSex{sex});
}

void assign_snapshot_identities(entt::registry& registry)
{
    registry.storage<components::EntitySnapshotIdentity>().clear();

    std::vector<entt::entity> entities{};
    for (const entt::entity entity : registry.view<components::GridPosition>()) {
        if (registry.all_of<components::WorldTag>(entity)) {
            continue;
        }

        entities.push_back(entity);
    }

    for (const entt::entity entity : entities) {
        const std::optional<EntitySnapshotKey> key =
            compute_entity_snapshot_key_from_entt_order(registry, entity);
        if (!key.has_value()) {
            continue;
        }

        set_entity_snapshot_identity(registry, entity, *key);
    }
}

bool compare_entities_for_deterministic_iteration(
    entt::registry& registry,
    const entt::entity left,
    const entt::entity right)
{
    const std::optional<EntitySnapshotKey> left_key = compute_entity_snapshot_key(registry, left);
    const std::optional<EntitySnapshotKey> right_key = compute_entity_snapshot_key(registry, right);
    if (left_key.has_value() && right_key.has_value()) {
        if (compare_entity_snapshot_keys(*left_key, *right_key)) {
            return true;
        }

        if (compare_entity_snapshot_keys(*right_key, *left_key)) {
            return false;
        }
    }
    else if (left_key.has_value() != right_key.has_value()) {
        return left_key.has_value();
    }

    if (registry.all_of<components::GridPosition>(left) && registry.all_of<components::GridPosition>(right)) {
        const core::GridPos left_cell = registry.get<components::GridPosition>(left).cell;
        const core::GridPos right_cell = registry.get<components::GridPosition>(right).cell;
        if (left_cell.y != right_cell.y) {
            return left_cell.y < right_cell.y;
        }

        if (left_cell.x != right_cell.x) {
            return left_cell.x < right_cell.x;
        }
    }

    return static_cast<entt::id_type>(left) < static_cast<entt::id_type>(right);
}

std::vector<entt::entity> sort_entities_by_snapshot_key(
    entt::registry& registry,
    std::vector<entt::entity> entities)
{
    std::sort(entities.begin(), entities.end(), [&registry](const entt::entity left, const entt::entity right) {
        return compare_entities_for_deterministic_iteration(registry, left, right);
    });
    return entities;
}

void annotate_command_entity_keys(entt::registry& registry, player::PlayerCommand& command)
{
    const std::vector<EntitySnapshotKey> previous_unit_keys = command.unit_keys;
    command.unit_keys.clear();
    command.unit_keys.reserve(command.unit_ids.size());

    bool units_failed = false;
    for (const entt::entity unit : command.unit_ids) {
        const auto key = ensure_entity_snapshot_key(registry, unit);
        if (!key.has_value()) {
            units_failed = true;
            break;
        }

        command.unit_keys.push_back(*key);
    }

    if (units_failed) {
        if (previous_unit_keys.size() == command.unit_ids.size()) {
            command.unit_keys = previous_unit_keys;
        }
        else {
            command.unit_keys.clear();
        }
    }

    const std::optional<EntitySnapshotKey> previous_target_key = command.target_entity_key;
    if (command.type == player::PlayerCommandType::Attack
        || command.type == player::PlayerCommandType::SpawnWorker
        || command.type == player::PlayerCommandType::SpawnMilitia
        || command.type == player::PlayerCommandType::SpawnMage
        || command.type == player::PlayerCommandType::DestroyBuilding
        || command.type == player::PlayerCommandType::ResumeBuild
        || command.type == player::PlayerCommandType::Garrison
        || command.type == player::PlayerCommandType::UnloadGarrison
        || command.type == player::PlayerCommandType::AdvanceAge
        || command.type == player::PlayerCommandType::RenewFarm
        || command.type == player::PlayerCommandType::ResearchCartography
        || command.type == player::PlayerCommandType::ResearchTrades
        || command.type == player::PlayerCommandType::ResearchSpy
        || command.type == player::PlayerCommandType::MarketSellWood
        || command.type == player::PlayerCommandType::MarketSellFood
        || command.type == player::PlayerCommandType::MarketBuyWood
        || command.type == player::PlayerCommandType::MarketBuyFood) {
        if (command.target_entity != entt::null) {
            const auto key = ensure_entity_snapshot_key(registry, command.target_entity);
            if (key.has_value()) {
                command.target_entity_key = key;
            }
            else if (!previous_target_key.has_value()) {
                command.target_entity_key.reset();
            }
            else {
                command.target_entity_key = previous_target_key;
            }
        }
    }

    if (command.type == player::PlayerCommandType::DestroyBuilding
        && !command.target_entity_key.has_value()) {
        const entt::entity building =
            find_owned_building_at_cell(registry, command.player_slot, command.cell);
        if (building != entt::null) {
            command.target_entity = building;
            command.target_entity_key = ensure_entity_snapshot_key(registry, building);
        }
    }
}

void resolve_command_entity_ids(entt::registry& registry, player::PlayerCommand& command)
{
    if (!command.unit_keys.empty()) {
        command.unit_ids.clear();
        command.unit_ids.reserve(command.unit_keys.size());
        for (const EntitySnapshotKey key : command.unit_keys) {
            const entt::entity unit = resolve_entity_snapshot_key(registry, key);
            if (unit != entt::null) {
                command.unit_ids.push_back(unit);
            }
        }
    }

    if (command.target_entity_key.has_value()) {
        command.target_entity = resolve_entity_snapshot_key(registry, *command.target_entity_key);
    }

    if (command.type == player::PlayerCommandType::DestroyBuilding
        && (!registry.valid(command.target_entity)
            || !registry.any_of<components::BuildingTag>(command.target_entity))) {
        const entt::entity building =
            find_owned_building_at_cell(registry, command.player_slot, command.cell);
        if (building != entt::null) {
            command.target_entity = building;
        }
    }
}

} // namespace aoa::sim::snapshot
