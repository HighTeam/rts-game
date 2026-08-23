#include "render/sim_render_snapshot.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "math/fixed.hpp"
#include "net/net_constants.hpp"
#include "render/building_sight_memory.hpp"
#include "render/game_renderer.hpp"
#include "data/content_types.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/building_process.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/content_pack.hpp"
#include "sim/components/definition_ref.hpp"
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
#include "sim/components/entity_snapshot_identity.hpp"
#include "sim/player/player_commands.hpp"
#include "sim/player/player_economy.hpp"
#include "sim/spawn/unit_spawn.hpp"
#include "sim/systems/visibility_system.hpp"
#include "sim/systems/pathfinding.hpp"

#include <algorithm>
#include <cmath>

namespace aoa::render {

namespace {

RenderEntityPose capture_entity_pose(
    const entt::registry& registry,
    const entt::entity entity,
    const std::uint8_t local_player_slot,
    const bool shrouded = false)
{
    RenderEntityPose pose{};
    pose.entity = entity;
    pose.shrouded = shrouded;
    if (registry.any_of<sim::components::EntitySnapshotIdentity>(entity)) {
        pose.snapshot_key = registry.get<sim::components::EntitySnapshotIdentity>(entity).key;
    }

    if (registry.any_of<sim::components::GridPosition>(entity)) {
        const auto& cell = registry.get<sim::components::GridPosition>(entity).cell;
        pose.grid_x = cell.x;
        pose.grid_y = cell.y;
    }

    if (registry.all_of<sim::components::PreviousWorldPosition, sim::components::WorldPosition>(entity)) {
        const auto& previous = registry.get<sim::components::PreviousWorldPosition>(entity);
        const auto& current = registry.get<sim::components::WorldPosition>(entity);
        pose.prev_x = previous.x.to_float();
        pose.prev_y = previous.y.to_float();
        pose.cur_x = current.x.to_float();
        pose.cur_y = current.y.to_float();
    }
    else if (registry.any_of<sim::components::WorldPosition>(entity)) {
        const auto& current = registry.get<sim::components::WorldPosition>(entity);
        pose.prev_x = current.x.to_float();
        pose.prev_y = current.y.to_float();
        pose.cur_x = current.x.to_float();
        pose.cur_y = current.y.to_float();
    }
    else if (registry.any_of<sim::components::GridPosition>(entity)) {
        // Buildings (e.g. TC) have no WorldPosition; never leave cur_x/cur_y at 0 —
        // that misplaces render-time vision at the map origin.
        const auto& cell = registry.get<sim::components::GridPosition>(entity).cell;
        float origin_x = math::tile_center_coord(cell.x).to_float();
        float origin_y = math::tile_center_coord(cell.y).to_float();
        if (registry.any_of<sim::components::BuildingTag>(entity)
            || registry.any_of<sim::components::TownCenterTag>(entity)) {
            sim::components::BuildingFootprint footprint{};
            if (registry.any_of<sim::components::BuildingFootprint>(entity)) {
                footprint = registry.get<sim::components::BuildingFootprint>(entity);
            }
            const bool is_tc = registry.any_of<sim::components::TownCenterTag>(entity);
            footprint = sim::components::effective_building_footprint(footprint, is_tc);
            origin_x = static_cast<float>(cell.x) + static_cast<float>(footprint.width) * 0.5F;
            origin_y = static_cast<float>(cell.y) + static_cast<float>(footprint.height) * 0.5F;
        }

        pose.prev_x = origin_x;
        pose.prev_y = origin_y;
        pose.cur_x = origin_x;
        pose.cur_y = origin_y;
    }

    if (registry.all_of<sim::components::MoveSegment>(entity)) {
        const auto& segment = registry.get<sim::components::MoveSegment>(entity);
        pose.has_move_segment = segment.ticks_total > 0;
        pose.move_from_x = segment.from_x.to_float();
        pose.move_from_y = segment.from_y.to_float();
        pose.move_to_x = segment.to_x.to_float();
        pose.move_to_y = segment.to_y.to_float();
        pose.move_ticks_elapsed = segment.ticks_elapsed;
        pose.move_ticks_total = segment.ticks_total;
    }

    if (registry.any_of<sim::components::Health>(entity)) {
        const auto& health = registry.get<sim::components::Health>(entity);
        pose.health_current = health.current.to_int();
        pose.health_max = health.max.to_int();
    }

    pose.is_enemy = sim::components::is_opponent_entity(registry, entity, local_player_slot);
    pose.is_worker = registry.any_of<sim::components::WorkerUnitTag>(entity);
    if (pose.is_worker && registry.any_of<sim::components::CarriedWood>(entity)) {
        pose.carried_wood = registry.get<sim::components::CarriedWood>(entity).amount;
    }
    if (pose.is_worker && registry.any_of<sim::components::CarriedFood>(entity)) {
        pose.carried_food = registry.get<sim::components::CarriedFood>(entity).amount;
    }
    if (pose.is_worker && registry.any_of<sim::components::CarriedMoney>(entity)) {
        pose.carried_money = registry.get<sim::components::CarriedMoney>(entity).amount;
    }
    pose.is_militia = registry.any_of<sim::components::MilitiaUnitTag>(entity);
    pose.is_mage = registry.any_of<sim::components::MageUnitTag>(entity);
    if (registry.any_of<sim::components::UnitSex>(entity)) {
        pose.unit_sex = registry.get<sim::components::UnitSex>(entity).value;
    }
    pose.is_projectile = registry.any_of<sim::components::Projectile>(entity);
    if (pose.is_projectile) {
        const auto& projectile = registry.get<sim::components::Projectile>(entity);
        pose.is_arrow = projectile.is_arrow;
        pose.projectile_reveal_slot = projectile.reveal_to_slot;
    }
    pose.is_town_center = registry.any_of<sim::components::TownCenterTag>(entity);
    pose.is_house = registry.any_of<sim::components::HouseTag>(entity);
    pose.is_lumber_camp = registry.any_of<sim::components::LumberCampTag>(entity);
    pose.is_mill = registry.any_of<sim::components::MillTag>(entity);
    pose.is_mining_camp = registry.any_of<sim::components::MiningCampTag>(entity);
    pose.is_barracks = registry.any_of<sim::components::BarracksTag>(entity);
    pose.is_mage_academy = registry.any_of<sim::components::MageAcademyTag>(entity);
    pose.is_tower = registry.any_of<sim::components::TowerTag>(entity);
    pose.is_market = registry.any_of<sim::components::MarketTag>(entity);
    pose.is_extractor = registry.any_of<sim::components::ExtractorTag>(entity);
    pose.is_garden = registry.any_of<sim::components::GardenTag>(entity);
    pose.is_reservoir = registry.any_of<sim::components::ReservoirTag>(entity);
    pose.is_farm = registry.any_of<sim::components::FarmTag>(entity);
    if (registry.any_of<sim::components::DefinitionRef>(entity)) {
        const std::string& archetype_id = registry.get<sim::components::DefinitionRef>(entity).id;
        if (archetype_id == constants::GARDEN_BUILDING_ID) {
            pose.is_garden = true;
        }
        if (archetype_id == constants::RESERVOIR_BUILDING_ID) {
            pose.is_reservoir = true;
        }
        if (archetype_id == constants::FARM_BUILDING_ID) {
            pose.is_farm = true;
        }
    }
    if (registry.any_of<sim::components::FarmFood>(entity)) {
        const auto& farm_food = registry.get<sim::components::FarmFood>(entity);
        pose.farm_food_remaining = farm_food.remaining;
        pose.farm_food_max = farm_food.max;
    }
    if (sim::components::building_has_active_process(registry, entity)) {
        const auto& process = registry.get<sim::components::BuildingProcess>(entity);
        pose.has_process = true;
        pose.process_percent = sim::components::building_process_percent(process);
        pose.process_is_research = sim::components::building_process_is_research(process.kind);
    }
    if (registry.any_of<sim::components::BuildingVisualVariant>(entity)) {
        pose.house_variant = registry.get<sim::components::BuildingVisualVariant>(entity).index;
    }
    if (registry.any_of<sim::components::GarrisonHold>(entity)) {
        const auto& hold = registry.get<sim::components::GarrisonHold>(entity);
        pose.garrison_count = static_cast<int>(hold.units.size());
        pose.garrison_capacity = static_cast<int>(hold.capacity);
    }
    pose.is_mana_lake = registry.any_of<sim::components::ManaLakeTag>(entity);
    if (pose.is_mana_lake) {
        pose.lake_has_extractor = sim::spawn::find_extractor_on_mana_lake(
            const_cast<entt::registry&>(registry),
            entity) != entt::null;
    }

    if (registry.any_of<sim::components::ManaGenerationCooldown>(entity)) {
        pose.mana_gen_ticks_remaining =
            registry.get<sim::components::ManaGenerationCooldown>(entity).ticks_remaining;
    }

    pose.under_construction = registry.any_of<sim::components::UnderConstructionTag>(entity);
    sim::components::BuildingFootprint footprint{};
    if (registry.any_of<sim::components::BuildingFootprint>(entity)) {
        footprint = registry.get<sim::components::BuildingFootprint>(entity);
    }
    const sim::components::BuildingFootprint effective_footprint =
        sim::components::effective_building_footprint(footprint, pose.is_town_center);
    pose.footprint_width = effective_footprint.width;
    pose.footprint_height = effective_footprint.height;
    if (registry.any_of<sim::components::PlayerOwnedTag>(entity)) {
        pose.player_slot = sim::components::entity_player_slot(registry, entity);
    }
    else {
        pose.is_nature = true;
    }

    if (registry.any_of<sim::components::DefinitionRef>(entity)) {
        pose.archetype_id = registry.get<sim::components::DefinitionRef>(entity).id;
        const auto world_view = registry.view<sim::components::WorldTag, sim::components::ContentPack>();
        if (world_view.begin() != world_view.end()) {
            const auto& content = world_view.get<sim::components::ContentPack>(*world_view.begin()).content;
            const auto& definition_ref = registry.get<sim::components::DefinitionRef>(entity);
            const data::ArchetypeDefinition* definition =
                data::find_archetype(content, definition_ref.id);
            if (definition != nullptr) {
                pose.melee_attack = definition->melee_attack;
                pose.melee_armor = definition->melee_armor;
                pose.pierce_attack = definition->pierce_attack;
                pose.pierce_armor = definition->pierce_armor;
                pose.attack_range = definition->attack_range;
            }
        }
    }

    if (registry.any_of<sim::components::MovePath>(entity)) {
        const auto& path = registry.get<sim::components::MovePath>(entity);
        pose.debug_path_cells = path.cells;
        pose.debug_path_next_index = path.next_index;
    }

    return pose;
}

void capture_hud_stats(
    const entt::registry& registry,
    const std::uint8_t player_slot,
    RenderHudPlayerStats& stats)
{
    const sim::components::Stockpile stockpile =
        sim::player::sum_player_stockpile(registry, player_slot);
    stats.town_wood = stockpile.wood;
    stats.town_food = stockpile.food;
    stats.town_money = stockpile.money;
    stats.town_mana = stockpile.mana;
    stats.town_mana_max = sim::player::player_mana_cap_max(registry, player_slot);
    stats.civil_cap_current = sim::player::count_player_units(registry, player_slot);
    stats.civil_cap_max = sim::player::player_civil_cap_max(registry, player_slot);

    const auto worker_view = registry.view<
        sim::components::WorkerUnitTag,
        sim::components::PlayerOwnedTag,
        sim::components::CarriedWood>();
    for (const entt::entity entity : worker_view) {
        if (sim::components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        stats.carried_wood = worker_view.get<sim::components::CarriedWood>(entity).amount;
        break;
    }

    const auto militia_view = registry.view<
        sim::components::MilitiaUnitTag,
        sim::components::PlayerOwnedTag,
        sim::components::Health>();
    for (const entt::entity entity : militia_view) {
        if (sim::components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        const auto& health = militia_view.get<sim::components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        stats.militia_hp = health.current.to_int();
        stats.militia_max_hp = health.max.to_int();
        break;
    }
}

} // namespace

SimRenderSnapshot capture_sim_render_snapshot(
    const entt::registry& registry,
    const std::uint8_t local_player_slot,
    BuildingSightMemory* building_sight_memory,
    const std::uint64_t tick_count)
{
    SimRenderSnapshot snapshot{};
    snapshot.tick_count = tick_count;

    const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
    if (world_view.begin() == world_view.end()) {
        return snapshot;
    }

    const entt::entity world = *world_view.begin();
    const auto& map = world_view.get<sim::components::MapGrid>(world);
    snapshot.map_width = map.width;
    snapshot.map_height = map.height;
    snapshot.tiles = map.tiles;
    snapshot.ground = map.ground;
    snapshot.forest_wood = map.forest_wood;
    snapshot.bush_food = map.bush_food;
    snapshot.mine_money = map.mine_money;
    if (registry.any_of<sim::components::MatchSession>(world)) {
        const auto& session = registry.get<sim::components::MatchSession>(world);
        snapshot.player_color_indices = session.player_color_indices;
        snapshot.player_ages = session.player_ages;
        snapshot.player_civilizations = session.player_civilizations;
        snapshot.vision_source_slots_mask =
            sim::components::cartography_vision_slots_mask(session, local_player_slot);
        if (registry.any_of<sim::components::MapPingList>(world)) {
            for (const sim::components::MapPing& ping :
                 registry.get<sim::components::MapPingList>(world).pings) {
                if (ping.player_slot != local_player_slot
                    && !sim::components::slots_are_allied(
                        session, local_player_slot, ping.player_slot)) {
                    continue;
                }
                snapshot.map_pings.push_back(ping);
            }
        }
    }
    else {
        snapshot.vision_source_slots_mask = sim::components::player_slot_bit(local_player_slot);
        if (registry.any_of<sim::components::MapPingList>(world)) {
            snapshot.map_pings = registry.get<sim::components::MapPingList>(world).pings;
        }
    }

    const sim::components::FogOfWarState* fog = nullptr;
    if (registry.any_of<sim::components::FogOfWarState>(world)) {
        fog = &registry.get<sim::components::FogOfWarState>(world);
        const std::size_t cells_per_player =
            static_cast<std::size_t>(map.width * map.height);
        const std::size_t offset =
            static_cast<std::size_t>(local_player_slot) * cells_per_player;
        snapshot.fog_explored.assign(
            fog->explored.begin() + static_cast<std::ptrdiff_t>(offset),
            fog->explored.begin() + static_cast<std::ptrdiff_t>(offset + cells_per_player));
        snapshot.fog_visible.assign(
            fog->visible.begin() + static_cast<std::ptrdiff_t>(offset),
            fog->visible.begin() + static_cast<std::ptrdiff_t>(offset + cells_per_player));
        snapshot.fog_memory_tiles.assign(
            fog->memory_tiles.begin() + static_cast<std::ptrdiff_t>(offset),
            fog->memory_tiles.begin() + static_cast<std::ptrdiff_t>(offset + cells_per_player));
        snapshot.fog_memory_forest_wood.assign(
            fog->memory_forest_wood.begin() + static_cast<std::ptrdiff_t>(offset),
            fog->memory_forest_wood.begin() + static_cast<std::ptrdiff_t>(offset + cells_per_player));
        snapshot.fog_memory_bush_food.assign(
            fog->memory_bush_food.begin() + static_cast<std::ptrdiff_t>(offset),
            fog->memory_bush_food.begin() + static_cast<std::ptrdiff_t>(offset + cells_per_player));
        snapshot.fog_memory_mine_money.assign(
            fog->memory_mine_money.begin() + static_cast<std::ptrdiff_t>(offset),
            fog->memory_mine_money.begin() + static_cast<std::ptrdiff_t>(offset + cells_per_player));
    }

    const auto should_draw_entity = [&](const entt::entity entity) {
        if (!sim::systems::unstarted_construction_visible_to_slot(
                registry, entity, local_player_slot)) {
            return false;
        }

        if (fog == nullptr) {
            return true;
        }

        return sim::systems::is_entity_visible_to_slot(
            registry,
            *fog,
            entity,
            local_player_slot);
    };

    const auto building_view = registry.view<
        sim::components::BuildingTag,
        sim::components::GridPosition,
        sim::components::Health>();
    for (const entt::entity entity : building_view) {
        if (should_draw_entity(entity)) {
            RenderEntityPose pose = capture_entity_pose(registry, entity, local_player_slot);
            if (building_sight_memory != nullptr) {
                building_sight_memory->observe_visible(pose);
            }
            snapshot.buildings.push_back(std::move(pose));
            continue;
        }

        // Explored-but-not-visible: never pull live construction/completion.
        // Only show the last pose this player actually saw.
        if (building_sight_memory == nullptr) {
            continue;
        }

        const RenderEntityPose* remembered = building_sight_memory->find(entity);
        if (remembered == nullptr) {
            continue;
        }

        if (fog != nullptr
            && !sim::systems::is_building_renderable_in_shroud(
                   registry, *fog, entity, local_player_slot)) {
            continue;
        }

        snapshot.buildings.push_back(*remembered);
    }

    if (building_sight_memory != nullptr) {
        // Drop memories for buildings that no longer exist.
        for (const RenderEntityPose& remembered : building_sight_memory->remembered_poses()) {
            if (!registry.valid(remembered.entity)
                || !registry.any_of<sim::components::BuildingTag>(remembered.entity)) {
                building_sight_memory->forget(remembered.entity);
            }
        }
    }

    // Lakes are terrain-like: once explored they stay drawn, and they never die.
    const auto lake_view = registry.view<
        sim::components::ManaLakeTag,
        sim::components::GridPosition,
        sim::components::BuildingFootprint>();
    for (const entt::entity entity : lake_view) {
        if (fog != nullptr) {
            const auto& anchor = lake_view.get<sim::components::GridPosition>(entity);
            const auto& footprint = lake_view.get<sim::components::BuildingFootprint>(entity);
            bool explored = false;
            for (int y = 0; y < footprint.height && !explored; ++y) {
                for (int x = 0; x < footprint.width && !explored; ++x) {
                    explored = sim::systems::is_cell_explored_to_slot(
                        *fog,
                        core::GridPos{anchor.cell.x + x, anchor.cell.y + y},
                        local_player_slot);
                }
            }

            if (!explored) {
                continue;
            }
        }

        snapshot.buildings.push_back(capture_entity_pose(registry, entity, local_player_slot));
    }

    const auto unit_view = registry.view<
        sim::components::UnitTag,
        sim::components::GridPosition,
        sim::components::Health>();
    for (const entt::entity entity : unit_view) {
        const auto& health = unit_view.get<sim::components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        if (!should_draw_entity(entity)) {
            continue;
        }

        if (registry.any_of<sim::components::GarrisonedTag>(entity)) {
            continue;
        }

        snapshot.units.push_back(capture_entity_pose(registry, entity, local_player_slot));
    }

    const auto projectile_view = registry.view<
        sim::components::Projectile,
        sim::components::WorldPosition>();
    for (const entt::entity entity : projectile_view) {
        snapshot.units.push_back(capture_entity_pose(registry, entity, local_player_slot));
    }

    for (std::uint8_t player_slot = 0U;
        player_slot < static_cast<std::uint8_t>(snapshot.hud_by_player.size());
        ++player_slot) {
        capture_hud_stats(registry, player_slot, snapshot.hud_by_player[player_slot]);
    }

    return snapshot;
}

std::pair<float, float> interpolate_render_pose(
    const RenderEntityPose& pose,
    const float interpolation_alpha)
{
    const float max_extrapolation_alpha = net::constants::LOCKSTEP_MAX_RENDER_EXTRAPOLATION_ALPHA;

    if (pose.has_move_segment && pose.move_ticks_total > 0) {
        const float progress = (static_cast<float>(pose.move_ticks_elapsed)
                + std::clamp(interpolation_alpha, 0.0F, 1.0F + max_extrapolation_alpha))
            / static_cast<float>(pose.move_ticks_total);
        const math::Fixed t = math::Fixed::from_float(std::clamp(progress, 0.0F, 1.0F));
        return {
            math::fixed_lerp(
                math::Fixed::from_float(pose.move_from_x),
                math::Fixed::from_float(pose.move_to_x),
                t).to_float(),
            math::fixed_lerp(
                math::Fixed::from_float(pose.move_from_y),
                math::Fixed::from_float(pose.move_to_y),
                t).to_float(),
        };
    }

    if (interpolation_alpha <= 1.0F) {
        const math::Fixed t = math::Fixed::from_float(std::clamp(interpolation_alpha, 0.0F, 1.0F));
        return {
            math::fixed_lerp(
                math::Fixed::from_float(pose.prev_x),
                math::Fixed::from_float(pose.cur_x),
                t).to_float(),
            math::fixed_lerp(
                math::Fixed::from_float(pose.prev_y),
                math::Fixed::from_float(pose.cur_y),
                t).to_float(),
        };
    }

    const float extrapolation_alpha =
        std::min(interpolation_alpha - 1.0F, max_extrapolation_alpha);
    const math::Fixed delta_x =
        math::Fixed::from_float(pose.cur_x) - math::Fixed::from_float(pose.prev_x);
    const math::Fixed delta_y =
        math::Fixed::from_float(pose.cur_y) - math::Fixed::from_float(pose.prev_y);
    const math::Fixed extrapolation = math::Fixed::from_float(extrapolation_alpha);
    return {
        (math::Fixed::from_float(pose.cur_x) + delta_x * extrapolation).to_float(),
        (math::Fixed::from_float(pose.cur_y) + delta_y * extrapolation).to_float(),
    };
}

sf::Vector2f render_pose_screen_position(
    const GameRenderer& renderer,
    const RenderEntityPose& pose,
    const float interpolation_alpha)
{
    const auto [world_x, world_z] = interpolate_render_pose(pose, interpolation_alpha);
    return renderer.world_to_screen(world_x, 0.0F, world_z);
}

namespace {

[[nodiscard]] bool is_alive_player_unit_pose(
    const RenderEntityPose& pose,
    const std::uint8_t local_player_slot)
{
    return pose.entity != entt::null
        && pose.health_current > 0
        && !pose.is_enemy
        && !pose.is_projectile
        && pose.player_slot == local_player_slot;
}

[[nodiscard]] entt::entity pick_unit_at_screen_any_team(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const std::uint8_t local_player_slot,
    const bool pick_opponent)
{
    entt::entity best = entt::null;
    float best_distance_sq = pick_radius_px * pick_radius_px;

    for (const RenderEntityPose& pose : snapshot.units) {
        if (pick_opponent) {
            if (!pose.is_enemy || pose.health_current <= 0) {
                continue;
            }
        }
        else if (!is_alive_player_unit_pose(pose, local_player_slot)) {
            continue;
        }

        const auto [world_x, world_z] = interpolate_render_pose(pose, 1.0F);
        if (!snapshot.fog_visible.empty()) {
            const core::GridPos visibility_cell =
                snapshot_world_visibility_cell(world_x, world_z);
            if (!snapshot_cell_is_visible(snapshot, visibility_cell)) {
                continue;
            }
        }

        const sf::Vector2f unit_screen = render_pose_screen_position(renderer, pose, 1.0F);
        const float dx = unit_screen.x - screen_position.x;
        const float dy = unit_screen.y - screen_position.y;
        const float distance_sq = dx * dx + dy * dy;
        if (distance_sq > best_distance_sq) {
            continue;
        }

        if (best == entt::null || distance_sq < best_distance_sq
            || (distance_sq == best_distance_sq
                && static_cast<entt::id_type>(pose.entity)
                    < static_cast<entt::id_type>(best))) {
            best = pose.entity;
            best_distance_sq = distance_sq;
        }
    }

    return best;
}

[[nodiscard]] std::optional<core::GridPos> pick_resource_forest_at(
    const SimRenderSnapshot& snapshot,
    const core::GridPos cell)
{
    if (!core::is_inside_grid(cell, snapshot.map_width, snapshot.map_height)) {
        return std::nullopt;
    }

    const int index = core::grid_index(cell, snapshot.map_width);
    const auto tile = snapshot.tiles[static_cast<std::size_t>(index)];
    if (tile == sim::components::TileType::Forest) {
        if (snapshot.forest_wood[static_cast<std::size_t>(index)] <= 0) {
            return std::nullopt;
        }

        return cell;
    }

    if (tile == sim::components::TileType::Berries || tile == sim::components::TileType::Blueberries) {
        if (index >= static_cast<int>(snapshot.bush_food.size())
            || snapshot.bush_food[static_cast<std::size_t>(index)] <= 0) {
            return std::nullopt;
        }

        return cell;
    }

    if (tile == sim::components::TileType::GoldMine) {
        if (index >= static_cast<int>(snapshot.mine_money.size())
            || snapshot.mine_money[static_cast<std::size_t>(index)] <= 0) {
            return std::nullopt;
        }

        return cell;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<core::GridPos> pick_resource_forest_known_at(
    const SimRenderSnapshot& snapshot,
    const core::GridPos cell)
{
    if (!core::is_inside_grid(cell, snapshot.map_width, snapshot.map_height)) {
        return std::nullopt;
    }

    if (snapshot_cell_is_unexplored(snapshot, cell)) {
        return std::nullopt;
    }

    if (snapshot_cell_is_visible(snapshot, cell)) {
        return pick_resource_forest_at(snapshot, cell);
    }

    const std::size_t index = static_cast<std::size_t>(core::grid_index(cell, snapshot.map_width));
    if (index >= snapshot.fog_memory_tiles.size()) {
        return pick_resource_forest_at(snapshot, cell);
    }

    const auto memory_tile = static_cast<sim::components::TileType>(snapshot.fog_memory_tiles[index]);
    if (memory_tile == sim::components::TileType::Forest) {
        if (index >= snapshot.fog_memory_forest_wood.size()
            || snapshot.fog_memory_forest_wood[index] <= 0) {
            return std::nullopt;
        }

        return cell;
    }

    if (memory_tile == sim::components::TileType::Berries
        || memory_tile == sim::components::TileType::Blueberries) {
        if (index >= snapshot.fog_memory_bush_food.size()
            || snapshot.fog_memory_bush_food[index] <= 0) {
            return std::nullopt;
        }

        return cell;
    }

    if (memory_tile == sim::components::TileType::GoldMine) {
        if (index >= snapshot.fog_memory_mine_money.size()
            || snapshot.fog_memory_mine_money[index] <= 0) {
            return std::nullopt;
        }

        return cell;
    }

    return std::nullopt;
}

} // namespace

bool snapshot_cell_is_unexplored(const SimRenderSnapshot& snapshot, const core::GridPos cell)
{
    if (snapshot.fog_visible.empty() || snapshot.fog_explored.empty()) {
        return false;
    }

    if (!core::is_inside_grid(cell, snapshot.map_width, snapshot.map_height)) {
        return false;
    }

    const std::size_t index = static_cast<std::size_t>(cell.y * snapshot.map_width + cell.x);
    if (index >= snapshot.fog_visible.size() || index >= snapshot.fog_explored.size()) {
        return false;
    }

    return snapshot.fog_visible[index] == 0U && snapshot.fog_explored[index] == 0U;
}

bool snapshot_cell_is_visible(const SimRenderSnapshot& snapshot, const core::GridPos cell)
{
    if (snapshot.fog_visible.empty()) {
        return true;
    }

    if (!core::is_inside_grid(cell, snapshot.map_width, snapshot.map_height)) {
        return false;
    }

    const std::size_t index = static_cast<std::size_t>(cell.y * snapshot.map_width + cell.x);
    if (index >= snapshot.fog_visible.size()) {
        return false;
    }

    return snapshot.fog_visible[index] != 0U;
}

core::GridPos snapshot_world_visibility_cell(const float world_x, const float world_z)
{
    return {
        static_cast<int>(std::floor(world_x)),
        static_cast<int>(std::floor(world_z)),
    };
}

entt::entity pick_hovered_unit_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px)
{
    entt::entity best = entt::null;
    float best_distance_sq = pick_radius_px * pick_radius_px;

    for (const RenderEntityPose& pose : snapshot.units) {
        if (pose.health_current <= 0) {
            continue;
        }

        const auto [world_x, world_z] = interpolate_render_pose(pose, 1.0F);
        if (!snapshot.fog_visible.empty()) {
            const core::GridPos visibility_cell =
                snapshot_world_visibility_cell(world_x, world_z);
            if (!snapshot_cell_is_visible(snapshot, visibility_cell)) {
                continue;
            }
        }

        const sf::Vector2f unit_screen = render_pose_screen_position(renderer, pose, 1.0F);
        const float dx = unit_screen.x - screen_position.x;
        const float dy = unit_screen.y - screen_position.y;
        const float distance_sq = dx * dx + dy * dy;
        if (distance_sq > best_distance_sq) {
            continue;
        }

        if (best == entt::null || distance_sq < best_distance_sq
            || (distance_sq == best_distance_sq
                && static_cast<entt::id_type>(pose.entity)
                    < static_cast<entt::id_type>(best))) {
            best = pose.entity;
            best_distance_sq = distance_sq;
        }
    }

    return best;
}

entt::entity pick_player_unit_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const std::uint8_t local_player_slot)
{
    return pick_unit_at_screen_any_team(
        snapshot,
        renderer,
        screen_position,
        pick_radius_px,
        local_player_slot,
        false);
}

entt::entity pick_enemy_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const std::uint8_t local_player_slot)
{
    return pick_unit_at_screen_any_team(
        snapshot,
        renderer,
        screen_position,
        pick_radius_px,
        local_player_slot,
        true);
}

entt::entity pick_player_building_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const std::uint8_t local_player_slot)
{
    const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
    if (!grid_cell.has_value()) {
        return entt::null;
    }

    entt::entity best = entt::null;
    float best_distance_sq = pick_radius_px * pick_radius_px;

    for (const RenderEntityPose& pose : snapshot.buildings) {
        if (pose.is_nature || pose.player_slot != local_player_slot || pose.health_current <= 0) {
            continue;
        }

        const sim::components::BuildingFootprint footprint{
            pose.footprint_width,
            pose.footprint_height,
        };
        const sim::components::GridPosition anchor_pos{{pose.grid_x, pose.grid_y}};
        if (!sim::components::building_contains_cell(anchor_pos, footprint, *grid_cell)) {
            continue;
        }

        const sf::Vector2f tile_screen = renderer.tile_center_screen(
            grid_cell->x,
            grid_cell->y,
            constants::RENDER_ENTITY_BASE_LIFT);
        const float dx = tile_screen.x - screen_position.x;
        const float dy = tile_screen.y - screen_position.y;
        const float distance_sq = dx * dx + dy * dy;
        if (distance_sq > best_distance_sq) {
            continue;
        }

        if (best == entt::null || distance_sq < best_distance_sq
            || (distance_sq == best_distance_sq
                && static_cast<entt::id_type>(pose.entity)
                    < static_cast<entt::id_type>(best))) {
            best = pose.entity;
            best_distance_sq = distance_sq;
        }
    }

    return best;
}

entt::entity pick_enemy_building_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    const std::uint8_t local_player_slot)
{
    const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
    if (!grid_cell.has_value()) {
        return entt::null;
    }

    if (snapshot_cell_is_unexplored(snapshot, *grid_cell)) {
        return entt::null;
    }

    entt::entity best = entt::null;
    float best_distance_sq = pick_radius_px * pick_radius_px;

    for (const RenderEntityPose& pose : snapshot.buildings) {
        if (pose.is_nature || pose.player_slot == local_player_slot || pose.health_current <= 0) {
            continue;
        }

        // Visible enemies always; shrouded memory poses are selectable for info.
        if (!pose.shrouded && !snapshot_cell_is_visible(snapshot, *grid_cell)) {
            continue;
        }

        const sim::components::BuildingFootprint footprint{
            pose.footprint_width,
            pose.footprint_height,
        };
        const sim::components::GridPosition anchor_pos{{pose.grid_x, pose.grid_y}};
        if (!sim::components::building_contains_cell(anchor_pos, footprint, *grid_cell)) {
            continue;
        }

        const sf::Vector2f tile_screen = renderer.tile_center_screen(
            grid_cell->x,
            grid_cell->y,
            constants::RENDER_ENTITY_BASE_LIFT);
        const float dx = tile_screen.x - screen_position.x;
        const float dy = tile_screen.y - screen_position.y;
        const float distance_sq = dx * dx + dy * dy;
        if (distance_sq > best_distance_sq) {
            continue;
        }

        if (best == entt::null || distance_sq < best_distance_sq
            || (distance_sq == best_distance_sq
                && static_cast<entt::id_type>(pose.entity)
                    < static_cast<entt::id_type>(best))) {
            best = pose.entity;
            best_distance_sq = distance_sq;
        }
    }

    return best;
}

