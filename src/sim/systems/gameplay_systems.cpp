#include "sim/systems/gameplay_systems.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/content_pack.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/systems/pathfinding.hpp"

#include "math/fixed.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace aoa::sim::systems {

namespace {

entt::entity find_world_entity(entt::registry& registry)
{
    const auto view = registry.view<components::WorldTag>();
    if (view.begin() == view.end()) {
        return entt::null;
    }

    return *view.begin();
}

const data::ArchetypeDefinition* find_unit_archetype_from_ref(
    const components::ContentPack& content_pack,
    const components::DefinitionRef& definition_ref)
{
    return data::find_unit_archetype(content_pack.content, definition_ref.id);
}

void deplete_forest_tile(components::MapGrid& map, const core::GridPos cell)
{
    if (!core::is_inside_grid(cell, map.width, map.height)) {
        return;
    }

    const int index = core::grid_index(cell, map.width);
    map.forest_wood[static_cast<std::size_t>(index)] = 0;
    map.tiles[static_cast<std::size_t>(index)] = components::TileType::Grass;
}

bool is_occupied(entt::registry& registry, const core::GridPos cell, const entt::entity ignore)
{
    return is_movement_blocked(registry, cell, ignore);
}

bool forest_tile_has_wood(const components::MapGrid& map, const core::GridPos cell)
{
    if (!core::is_inside_grid(cell, map.width, map.height)) {
        return false;
    }

    const int index = core::grid_index(cell, map.width);
    if (map.tiles[static_cast<std::size_t>(index)] != components::TileType::Forest) {
        return false;
    }

    return map.forest_wood[static_cast<std::size_t>(index)] > 0;
}

core::GridPos find_nearest_forest_with_wood(
    const components::MapGrid& map,
    const core::GridPos from)
{
    core::GridPos best{-1, -1};
    int best_distance = std::numeric_limits<int>::max();

    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const int index = core::grid_index({x, y}, map.width);
            if (map.forest_wood[static_cast<std::size_t>(index)] <= 0) {
                continue;
            }

            const int distance = std::abs(from.x - x) + std::abs(from.y - y);
            if (distance < best_distance) {
                best_distance = distance;
                best = {x, y};
            }
        }
    }

    return best;
}

core::GridPos find_adjacent_walkable(
    const components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos target,
    const entt::entity ignore)
{
    const std::array<core::GridPos, 8> offsets = {
        core::GridPos{0, -1},
        core::GridPos{1, 0},
        core::GridPos{0, 1},
        core::GridPos{-1, 0},
        core::GridPos{1, -1},
        core::GridPos{1, 1},
        core::GridPos{-1, 1},
        core::GridPos{-1, -1},
    };

    for (const core::GridPos offset : offsets) {
        const core::GridPos candidate{target.x + offset.x, target.y + offset.y};
        if (!is_tile_walkable(map, candidate, false)) {
            continue;
        }

        if (!is_occupied(registry, candidate, ignore)) {
            return candidate;
        }
    }

    return target;
}

void sync_grid_from_world(entt::registry& registry, const entt::entity entity);

void assign_path(
    entt::registry& registry,
    const entt::entity entity,
    const components::MapGrid& map,
    const core::GridPos goal)
{
    if (registry.all_of<components::WorldPosition, components::GridPosition>(entity)) {
        sync_grid_from_world(registry, entity);
    }

    const auto& pos = registry.get<components::GridPosition>(entity).cell;
    auto& path = registry.get_or_emplace<components::MovePath>(entity);
    path.cells = find_path(map, pos, goal, registry, entity, false);
    path.next_index = 0;
    registry.remove<components::MoveSegment>(entity);
}

