#include "app/game_input.hpp"

#include "app/command_panel.hpp"
#include "core/constants.hpp"
#include "core/grid.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/content_pack.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/fog_of_war.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/tags.hpp"
#include "sim/player/player_command.hpp"
#include "sim/player/player_commands.hpp"
#include "sim/player/player_economy.hpp"
#include "sim/simulation.hpp"
#include "data/content_types.hpp"
#include "net/lockstep_session.hpp"
#include "net/net_constants.hpp"
#include "render/game_renderer.hpp"
#include "sim/systems/pathfinding.hpp"
#include "sim/systems/visibility_system.hpp"

#include <cctype>

#include "math/fixed.hpp"

#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace aoa::app {

namespace {

sim::player::PlayerCommand make_command(
    sim::Simulation& simulation,
    const sim::player::PlayerCommandType type,
    const std::vector<entt::entity>& units,
    const render::SimRenderSnapshot* render_snapshot)
{
    sim::player::PlayerCommand command{};
    if (render_snapshot != nullptr) {
        command.execute_tick = render_snapshot->tick_count
            + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
    }
    else {
        command.execute_tick = simulation.next_command_execute_tick();
    }
    command.type = type;
    command.unit_ids = units;
    return command;
}

void fill_move_command_from_screen(
    sim::player::PlayerCommand& command,
    const render::GameRenderer& renderer,
    const core::GridPos goal_cell,
    const sf::Vector2f screen_position)
{
    command.cell = goal_cell;
    const auto [world_x, world_z] = renderer.screen_to_world_xz(screen_position.x, screen_position.y);
    command.has_goal_world = true;
    command.goal_world_x = math::Fixed::from_float(world_x);
    command.goal_world_y = math::Fixed::from_float(world_z);
}

} // namespace

void GameInput::play_order_ack_sfx(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    const sim::player::PlayerCommand& command) const
{
    if (game_audio_ == nullptr) {
        return;
    }

    using sim::player::PlayerCommandType;
    if (command.type == PlayerCommandType::Move) {
        game_audio_->play_random_move_ack();
        return;
    }

    if (command.type == PlayerCommandType::BuildTownCenter
        || command.type == PlayerCommandType::BuildHouse
        || command.type == PlayerCommandType::ResumeBuild) {
        game_audio_->play_sfx(audio::SfxId::Building);
        return;
    }

    if (command.type != PlayerCommandType::Gather) {
        return;
    }

    if (!selection_has_worker(simulation, render_snapshot)) {
        return;
    }

    sim::components::TileType tile = sim::components::TileType::Grass;
    if (render_snapshot != nullptr
        && core::is_inside_grid(
            command.cell,
            render_snapshot->map_width,
            render_snapshot->map_height)) {
        const int index = core::grid_index(command.cell, render_snapshot->map_width);
        if (static_cast<std::size_t>(index) < render_snapshot->tiles.size()) {
            tile = render_snapshot->tiles[static_cast<std::size_t>(index)];
        }
    }
    else {
        auto& registry = simulation.registry();
        const auto world_view =
            registry.view<sim::components::WorldTag, sim::components::MapGrid>();
        if (world_view.begin() != world_view.end()) {
            const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
            if (core::is_inside_grid(command.cell, map.width, map.height)) {
                tile = map.tiles[static_cast<std::size_t>(
                    core::grid_index(command.cell, map.width))];
            }
        }
    }

    if (tile == sim::components::TileType::Forest) {
        game_audio_->play_sfx(audio::SfxId::Chopping);
        return;
    }

    if (tile == sim::components::TileType::GoldMine) {
        game_audio_->play_sfx(audio::SfxId::Mining);
        return;
    }

    if (tile == sim::components::TileType::Berries
        || tile == sim::components::TileType::Blueberries) {
        game_audio_->play_sfx(audio::SfxId::Gathering);
    }
}

void GameInput::play_select_ack_if_own_units(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot) const
{
    if (game_audio_ == nullptr || selection_.units.empty()) {
        return;
    }

    if (!selection_has_worker(simulation, render_snapshot)
        && !selection_has_militia(simulation, render_snapshot)) {
        return;
    }

    game_audio_->play_random_select_ack();
}

void GameInput::submit_player_command(
    sim::Simulation& simulation,
    sim::player::PlayerCommand command)
{
    play_order_ack_sfx(simulation, nullptr, command);

    if (lockstep_session_ != nullptr) {
        lockstep_session_->submit_local_command(std::move(command));
        return;
    }

    simulation.enqueue_player_command(std::move(command));
}

bool GameInput::selection_has_worker(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot) const
{
    if (selection_.units.empty()) {
        return false;
    }

    if (render_snapshot != nullptr) {
        for (const entt::entity entity : selection_.units) {
            for (const render::RenderEntityPose& pose : render_snapshot->units) {
                if (pose.entity == entity && pose.is_worker && pose.health_current > 0
                    && pose.player_slot == local_player_slot_) {
                    return true;
                }
            }
        }

        return false;
    }

    auto& registry = simulation.registry();
    for (const entt::entity entity : selection_.units) {
        if (!registry.valid(entity)) {
            continue;
        }

        if (!registry.any_of<sim::components::WorkerUnitTag>(entity)) {
            continue;
        }

        if (sim::components::entity_player_slot(registry, entity) != local_player_slot_) {
            continue;
        }

        return true;
    }

    return false;
}

bool GameInput::selection_has_militia(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot) const
{
    if (selection_.units.empty()) {
        return false;
    }

    if (render_snapshot != nullptr) {
        for (const entt::entity entity : selection_.units) {
            for (const render::RenderEntityPose& pose : render_snapshot->units) {
                if (pose.entity == entity && pose.is_militia && pose.health_current > 0
                    && pose.player_slot == local_player_slot_) {
                    return true;
                }
            }
        }

        return false;
    }

    auto& registry = simulation.registry();
    for (const entt::entity entity : selection_.units) {
        if (!registry.valid(entity)) {
            continue;
        }

        if (!registry.any_of<sim::components::MilitiaUnitTag>(entity)) {
            continue;
        }

        if (sim::components::entity_player_slot(registry, entity) != local_player_slot_) {
            continue;
        }

        return true;
    }

    return false;
}

CommandPanelBuildOptions GameInput::current_build_options(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot) const
{
    CommandPanelBuildOptions options{};
    options.town_center_wood_cost = constants::TOWN_CENTER_BUILD_WOOD_COST;
    options.house_wood_cost = constants::HOUSE_BUILD_WOOD_COST;
    options.worker_food_cost = constants::WORKER_FOOD_COST;
    options.militia_food_cost = constants::MILITIA_FOOD_COST;

    auto& registry = simulation.registry();
    entt::entity world = entt::null;
    {
        const auto world_view = registry.view<sim::components::WorldTag>();
        if (world_view.begin() != world_view.end()) {
            world = *world_view.begin();
        }
    }

    if (world != entt::null && registry.any_of<sim::components::ContentPack>(world)) {
        const auto& content_pack = registry.get<sim::components::ContentPack>(world);
        const data::ArchetypeDefinition* town_center =
            data::find_archetype(content_pack.content, "town_center");
        if (town_center != nullptr && town_center->build_wood_cost > 0) {
            options.town_center_wood_cost = town_center->build_wood_cost;
        }
        if (town_center != nullptr && town_center->spawn_worker_food_cost > 0) {
            options.worker_food_cost = town_center->spawn_worker_food_cost;
        }
        if (town_center != nullptr && town_center->spawn_militia_food_cost > 0) {
            options.militia_food_cost = town_center->spawn_militia_food_cost;
        }

        const data::ArchetypeDefinition* house =
            data::find_archetype(content_pack.content, std::string(constants::HOUSE_BUILDING_ID));
        if (house != nullptr && house->build_wood_cost > 0) {
            options.house_wood_cost = house->build_wood_cost;
        }
    }

    if (render_snapshot != nullptr
        && local_player_slot_ < render_snapshot->hud_by_player.size()) {
        const int town_wood = render_snapshot->hud_by_player[local_player_slot_].town_wood;
        const int town_food = render_snapshot->hud_by_player[local_player_slot_].town_food;
        options.can_afford_town_center = town_wood >= options.town_center_wood_cost;
        options.can_afford_house = town_wood >= options.house_wood_cost;
        options.can_afford_worker = town_food >= options.worker_food_cost;
        options.can_afford_militia = town_food >= options.militia_food_cost;
        return options;
    }

    options.can_afford_town_center = sim::player::can_afford_player_wood(
        registry,
        local_player_slot_,
        options.town_center_wood_cost);
    options.can_afford_house = sim::player::can_afford_player_wood(
        registry,
        local_player_slot_,
        options.house_wood_cost);
    options.can_afford_worker = sim::player::can_afford_player_food(
        registry,
        local_player_slot_,
        options.worker_food_cost);
    options.can_afford_militia = sim::player::can_afford_player_food(
        registry,
        local_player_slot_,
        options.militia_food_cost);
    return options;
}