entt::entity pick_mana_lake_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position)
{
    const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
    if (!grid_cell.has_value()) {
        return entt::null;
    }

    if (snapshot_cell_is_unexplored(snapshot, *grid_cell)) {
        return entt::null;
    }

    for (const RenderEntityPose& pose : snapshot.buildings) {
        if (!pose.is_mana_lake || pose.lake_has_extractor) {
            continue;
        }

        const sim::components::BuildingFootprint footprint{
            pose.footprint_width,
            pose.footprint_height,
        };
        const sim::components::GridPosition anchor_pos{{pose.grid_x, pose.grid_y}};
        if (!sim::components::building_contains_cell(anchor_pos, footprint, *grid_cell)) {
            continue;
        }

        return pose.entity;
    }

    return entt::null;
}

bool snapshot_cell_covered_by_mana_lake(
    const SimRenderSnapshot& snapshot,
    const core::GridPos cell)
{
    for (const RenderEntityPose& pose : snapshot.buildings) {
        if (!pose.is_mana_lake) {
            continue;
        }

        const sim::components::BuildingFootprint footprint{
            pose.footprint_width,
            pose.footprint_height,
        };
        const sim::components::GridPosition anchor_pos{{pose.grid_x, pose.grid_y}};
        if (sim::components::building_contains_cell(anchor_pos, footprint, cell)) {
            return true;
        }
    }

    return false;
}

