#include "app/window_display.hpp"

#include "core/constants.hpp"
#include "render/game_renderer.hpp"

#include <SFML/Window/VideoMode.hpp>

namespace aoa::app {

namespace {

void apply_window_frame_limits(sf::Window& window)
{
    window.setVerticalSyncEnabled(false);
    window.setFramerateLimit(constants::TARGET_DISPLAY_FPS);
}

} // namespace

void enter_fullscreen(
    sf::Window& window,
    const GraphicsContextResetFn& reset_graphics_context,
    WindowDisplaySettings& settings)
{
    settings.windowed_size = window.getSize();
    settings.windowed_position = window.getPosition();

    const sf::VideoMode desktop_mode = sf::VideoMode::getDesktopMode();
    window.create(
        desktop_mode,
        settings.title,
        sf::State::Fullscreen,
        settings.context_settings);
    apply_window_frame_limits(window);
    (void)window.setActive(true);
    window.setMouseCursorVisible(true);
    window.setMouseCursorGrabbed(true);

    reset_graphics_context(window.getSize());
    settings.fullscreen = true;
}

void leave_fullscreen(
    sf::Window& window,
    const GraphicsContextResetFn& reset_graphics_context,
    WindowDisplaySettings& settings)
{
    if (settings.windowed_size.x == 0U || settings.windowed_size.y == 0U) {
        settings.windowed_size = {constants::DEFAULT_WINDOW_WIDTH, constants::DEFAULT_WINDOW_HEIGHT};
    }

    window.create(
        sf::VideoMode(settings.windowed_size),
        settings.title,
        sf::Style::Default,
        sf::State::Windowed,
        settings.context_settings);
    apply_window_frame_limits(window);
    (void)window.setActive(true);
    window.setPosition(settings.windowed_position);
    window.setMouseCursorVisible(true);
    window.setMouseCursorGrabbed(false);

    reset_graphics_context(window.getSize());
    settings.fullscreen = false;
}

void enter_fullscreen(sf::Window& window, render::GameRenderer& renderer, WindowDisplaySettings& settings)
{
    enter_fullscreen(
        window,
        [&renderer](const sf::Vector2u size) { renderer.reset_graphics_context(size); },
        settings);
}

void leave_fullscreen(sf::Window& window, render::GameRenderer& renderer, WindowDisplaySettings& settings)
{
    leave_fullscreen(
        window,
        [&renderer](const sf::Vector2u size) { renderer.reset_graphics_context(size); },
        settings);
}

void initialize_window_display_settings(sf::Window& window, WindowDisplaySettings& settings)
{
    settings.windowed_size = window.getSize();
    settings.windowed_position = window.getPosition();
}

bool handle_display_key(
    sf::Window& window,
    render::GameRenderer& renderer,
    WindowDisplaySettings& settings,
    const sf::Keyboard::Key key,
    const bool allow_fog_toggle)
{
    if (key == sf::Keyboard::Key::F9) {
        renderer.toggle_perf_hud();
        return false;
    }

    if (key == sf::Keyboard::Key::F10) {
        if (allow_fog_toggle) {
            renderer.toggle_fog_of_war();
        }
        return false;
    }

    if (key == sf::Keyboard::Key::F11) {
        renderer.toggle_grid_lines();
        return false;
    }

    if (key == sf::Keyboard::Key::F12) {
        if (settings.fullscreen) {
            leave_fullscreen(window, renderer, settings);
        }
        else {
            enter_fullscreen(window, renderer, settings);
        }
        return true;
    }

    return false;
}

} // namespace aoa::app