bool is_next_path_step_blocked(entt::registry& registry, const entt::entity entity)
{
    if (!registry.any_of<components::MovePath>(entity)) {
        return false;
    }

    const auto& path = registry.get<components::MovePath>(entity);
    if (path.next_index >= static_cast<int>(path.cells.size())) {
        return false;
    }

    const core::GridPos next_cell = path.cells[static_cast<std::size_t>(path.next_index)];
    return is_occupied(registry, next_cell, entity);
}

void replan_path_if_blocked(
    entt::registry& registry,
    const entt::entity entity,
    const components::MapGrid& map)
{
    if (!registry.any_of<components::MovePath>(entity) || registry.any_of<components::MoveSegment>(entity)) {
        return;
    }

    if (!is_next_path_step_blocked(registry, entity)) {
        return;
    }

    const auto& path = registry.get<components::MovePath>(entity);
    if (path.cells.empty()) {
        return;
    }

    const core::GridPos goal = path.cells.back();
    assign_path(registry, entity, map, goal);
}

bool reassign_worker_forest_target(
    entt::registry& registry,
    const entt::entity worker,
    components::MapGrid& map,
    const core::GridPos worker_pos)
{
    const core::GridPos next_forest = find_nearest_forest_with_wood(map, worker_pos);
    if (next_forest.x < 0) {
        registry.remove<components::GatherTarget>(worker);
        registry.get<components::WorkerBrain>(worker).state = components::WorkerState::Idle;
        return false;
    }

    registry.get_or_emplace<components::GatherTarget>(worker).cell = next_forest;
    registry.get<components::WorkerBrain>(worker).state = components::WorkerState::MovingToResource;
    const core::GridPos stand_tile = find_adjacent_walkable(map, registry, next_forest, worker);
    assign_path(registry, worker, map, stand_tile);
    return true;
}

void sync_grid_from_world(entt::registry& registry, const entt::entity entity)
{
    if (!registry.all_of<components::WorldPosition, components::GridPosition>(entity)) {
        return;
    }

    const auto& world = registry.get<components::WorldPosition>(entity);
    auto& grid = registry.get<components::GridPosition>(entity);
    grid.cell.x = world.x.to_int();
    grid.cell.y = world.y.to_int();
}

void snapshot_previous_world_positions(entt::registry& registry)
{
    const auto view = registry.view<components::WorldPosition>();
    for (const entt::entity entity : view) {
        const auto& world = view.get<components::WorldPosition>(entity);
        registry.get_or_emplace<components::PreviousWorldPosition>(entity).x = world.x;
        registry.get_or_emplace<components::PreviousWorldPosition>(entity).y = world.y;
    }
}

void begin_move_segment(
    entt::registry& registry,
    const entt::entity entity,
    const core::GridPos next_cell,
    const int move_ticks_per_tile)
{
    auto& world = registry.get<components::WorldPosition>(entity);
    auto& segment = registry.get_or_emplace<components::MoveSegment>(entity);
    segment.from_x = world.x;
    segment.from_y = world.y;
    segment.to_x = math::tile_center_coord(next_cell.x);
    segment.to_y = math::tile_center_coord(next_cell.y);
    segment.ticks_elapsed = 0;
    segment.ticks_total = math::compute_move_segment_ticks(
        segment.from_x,
        segment.from_y,
        segment.to_x,
        segment.to_y,
        move_ticks_per_tile);
}

bool try_begin_next_path_step(
    entt::registry& registry,
    const entt::entity entity,
    const int move_ticks_per_tile)
{
    if (registry.any_of<components::MoveSegment>(entity)) {
        return false;
    }

    if (!registry.any_of<components::MovePath>(entity)) {
        return false;
    }

    auto& path = registry.get<components::MovePath>(entity);
    if (path.next_index >= static_cast<int>(path.cells.size())) {
        registry.remove<components::MovePath>(entity);
        return false;
    }

    const core::GridPos next_cell = path.cells[static_cast<std::size_t>(path.next_index)];
    if (is_occupied(registry, next_cell, entity)) {
        return false;
    }

    begin_move_segment(registry, entity, next_cell, move_ticks_per_tile);
    return true;
}