void GameInput::sync_command_panel_mode(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot)
{
    if (command_panel_mode_ == CommandPanelMode::BuildMenu
        || command_panel_mode_ == CommandPanelMode::PlaceTownCenter
        || command_panel_mode_ == CommandPanelMode::PlaceHouse) {
        if (!selection_has_worker(simulation, render_snapshot)) {
            command_panel_mode_ = CommandPanelMode::Empty;
            placement_ghost_anchor_.reset();
            placement_ghost_valid_ = false;
            attack_targeting_mode_ = false;
        }
        return;
    }

    if (selection_has_worker(simulation, render_snapshot)) {
        command_panel_mode_ = CommandPanelMode::WorkerActions;
        return;
    }

    if (selection_has_militia(simulation, render_snapshot)) {
        command_panel_mode_ = CommandPanelMode::MilitiaActions;
        return;
    }

    if (selection_.building != entt::null) {
        bool under_construction = false;
        bool is_town_center = false;
        bool is_house = false;
        if (render_snapshot != nullptr) {
            for (const render::RenderEntityPose& pose : render_snapshot->buildings) {
                if (pose.entity != selection_.building) {
                    continue;
                }

                under_construction = pose.under_construction;
                is_town_center = pose.is_town_center;
                is_house = pose.is_house;
                break;
            }
        }
        else {
            auto& registry = simulation.registry();
            if (registry.valid(selection_.building)) {
                under_construction =
                    registry.any_of<sim::components::UnderConstructionTag>(selection_.building);
                is_town_center = registry.any_of<sim::components::TownCenterTag>(selection_.building);
                is_house = registry.any_of<sim::components::HouseTag>(selection_.building);
            }
        }

        std::uint8_t building_slot = local_player_slot_;
        if (render_snapshot != nullptr) {
            for (const render::RenderEntityPose& pose : render_snapshot->buildings) {
                if (pose.entity == selection_.building) {
                    building_slot = pose.player_slot;
                    break;
                }
            }
        }
        else if (simulation.registry().valid(selection_.building)) {
            building_slot =
                sim::components::entity_player_slot(simulation.registry(), selection_.building);
        }

        if (building_slot == local_player_slot_) {
            if (under_construction && (is_town_center || is_house)) {
                // Same Deselect/Destroy set as finished House.
                command_panel_mode_ = CommandPanelMode::HouseActions;
                return;
            }

            if (is_town_center && !under_construction) {
                command_panel_mode_ = CommandPanelMode::TownCenterActions;
                return;
            }

            if (is_house && !under_construction) {
                command_panel_mode_ = CommandPanelMode::HouseActions;
                return;
            }
        }
    }

    command_panel_mode_ = CommandPanelMode::Empty;
}

bool GameInput::apply_command_panel_action(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    const CommandPanelAction action)
{
    if (action != CommandPanelAction::None && game_audio_ != nullptr) {
        game_audio_->play_wooden_click();
    }

    switch (action) {
    case CommandPanelAction::Build:
        command_panel_mode_ = CommandPanelMode::BuildMenu;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::Back:
        command_panel_mode_ = CommandPanelMode::WorkerActions;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::Attack:
        attack_targeting_mode_ = true;
        return true;
    case CommandPanelAction::Stop: {
        if (selection_.units.empty()) {
            return true;
        }

        sim::player::PlayerCommand command = make_command(
            simulation,
            sim::player::PlayerCommandType::Stop,
            selection_.units,
            render_snapshot);
        submit_player_command(simulation, std::move(command));
        attack_targeting_mode_ = false;
        return true;
    }
    case CommandPanelAction::Deselect:
        clear_selection();
        return true;
    case CommandPanelAction::Kill: {
        if (selection_.units.empty()) {
            return true;
        }

        sim::player::PlayerCommand command = make_command(
            simulation,
            sim::player::PlayerCommandType::KillUnits,
            selection_.units,
            render_snapshot);
        submit_player_command(simulation, std::move(command));
        clear_selection();
        return true;
    }
    case CommandPanelAction::BuildTownCenter:
        if (!current_build_options(simulation, render_snapshot).can_afford_town_center) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceTownCenter;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::BuildHouse:
        if (!current_build_options(simulation, render_snapshot).can_afford_house) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceHouse;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::SpawnWorker:
        if (selection_.building != entt::null) {
            sim::player::PlayerCommand command{};
            if (render_snapshot != nullptr) {
                command.execute_tick = render_snapshot->tick_count
                    + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
            }
            else {
                command.execute_tick = simulation.next_command_execute_tick();
            }
            command.type = sim::player::PlayerCommandType::SpawnWorker;
            command.target_entity = selection_.building;
            submit_player_command(simulation, std::move(command));
        }
        return true;
    case CommandPanelAction::SpawnMilitia:
        if (selection_.building != entt::null) {
            sim::player::PlayerCommand command{};
            if (render_snapshot != nullptr) {
                command.execute_tick = render_snapshot->tick_count
                    + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
            }
            else {
                command.execute_tick = simulation.next_command_execute_tick();
            }
            command.type = sim::player::PlayerCommandType::SpawnMilitia;
            command.target_entity = selection_.building;
            submit_player_command(simulation, std::move(command));
        }
        return true;
    case CommandPanelAction::Destroy:
        if (selection_.building != entt::null) {
            sim::player::PlayerCommand command{};
            if (render_snapshot != nullptr) {
                command.execute_tick = render_snapshot->tick_count
                    + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
            }
            else {
                command.execute_tick = simulation.next_command_execute_tick();
            }
            command.type = sim::player::PlayerCommandType::DestroyBuilding;
            command.target_entity = selection_.building;
            submit_player_command(simulation, std::move(command));
            clear_selection();
        }
        return true;
    case CommandPanelAction::None:
        break;
    }

    return false;
}

bool GameInput::handle_command_panel_click(
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    const sf::Vector2f screen_position)
{
    (void)renderer;
    if (hit_test_command_panel_frame(window.getSize(), screen_position.x, screen_position.y)
        && command_panel_mode_ != CommandPanelMode::PlaceTownCenter
        && command_panel_mode_ != CommandPanelMode::PlaceHouse) {
        const CommandPanelAction action = hit_test_command_panel(
            command_panel_mode_,
            window.getSize(),
            screen_position.x,
            screen_position.y,
            current_build_options(simulation, render_snapshot));
        if (action == CommandPanelAction::None) {
            return true;
        }

        return apply_command_panel_action(simulation, render_snapshot, action);
    }

    return false;
}

