#pragma once

#include "render/camera.hpp"
#include "render/hud_overlay.hpp"
#include "sim/simulation.hpp"

#include <SFML/System/Vector2.hpp>

#include <array>

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
    struct SceneVertex {
        float x{0.0F};
        float y{0.0F};
        float z{0.0F};
        float r{0.0F};
        float g{0.0F};
        float b{0.0F};
    };

    void create_shader_program();
    void destroy_gl_objects();
    [[nodiscard]] SceneVertex make_scene_vertex(
        float world_x,
        float world_y,
        float world_z,
        float r,
        float g,
        float b) const;
    void draw_scene_triangles(const std::array<SceneVertex, 6>& triangle_vertices, int vertex_count) const;
    void draw_scene_quad(
        float wx0,
        float wy0,
        float wz0,
        float wx1,
        float wy1,
        float wz1,
        float wx2,
        float wy2,
        float wz2,
        float wx3,
        float wy3,
        float wz3,
        float r,
        float g,
        float b,
        float light_factor) const;
    void draw_extruded_tile(
        int grid_x,
        int grid_y,
        float extrude_height,
        float r,
        float g,
        float b) const;
    void draw_entity_prism(int grid_x, int grid_y, float height, float r, float g, float b) const;
    void draw_selection_outline(int grid_x, int grid_y) const;
    void apply_team_color(float base_r, float base_g, float base_b, float& r, float& g, float& b) const;

    unsigned int scene_shader_program_{0U};
    sf::Vector2u window_size_{0U, 0U};
    ClassicCamera camera_{};
    HudOverlay hud_overlay_{};
    bool map_framed_{false};
};

bool init_gl_loader();

} // namespace aoa::render