void advance_move_segment(
    entt::registry& registry,
    const entt::entity entity,
    const int move_ticks_per_tile)
{
    auto& segment = registry.get<components::MoveSegment>(entity);
    if (segment.ticks_total <= 0) {
        registry.remove<components::MoveSegment>(entity);
        return;
    }

    ++segment.ticks_elapsed;
    if (segment.ticks_elapsed > segment.ticks_total) {
        segment.ticks_elapsed = segment.ticks_total;
    }

    const math::Fixed progress = math::Fixed::from_int(segment.ticks_elapsed)
        / math::Fixed::from_int(segment.ticks_total);
    auto& world = registry.get<components::WorldPosition>(entity);
    world.x = math::fixed_lerp(segment.from_x, segment.to_x, progress);
    world.y = math::fixed_lerp(segment.from_y, segment.to_y, progress);

    if (segment.ticks_elapsed < segment.ticks_total) {
        return;
    }

    world.x = segment.to_x;
    world.y = segment.to_y;
    sync_grid_from_world(registry, entity);
    registry.remove<components::MoveSegment>(entity);

    if (registry.any_of<components::MovePath>(entity)) {
        auto& path = registry.get<components::MovePath>(entity);
        ++path.next_index;
        if (path.next_index >= static_cast<int>(path.cells.size())) {
            registry.remove<components::MovePath>(entity);
        }
    }

    if (!try_begin_next_path_step(registry, entity, move_ticks_per_tile)) {
        return;
    }
}