namespace {

[[nodiscard]] bool resolve_map_size(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    int& map_width,
    int& map_height)
{
    if (render_snapshot != nullptr) {
        map_width = render_snapshot->map_width;
        map_height = render_snapshot->map_height;
        return map_width > 0 && map_height > 0;
    }

    auto& registry = simulation.registry();
    const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
    if (world_view.begin() == world_view.end()) {
        return false;
    }

    const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
    map_width = map.width;
    map_height = map.height;
    return map_width > 0 && map_height > 0;
}

[[nodiscard]] bool selection_contains_entity(
    const std::vector<entt::entity>& units,
    const entt::entity entity)
{
    return std::find(units.begin(), units.end(), entity) != units.end();
}

[[nodiscard]] bool can_place_ghost_footprint(
    const sim::components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos anchor,
    const int footprint,
    const std::vector<entt::entity>& ignore_units)
{
    for (int y = 0; y < footprint; ++y) {
        for (int x = 0; x < footprint; ++x) {
            const core::GridPos cell{anchor.x + x, anchor.y + y};
            if (!sim::systems::is_tile_walkable(map, cell, false)) {
                return false;
            }

            const auto building_view = registry.view<
                sim::components::BuildingTag,
                sim::components::GridPosition,
                sim::components::Health>();
            for (const entt::entity building : building_view) {
                if (building_view.get<sim::components::Health>(building).current.raw() <= 0) {
                    continue;
                }

                sim::components::BuildingFootprint other_footprint{};
                if (registry.any_of<sim::components::BuildingFootprint>(building)) {
                    other_footprint = registry.get<sim::components::BuildingFootprint>(building);
                }
                other_footprint = sim::components::effective_building_footprint(
                    other_footprint,
                    registry.any_of<sim::components::TownCenterTag>(building));
                if (sim::components::building_contains_cell(
                        building_view.get<sim::components::GridPosition>(building),
                        other_footprint,
                        cell)) {
                    return false;
                }
            }

            const auto unit_view = registry.view<
                sim::components::UnitTag,
                sim::components::GridPosition,
                sim::components::Health>();
            for (const entt::entity entity : unit_view) {
                if (unit_view.get<sim::components::Health>(entity).current.raw() <= 0) {
                    continue;
                }
                if (selection_contains_entity(ignore_units, entity)) {
                    continue;
                }
                if (sim::systems::unit_occupancy_grid_cell(registry, entity) == cell) {
                    return false;
                }
            }
        }
    }

    return true;
}

} // namespace

bool GameInput::handle_minimap_navigation(
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    const sf::Vector2f screen_position)
{
    int map_width = 0;
    int map_height = 0;
    if (!resolve_map_size(simulation, render_snapshot, map_width, map_height)) {
        return false;
    }

    const auto world = minimap_screen_to_world(
        window.getSize(),
        screen_position.x,
        screen_position.y,
        map_width,
        map_height);
    if (!world.has_value()) {
        return false;
    }

    renderer.center_camera_on_world_keep_zoom(world->first, world->second);
    return true;
}

sim::player::SelectionModifyMode GameInput::current_modify_mode() const
{
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl)
        || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl)) {
        return sim::player::SelectionModifyMode::Toggle;
    }

    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift)
        || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)) {
        return sim::player::SelectionModifyMode::Add;
    }

    return sim::player::SelectionModifyMode::Replace;
}

void GameInput::reset_frame_clock()
{
    previous_frame_time_ = std::chrono::steady_clock::now();
    frame_clock_initialized_ = true;
}

void GameInput::update_hover(
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot)
{
    hover_ = HoverHighlight{};

    if (left_button_down_) {
        return;
    }

    const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
    const sf::Vector2f screen_position{
        static_cast<float>(mouse_position.x),
        static_cast<float>(mouse_position.y),
    };
    const float pick_radius_px = renderer.selection_pick_radius_px();

    const auto hovered_grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
    if (hovered_grid_cell.has_value()) {
        if (render_snapshot != nullptr) {
            if (render::snapshot_cell_is_unexplored(*render_snapshot, *hovered_grid_cell)) {
                return;
            }
        }
        else {
            const auto world_view = simulation.registry().view<
                sim::components::WorldTag,
                sim::components::FogOfWarState>();
            if (world_view.begin() != world_view.end()) {
                const auto& fog = world_view.get<sim::components::FogOfWarState>(*world_view.begin());
                if (!sim::systems::is_cell_visible_to_slot(fog, *hovered_grid_cell, local_player_slot_)
                    && !sim::systems::is_cell_explored_to_slot(fog, *hovered_grid_cell, local_player_slot_)) {
                    return;
                }
            }
        }
    }

    if (render_snapshot != nullptr) {
        const entt::entity hovered_unit = render::pick_hovered_unit_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px);
        if (hovered_unit != entt::null) {
            hover_.unit = hovered_unit;
            for (const render::RenderEntityPose& pose : render_snapshot->units) {
                if (pose.entity == hovered_unit) {
                    hover_.unit_is_enemy = pose.is_enemy;
                    break;
                }
            }
            return;
        }

        if (hovered_grid_cell.has_value()) {
            for (const render::RenderEntityPose& pose : render_snapshot->buildings) {
                if (pose.health_current <= 0) {
                    continue;
                }

                const sim::components::BuildingFootprint footprint{
                    pose.footprint_width,
                    pose.footprint_height,
                };
                const sim::components::GridPosition anchor{{pose.grid_x, pose.grid_y}};
                if (sim::components::building_contains_cell(anchor, footprint, *hovered_grid_cell)) {
                    hover_.building = pose.entity;
                    hover_.building_is_enemy = pose.player_slot != local_player_slot_;
                    return;
                }
            }
        }

        const entt::entity hovered_building = render::pick_player_building_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);
        if (hovered_building != entt::null) {
            hover_.building = hovered_building;
            hover_.building_is_enemy = false;
            return;
        }

        const entt::entity hovered_enemy_building = render::pick_enemy_building_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);
        if (hovered_enemy_building != entt::null) {
            hover_.building = hovered_enemy_building;
            hover_.building_is_enemy = true;
            return;
        }

        bool allow_resource_hover = !selection_.has_units();
        if (!allow_resource_hover) {
            for (const entt::entity entity : selection_.units) {
                for (const render::RenderEntityPose& pose : render_snapshot->units) {
                    if (pose.entity == entity && pose.is_worker) {
                        allow_resource_hover = true;
                        break;
                    }
                }

                if (allow_resource_hover) {
                    break;
                }
            }
        }

        if (allow_resource_hover) {
            hover_.resource_cell = render::pick_resource_forest_at_screen(
                *render_snapshot,
                renderer,
                screen_position,
                pick_radius_px,
                local_player_slot_);
        }

        return;
    }

    auto& registry = simulation.registry();

    const entt::entity hovered_unit = sim::player::pick_hovered_unit_at_screen(
        registry,
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);
    if (hovered_unit != entt::null) {
        hover_.unit = hovered_unit;
        hover_.unit_is_enemy =
            sim::components::is_opponent_entity(registry, hovered_unit, local_player_slot_);
        return;
    }

    if (hovered_grid_cell.has_value()) {
        const auto building_view = registry.view<
            sim::components::BuildingTag,
            sim::components::PlayerOwnedTag,
            sim::components::GridPosition,
            sim::components::Health>();
        for (const entt::entity entity : building_view) {
            const auto& health = building_view.get<sim::components::Health>(entity);
            if (health.current.raw() <= 0) {
                continue;
            }

            const auto& anchor = building_view.get<sim::components::GridPosition>(entity);
            sim::components::BuildingFootprint footprint{};
            if (registry.any_of<sim::components::BuildingFootprint>(entity)) {
                footprint = registry.get<sim::components::BuildingFootprint>(entity);
            }
            footprint = sim::components::effective_building_footprint(
                footprint,
                registry.any_of<sim::components::TownCenterTag>(entity));
            if (sim::components::building_contains_cell(anchor, footprint, *hovered_grid_cell)) {
                hover_.building = entity;
                hover_.building_is_enemy =
                    sim::components::entity_player_slot(registry, entity) != local_player_slot_;
                return;
            }
        }
    }

    const entt::entity hovered_building = sim::player::pick_player_building_at_screen(
        registry,
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);
    if (hovered_building != entt::null) {
        hover_.building = hovered_building;
        hover_.building_is_enemy = false;
        return;
    }

    const entt::entity hovered_enemy_building = sim::player::pick_enemy_building_at_screen(
        registry,
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);
    if (hovered_enemy_building != entt::null) {
        hover_.building = hovered_enemy_building;
        hover_.building_is_enemy = true;
        return;
    }

    bool allow_resource_hover = !selection_.has_units();
    if (!allow_resource_hover) {
        for (const entt::entity entity : selection_.units) {
            if (registry.any_of<sim::components::WorkerUnitTag>(entity)) {
                allow_resource_hover = true;
                break;
            }
        }
    }

    if (allow_resource_hover) {
        hover_.resource_cell = sim::player::pick_resource_forest_at_screen(
            registry,
            renderer,
            screen_position,
            renderer.selection_pick_radius_px(),
            local_player_slot_);
    }
}

