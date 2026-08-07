#include "sim/snapshot/entity_snapshot_key.hpp"

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

    if (registry.any_of<components::TownCenterTag>(entity)) {
        return EntitySnapshotCategory::TownCenter;
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

} // namespace

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
    registry.emplace_or_replace<components::EntitySnapshotIdentity>(entity, components::EntitySnapshotIdentity{key});
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
    command.unit_keys.clear();
    command.unit_keys.reserve(command.unit_ids.size());

    for (const entt::entity unit : command.unit_ids) {
        const auto key = compute_entity_snapshot_key(registry, unit);
        if (!key.has_value()) {
            command.unit_keys.clear();
            return;
        }

        command.unit_keys.push_back(*key);
    }

    command.target_entity_key.reset();
    if (command.type == player::PlayerCommandType::Attack
        || command.type == player::PlayerCommandType::SpawnWorker) {
        if (command.target_entity != entt::null) {
            command.target_entity_key = compute_entity_snapshot_key(registry, command.target_entity);
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
}

} // namespace aoa::sim::snapshot