void run_worker_system(entt::registry& registry, components::MapGrid& map, const components::ContentPack& content)
{
    const auto view = registry.view<
        components::WorkerUnitTag,
        components::GridPosition,
        components::WorkerBrain,
        components::CarriedWood,
        components::DefinitionRef,
        components::Health>();

    for (const entt::entity worker : view) {
        const bool manual = registry.any_of<components::ManualControlTag>(worker);
        const bool has_gather_target = registry.any_of<components::GatherTarget>(worker);

        auto& brain = view.get<components::WorkerBrain>(worker);
        auto& carried = view.get<components::CarriedWood>(worker);
        const auto& definition_ref = view.get<components::DefinitionRef>(worker);
        const auto* definition = find_unit_archetype_from_ref(content, definition_ref);
        if (definition == nullptr) {
            continue;
        }

        const auto& worker_pos = view.get<components::GridPosition>(worker).cell;

        entt::entity town_center = entt::null;
        const auto town_center_view = registry.view<components::TownCenterTag, components::GridPosition>();
        if (town_center_view.begin() != town_center_view.end()) {
            town_center = *town_center_view.begin();
        }

        if (town_center == entt::null) {
            continue;
        }

        const core::GridPos depot_pos = registry.get<components::GridPosition>(town_center).cell;
        const bool should_auto_deposit = !manual || has_gather_target;

        if (carried.amount >= definition->carry_capacity && should_auto_deposit) {
            brain.state = components::WorkerState::MovingToDeposit;
            const core::GridPos stand_tile = find_adjacent_walkable(map, registry, depot_pos, worker);
            if (core::chebyshev_distance(worker_pos, depot_pos) <= 1) {
                brain.state = components::WorkerState::Depositing;
            }
            else if (!registry.any_of<components::MovePath>(worker)) {
                assign_path(registry, worker, map, stand_tile);
            }
        }
        else if (manual && !has_gather_target) {
            continue;
        }
        else if (carried.amount < definition->carry_capacity) {
            core::GridPos forest{-1, -1};
            if (manual && has_gather_target) {
                forest = registry.get<components::GatherTarget>(worker).cell;
                if (!forest_tile_has_wood(map, forest)) {
                    if (!reassign_worker_forest_target(registry, worker, map, worker_pos)) {
                        continue;
                    }

                    forest = registry.get<components::GatherTarget>(worker).cell;
                }
            }
            else {
                forest = find_nearest_forest_with_wood(map, worker_pos);
            }

            if (forest.x < 0) {
                continue;
            }

            if (worker_pos == forest) {
                brain.state = components::WorkerState::Gathering;
            }
            else if (core::chebyshev_distance(worker_pos, forest) == 1) {
                brain.state = components::WorkerState::Gathering;
            }
            else {
                brain.state = components::WorkerState::MovingToResource;
                if (!registry.any_of<components::MovePath>(worker)) {
                    const core::GridPos stand_tile = find_adjacent_walkable(map, registry, forest, worker);
                    assign_path(registry, worker, map, stand_tile);
                }
            }
        }

        if (brain.state == components::WorkerState::Gathering && carried.amount < definition->carry_capacity) {
            for (int y = 0; y < map.height; ++y) {
                for (int x = 0; x < map.width; ++x) {
                    if (core::chebyshev_distance(worker_pos, core::GridPos{x, y}) != 1) {
                        continue;
                    }

                    const int index = core::grid_index({x, y}, map.width);
                    auto& wood = map.forest_wood[static_cast<std::size_t>(index)];
                    if (wood <= 0) {
                        continue;
                    }

                    const int gather_amount = std::min(
                        definition->gather_per_tick,
                        definition->carry_capacity - carried.amount);
                    if (gather_amount <= 0) {
                        break;
                    }

                    wood -= gather_amount;
                    carried.amount += gather_amount;
                    if (wood <= 0) {
                        deplete_forest_tile(map, {x, y});
                        if (registry.any_of<components::GatherTarget>(worker)
                            && registry.get<components::GatherTarget>(worker).cell == core::GridPos{x, y}) {
                            reassign_worker_forest_target(registry, worker, map, worker_pos);
                        }
                    }

                    if (carried.amount >= definition->carry_capacity) {
                        registry.remove<components::MovePath>(worker);
                    }
                    break;
                }
            }
        }

        if (brain.state == components::WorkerState::Depositing) {
            auto& stockpile = registry.get<components::Stockpile>(town_center);
            stockpile.wood += carried.amount;
            carried.amount = 0;
            registry.remove<components::MovePath>(worker);
            brain.state = components::WorkerState::Idle;
        }
    }
}

void run_attack_chase_system(entt::registry& registry, const components::MapGrid& map)
{
    const auto view = registry.view<
        components::UnitTag,
        components::GridPosition,
        components::Health,
        components::AttackOrder>();

    std::vector<entt::entity> attackers{};
    for (const entt::entity attacker : view) {
        attackers.push_back(attacker);
    }

    std::sort(attackers.begin(), attackers.end(), [](const entt::entity left, const entt::entity right) {
        return static_cast<entt::id_type>(left) < static_cast<entt::id_type>(right);
    });

    for (const entt::entity attacker : attackers) {
        const auto& attacker_health = registry.get<components::Health>(attacker);
        if (attacker_health.current.raw() <= 0) {
            continue;
        }

        const entt::entity target = registry.get<components::AttackOrder>(attacker).target;
        if (target == entt::null || !registry.valid(target)
            || !registry.all_of<components::Health, components::GridPosition>(target)) {
            registry.remove<components::AttackOrder>(attacker);
            continue;
        }

        const auto& target_health = registry.get<components::Health>(target);
        if (target_health.current.raw() <= 0) {
            registry.remove<components::AttackOrder>(attacker);
            continue;
        }

        const core::GridPos attacker_pos = registry.get<components::GridPosition>(attacker).cell;
        const core::GridPos target_pos = registry.get<components::GridPosition>(target).cell;
        if (core::chebyshev_distance(attacker_pos, target_pos) <= 1) {
            registry.remove<components::MovePath>(attacker);
            registry.remove<components::MoveSegment>(attacker);
            continue;
        }

        const core::GridPos stand_tile = find_adjacent_walkable(map, registry, target_pos, attacker);
        if (stand_tile == target_pos) {
            continue;
        }

        if (registry.any_of<components::MoveSegment>(attacker)) {
            continue;
        }

        bool path_reaches_target = false;
        if (registry.any_of<components::MovePath>(attacker)) {
            const auto& path = registry.get<components::MovePath>(attacker);
            if (!path.cells.empty() && path.next_index < static_cast<int>(path.cells.size())) {
                const core::GridPos path_goal = path.cells.back();
                path_reaches_target = core::chebyshev_distance(path_goal, target_pos) <= 1;
            }
        }

        if (path_reaches_target) {
            continue;
        }

        assign_path(registry, attacker, map, stand_tile);
    }
}

