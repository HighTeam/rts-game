#include "sim/spawn/unit_spawn.hpp"

#include "math/fixed.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"

#include <string>

namespace aoa::sim::spawn {

entt::entity spawn_player_worker(
    entt::registry& registry,
    const data::ArchetypeDefinition& worker_archetype,
    const core::GridPos spawn_cell)
{
    const entt::entity entity = registry.create();
    registry.emplace<components::UnitTag>(entity);
    registry.emplace<components::PlayerOwnedTag>(entity);
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

    return entity;
}

} // namespace aoa::sim::spawn