bool snapshot_cell_blocked_by_unit(
    const SimRenderSnapshot& snapshot,
    const core::GridPos cell)
{
    for (const RenderEntityPose& pose : snapshot.units) {
        if (pose.health_current <= 0) {
            continue;
        }

        if (pose.grid_x == cell.x && pose.grid_y == cell.y) {
            return true;
        }

        if (static_cast<int>(pose.cur_x) == cell.x && static_cast<int>(pose.cur_y) == cell.y) {
            return true;
        }
    }

    return false;
}

bool snapshot_can_place_extractor_at(
    const SimRenderSnapshot& snapshot,
    const core::GridPos anchor_cell)
{
    for (const RenderEntityPose& pose : snapshot.buildings) {
        if (!pose.is_mana_lake || pose.lake_has_extractor) {
            continue;
        }

        if (pose.grid_x == anchor_cell.x && pose.grid_y == anchor_cell.y) {
            return true;
        }
    }

    return false;
}

std::optional<core::GridPos> snapshot_extractor_snap_anchor(
    const SimRenderSnapshot& snapshot,
    const core::GridPos hover_cell)
{
    for (const RenderEntityPose& pose : snapshot.buildings) {
        if (!pose.is_mana_lake || pose.lake_has_extractor) {
            continue;
        }

        if (pose.footprint_width != constants::EXTRACTOR_FOOTPRINT_TILES
            || pose.footprint_height != constants::EXTRACTOR_FOOTPRINT_TILES) {
            continue;
        }

        const sim::components::BuildingFootprint footprint{
            pose.footprint_width,
            pose.footprint_height,
        };
        const sim::components::GridPosition anchor_pos{{pose.grid_x, pose.grid_y}};
        if (!sim::components::building_contains_cell(anchor_pos, footprint, hover_cell)) {
            continue;
        }

        return core::GridPos{pose.grid_x, pose.grid_y};
    }

    return std::nullopt;
}

