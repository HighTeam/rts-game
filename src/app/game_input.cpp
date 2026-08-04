#include "app/game_input.hpp"

#include "core/constants.hpp"
#include "core/grid.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/tags.hpp"
#include "sim/player/player_command.hpp"
#include "sim/player/player_commands.hpp"
#include "sim/simulation.hpp"
#include "net/lockstep_session.hpp"

#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include <algorithm>
#include <cmath>

namespace aoa::app {

namespace {

sim::player::PlayerCommand make_command(
    sim::Simulation& simulation,
    const sim::player::PlayerCommandType type,
    const std::vector<entt::entity>& units)
{
    sim::player::PlayerCommand command{};
    command.execute_tick = simulation.next_command_execute_tick();
    command.type = type;
    command.unit_ids = units;
    return command;
}

} // namespace

void GameInput::submit_player_command(
    sim::Simulation& simulation,
    sim::player::PlayerCommand command)
{
    if (lockstep_session_ != nullptr) {
        lockstep_session_->submit_local_command(std::move(command));
        return;
    }

    simulation.enqueue_player_command(std::move(command));
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
    sim::Simulation& simulation)
{
    hover_ = HoverHighlight{};

    if (left_button_down_) {
        return;
    }

    auto& registry = simulation.registry();
    const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
    const sf::Vector2f screen_position{
        static_cast<float>(mouse_position.x),
        static_cast<float>(mouse_position.y),
    };

    const entt::entity hovered_unit = sim::player::pick_hovered_unit_at_screen(
        registry,
        renderer,
        screen_position,
        renderer.selection_pick_radius_px());
    if (hovered_unit != entt::null) {
        hover_.unit = hovered_unit;
        hover_.unit_is_enemy = registry.any_of<sim::components::EnemyTag>(hovered_unit);
        return;
    }

    const entt::entity hovered_building = sim::player::pick_player_building_at_screen(
        registry,
        renderer,
        screen_position,
        renderer.selection_pick_radius_px());
    if (hovered_building != entt::null) {
        hover_.building = hovered_building;
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
            renderer.selection_pick_radius_px());
    }
}

void GameInput::update_continuous(
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation)
{
    sim::player::prune_dead_selection(selection_.units, simulation.registry());

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

    if (left_button_down_ && left_press_position_.has_value()) {
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

    update_hover(window, renderer, simulation);

    if (left_button_down_) {
        return;
    }

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
    if (pan_length <= 0.0F) {
        return;
    }

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

void GameInput::finalize_left_release(
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const sf::Vector2i mouse_position)
{
    (void)window;

    const sim::player::SelectionModifyMode mode = current_modify_mode();
    const sf::Vector2f screen_position{
        static_cast<float>(mouse_position.x),
        static_cast<float>(mouse_position.y),
    };

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
                screen_position);
            sim::player::apply_selection(selection_.units, simulation.registry(), picked, mode);
            if (mode == sim::player::SelectionModifyMode::Replace && !picked.empty()) {
                selection_.clear_resource();
                selection_.clear_building();
            }
            return;
        }
    }

    if (mode != sim::player::SelectionModifyMode::Replace) {
        const entt::entity picked = sim::player::pick_player_unit_at_screen(
            simulation.registry(),
            renderer,
            screen_position,
            renderer.selection_pick_radius_px());

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
        return;
    }

    const entt::entity picked_unit = sim::player::pick_player_unit_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        renderer.selection_pick_radius_px());

    if (picked_unit != entt::null) {
        selection_.units = {picked_unit};
        selection_.clear_resource();
        selection_.clear_building();
        return;
    }

    const entt::entity picked_building = sim::player::pick_player_building_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        renderer.selection_pick_radius_px());

    if (picked_building != entt::null) {
        selection_.clear_units();
        selection_.clear_resource();
        selection_.building = picked_building;
        return;
    }

    const std::optional<core::GridPos> picked_resource = sim::player::pick_resource_forest_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        renderer.selection_pick_radius_px());

    if (picked_resource.has_value()) {
        selection_.clear_units();
        selection_.clear_building();
        selection_.resource_cell = picked_resource;
        return;
    }

    selection_.clear();
}