void run_enemy_militia_ai(entt::registry& registry)
{
    const auto view = registry.view<components::MilitiaUnitTag, components::EnemyTag, components::GridPosition>();

    for (const entt::entity militia : view) {
        entt::entity target = entt::null;
        int best_distance = std::numeric_limits<int>::max();
        const core::GridPos militia_pos = view.get<components::GridPosition>(militia).cell;

        const auto player_view =
            registry.view<components::PlayerOwnedTag, components::GridPosition, components::Health>();
        for (const entt::entity player_entity : player_view) {
            if (!registry.any_of<components::UnitTag>(player_entity)) {
                continue;
            }

            const auto& health = player_view.get<components::Health>(player_entity);
            if (health.current.raw() <= 0) {
                continue;
            }

            const core::GridPos player_pos = player_view.get<components::GridPosition>(player_entity).cell;
            const int distance = core::chebyshev_distance(militia_pos, player_pos);
            if (distance < best_distance) {
                best_distance = distance;
                target = player_entity;
            }
        }

        if (target == entt::null) {
            continue;
        }

        registry.get_or_emplace<components::AttackOrder>(militia).target = target;
    }
}

void run_movement_system(
    entt::registry& registry,
    const components::MapGrid& map,
    const components::ContentPack& content)
{
    const auto view = registry.view<
        components::UnitTag,
        components::WorldPosition,
        components::GridPosition,
        components::DefinitionRef>();

    for (const entt::entity entity : view) {
        const auto& definition_ref = view.get<components::DefinitionRef>(entity);
        const auto* definition = find_unit_archetype_from_ref(content, definition_ref);
        if (definition == nullptr) {
            continue;
        }

        if (registry.any_of<components::MoveSegment>(entity)) {
            advance_move_segment(registry, entity, definition->move_ticks_per_tile);
            continue;
        }

        replan_path_if_blocked(registry, entity, map);
        try_begin_next_path_step(registry, entity, definition->move_ticks_per_tile);
    }
}

void run_worker_deposit_system(entt::registry& registry)
{
    const auto town_center_view = registry.view<components::TownCenterTag, components::GridPosition, components::Stockpile>();
    if (town_center_view.begin() == town_center_view.end()) {
        return;
    }

    const entt::entity town_center = *town_center_view.begin();
    const core::GridPos depot_pos = town_center_view.get<components::GridPosition>(town_center).cell;

    const auto worker_view = registry.view<
        components::WorkerUnitTag,
        components::GridPosition,
        components::CarriedWood,
        components::Health>();

    for (const entt::entity worker : worker_view) {
        const auto& health = worker_view.get<components::Health>(worker);
        if (health.current.raw() <= 0) {
            continue;
        }

        auto& carried = worker_view.get<components::CarriedWood>(worker);
        if (carried.amount <= 0) {
            continue;
        }

        const core::GridPos worker_pos = worker_view.get<components::GridPosition>(worker).cell;
        if (core::chebyshev_distance(worker_pos, depot_pos) > 1) {
            continue;
        }

        auto& stockpile = registry.get<components::Stockpile>(town_center);
        stockpile.wood += carried.amount;
        carried.amount = 0;
        registry.remove<components::MovePath>(worker);
        registry.remove<components::MoveSegment>(worker);

        if (registry.any_of<components::WorkerBrain>(worker)) {
            registry.get<components::WorkerBrain>(worker).state = components::WorkerState::Idle;
        }
    }
}

