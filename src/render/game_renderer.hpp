#pragma once

#include "sim/simulation.hpp"

#include <SFML/System/Vector2.hpp>

namespace aoa::render {

class GameRenderer {
public:
    GameRenderer();
    ~GameRenderer();

    GameRenderer(const GameRenderer&) = delete;
    GameRenderer& operator=(const GameRenderer&) = delete;

    void resize(sf::Vector2u window_size);
    void draw(const sim::Simulation& simulation);

private:
    void create_shader_program();
    void destroy_gl_objects();
    void draw_colored_quad(float x, float y, float width, float height, float r, float g, float b);

    unsigned int shader_program_{0U};
    sf::Vector2u window_size_{0U, 0U};
};

bool init_gl_loader();

} // namespace aoa::render