void GameInput::sync_audio_volumes_from_menu()
{
    if (game_audio_ == nullptr) {
        return;
    }

    game_audio_->set_master_volume(game_menu_.master_volume);
    game_audio_->set_music_volume(game_menu_.music_volume);
    game_audio_->set_sfx_volume(game_menu_.sfx_volume);
}

void GameInput::apply_game_menu_action(const GameMenuAction action)
{
    switch (action) {
    case GameMenuAction::None:
        return;
    case GameMenuAction::ToggleMenu:
        game_menu_.toggle();
        return;
    case GameMenuAction::Resume:
        game_menu_.close();
        return;
    case GameMenuAction::Save:
    case GameMenuAction::Load:
        return;
    case GameMenuAction::ExitToMainMenu:
        exit_to_main_menu_requested_ = true;
        game_menu_.close();
        return;
    case GameMenuAction::OpenSettings:
        game_menu_.screen = GameMenuScreen::SettingsGame;
        game_menu_.dragging_slider = GameMenuSlider::None;
        return;
    case GameMenuAction::ExitGame:
        exit_game_requested_ = true;
        game_menu_.close();
        return;
    case GameMenuAction::SettingsBack:
        game_menu_.open_main();
        return;
    case GameMenuAction::SettingsTabGame:
        game_menu_.screen = GameMenuScreen::SettingsGame;
        game_menu_.dragging_slider = GameMenuSlider::None;
        return;
    case GameMenuAction::SettingsTabAudio:
        game_menu_.screen = GameMenuScreen::SettingsAudio;
        game_menu_.dragging_slider = GameMenuSlider::None;
        return;
    case GameMenuAction::ToggleFullscreen:
        fullscreen_toggle_requested_ = true;
        return;
    case GameMenuAction::BeginDragMaster:
        game_menu_.dragging_slider = GameMenuSlider::Master;
        return;
    case GameMenuAction::BeginDragMusic:
        game_menu_.dragging_slider = GameMenuSlider::Music;
        return;
    case GameMenuAction::BeginDragSfx:
        game_menu_.dragging_slider = GameMenuSlider::Sfx;
        return;
    }
}

bool GameInput::handle_game_menu_event(const sf::Event& event, const sf::Window& window)
{
    const sf::Vector2u window_size = window.getSize();

    if (const auto* key_pressed = event.getIf<sf::Event::KeyPressed>()) {
        if (key_pressed->code != sf::Keyboard::Key::Escape) {
            return game_menu_.is_open();
        }

        if (game_menu_.screen == GameMenuScreen::SettingsGame
            || game_menu_.screen == GameMenuScreen::SettingsAudio) {
            game_menu_.open_main();
            return true;
        }

        game_menu_.toggle();
        return true;
    }

    if (const auto* mouse_pressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (mouse_pressed->button != sf::Mouse::Button::Left) {
            return game_menu_.is_open();
        }

        const sf::Vector2i mouse = sf::Mouse::getPosition(window);
        const float mouse_x = static_cast<float>(mouse.x);
        const float mouse_y = static_cast<float>(mouse.y);

        if (!game_menu_.is_open()) {
            if (menu_button_rect(window_size).contains(mouse_x, mouse_y)) {
                game_menu_.open_main();
                left_button_down_ = false;
                left_press_position_.reset();
                selection_box_.active = false;
                return true;
            }
            return false;
        }

        if (game_menu_.screen == GameMenuScreen::Main) {
            const GameMenuAction action =
                hit_test_menu_button(build_main_menu_buttons(window_size), mouse_x, mouse_y);
            if (action != GameMenuAction::None) {
                apply_game_menu_action(action);
            }
            return true;
        }

        if (game_menu_.screen == GameMenuScreen::SettingsAudio) {
            const GameMenuSlider slider = hit_test_volume_slider(window_size, mouse_x, mouse_y);
            if (slider != GameMenuSlider::None) {
                game_menu_.dragging_slider = slider;
                apply_slider_drag(game_menu_, window_size, mouse_x);
                sync_audio_volumes_from_menu();
                return true;
            }
        }

        const GameMenuAction action = hit_test_menu_button(
            build_settings_buttons(game_menu_, window_size),
            mouse_x,
            mouse_y);
        if (action != GameMenuAction::None) {
            apply_game_menu_action(action);
        }
        return true;
    }

    if (const auto* mouse_released = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (mouse_released->button == sf::Mouse::Button::Left) {
            game_menu_.dragging_slider = GameMenuSlider::None;
        }
        return game_menu_.is_open();
    }

    if (event.getIf<sf::Event::MouseMoved>() != nullptr) {
        if (game_menu_.dragging_slider != GameMenuSlider::None) {
            const sf::Vector2i mouse = sf::Mouse::getPosition(window);
            apply_slider_drag(game_menu_, window_size, static_cast<float>(mouse.x));
            sync_audio_volumes_from_menu();
        }
        return game_menu_.is_open();
    }

    if (event.getIf<sf::Event::MouseWheelScrolled>() != nullptr) {
        return game_menu_.is_open();
    }

    return game_menu_.is_open();
}

