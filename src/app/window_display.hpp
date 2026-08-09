#pragma once

#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Window.hpp>

#include <functional>
#include <string>

namespace aoa::render {

class GameRenderer;

} // namespace aoa::render

namespace aoa::app {

struct WindowDisplaySettings {
    bool fullscreen{false};
    sf::Vector2u windowed_size{0U, 0U};
    sf::Vector2i windowed_position{0, 0};
    std::string title{};
    sf::ContextSettings context_settings{};
};

// Recreates GL objects owned by the caller after the window (and its context) is recreated.
using GraphicsContextResetFn = std::function<void(sf::Vector2u)>;

void initialize_window_display_settings(sf::Window& window, WindowDisplaySettings& settings);

void enter_fullscreen(
    sf::Window& window,
    const GraphicsContextResetFn& reset_graphics_context,
    WindowDisplaySettings& settings);

void leave_fullscreen(
    sf::Window& window,
    const GraphicsContextResetFn& reset_graphics_context,
    WindowDisplaySettings& settings);

void enter_fullscreen(sf::Window& window, render::GameRenderer& renderer, WindowDisplaySettings& settings);

void leave_fullscreen(sf::Window& window, render::GameRenderer& renderer, WindowDisplaySettings& settings);

// Returns true when display mode changed (fullscreen toggle).
// F10 fog toggle is only applied when allow_fog_toggle is true (singleplayer).
[[nodiscard]] bool handle_display_key(
    sf::Window& window,
    render::GameRenderer& renderer,
    WindowDisplaySettings& settings,
    sf::Keyboard::Key key,
    bool allow_fog_toggle = true);

} // namespace aoa::app