void run_combat_system(entt::registry& registry, const components::ContentPack& content)
{
    const auto view = registry.view<
        components::UnitTag,
        components::GridPosition,
        components::DefinitionRef,
        components::AttackOrder,
        components::AttackCooldown,
        components::Health>();

    std::vector<entt::entity> attackers{};
    for (const entt::entity attacker : view) {
        attackers.push_back(attacker);
    }

    std::sort(attackers.begin(), attackers.end(), [](const entt::entity left, const entt::entity right) {
        return static_cast<entt::id_type>(left) < static_cast<entt::id_type>(right);
    });

    for (const entt::entity attacker : attackers) {
        auto& cooldown = registry.get<components::AttackCooldown>(attacker);
        if (cooldown.ticks_remaining > 0) {
            --cooldown.ticks_remaining;
            continue;
        }

        const entt::entity target = registry.get<components::AttackOrder>(attacker).target;
        if (target == entt::null || !registry.valid(target) || !registry.any_of<components::Health>(target)) {
            continue;
        }

        auto& target_health = registry.get<components::Health>(target);
        if (target_health.current.raw() <= 0) {
            continue;
        }

        const core::GridPos attacker_pos = registry.get<components::GridPosition>(attacker).cell;
        const core::GridPos target_pos = registry.get<components::GridPosition>(target).cell;
        if (core::chebyshev_distance(attacker_pos, target_pos) > 1) {
            continue;
        }

        const auto* definition = find_unit_archetype_from_ref(
            content,
            registry.get<components::DefinitionRef>(attacker));
        if (definition == nullptr || definition->melee_attack <= 0) {
            continue;
        }

        target_health.current = target_health.current - math::Fixed::from_int(definition->melee_attack);
        cooldown.ticks_remaining = definition->attack_cooldown_ticks;
    }
}

void run_death_cleanup(entt::registry& registry)
{
    std::vector<entt::entity> to_destroy{};

    const auto unit_view = registry.view<components::UnitTag, components::Health>();
    for (const entt::entity entity : unit_view) {
        if (unit_view.get<components::Health>(entity).current.raw() <= 0) {
            to_destroy.push_back(entity);
        }
    }

    const auto building_view = registry.view<components::BuildingTag, components::Health>();
    for (const entt::entity entity : building_view) {
        if (building_view.get<components::Health>(entity).current.raw() <= 0) {
            to_destroy.push_back(entity);
        }
    }

    for (const entt::entity entity : to_destroy) {
        registry.destroy(entity);
    }
}

} // namespace

void run_gameplay_systems(entt::registry& registry)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return;
    }

    auto& map = registry.get<components::MapGrid>(world);
    const auto& content = registry.get<components::ContentPack>(world);

    run_worker_system(registry, map, content);
    run_worker_deposit_system(registry);
    run_enemy_militia_ai(registry);
    run_attack_chase_system(registry, map);
    run_movement_system(registry, map, content);
    run_combat_system(registry, content);
    run_death_cleanup(registry);
}

void snapshot_world_positions_for_render(entt::registry& registry)
{
    snapshot_previous_world_positions(registry);
}

