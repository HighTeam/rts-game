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
#include "sim/systems/pathfinding.hpp"

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

const data::UnitDefinition* find_unit_definition(
    const components::ContentPack& content,
    const components::DefinitionRef& definition_ref)
{
    const auto iterator = content.civ.units.find(definition_ref.id);
    if (iterator == content.civ.units.end()) {
        return nullptr;
    }

    return &iterator->second;
}

bool is_occupied(entt::registry& registry, const core::GridPos cell, const entt::entity ignore)
{
    const auto unit_view = registry.view<components::UnitTag, components::GridPosition, components::Health>();
    for (const entt::entity entity : unit_view) {
        if (entity == ignore) {
            continue;
        }

        const auto& health = unit_view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        if (unit_view.get<components::GridPosition>(entity).cell == cell) {
            return true;
        }
    }

    const auto building_view = registry.view<components::BuildingTag, components::GridPosition, components::Health>();
    for (const entt::entity entity : building_view) {
        if (entity == ignore) {
            continue;
        }

        const auto& health = building_view.get<components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        if (building_view.get<components::GridPosition>(entity).cell == cell) {
            return true;
        }
    }

    return false;
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

void assign_path(
    entt::registry& registry,
    const entt::entity entity,
    const components::MapGrid& map,
    const core::GridPos goal)
{
    const auto& pos = registry.get<components::GridPosition>(entity).cell;
    auto& path = registry.get_or_emplace<components::MovePath>(entity);
    path.cells = find_path(map, pos, goal, false);
    path.next_index = 0;
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
        auto& brain = view.get<components::WorkerBrain>(worker);
        auto& carried = view.get<components::CarriedWood>(worker);
        const auto& definition_ref = view.get<components::DefinitionRef>(worker);
        const auto* definition = find_unit_definition(content, definition_ref);
        if (definition == nullptr) {
            continue;
        }

        const auto& worker_pos = view.get<components::GridPosition>(worker).cell;

        entt::entity town_center = entt::null;
        const auto town_center_view = registry.view<components::TownCenterTag, components::GridPosition>();
        for (const entt::entity entity : town_center_view) {
            town_center = entity;
            break;
        }

        if (town_center == entt::null) {
            continue;
        }

        const core::GridPos depot_pos = registry.get<components::GridPosition>(town_center).cell;

        if (carried.amount >= definition->carry_capacity) {
            brain.state = components::WorkerState::MovingToDeposit;
            const core::GridPos stand_tile = find_adjacent_walkable(map, registry, depot_pos, worker);
            if (core::chebyshev_distance(worker_pos, depot_pos) <= 1) {
                brain.state = components::WorkerState::Depositing;
            }
            else if (!registry.any_of<components::MovePath>(worker)) {
                assign_path(registry, worker, map, stand_tile);
            }
        }
        else {
            const core::GridPos forest = find_nearest_forest_with_wood(map, worker_pos);
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

        if (brain.state == components::WorkerState::Gathering) {
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

                    const int gather_amount = definition->gather_per_tick;
                    wood -= gather_amount;
                    carried.amount += gather_amount;
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

void run_militia_ai(entt::registry& registry, const components::MapGrid& map)
{
    const auto view = registry.view<components::MilitiaUnitTag, components::PlayerOwnedTag, components::GridPosition>();

    for (const entt::entity militia : view) {
        entt::entity target = entt::null;
        int best_distance = std::numeric_limits<int>::max();
        const core::GridPos militia_pos = view.get<components::GridPosition>(militia).cell;

        const auto enemy_view = registry.view<components::EnemyTag, components::GridPosition, components::Health>();
        for (const entt::entity enemy : enemy_view) {
            const auto& health = enemy_view.get<components::Health>(enemy);
            if (health.current.raw() <= 0) {
                continue;
            }

            const core::GridPos enemy_pos = enemy_view.get<components::GridPosition>(enemy).cell;
            const int distance = core::chebyshev_distance(militia_pos, enemy_pos);
            if (distance < best_distance) {
                best_distance = distance;
                target = enemy;
            }
        }

        if (target == entt::null) {
            continue;
        }

        registry.get_or_emplace<components::AttackOrder>(militia).target = target;

        if (best_distance <= 1) {
            registry.remove<components::MovePath>(militia);
            continue;
        }

        if (!registry.any_of<components::MovePath>(militia)) {
            const core::GridPos enemy_pos = registry.get<components::GridPosition>(target).cell;
            const core::GridPos stand_tile = find_adjacent_walkable(map, registry, enemy_pos, militia);
            assign_path(registry, militia, map, stand_tile);
        }
    }
}

void run_enemy_militia_ai(entt::registry& registry, const components::MapGrid& map)
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

        if (best_distance <= 1) {
            registry.remove<components::MovePath>(militia);
            continue;
        }

        if (!registry.any_of<components::MovePath>(militia)) {
            const core::GridPos target_pos = registry.get<components::GridPosition>(target).cell;
            const core::GridPos stand_tile = find_adjacent_walkable(map, registry, target_pos, militia);
            assign_path(registry, militia, map, stand_tile);
        }
    }
}

void run_movement_system(
    entt::registry& registry,
    const components::ContentPack& content)
{
    const auto view = registry.view<
        components::UnitTag,
        components::GridPosition,
        components::MoveCooldown,
        components::DefinitionRef>();

    for (const entt::entity entity : view) {
        auto& cooldown = view.get<components::MoveCooldown>(entity);
        if (cooldown.ticks_remaining > 0) {
            --cooldown.ticks_remaining;
            continue;
        }

        if (!registry.any_of<components::MovePath>(entity)) {
            continue;
        }

        auto& path = registry.get<components::MovePath>(entity);
        if (path.next_index >= static_cast<int>(path.cells.size())) {
            registry.remove<components::MovePath>(entity);
            continue;
        }

        const core::GridPos next_cell = path.cells[static_cast<std::size_t>(path.next_index)];
        if (is_occupied(registry, next_cell, entity)) {
            continue;
        }

        view.get<components::GridPosition>(entity).cell = next_cell;
        ++path.next_index;

        const auto& definition_ref = view.get<components::DefinitionRef>(entity);
        const auto* definition = find_unit_definition(content, definition_ref);
        if (definition != nullptr) {
            cooldown.ticks_remaining = definition->move_ticks_per_tile;
        }

        if (path.next_index >= static_cast<int>(path.cells.size())) {
            registry.remove<components::MovePath>(entity);
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

        const auto* definition = find_unit_definition(
            content,
            registry.get<components::DefinitionRef>(attacker));
        if (definition == nullptr || definition->attack_damage <= 0) {
            continue;
        }

        target_health.current = target_health.current - math::Fixed::from_int(definition->attack_damage);
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
    run_militia_ai(registry, map);
    run_enemy_militia_ai(registry, map);
    run_movement_system(registry, content);
    run_combat_system(registry, content);
    run_death_cleanup(registry);
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
    }

    registry.get<components::SimState>(world).state_hash = hash;
}

} // namespace aoa::sim::systems