void GameInput::update_continuous(
    sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot)
{
    if (game_menu_.is_open()) {
        selection_box_.active = false;
        left_button_down_ = false;
        left_press_position_.reset();
        minimap_navigation_active_ = false;
        placement_ghost_anchor_.reset();
        placement_ghost_valid_ = false;
        hover_ = {};
        if (game_menu_.dragging_slider != GameMenuSlider::None) {
            const sf::Vector2i mouse = sf::Mouse::getPosition(window);
            apply_slider_drag(game_menu_, window.getSize(), static_cast<float>(mouse.x));
            sync_audio_volumes_from_menu();
        }
        update_game_cursor(window, simulation, render_snapshot);
        return;
    }

    if (render_snapshot != nullptr) {
        render::prune_dead_selection(selection_.units, *render_snapshot, local_player_slot_);
    }
    else {
        sim::player::prune_dead_selection(selection_.units, simulation.registry());
    }

    const auto now = std::chrono::steady_clock::now();
    if (!frame_clock_initialized_) {
        previous_frame_time_ = now;
        frame_clock_initialized_ = true;
        return;
    }

    const float delta_seconds = std::clamp(
        static_cast<float>(
            std::chrono::duration<double>(now - previous_frame_time_).count()),
        0.0F,
        0.1F);
    previous_frame_time_ = now;

    if (left_button_down_ && minimap_navigation_active_) {
        selection_box_.active = false;
        const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
        const sf::Vector2f screen_position{
            static_cast<float>(mouse_position.x),
            static_cast<float>(mouse_position.y),
        };
        (void)handle_minimap_navigation(
            window,
            renderer,
            simulation,
            render_snapshot,
            screen_position);
    }
    else if (left_button_down_ && left_press_position_.has_value()) {
        selection_box_.active = true;
        selection_box_.start = sf::Vector2f{
            static_cast<float>(left_press_position_->x),
            static_cast<float>(left_press_position_->y),
        };
        const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
        selection_box_.current = sf::Vector2f{
            static_cast<float>(mouse_position.x),
            static_cast<float>(mouse_position.y),
        };
    }
    else {
        selection_box_.active = false;
    }

    update_hover(window, renderer, simulation, render_snapshot);

    placement_ghost_anchor_.reset();
    placement_ghost_valid_ = false;
    if (command_panel_mode_ == CommandPanelMode::PlaceTownCenter
        || command_panel_mode_ == CommandPanelMode::PlaceHouse) {
        const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
        const auto center_cell = renderer.screen_to_grid(
            static_cast<float>(mouse_position.x),
            static_cast<float>(mouse_position.y));
        if (center_cell.has_value()) {
            const bool placing_house = command_panel_mode_ == CommandPanelMode::PlaceHouse;
            const core::GridPos anchor = placing_house
                ? house_anchor_from_center_cell(*center_cell)
                : town_center_anchor_from_center_cell(*center_cell);
            placement_ghost_anchor_ = anchor;
            const int footprint = placing_house
                ? constants::HOUSE_FOOTPRINT_TILES
                : constants::TOWN_CENTER_FOOTPRINT_TILES;
            auto& registry = simulation.registry();
            const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
            if (world_view.begin() != world_view.end()) {
                const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
                placement_ghost_valid_ = can_place_ghost_footprint(
                    map,
                    registry,
                    anchor,
                    footprint,
                    selection_.units);
            }
            else if (render_snapshot != nullptr) {
                placement_ghost_valid_ = true;
                for (int y = 0; y < footprint && placement_ghost_valid_; ++y) {
                    for (int x = 0; x < footprint; ++x) {
                        const core::GridPos cell{anchor.x + x, anchor.y + y};
                        if (!core::is_inside_grid(
                                cell,
                                render_snapshot->map_width,
                                render_snapshot->map_height)) {
                            placement_ghost_valid_ = false;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (!left_button_down_) {
        float pan_x = 0.0F;
        float pan_y = 0.0F;

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left)) {
            pan_x += 1.0F;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right)) {
            pan_x -= 1.0F;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up)) {
            pan_y += 1.0F;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down)) {
            pan_y -= 1.0F;
        }

        const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
        const sf::Vector2u window_size = window.getSize();
        const int edge_margin = constants::CAMERA_EDGE_SCROLL_MARGIN_PX;

        if (mouse_position.x >= 0
            && mouse_position.y >= 0
            && static_cast<unsigned int>(mouse_position.x) < window_size.x
            && static_cast<unsigned int>(mouse_position.y) < window_size.y) {
            if (mouse_position.x < edge_margin) {
                pan_x += 1.0F;
            }
            else if (mouse_position.x >= static_cast<int>(window_size.x) - edge_margin) {
                pan_x -= 1.0F;
            }

            if (mouse_position.y < edge_margin) {
                pan_y += 1.0F;
            }
            else if (mouse_position.y >= static_cast<int>(window_size.y) - edge_margin) {
                pan_y -= 1.0F;
            }
        }

        const float pan_length = std::sqrt(pan_x * pan_x + pan_y * pan_y);
        if (pan_length > 0.0F) {
            pan_x /= pan_length;
            pan_y /= pan_length;

            const bool keyboard_active = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left)
                || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right)
                || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up)
                || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down);

            const float speed = keyboard_active
                ? constants::CAMERA_KEYBOARD_PAN_SPEED_PX_PER_SEC
                : constants::CAMERA_EDGE_SCROLL_SPEED_PX_PER_SEC;

            renderer.pan_camera(
                pan_x * speed * delta_seconds,
                pan_y * speed * delta_seconds);
        }
    }

    renderer.update_camera(delta_seconds);
    update_game_cursor(window, simulation, render_snapshot);
}

CursorShape GameInput::resolve_cursor_shape(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot) const
{
    if (command_panel_mode_ == CommandPanelMode::PlaceTownCenter
        || command_panel_mode_ == CommandPanelMode::PlaceHouse) {
        if (!placement_ghost_anchor_.has_value()) {
            return CursorShape::Cross;
        }

        return placement_ghost_valid_ ? CursorShape::Check : CursorShape::Cross;
    }

    if (selection_.units.empty()) {
        return CursorShape::Normal;
    }

    if (hover_.unit != entt::null) {
        return hover_.unit_is_enemy ? CursorShape::Attack : CursorShape::AttackRestricted;
    }

    if (hover_.building != entt::null) {
        if (hover_.building_is_enemy) {
            return CursorShape::Attack;
        }

        if (selection_has_worker(simulation, render_snapshot)) {
            return CursorShape::Target;
        }

        return CursorShape::AttackRestricted;
    }

    if (hover_.resource_cell.has_value()
        && selection_has_worker(simulation, render_snapshot)) {
        return CursorShape::Target;
    }

    if (attack_targeting_mode_) {
        return CursorShape::Restricted;
    }

    return CursorShape::Normal;
}

void GameInput::update_game_cursor(
    sf::Window& window,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot)
{
    if (game_cursor_ == nullptr) {
        return;
    }

    game_cursor_->set_player_color(cursor_color_for_player_slot(local_player_slot_));
    game_cursor_->set_shape(resolve_cursor_shape(simulation, render_snapshot));
    game_cursor_->apply(window);
}

