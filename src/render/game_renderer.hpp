#pragma once

#include "render/camera.hpp"
#include "render/hud_overlay.hpp"
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

    void pan_camera(float delta_x, float delta_y);
    void zoom_camera(float delta);

private:
    void create_shader_program();
    void destroy_gl_objects();
    void draw_colored_polygon(
        const std::array<sf::Vector2f, 4>& corners,
        float r,
        float g,
        float b);
    void draw_iso_tile(int grid_x, int grid_y, float r, float g, float b);
    void draw_iso_marker(int grid_x, int grid_y, float r, float g, float b);

    unsigned int shader_program_{0U};
    sf::Vector2u window_size_{0U, 0U};
    ClassicCamera camera_{};
    HudOverlay hud_overlay_{};
    bool map_framed_{false};
};

bool init_gl_loader();

} // namespace aoa::render