std::vector<entt::entity> pick_player_units_in_screen_rect(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f rect_min,
    const sf::Vector2f rect_max,
    const std::uint8_t local_player_slot)
{
    const float min_x = std::min(rect_min.x, rect_max.x);
    const float max_x = std::max(rect_min.x, rect_max.x);
    const float min_y = std::min(rect_min.y, rect_max.y);
    const float max_y = std::max(rect_min.y, rect_max.y);

    std::vector<entt::entity> picked{};
    for (const RenderEntityPose& pose : snapshot.units) {
        if (!is_alive_player_unit_pose(pose, local_player_slot)) {
            continue;
        }

        const sf::Vector2f unit_screen = render_pose_screen_position(renderer, pose, 1.0F);
        if (unit_screen.x < min_x || unit_screen.x > max_x || unit_screen.y < min_y
            || unit_screen.y > max_y) {
            continue;
        }

        picked.push_back(pose.entity);
    }

    std::sort(picked.begin(), picked.end(), [](const entt::entity left, const entt::entity right) {
        return static_cast<entt::id_type>(left) < static_cast<entt::id_type>(right);
    });

    return picked;
}

std::optional<core::GridPos> pick_resource_forest_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    const sf::Vector2f screen_position,
    const float pick_radius_px,
    [[maybe_unused]] const std::uint8_t local_player_slot)
{
    const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
    if (!grid_cell.has_value()) {
        return std::nullopt;
    }

    if (pick_resource_forest_known_at(snapshot, *grid_cell).has_value()) {
        return grid_cell;
    }

    const sf::Vector2f center_screen = renderer.tile_center_screen(
        grid_cell->x,
        grid_cell->y,
        constants::RENDER_ENTITY_BASE_LIFT);
    const float dx = center_screen.x - screen_position.x;
    const float dy = center_screen.y - screen_position.y;
    if ((dx * dx + dy * dy) > pick_radius_px * pick_radius_px) {
        return std::nullopt;
    }

    return pick_resource_forest_known_at(snapshot, *grid_cell);
}

