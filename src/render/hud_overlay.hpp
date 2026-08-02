#pragma once

#include "sim/simulation.hpp"

#include <SFML/System/Vector2.hpp>

#include <string>

namespace aoa::render {

class HudOverlay {
public:
    void draw(
        const sim::Simulation& simulation,
        sf::Vector2u window_size,
        unsigned int shader_program) const;

private:
    void draw_string(
        sf::Vector2u window_size,
        unsigned int shader_program,
        float x,
        float y,
        const std::string& text,
        float r,
        float g,
        float b) const;
};

} // namespace aoa::render