void GameInput::finalize_left_release(
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const sf::Vector2i mouse_position,
    const render::SimRenderSnapshot* render_snapshot)
{
    struct PanelSyncGuard {
        std::function<void()> on_exit{};
        ~PanelSyncGuard()
        {
            if (on_exit) {
                on_exit();
            }
        }
    } panel_sync_guard{
        [this, &simulation, render_snapshot]() {
            sync_command_panel_mode(simulation, render_snapshot);
        },
    };

    const sim::player::SelectionModifyMode mode = current_modify_mode();
    const sf::Vector2f screen_position{
        static_cast<float>(mouse_position.x),
        static_cast<float>(mouse_position.y),
    };
    const float pick_radius_px = renderer.selection_pick_radius_px();

    if (minimap_navigation_active_) {
        minimap_navigation_active_ = false;
        (void)handle_minimap_navigation(
            window,
            renderer,
            simulation,
            render_snapshot,
            screen_position);
        return;
    }

    if (handle_command_panel_click(
            window,
            renderer,
            simulation,
            render_snapshot,
            screen_position)) {
        return;
    }

    if (attack_targeting_mode_) {
        attack_targeting_mode_ = false;
        if (try_issue_attack_at_screen(
                window,
                renderer,
                simulation,
                render_snapshot,
                screen_position)) {
            return;
        }
    }

    if (command_panel_mode_ == CommandPanelMode::PlaceTownCenter
        || command_panel_mode_ == CommandPanelMode::PlaceHouse) {
        if (hit_test_command_panel_frame(
                window.getSize(),
                screen_position.x,
                screen_position.y)) {
            return;
        }

        const auto center_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
        if (!center_cell.has_value() || selection_.units.empty() || !placement_ghost_valid_) {
            return;
        }

        const bool placing_house = command_panel_mode_ == CommandPanelMode::PlaceHouse;
        sim::player::PlayerCommand command = make_command(
            simulation,
            placing_house ? sim::player::PlayerCommandType::BuildHouse
                          : sim::player::PlayerCommandType::BuildTownCenter,
            selection_.units,
            render_snapshot);
        command.cell = placing_house ? house_anchor_from_center_cell(*center_cell)
                                     : town_center_anchor_from_center_cell(*center_cell);
        submit_player_command(simulation, std::move(command));
        command_panel_mode_ = CommandPanelMode::WorkerActions;
        placement_ghost_anchor_.reset();
        placement_ghost_valid_ = false;
        return;
    }

    if (render_snapshot != nullptr) {
        if (left_press_position_.has_value()) {
            const int dx = mouse_position.x - left_press_position_->x;
            const int dy = mouse_position.y - left_press_position_->y;
            const int drag_distance_sq = dx * dx + dy * dy;
            const int threshold = constants::SELECTION_BOX_DRAG_THRESHOLD_PX;

            if (drag_distance_sq >= threshold * threshold) {
                const sf::Vector2f start{
                    static_cast<float>(left_press_position_->x),
                    static_cast<float>(left_press_position_->y),
                };
                const std::vector<entt::entity> picked = render::pick_player_units_in_screen_rect(
                    *render_snapshot,
                    renderer,
                    start,
                    screen_position,
                    local_player_slot_);
                render::apply_selection_from_snapshot(
                    selection_.units,
                    *render_snapshot,
                    picked,
                    mode,
                    local_player_slot_);
                if (mode == sim::player::SelectionModifyMode::Replace && !picked.empty()) {
                    selection_.clear_resource();
                    selection_.clear_building();
                }
                if (!picked.empty()) {
                    play_select_ack_if_own_units(simulation, render_snapshot);
                }
                return;
            }
        }

        if (mode != sim::player::SelectionModifyMode::Replace) {
            const entt::entity picked = render::pick_player_unit_at_screen(
                *render_snapshot,
                renderer,
                screen_position,
                pick_radius_px,
                local_player_slot_);

            if (picked == entt::null) {
                return;
            }

            render::apply_selection_from_snapshot(
                selection_.units,
                *render_snapshot,
                {picked},
                mode,
                local_player_slot_);
            selection_.clear_resource();
            selection_.clear_building();
            play_select_ack_if_own_units(simulation, render_snapshot);
            return;
        }

        const entt::entity picked_unit = render::pick_player_unit_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);

        if (picked_unit != entt::null) {
            selection_.units = {picked_unit};
            selection_.clear_resource();
            selection_.clear_building();
            play_select_ack_if_own_units(simulation, render_snapshot);
            return;
        }

        const entt::entity picked_enemy_unit = render::pick_enemy_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);
        if (picked_enemy_unit != entt::null) {
            selection_.units = {picked_enemy_unit};
            selection_.clear_resource();
            selection_.clear_building();
            return;
        }

        const entt::entity picked_building = render::pick_player_building_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);

        if (picked_building != entt::null) {
            selection_.clear_units();
            selection_.clear_resource();
            selection_.building = picked_building;
            return;
        }

        const entt::entity picked_enemy_building = render::pick_enemy_building_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);
        if (picked_enemy_building != entt::null) {
            selection_.clear_units();
            selection_.clear_resource();
            selection_.building = picked_enemy_building;
            return;
        }

        const std::optional<core::GridPos> picked_resource = render::pick_resource_forest_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);

        if (picked_resource.has_value()) {
            selection_.clear_units();
            selection_.clear_building();
            selection_.resource_cell = picked_resource;
            return;
        }

        selection_.clear();
        return;
    }

    if (left_press_position_.has_value()) {
        const int dx = mouse_position.x - left_press_position_->x;
        const int dy = mouse_position.y - left_press_position_->y;
        const int drag_distance_sq = dx * dx + dy * dy;
        const int threshold = constants::SELECTION_BOX_DRAG_THRESHOLD_PX;

        if (drag_distance_sq >= threshold * threshold) {
            const sf::Vector2f start{
                static_cast<float>(left_press_position_->x),
                static_cast<float>(left_press_position_->y),
            };
            const std::vector<entt::entity> picked = sim::player::pick_player_units_in_screen_rect(
                simulation.registry(),
                renderer,
                start,
                screen_position,
                local_player_slot_);
            sim::player::apply_selection(selection_.units, simulation.registry(), picked, mode);
            if (mode == sim::player::SelectionModifyMode::Replace && !picked.empty()) {
                selection_.clear_resource();
                selection_.clear_building();
            }
            if (!picked.empty()) {
                play_select_ack_if_own_units(simulation, nullptr);
            }
            return;
        }
    }

    if (mode != sim::player::SelectionModifyMode::Replace) {
        const entt::entity picked = sim::player::pick_player_unit_at_screen(
            simulation.registry(),
            renderer,
            screen_position,
            renderer.selection_pick_radius_px(),
            local_player_slot_);

        if (picked == entt::null) {
            return;
        }

        sim::player::apply_selection(
            selection_.units,
            simulation.registry(),
            {picked},
            mode);
        selection_.clear_resource();
        selection_.clear_building();
        play_select_ack_if_own_units(simulation, nullptr);
        return;
    }

    const entt::entity picked_unit = sim::player::pick_player_unit_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);

    if (picked_unit != entt::null) {
        selection_.units = {picked_unit};
        selection_.clear_resource();
        selection_.clear_building();
        play_select_ack_if_own_units(simulation, nullptr);
        return;
    }

    const entt::entity picked_enemy_unit = sim::player::pick_enemy_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);
    if (picked_enemy_unit != entt::null) {
        selection_.units = {picked_enemy_unit};
        selection_.clear_resource();
        selection_.clear_building();
        return;
    }

    const entt::entity picked_building = sim::player::pick_player_building_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);

    if (picked_building != entt::null) {
        selection_.clear_units();
        selection_.clear_resource();
        selection_.building = picked_building;
        return;
    }

    const entt::entity picked_enemy_building = sim::player::pick_enemy_building_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);
    if (picked_enemy_building != entt::null) {
        selection_.clear_units();
        selection_.clear_resource();
        selection_.building = picked_enemy_building;
        return;
    }

    const std::optional<core::GridPos> picked_resource = sim::player::pick_resource_forest_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);

    if (picked_resource.has_value()) {
        selection_.clear_units();
        selection_.clear_building();
        selection_.resource_cell = picked_resource;
        return;
    }

    selection_.clear();
}

void GameInput::submit_chat_message(std::string text)
{
    if (text.empty()) {
        return;
    }

    if (text.size() > static_cast<std::size_t>(constants::CHAT_MAX_MESSAGE_LENGTH)) {
        text.resize(static_cast<std::size_t>(constants::CHAT_MAX_MESSAGE_LENGTH));
    }

    if (lockstep_session_ != nullptr) {
        lockstep_session_->send_chat_message(text);
        return;
    }

    if (chat_state_ != nullptr) {
        chat_state_->push_message(local_player_slot_, std::move(text));
    }
}

bool GameInput::handle_chat_event(const sf::Event& event)
{
    if (const auto* key_pressed = event.getIf<sf::Event::KeyPressed>()) {
        if (!chat_composing_ && key_pressed->code == sf::Keyboard::Key::Enter) {
            chat_composing_ = true;
            chat_draft_.clear();
            return true;
        }

        if (!chat_composing_) {
            return false;
        }

        if (key_pressed->code == sf::Keyboard::Key::Escape) {
            chat_composing_ = false;
            chat_draft_.clear();
            return true;
        }

        if (key_pressed->code == sf::Keyboard::Key::Enter) {
            const std::string message = chat_draft_;
            chat_composing_ = false;
            chat_draft_.clear();
            if (!message.empty()) {
                submit_chat_message(message);
            }
            return true;
        }

        if (key_pressed->code == sf::Keyboard::Key::Backspace) {
            if (!chat_draft_.empty()) {
                chat_draft_.pop_back();
            }
            return true;
        }

        return true;
    }

    if (!chat_composing_) {
        return false;
    }

    if (const auto* text_entered = event.getIf<sf::Event::TextEntered>()) {
        const auto unicode = text_entered->unicode;
        if (unicode == 8U || unicode == 13U || unicode == 27U) {
            return true;
        }

        if (unicode < 32U || unicode > 126U) {
            return true;
        }

        if (chat_draft_.size()
            >= static_cast<std::size_t>(constants::CHAT_MAX_MESSAGE_LENGTH)) {
            return true;
        }

        chat_draft_.push_back(static_cast<char>(unicode));
        return true;
    }

    return chat_composing_;
}

bool GameInput::try_issue_attack_at_screen(
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    const sf::Vector2f screen_position)
{
    (void)window;
    if (selection_.units.empty()) {
        return false;
    }

    const float pick_radius_px = renderer.selection_pick_radius_px();
    if (render_snapshot != nullptr) {
        const entt::entity enemy = render::pick_enemy_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);
        if (enemy != entt::null) {
            sim::player::PlayerCommand command = make_command(
                simulation,
                sim::player::PlayerCommandType::Attack,
                selection_.units,
                render_snapshot);
            command.target_entity = enemy;
            submit_player_command(simulation, std::move(command));
            return true;
        }

        const entt::entity enemy_building = render::pick_enemy_building_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);
        if (enemy_building != entt::null) {
            sim::player::PlayerCommand command = make_command(
                simulation,
                sim::player::PlayerCommandType::Attack,
                selection_.units,
                render_snapshot);
            command.target_entity = enemy_building;
            submit_player_command(simulation, std::move(command));
            return true;
        }

        return false;
    }

    auto& registry = simulation.registry();
    const entt::entity enemy = sim::player::pick_enemy_at_screen(
        registry,
        renderer,
        screen_position,
        pick_radius_px,
        local_player_slot_);
    if (enemy != entt::null) {
        sim::player::PlayerCommand command =
            make_command(simulation, sim::player::PlayerCommandType::Attack, selection_.units, nullptr);
        command.target_entity = enemy;
        submit_player_command(simulation, std::move(command));
        return true;
    }

    const entt::entity enemy_building = sim::player::pick_enemy_building_at_screen(
        registry,
        renderer,
        screen_position,
        pick_radius_px,
        local_player_slot_);
    if (enemy_building != entt::null) {
        sim::player::PlayerCommand command =
            make_command(simulation, sim::player::PlayerCommandType::Attack, selection_.units, nullptr);
        command.target_entity = enemy_building;
        submit_player_command(simulation, std::move(command));
        return true;
    }

    return false;
}