bool snapshot_has_town_center(const SimRenderSnapshot& snapshot, const entt::entity entity)
{
    for (const RenderEntityPose& pose : snapshot.buildings) {
        if (pose.entity == entity) {
            return pose.is_town_center;
        }
    }

    return false;
}

bool snapshot_has_town_center_at_cell(
    const SimRenderSnapshot& snapshot,
    const core::GridPos cell,
    const std::uint8_t local_player_slot)
{
    for (const RenderEntityPose& pose : snapshot.buildings) {
        if (!pose.is_town_center || pose.player_slot != local_player_slot) {
            continue;
        }

        const sim::components::BuildingFootprint footprint{
            pose.footprint_width,
            pose.footprint_height,
        };
        const sim::components::GridPosition anchor_pos{{pose.grid_x, pose.grid_y}};
        if (sim::components::building_contains_cell(anchor_pos, footprint, cell)) {
            return true;
        }
    }

    return false;
}

void prune_dead_selection(
    std::vector<entt::entity>& selected,
    const SimRenderSnapshot& snapshot,
    const std::uint8_t local_player_slot)
{
    selected.erase(
        std::remove_if(
            selected.begin(),
            selected.end(),
            [&snapshot, local_player_slot](const entt::entity entity) {
                for (const RenderEntityPose& pose : snapshot.units) {
                    if (pose.entity != entity) {
                        continue;
                    }

                    return !is_alive_player_unit_pose(pose, local_player_slot);
                }

                return true;
            }),
        selected.end());
}

void apply_selection_from_snapshot(
    std::vector<entt::entity>& selected,
    const SimRenderSnapshot& snapshot,
    const std::vector<entt::entity>& picked,
    const sim::player::SelectionModifyMode mode,
    const std::uint8_t local_player_slot)
{
    if (mode == sim::player::SelectionModifyMode::Replace) {
        selected = picked;
        return;
    }

    for (const entt::entity entity : picked) {
        bool alive = false;
        for (const RenderEntityPose& pose : snapshot.units) {
            if (pose.entity == entity && is_alive_player_unit_pose(pose, local_player_slot)) {
                alive = true;
                break;
            }
        }

        if (!alive) {
            continue;
        }

        const auto iterator = std::find(selected.begin(), selected.end(), entity);
        if (mode == sim::player::SelectionModifyMode::Add) {
            if (iterator == selected.end()) {
                selected.push_back(entity);
            }
            continue;
        }

        if (iterator == selected.end()) {
            selected.push_back(entity);
        }
        else {
            selected.erase(iterator);
        }
    }

    std::sort(selected.begin(), selected.end(), [](const entt::entity left, const entt::entity right) {
        return static_cast<entt::id_type>(left) < static_cast<entt::id_type>(right);
    });
}

} // namespace aoa::render
