#pragma once

#include "core/constants.hpp"
#include "render/camera.hpp"
#include "net/lockstep_network_hud.hpp"
#include "render/hud_overlay.hpp"
#include "render/sim_render_snapshot.hpp"
#include "sim/simulation.hpp"

#include "core/grid.hpp"

#include <SFML/System/Vector2.hpp>

#include <array>
#include <cstdint>
#include <entt/entt.hpp>
#include <optional>
#include <utility>
#include <vector>

namespace aoa::render {

struct SelectionBoxOverlay {
    bool active{false};
    sf::Vector2f start{};
    sf::Vector2f current{};
};

struct SceneHighlight {
    float world_x{0.0F};
    float world_z{0.0F};
    float r{0.0F};
    float g{0.0F};
    float b{0.0F};
    float scale{constants::RENDER_HOVER_OUTLINE_SCALE};
};

class GameRenderer {
public:
    GameRenderer();
    ~GameRenderer();

    GameRenderer(const GameRenderer&) = delete;
    GameRenderer& operator=(const GameRenderer&) = delete;

    void resize(sf::Vector2u window_size, bool preserve_camera_view = false);
    void set_local_player_slot(std::uint8_t player_slot);
    void update_camera(float delta_seconds);
    void toggle_grid_lines();
    void reset_graphics_context(sf::Vector2u window_size);
    void draw(
        const sim::Simulation& simulation,
        const std::vector<entt::entity>& selected_entities,
        float interpolation_alpha,
        const SelectionBoxOverlay& selection_box,
        entt::entity hover_unit,
        bool hover_unit_is_enemy,
        entt::entity hover_building,
        std::optional<core::GridPos> hover_resource_cell,
        std::optional<core::GridPos> selected_resource_cell,
        entt::entity selected_building,
        float fps,
        const net::LockstepNetworkHudStats& network_stats = {});

    void draw_snapshot(
        const SimRenderSnapshot& snapshot,
        const std::vector<entt::entity>& selected_entities,
        float interpolation_alpha,
        const SelectionBoxOverlay& selection_box,
        entt::entity hover_unit,
        bool hover_unit_is_enemy,
        entt::entity hover_building,
        std::optional<core::GridPos> hover_resource_cell,
        std::optional<core::GridPos> selected_resource_cell,
        entt::entity selected_building,
        float fps,
        const net::LockstepNetworkHudStats& network_stats = {});

    void clear_frame() const;
    void draw_waiting_overlay(const std::string& title, const std::string& subtitle) const;
    void reset_camera_frame();

    void pan_camera(float delta_x, float delta_y);
    void step_zoom_camera(int direction, float anchor_screen_x, float anchor_screen_y);
    [[nodiscard]] std::optional<core::GridPos> screen_to_grid(float screen_x, float screen_y) const;
    [[nodiscard]] std::pair<float, float> screen_to_world_xz(float screen_x, float screen_y) const;
    [[nodiscard]] sf::Vector2f world_to_screen(float world_x, float world_y, float world_z) const;
    [[nodiscard]] sf::Vector2f tile_center_screen(int grid_x, int grid_y, float world_y) const;
    [[nodiscard]] sf::Vector2f unit_screen_position(
        const entt::registry& registry,
        entt::entity entity,
        float interpolation_alpha) const;
    [[nodiscard]] float selection_pick_radius_px() const;

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
    void create_scene_batch_gl();
    void destroy_gl_objects();
    void append_scene_vertices(const SceneVertex* vertices, std::size_t vertex_count) const;
    void flush_scene_batch() const;
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
    void draw_screen_rect_outline(
        float screen_x0,
        float screen_y0,
        float screen_x1,
        float screen_y1,
        float r,
        float g,
        float b) const;
    void draw_screen_rect_outline_immediate(
        float screen_x0,
        float screen_y0,
        float screen_x1,
        float screen_y1,
        float r,
        float g,
        float b) const;
    void draw_extruded_tile(
        int grid_x,
        int grid_y,
        float extrude_height,
        float r,
        float g,
        float b) const;
    void draw_tile_grid_lines(int grid_x, int grid_y, float extrude_height) const;
    void draw_entity_prism(
        float world_x,
        float world_z,
        float height,
        float r,
        float g,
        float b) const;
    void draw_entity_cylinder(
        float world_x,
        float world_z,
        float height,
        float radius,
        float r,
        float g,
        float b) const;
    void draw_ground_highlight(const SceneHighlight& highlight) const;
    void draw_unit_ground_highlight(const SceneHighlight& highlight) const;
    void draw_selection_outline(float world_x, float world_z) const;
    void draw_unit_selection_outline(float world_x, float world_z) const;
    void apply_team_color(float base_r, float base_g, float base_b, float& r, float& g, float& b) const;
    [[nodiscard]] std::pair<float, float> unit_render_world_xz(
        const entt::registry& registry,
        entt::entity entity,
        float interpolation_alpha) const;
    void try_frame_player_start(
        int map_width,
        int map_height,
        const sim::Simulation* simulation,
        const SimRenderSnapshot* snapshot);
    [[nodiscard]] std::optional<core::GridPos> find_local_town_center_cell(
        const sim::Simulation* simulation,
        const SimRenderSnapshot* snapshot) const;
    [[nodiscard]] std::optional<core::GridPos> find_first_town_center_cell(
        const sim::Simulation* simulation,
        const SimRenderSnapshot* snapshot) const;
    void center_camera_on_grid_cell(const core::GridPos& cell);
    void center_camera_on_map_center(int map_width, int map_height);

    unsigned int scene_shader_program_{0U};
    unsigned int scene_vao_{0U};
    unsigned int scene_vbo_{0U};
    mutable std::vector<SceneVertex> scene_batch_{};
    sf::Vector2u window_size_{0U, 0U};
    ClassicCamera camera_{};
    HudOverlay hud_overlay_{};
    bool map_framed_{false};
    bool show_grid_lines_{false};
    std::uint8_t local_player_slot_{0U};
};

bool init_gl_loader();

} // namespace aoa::render