void compute_state_hash(entt::registry& registry)
{
    const entt::entity world = find_world_entity(registry);
    if (world == entt::null) {
        return;
    }

    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](const std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };

    const auto& map = registry.get<components::MapGrid>(world);
    mix(static_cast<std::uint64_t>(map.width));
    mix(static_cast<std::uint64_t>(map.height));
    for (const components::TileType tile : map.tiles) {
        mix(static_cast<std::uint64_t>(tile));
    }
    for (const int wood : map.forest_wood) {
        mix(static_cast<std::uint64_t>(wood));
    }

    std::vector<entt::entity> entities{};
    for (const entt::entity entity : registry.view<components::GridPosition>()) {
        if (registry.all_of<components::WorldTag>(entity)) {
            continue;
        }

        entities.push_back(entity);
    }
    std::sort(entities.begin(), entities.end());

    for (const entt::entity entity : entities) {
        mix(static_cast<std::uint64_t>(entity));

        if (registry.any_of<components::GridPosition>(entity)) {
            const auto& pos = registry.get<components::GridPosition>(entity).cell;
            mix(static_cast<std::uint64_t>(pos.x));
            mix(static_cast<std::uint64_t>(pos.y));
        }

        if (registry.any_of<components::WorldPosition>(entity)) {
            const auto& world_position = registry.get<components::WorldPosition>(entity);
            mix(static_cast<std::uint64_t>(world_position.x.raw()));
            mix(static_cast<std::uint64_t>(world_position.y.raw()));
        }

        if (registry.any_of<components::Health>(entity)) {
            const auto& health = registry.get<components::Health>(entity);
            mix(static_cast<std::uint64_t>(health.current.raw()));
            mix(static_cast<std::uint64_t>(health.max.raw()));
        }

        if (registry.any_of<components::CarriedWood>(entity)) {
            mix(static_cast<std::uint64_t>(registry.get<components::CarriedWood>(entity).amount));
        }

        if (registry.any_of<components::Stockpile>(entity)) {
            mix(static_cast<std::uint64_t>(registry.get<components::Stockpile>(entity).wood));
        }

        if (registry.any_of<components::AttackOrder>(entity)) {
            mix(static_cast<std::uint64_t>(
                entt::to_integral(registry.get<components::AttackOrder>(entity).target)));
        }

        if (registry.any_of<components::AttackCooldown>(entity)) {
            mix(static_cast<std::uint64_t>(
                registry.get<components::AttackCooldown>(entity).ticks_remaining));
        }

        if (registry.any_of<components::GatherTarget>(entity)) {
            const auto& gather_target = registry.get<components::GatherTarget>(entity);
            mix(static_cast<std::uint64_t>(gather_target.cell.x));
            mix(static_cast<std::uint64_t>(gather_target.cell.y));
        }

        if (registry.any_of<components::WorkerBrain>(entity)) {
            mix(static_cast<std::uint64_t>(
                static_cast<std::uint8_t>(registry.get<components::WorkerBrain>(entity).state)));
        }

        if (registry.any_of<components::ManualControlTag>(entity)) {
            mix(1U);
        }

        if (registry.any_of<components::MovePath>(entity)) {
            const auto& path = registry.get<components::MovePath>(entity);
            mix(static_cast<std::uint64_t>(path.next_index));
            mix(static_cast<std::uint64_t>(path.cells.size()));
            for (const core::GridPos cell : path.cells) {
                mix(static_cast<std::uint64_t>(cell.x));
                mix(static_cast<std::uint64_t>(cell.y));
            }
        }

        if (registry.any_of<components::MoveSegment>(entity)) {
            const auto& segment = registry.get<components::MoveSegment>(entity);
            mix(static_cast<std::uint64_t>(segment.from_x.raw()));
            mix(static_cast<std::uint64_t>(segment.from_y.raw()));
            mix(static_cast<std::uint64_t>(segment.to_x.raw()));
            mix(static_cast<std::uint64_t>(segment.to_y.raw()));
            mix(static_cast<std::uint64_t>(segment.ticks_elapsed));
            mix(static_cast<std::uint64_t>(segment.ticks_total));
        }
    }

    registry.get<components::SimState>(world).state_hash = hash;
}

} // namespace aoa::sim::systems
