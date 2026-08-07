#include "sim/spawn/unit_spawn.hpp"

#include "math/fixed.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"

#include <string>

namespace aoa::sim::spawn {

entt::entity spawn_player_worker(
    entt::registry& registry,
    const data::ArchetypeDefinition& worker_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot)
{
    const entt::entity entity = registry.create();
    registry.emplace<components::UnitTag>(entity);
    registry.emplace<components::PlayerOwnedTag>(entity);
    registry.emplace<components::PlayerSlot>(entity, components::PlayerSlot{player_slot});
    registry.emplace<components::WorkerUnitTag>(entity);
    registry.emplace<components::WorkerBrain>(entity);
    registry.emplace<components::CarriedWood>(entity);
    registry.emplace<components::GridPosition>(entity, components::GridPosition{spawn_cell});
    registry.emplace<components::DefinitionRef>(
        entity,
        components::DefinitionRef{std::string(worker_archetype.id)});
    registry.emplace<components::Health>(
        entity,
        components::Health{worker_archetype.max_hp, worker_archetype.max_hp});
    registry.emplace<components::MoveCooldown>(entity);
    registry.emplace<components::AttackCooldown>(entity);
    registry.emplace<components::WorldPosition>(
        entity,
        components::WorldPosition{
            math::tile_center_coord(spawn_cell.x),
            math::tile_center_coord(spawn_cell.y)});
    registry.emplace<components::PreviousWorldPosition>(
        entity,
        components::PreviousWorldPosition{
            math::tile_center_coord(spawn_cell.x),
            math::tile_center_coord(spawn_cell.y)});

    snapshot::set_entity_snapshot_identity(
        registry,
        entity,
        snapshot::EntitySnapshotKey{
            player_slot,
            snapshot::EntitySnapshotCategory::Worker,
            snapshot::next_entity_snapshot_ordinal(
                registry,
                player_slot,
                snapshot::EntitySnapshotCategory::Worker)});

    return entity;
}

entt::entity spawn_player_militia(
    entt::registry& registry,
    const data::ArchetypeDefinition& militia_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot)
{
    const entt::entity entity = registry.create();
    registry.emplace<components::UnitTag>(entity);
    registry.emplace<components::PlayerOwnedTag>(entity);
    registry.emplace<components::PlayerSlot>(entity, components::PlayerSlot{player_slot});
    registry.emplace<components::MilitiaUnitTag>(entity);
    registry.emplace<components::GridPosition>(entity, components::GridPosition{spawn_cell});
    registry.emplace<components::DefinitionRef>(
        entity,
        components::DefinitionRef{std::string(militia_archetype.id)});
    registry.emplace<components::Health>(
        entity,
        components::Health{militia_archetype.max_hp, militia_archetype.max_hp});
    registry.emplace<components::MoveCooldown>(entity);
    registry.emplace<components::AttackCooldown>(entity);
    registry.emplace<components::WorldPosition>(
        entity,
        components::WorldPosition{
            math::tile_center_coord(spawn_cell.x),
            math::tile_center_coord(spawn_cell.y)});
    registry.emplace<components::PreviousWorldPosition>(
        entity,
        components::PreviousWorldPosition{
            math::tile_center_coord(spawn_cell.x),
            math::tile_center_coord(spawn_cell.y)});

    snapshot::set_entity_snapshot_identity(
        registry,
        entity,
        snapshot::EntitySnapshotKey{
            player_slot,
            snapshot::EntitySnapshotCategory::Militia,
            snapshot::next_entity_snapshot_ordinal(
                registry,
                player_slot,
                snapshot::EntitySnapshotCategory::Militia)});

    return entity;
}

entt::entity spawn_player_town_center(
    entt::registry& registry,
    const data::ArchetypeDefinition& town_center_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const int starting_wood)
{
    const entt::entity town_center = registry.create();
    registry.emplace<components::BuildingTag>(town_center);
    registry.emplace<components::TownCenterTag>(town_center);
    registry.emplace<components::PlayerOwnedTag>(town_center);
    registry.emplace<components::PlayerSlot>(town_center, components::PlayerSlot{player_slot});
    registry.emplace<components::GridPosition>(town_center, components::GridPosition{spawn_cell});
    registry.emplace<components::DefinitionRef>(
        town_center,
        components::DefinitionRef{std::string(town_center_archetype.id)});
    registry.emplace<components::Health>(
        town_center,
        components::Health{town_center_archetype.max_hp, town_center_archetype.max_hp});
    registry.emplace<components::Stockpile>(town_center, components::Stockpile{starting_wood});
    snapshot::set_entity_snapshot_identity(
        registry,
        town_center,
        snapshot::EntitySnapshotKey{
            player_slot,
            snapshot::EntitySnapshotCategory::TownCenter,
            snapshot::next_entity_snapshot_ordinal(
                registry,
                player_slot,
                snapshot::EntitySnapshotCategory::TownCenter)});
    return town_center;
}

} // namespace aoa::sim::spawn