render::HudUnitContext GameInput::make_hud_context(
    const sf::Window& window,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot) const
{
    render::HudUnitContext context{};
    context.command_panel_mode = command_panel_mode_;
    context.build_options = current_build_options(simulation, render_snapshot);
    const sf::Vector2i mouse = sf::Mouse::getPosition(window);
    context.mouse_screen_position = sf::Vector2f{
        static_cast<float>(mouse.x),
        static_cast<float>(mouse.y),
    };
    context.chat_composing = chat_composing_;
    context.chat_draft = chat_draft_;
    context.game_menu = game_menu_;
    if (chat_state_ != nullptr) {
        context.chat_lines = chat_state_->snapshot();
    }

    if (command_panel_pressed_slot_ >= 0
        && std::chrono::steady_clock::now() < command_panel_press_until_) {
        context.command_panel_pressed_slot = command_panel_pressed_slot_;
    }

    context.selected_resource_cell = selection_.resource_cell;
    context.hover_unit = hover_.unit;
    context.hover_unit_is_enemy = hover_.unit_is_enemy;
    if (selection_.units.size() == 1U) {
        context.selected_single_unit = selection_.units.front();
    }
    else if (selection_.building != entt::null) {
        context.selected_single_unit = selection_.building;
    }

    if (selection_.building == entt::null) {
        return context;
    }

    if (render_snapshot != nullptr) {
        for (const render::RenderEntityPose& pose : render_snapshot->buildings) {
            if (pose.entity != selection_.building) {
                continue;
            }

            if (pose.health_current > 0) {
                context.has_selected_building_health = true;
                context.selected_building_health_current = pose.health_current;
                context.selected_building_health_max = pose.health_max;
                context.selected_building_is_house = pose.is_house;
                context.has_selected_building_owner = true;
                context.selected_building_player_slot = pose.player_slot;
            }
            break;
        }
        return context;
    }

    auto& registry = simulation.registry();
    if (registry.valid(selection_.building)
        && registry.any_of<sim::components::Health>(selection_.building)) {
        const auto& health = registry.get<sim::components::Health>(selection_.building);
        if (health.current.raw() > 0) {
            context.has_selected_building_health = true;
            context.selected_building_health_current = health.current.to_int();
            context.selected_building_health_max = health.max.to_int();
            context.selected_building_is_house =
                registry.any_of<sim::components::HouseTag>(selection_.building);
            if (registry.any_of<sim::components::PlayerOwnedTag>(selection_.building)) {
                context.has_selected_building_owner = true;
                context.selected_building_player_slot =
                    sim::components::entity_player_slot(registry, selection_.building);
            }
        }
    }

    return context;
}

