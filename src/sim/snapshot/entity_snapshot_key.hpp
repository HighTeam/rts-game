#pragma once

#include "core/grid.hpp"

#include <cstdint>
#include <entt/entt.hpp>
#include <optional>
#include <vector>

namespace aoa::sim::player {

struct PlayerCommand;

} // namespace aoa::sim::player

namespace aoa::sim::snapshot {

enum class EntitySnapshotCategory : std::uint8_t {
    Worker = 0,
    Militia = 1,
    TownCenter = 2,
    House = 3,
    LumberCamp = 4,
    Extractor = 5,
    ManaLake = 6,
    Mill = 7,
    MiningCamp = 8,
    Barracks = 9,
    MageAcademy = 10,
    Tower = 11,
    Market = 12,
    Mage = 13,
    Projectile = 14,
    Garden = 15,
    Reservoir = 16,
    Farm = 17,
};

struct EntitySnapshotKey {
    std::uint8_t player_slot{0U};
    EntitySnapshotCategory category{EntitySnapshotCategory::Worker};
    std::uint16_t ordinal{0U};

    auto operator==(const EntitySnapshotKey& other) const -> bool = default;
};

[[nodiscard]] bool compare_entity_snapshot_keys(
    const EntitySnapshotKey& left,
    const EntitySnapshotKey& right);

void mix_entity_snapshot_key(std::uint64_t& hash, const EntitySnapshotKey& key);

[[nodiscard]] std::optional<EntitySnapshotKey> compute_entity_snapshot_key(
    entt::registry& registry,
    entt::entity entity);

[[nodiscard]] entt::entity resolve_entity_snapshot_key(
    entt::registry& registry,
    EntitySnapshotKey key);

[[nodiscard]] bool compare_entities_for_deterministic_iteration(
    entt::registry& registry,
    entt::entity left,
    entt::entity right);

[[nodiscard]] std::vector<entt::entity> sort_entities_by_snapshot_key(
    entt::registry& registry,
    std::vector<entt::entity> entities);

void annotate_command_entity_keys(entt::registry& registry, player::PlayerCommand& command);

[[nodiscard]] entt::entity find_owned_building_at_cell(
    entt::registry& registry,
    std::uint8_t player_slot,
    core::GridPos cell);

void resolve_command_entity_ids(entt::registry& registry, player::PlayerCommand& command);

void assign_snapshot_identities(entt::registry& registry);

[[nodiscard]] std::uint16_t next_entity_snapshot_ordinal(

    entt::registry& registry,

    const std::uint8_t player_slot,

    const EntitySnapshotCategory category);

void set_entity_snapshot_identity(
    entt::registry& registry,
    const entt::entity entity,
    const EntitySnapshotKey key);

void refresh_unit_sex_from_identity(entt::registry& registry, entt::entity entity);

} // namespace aoa::sim::snapshot