void GameInput::handle_event(
    const sf::Event& event,
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation)
{
    if (const auto* key_pressed = event.getIf<sf::Event::KeyPressed>()) {
        if (key_pressed->code == sf::Keyboard::Key::W && selection_.building != entt::null) {
            auto& registry = simulation.registry();
            if (registry.any_of<sim::components::TownCenterTag>(selection_.building)) {
                sim::player::PlayerCommand command{};
                command.execute_tick = simulation.next_command_execute_tick();
                command.type = sim::player::PlayerCommandType::SpawnWorker;
                command.target_entity = selection_.building;
                submit_player_command(simulation, std::move(command));
            }
        }
    }

    if (const auto* scroll = event.getIf<sf::Event::MouseWheelScrolled>()) {
        const sf::Vector2u window_size = window.getSize();
        renderer.zoom_camera(
            scroll->delta * constants::CAMERA_CLASSIC_ZOOM_STEP,
            static_cast<float>(window_size.x) * 0.5F,
            static_cast<float>(window_size.y) * 0.5F);
    }

    if (const auto* mouse_pressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (mouse_pressed->button == sf::Mouse::Button::Left) {
            left_button_down_ = true;
            left_press_position_ = sf::Mouse::getPosition(window);
            selection_box_.active = true;
            selection_box_.start = sf::Vector2f{
                static_cast<float>(left_press_position_->x),
                static_cast<float>(left_press_position_->y),
            };
            selection_box_.current = selection_box_.start;
        }
        else if (mouse_pressed->button == sf::Mouse::Button::Right) {
            if (selection_.units.empty()) {
                return;
            }

            const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
            const sf::Vector2f screen_position{
                static_cast<float>(mouse_position.x),
                static_cast<float>(mouse_position.y),
            };

            auto& registry = simulation.registry();
            const entt::entity enemy = sim::player::pick_enemy_at_screen(
                registry,
                renderer,
                screen_position,
                renderer.selection_pick_radius_px());
            if (enemy != entt::null) {
                sim::player::PlayerCommand command =
                    make_command(simulation, sim::player::PlayerCommandType::Attack, selection_.units);
                command.target_entity = enemy;
                submit_player_command(simulation, std::move(command));
                return;
            }

            const std::optional<core::GridPos> forest_cell = sim::player::pick_resource_forest_at_screen(
                registry,
                renderer,
                screen_position,
                renderer.selection_pick_radius_px());
            if (forest_cell.has_value()) {
                sim::player::PlayerCommand command =
                    make_command(simulation, sim::player::PlayerCommandType::Gather, selection_.units);
                command.cell = *forest_cell;
                submit_player_command(simulation, std::move(command));
                return;
            }

            const entt::entity town_center = sim::player::pick_player_building_at_screen(
                registry,
                renderer,
                screen_position,
                renderer.selection_pick_radius_px());
            if (town_center != entt::null && registry.any_of<sim::components::TownCenterTag>(town_center)) {
                submit_player_command(
                    simulation,
                    make_command(
                        simulation,
                        sim::player::PlayerCommandType::Deposit,
                        selection_.units));
                return;
            }

            const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
            if (!grid_cell.has_value()) {
                return;
            }

            const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
            if (world_view.begin() == world_view.end()) {
                return;
            }

            const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
            if (!core::is_inside_grid(*grid_cell, map.width, map.height)) {
                return;
            }

            const auto town_center_at_cell = registry.view<
                sim::components::TownCenterTag,
                sim::components::PlayerOwnedTag,
                sim::components::GridPosition>();
            for (const entt::entity entity : town_center_at_cell) {
                if (town_center_at_cell.get<sim::components::GridPosition>(entity).cell != *grid_cell) {
                    continue;
                }

                submit_player_command(
                    simulation,
                    make_command(
                        simulation,
                        sim::player::PlayerCommandType::Deposit,
                        selection_.units));
                return;
            }

            sim::player::PlayerCommand command =
                make_command(simulation, sim::player::PlayerCommandType::Move, selection_.units);
            command.cell = *grid_cell;
            submit_player_command(simulation, std::move(command));
        }
    }

    if (const auto* mouse_released = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (mouse_released->button == sf::Mouse::Button::Left && left_button_down_) {
            left_button_down_ = false;
            finalize_left_release(window, renderer, simulation, sf::Mouse::getPosition(window));
            left_press_position_.reset();
            selection_box_.active = false;
        }
    }
}

} // namespace aoa::app