bool GameInput::handle_event(
    const sf::Event& event,
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot)
{
    if (handle_chat_event(event)) {
        return true;
    }

    if (game_menu_.is_open()) {
        return handle_game_menu_event(event, window);
    }

    if (const auto* key_pressed = event.getIf<sf::Event::KeyPressed>()) {
        if (key_pressed->code == sf::Keyboard::Key::Escape) {
            if (attack_targeting_mode_) {
                attack_targeting_mode_ = false;
                return true;
            }

            if (command_panel_mode_ == CommandPanelMode::PlaceTownCenter
                || command_panel_mode_ == CommandPanelMode::PlaceHouse
                || command_panel_mode_ == CommandPanelMode::BuildMenu) {
                command_panel_mode_ = selection_has_worker(simulation, render_snapshot)
                    ? CommandPanelMode::WorkerActions
                    : CommandPanelMode::Empty;
                return true;
            }

            game_menu_.open_main();
            return true;
        }

        if (const std::optional<int> slot = command_panel_slot_for_key(key_pressed->code);
            slot.has_value() && command_panel_mode_ != CommandPanelMode::Empty
            && command_panel_mode_ != CommandPanelMode::PlaceTownCenter
            && command_panel_mode_ != CommandPanelMode::PlaceHouse) {
            const CommandPanelAction action = action_for_command_panel_slot(
                command_panel_mode_,
                *slot,
                current_build_options(simulation, render_snapshot));
            if (action != CommandPanelAction::None) {
                command_panel_pressed_slot_ = *slot;
                command_panel_press_until_ = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(constants::HUD_COMMAND_PANEL_KEY_PRESS_TTL_MS);
                return apply_command_panel_action(simulation, render_snapshot, action);
            }
        }
    }

    if (const auto* scroll = event.getIf<sf::Event::MouseWheelScrolled>()) {
        const sf::Vector2u window_size = window.getSize();
        const int direction = scroll->delta > 0.0F ? 1 : (scroll->delta < 0.0F ? -1 : 0);
        renderer.step_zoom_camera(
            direction,
            static_cast<float>(window_size.x) * 0.5F,
            static_cast<float>(window_size.y) * 0.5F);
    }

    if (const auto* mouse_pressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (mouse_pressed->button == sf::Mouse::Button::Left) {
            left_press_position_ = sf::Mouse::getPosition(window);
            const sf::Vector2f press_screen{
                static_cast<float>(left_press_position_->x),
                static_cast<float>(left_press_position_->y),
            };
            if (menu_button_rect(window.getSize()).contains(press_screen.x, press_screen.y)) {
                game_menu_.open_main();
                left_button_down_ = false;
                left_press_position_.reset();
                selection_box_.active = false;
                return true;
            }

            left_button_down_ = true;
            minimap_navigation_active_ = hit_test_minimap_panel_frame(
                window.getSize(),
                press_screen.x,
                press_screen.y);
            if (minimap_navigation_active_) {
                selection_box_.active = false;
                (void)handle_minimap_navigation(
                    window,
                    renderer,
                    simulation,
                    render_snapshot,
                    press_screen);
            }
            else {
                selection_box_.active = true;
                selection_box_.start = press_screen;
                selection_box_.current = selection_box_.start;
            }
        }
        else if (mouse_pressed->button == sf::Mouse::Button::Right) {
            if (command_panel_mode_ == CommandPanelMode::PlaceTownCenter
                || command_panel_mode_ == CommandPanelMode::PlaceHouse
                || command_panel_mode_ == CommandPanelMode::BuildMenu) {
                command_panel_mode_ = selection_has_worker(simulation, render_snapshot)
                    ? CommandPanelMode::WorkerActions
                    : CommandPanelMode::Empty;
                return true;
            }

            if (selection_.units.empty()) {
                return true;
            }

            if (!selection_has_worker(simulation, render_snapshot)
                && !selection_has_militia(simulation, render_snapshot)) {
                return true;
            }

            const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
            const sf::Vector2f screen_position{
                static_cast<float>(mouse_position.x),
                static_cast<float>(mouse_position.y),
            };
            const float pick_radius_px = renderer.selection_pick_radius_px();

            if (hit_test_minimap_panel_frame(
                    window.getSize(),
                    screen_position.x,
                    screen_position.y)) {
                int map_width = 0;
                int map_height = 0;
                if (!resolve_map_size(simulation, render_snapshot, map_width, map_height)) {
                    return true;
                }

                const auto world = minimap_screen_to_world(
                    window.getSize(),
                    screen_position.x,
                    screen_position.y,
                    map_width,
                    map_height);
                if (!world.has_value()) {
                    return true;
                }

                const core::GridPos goal{
                    static_cast<int>(std::floor(world->first)),
                    static_cast<int>(std::floor(world->second)),
                };
                if (!core::is_inside_grid(goal, map_width, map_height)) {
                    return true;
                }

                sim::player::PlayerCommand command = make_command(
                    simulation,
                    sim::player::PlayerCommandType::Move,
                    selection_.units,
                    render_snapshot);
                command.cell = goal;
                command.has_goal_world = true;
                command.goal_world_x = math::Fixed::from_float(world->first);
                command.goal_world_y = math::Fixed::from_float(world->second);
                submit_player_command(simulation, std::move(command));
                return true;
            }

            if (render_snapshot != nullptr) {
                const entt::entity enemy = render::pick_enemy_at_screen(
                    *render_snapshot,
                    renderer,
                    screen_position,
                    pick_radius_px,
                    local_player_slot_);
                if (enemy != entt::null) {
                    sim::player::PlayerCommand command = make_command(
                        simulation,
                        sim::player::PlayerCommandType::Attack,
                        selection_.units,
                        render_snapshot);
                    command.target_entity = enemy;
                    submit_player_command(simulation, std::move(command));
                    return true;
                }

                const entt::entity enemy_building = render::pick_enemy_building_at_screen(
                    *render_snapshot,
                    renderer,
                    screen_position,
                    pick_radius_px,
                    local_player_slot_);
                if (enemy_building != entt::null) {
                    sim::player::PlayerCommand command = make_command(
                        simulation,
                        sim::player::PlayerCommandType::Attack,
                        selection_.units,
                        render_snapshot);
                    command.target_entity = enemy_building;
                    submit_player_command(simulation, std::move(command));
                    return true;
                }

                const std::optional<core::GridPos> forest_cell = render::pick_resource_forest_at_screen(
                    *render_snapshot,
                    renderer,
                    screen_position,
                    pick_radius_px,
                    local_player_slot_);
                if (forest_cell.has_value()
                    && selection_has_worker(simulation, render_snapshot)) {
                    sim::player::PlayerCommand command = make_command(
                        simulation,
                        sim::player::PlayerCommandType::Gather,
                        selection_.units,
                        render_snapshot);
                    command.cell = *forest_cell;
                    submit_player_command(simulation, std::move(command));
                    return true;
                }

                const entt::entity player_building = render::pick_player_building_at_screen(
                    *render_snapshot,
                    renderer,
                    screen_position,
                    pick_radius_px,
                    local_player_slot_);
                if (player_building != entt::null) {
                    bool under_construction = false;
                    bool is_town_center = false;
                    for (const render::RenderEntityPose& pose : render_snapshot->buildings) {
                        if (pose.entity != player_building) {
                            continue;
                        }

                        under_construction = pose.under_construction;
                        is_town_center = pose.is_town_center;
                        break;
                    }

                    if (under_construction && selection_has_worker(simulation, render_snapshot)) {
                        sim::player::PlayerCommand command = make_command(
                            simulation,
                            sim::player::PlayerCommandType::ResumeBuild,
                            selection_.units,
                            render_snapshot);
                        command.target_entity = player_building;
                        submit_player_command(simulation, std::move(command));
                        return true;
                    }

                    if (is_town_center) {
                        submit_player_command(
                            simulation,
                            make_command(
                                simulation,
                                sim::player::PlayerCommandType::Deposit,
                                selection_.units,
                                render_snapshot));
                        return true;
                    }
                }

                const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
                if (!grid_cell.has_value()) {
                    return true;
                }

                if (!core::is_inside_grid(
                        *grid_cell,
                        render_snapshot->map_width,
                        render_snapshot->map_height)) {
                    return true;
                }

                if (render::snapshot_has_town_center_at_cell(
                        *render_snapshot,
                        *grid_cell,
                        local_player_slot_)) {
                    submit_player_command(
                        simulation,
                        make_command(
                            simulation,
                            sim::player::PlayerCommandType::Deposit,
                            selection_.units,
                            render_snapshot));
                    return true;
                }

                sim::player::PlayerCommand command = make_command(
                    simulation,
                    sim::player::PlayerCommandType::Move,
                    selection_.units,
                    render_snapshot);
                fill_move_command_from_screen(command, renderer, *grid_cell, screen_position);
                submit_player_command(simulation, std::move(command));
                return true;
            }

            auto& registry = simulation.registry();
            const entt::entity enemy = sim::player::pick_enemy_at_screen(
                registry,
                renderer,
                screen_position,
                renderer.selection_pick_radius_px(),
                local_player_slot_);
            if (enemy != entt::null) {
                sim::player::PlayerCommand command =
                    make_command(simulation, sim::player::PlayerCommandType::Attack, selection_.units, nullptr);
                command.target_entity = enemy;
                submit_player_command(simulation, std::move(command));
                return true;
            }

            const entt::entity enemy_building = sim::player::pick_enemy_building_at_screen(
                registry,
                renderer,
                screen_position,
                pick_radius_px,
                local_player_slot_);
            if (enemy_building != entt::null) {
                sim::player::PlayerCommand command =
                    make_command(simulation, sim::player::PlayerCommandType::Attack, selection_.units, nullptr);
                command.target_entity = enemy_building;
                submit_player_command(simulation, std::move(command));
                return true;
            }

            const std::optional<core::GridPos> forest_cell = sim::player::pick_resource_forest_at_screen(
                registry,
                renderer,
                screen_position,
                pick_radius_px,
                local_player_slot_);
            if (forest_cell.has_value() && selection_has_worker(simulation, nullptr)) {
                sim::player::PlayerCommand command =
                    make_command(simulation, sim::player::PlayerCommandType::Gather, selection_.units, nullptr);
                command.cell = *forest_cell;
                submit_player_command(simulation, std::move(command));
                return true;
            }

            const entt::entity player_building = sim::player::pick_player_building_at_screen(
                registry,
                renderer,
                screen_position,
                pick_radius_px,
                local_player_slot_);
            if (player_building != entt::null) {
                if (registry.any_of<sim::components::UnderConstructionTag>(player_building)
                    && selection_has_worker(simulation, nullptr)) {
                    sim::player::PlayerCommand command = make_command(
                        simulation,
                        sim::player::PlayerCommandType::ResumeBuild,
                        selection_.units,
                        nullptr);
                    command.target_entity = player_building;
                    submit_player_command(simulation, std::move(command));
                    return true;
                }

                if (registry.any_of<sim::components::TownCenterTag>(player_building)) {
                    submit_player_command(
                        simulation,
                        make_command(
                            simulation,
                            sim::player::PlayerCommandType::Deposit,
                            selection_.units,
                            nullptr));
                    return true;
                }
            }

            const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
            if (!grid_cell.has_value()) {
                return true;
            }

            const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
            if (world_view.begin() == world_view.end()) {
                return true;
            }

            const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
            if (!core::is_inside_grid(*grid_cell, map.width, map.height)) {
                return true;
            }

            const auto town_center_at_cell = registry.view<
                sim::components::TownCenterTag,
                sim::components::PlayerOwnedTag,
                sim::components::PlayerSlot,
                sim::components::GridPosition>();
            for (const entt::entity entity : town_center_at_cell) {
                if (town_center_at_cell.get<sim::components::PlayerSlot>(entity).value != local_player_slot_) {
                    continue;
                }

                if (town_center_at_cell.get<sim::components::GridPosition>(entity).cell != *grid_cell) {
                    continue;
                }

                submit_player_command(
                    simulation,
                    make_command(
                        simulation,
                        sim::player::PlayerCommandType::Deposit,
                        selection_.units,
                        nullptr));
                return true;
            }

            sim::player::PlayerCommand command =
                make_command(simulation, sim::player::PlayerCommandType::Move, selection_.units, nullptr);
            fill_move_command_from_screen(command, renderer, *grid_cell, screen_position);
            submit_player_command(simulation, std::move(command));
        }
    }

    if (const auto* mouse_released = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (mouse_released->button == sf::Mouse::Button::Left && left_button_down_) {
            left_button_down_ = false;
            finalize_left_release(
                window,
                renderer,
                simulation,
                sf::Mouse::getPosition(window),
                render_snapshot);
            left_press_position_.reset();
            selection_box_.active = false;
            return true;
        }
    }

    return false;
}

} // namespace aoa::app
