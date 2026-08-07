#pragma once

#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Window.hpp>

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

void initialize_window_display_settings(sf::Window& window, WindowDisplaySettings& settings);

void handle_display_key(
    sf::Window& window,
    render::GameRenderer& renderer,
    WindowDisplaySettings& settings,
    sf::Keyboard::Key key);

} // namespace aoa::app
