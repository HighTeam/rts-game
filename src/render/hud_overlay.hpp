#pragma once

#include "sim/simulation.hpp"

#include <SFML/System/Vector2.hpp>

#include <string>

namespace aoa::render {

class HudOverlay {
public:
    void draw(const sim::Simulation& simulation, sf::Vector2u window_size) const;

private:
    [[nodiscard]] unsigned int hud_shader_program() const;

    void draw_string(
        sf::Vector2u window_size,
        float x,
        float y,
        const std::string& text,
        float r,
        float g,
        float b) const;
};

} // namespace aoa::render
