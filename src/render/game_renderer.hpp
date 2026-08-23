#pragma once

#include "core/constants.hpp"
#include "render/camera.hpp"
#include "net/lockstep_network_hud.hpp"
#include "render/hud_overlay.hpp"
#include "render/scene_textures.hpp"
#include "render/building_sight_memory.hpp"
#include "render/sim_render_snapshot.hpp"
#include "sim/simulation.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/systems/visibility_system.hpp"

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

struct OccluderSprite {
    int grid_x{0};
    int grid_y{0};
    SceneTextureKind texture_kind{SceneTextureKind::OakForestLarge};
    float width_scale{1.0F};
    float offset_x_tiles{0.0F};
    float offset_y_tiles{0.0F};
    float sort_y{0.0F};
};

class GameRenderer {
public:
    GameRenderer();
    ~GameRenderer();

    GameRenderer(const GameRenderer&) = delete;
    GameRenderer& operator=(const GameRenderer&) = delete;

    void resize(sf::Vector2u window_size, bool preserve_camera_view = false);
    void set_local_player_slot(std::uint8_t player_slot);
    [[nodiscard]] std::uint8_t local_player_slot() const { return local_player_slot_; }
    [[nodiscard]] sf::Vector2u window_size() const { return window_size_; }
    void update_camera(float delta_seconds);
    void toggle_grid_lines();
    void toggle_fog_of_war();
    void set_fog_of_war_enabled(const bool enabled) { fog_of_war_enabled_ = enabled; }
    [[nodiscard]] bool fog_of_war_enabled() const { return fog_of_war_enabled_; }
    void toggle_perf_hud();
    [[nodiscard]] bool show_perf_hud() const { return show_perf_hud_; }
    void set_show_perf_hud(const bool enabled) { show_perf_hud_ = enabled; }
    void toggle_selection_debug();
    [[nodiscard]] bool show_selection_debug() const { return show_selection_debug_; }
    void toggle_hitboxes();
    [[nodiscard]] bool show_hitboxes() const { return show_hitboxes_; }
    void reset_graphics_context(sf::Vector2u window_size);
    [[nodiscard]] bool local_player_has_seen_building(entt::entity entity) const;
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
        float tps = 0.0F,
        const net::LockstepNetworkHudStats& network_stats = {},
        const HudUnitContext& hud_context = {},
        std::optional<core::GridPos> placement_ghost_anchor = std::nullopt,
        bool placement_ghost_valid = false);

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
        float tps = 0.0F,
        const net::LockstepNetworkHudStats& network_stats = {},
        const HudUnitContext& hud_context = {},
        std::optional<core::GridPos> placement_ghost_anchor = std::nullopt,
        bool placement_ghost_valid = false);

    void clear_frame() const;
    void draw_waiting_overlay(const std::string& title, const std::string& subtitle) const;
    void reset_camera_frame();

    void pan_camera(float delta_x, float delta_y);
    void center_camera_on_world_keep_zoom(float world_x, float world_z);
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

    struct TexturedSceneVertex {
        float x{0.0F};
        float y{0.0F};
        float z{0.0F};
        float u{0.0F};
        float v{0.0F};
        float r{1.0F};
        float g{1.0F};
        float b{1.0F};
        float a{1.0F};
    };

    void create_shader_program();
    void create_textured_shader_program();
    void create_scene_batch_gl();
    void create_textured_scene_batch_gl();
    void destroy_gl_objects();
    void append_scene_vertices(const SceneVertex* vertices, std::size_t vertex_count) const;
    void append_textured_vertices(
        const TexturedSceneVertex* vertices,
        std::size_t vertex_count) const;
    void flush_scene_batch() const;
    void flush_textured_scene_batch(unsigned int texture_id) const;
    void draw_screen_sprite(
        float screen_left,
        float screen_top,
        float screen_width,
        float screen_height,
        float depth,
        unsigned int texture_id,
        float tint_r,
        float tint_g,
        float tint_b,
        float tint_a,
        float uv_left = 0.0F,
        float uv_right = 1.0F) const;
    void draw_textured_quad(
        const sf::Vector2f& top,
        const sf::Vector2f& right,
        const sf::Vector2f& bottom,
        const sf::Vector2f& left,
        float uv_top_u,
        float uv_top_v,
        float uv_right_u,
        float uv_right_v,
        float uv_bottom_u,
        float uv_bottom_v,
        float uv_left_u,
        float uv_left_v,
        float depth,
        float brightness_top,
        float brightness_right,
        float brightness_bottom,
        float brightness_left,
        float alpha = 1.0F) const;
    void draw_textured_iso_diamond_fog(
        const sf::Vector2f& top,
        const sf::Vector2f& right,
        const sf::Vector2f& bottom,
        const sf::Vector2f& left,
        float uv_top_u,
        float uv_top_v,
        float uv_right_u,
        float uv_right_v,
        float uv_bottom_u,
        float uv_bottom_v,
        float uv_left_u,
        float uv_left_v,
        float depth,
        float self_brightness,
        float corner_top,
        float corner_right,
        float corner_bottom,
        float corner_left,
        bool use_corner_fade,
        float alpha_top = 1.0F,
        float alpha_right = 1.0F,
        float alpha_bottom = 1.0F,
        float alpha_left = 1.0F) const;
    void draw_iso_diamond_sprite(
        int grid_x,
        int grid_y,
        SceneTextureKind texture_kind,
        float brightness,
        const std::vector<float>* fog_vertex_brightness = nullptr,
        int map_width = 0,
        int map_height = 0,
        float alpha = 1.0F,
        const float* corner_alphas = nullptr) const;
    void ensure_biome_vertex_weights(
        const std::vector<sim::components::GroundType>& ground,
        int map_width,
        int map_height) const;
    [[nodiscard]] const std::vector<float>& ensure_fog_vertex_brightness(
        std::uint64_t tick_count,
        const std::vector<std::uint8_t>& fog_explored,
        int map_width,
        int map_height,
        std::uint8_t player_slot,
        const std::vector<sim::systems::VisionSource>& vision_sources) const;
    [[nodiscard]] bool iso_tile_intersects_window(int grid_x, int grid_y, int pad_tiles) const;
    void draw_batched_ground_tiles(
        int map_width,
        int map_height,
        const std::vector<sim::components::GroundType>& ground,
        const std::vector<std::uint8_t>& fog_visible,
        const std::vector<std::uint8_t>& fog_explored,
        bool fog_enabled,
        const std::vector<float>* fog_vertex_brightness,
        std::uint64_t tick_count) const;
    void draw_iso_object_sprite(
        int grid_x,
        int grid_y,
        SceneTextureKind texture_kind,
        float width_scale,
        float sort_y,
        float brightness,
        float offset_x_tiles = 0.0F,
        float offset_y_tiles = 0.0F,
        int frame_index = 0,
        int frame_count = 1,
        float tint_r = 1.0F,
        float tint_g = 1.0F,
        float tint_b = 1.0F) const;
    struct PendingIsoSpriteDraw {
        float sort_key{0.0F};
        int grid_x{0};
        int grid_y{0};
        SceneTextureKind texture_kind{SceneTextureKind::Grass};
        float width_scale{1.0F};
        float brightness{1.0F};
        float tint_r{1.0F};
        float tint_g{1.0F};
        float tint_b{1.0F};
        float offset_x_tiles{0.0F};
        float offset_y_tiles{0.0F};
        // Horizontal strip animation: frame_count > 1 selects a column of the atlas.
        int frame_index{0};
        int frame_count{1};
    };

    struct PendingUnitDraw {
        float sort_key{0.0F};
        float world_x{0.0F};
        float world_z{0.0F};
        float r{0.0F};
        float g{0.0F};
        float b{0.0F};
        float silhouette_r{0.0F};
        float silhouette_g{0.0F};
        float silhouette_b{0.0F};
        float radius{constants::RENDER_UNIT_RADIUS};
        float height{constants::RENDER_UNIT_HEIGHT};
        bool draw_hat{false};
    };

    void queue_iso_object_sprite(
        int grid_x,
        int grid_y,
        SceneTextureKind texture_kind,
        float width_scale,
        float brightness,
        float sort_key,
        float offset_x_tiles = 0.0F,
        float offset_y_tiles = 0.0F,
        int frame_index = 0,
        int frame_count = 1,
        float tint_r = 1.0F,
        float tint_g = 1.0F,
        float tint_b = 1.0F) const;
    void queue_unit_draw(const PendingUnitDraw& draw) const;
    void flush_pending_depth_sorted_draws(unsigned int& active_textured_batch) const;
    [[nodiscard]] std::vector<OccluderSprite> collect_unit_front_occluder_sprites(
        float world_x,
        float world_z,
        const sim::components::MapGrid& map,
        const std::vector<sim::components::BuildingFootprint>& building_footprints,
        const std::vector<core::GridPos>& building_anchors,
        const std::vector<OccluderSprite>& building_sprites,
        const std::vector<std::uint8_t>* fog_visible,
        const std::vector<std::uint8_t>* fog_explored) const;
    void apply_iso_tile_scissor(
        int grid_x,
        int grid_y,
        float vertical_extent_scale,
        SceneTextureKind aspect_kind = SceneTextureKind::OakForestLarge) const;
    void clear_screen_scissor() const;
    void draw_iso_range_circle(
        float center_world_x,
        float center_world_z,
        float radius_tiles,
        float r,
        float g,
        float b) const;
    void draw_unit_occlusion_silhouette(
        float world_x,
        float world_z,
        float r,
        float g,
        float b,
        const std::vector<OccluderSprite>& occluder_sprites) const;
    [[nodiscard]] SceneTextureKind ground_texture_for_ground(
        sim::components::GroundType ground) const;
    [[nodiscard]] SceneTextureKind forest_tree_texture_for_cell(
        int grid_x,
        int grid_y,
        sim::components::GroundType ground) const;
    [[nodiscard]] float tree_sprite_width_scale(SceneTextureKind kind) const;
    [[nodiscard]] SceneTextureKind gold_mine_texture_for_cell(int grid_x, int grid_y) const;
    [[nodiscard]] SceneTextureKind rock_texture_for_cell(int grid_x, int grid_y) const;
    void draw_map_ping_marks(const std::vector<sim::components::MapPing>& pings) const;
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
    void draw_screen_line_immediate(
        float screen_x0,
        float screen_y0,
        float screen_x1,
        float screen_y1,
        float r,
        float g,
        float b) const;
    void draw_screen_thick_line_immediate(
        float screen_x0,
        float screen_y0,
        float screen_x1,
        float screen_y1,
        float thickness_px,
        float r,
        float g,
        float b) const;
    void draw_scene_line_xz(
        float x0,
        float z0,
        float x1,
        float z1,
        float y,
        float r,
        float g,
        float b,
        float width) const;
    void draw_scene_rect_outline(
        float x0,
        float z0,
        float x1,
        float z1,
        float y,
        float line_width,
        float r,
        float g,
        float b) const;
    void draw_unit_projected_silhouette_outline_immediate(
        float world_x,
        float world_z,
        float r,
        float g,
        float b) const;
    void project_unit_screen_bounds(
        float world_x,
        float world_z,
        float& min_screen_x,
        float& min_screen_y,
        float& max_screen_x,
        float& max_screen_y) const;
    void occluder_tile_screen_rect(
        int grid_x,
        int grid_y,
        bool is_building,
        float& min_screen_x,
        float& min_screen_y,
        float& max_screen_x,
        float& max_screen_y) const;
    [[nodiscard]] bool unit_screen_overlaps_occluder_tile(
        float world_x,
        float world_z,
        int grid_x,
        int grid_y,
        bool is_building) const;
    [[nodiscard]] bool unit_is_occluded_by_tile(
        float world_x,
        float world_z,
        int grid_x,
        int grid_y,
        bool is_building) const;
    void draw_extruded_tile(
        int grid_x,
        int grid_y,
        float extrude_height,
        float r,
        float g,
        float b) const;
    void draw_tile_grid_lines(
        int grid_x,
        int grid_y,
        float extrude_height,
        float line_lift = constants::RENDER_GRID_LINE_LIFT) const;
    void draw_iso_tile_grid_lines(int map_width, int map_height) const;
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
        float b,
        float height_start = 0.0F,
        float height_end = 1.0F) const;
    void draw_ground_highlight(const SceneHighlight& highlight) const;
    void draw_unit_ground_highlight(const SceneHighlight& highlight) const;
    void draw_hitbox_overlays(
        const sim::Simulation& simulation,
        float interpolation_alpha) const;
    void draw_hitbox_overlays_snapshot(
        const SimRenderSnapshot& snapshot,
        float interpolation_alpha) const;
    void draw_debug_path_for_unit(
        float world_x,
        float world_z,
        const std::vector<core::GridPos>& cells,
        int next_index) const;
    void draw_debug_path_overlays(
        const sim::Simulation& simulation,
        float interpolation_alpha) const;
    void draw_debug_path_overlays_snapshot(
        const SimRenderSnapshot& snapshot,
        float interpolation_alpha) const;
    void draw_selection_outline(float world_x, float world_z) const;
    void draw_unit_selection_outline(float world_x, float world_z) const;
    void draw_footprint_highlight(
        int anchor_x,
        int anchor_y,
        int width,
        int height,
        float r,
        float g,
        float b,
        float scale) const;
    void draw_footprint_selection_outline(int anchor_x, int anchor_y, int width, int height) const;
    void draw_interaction_highlights(
        const entt::registry* registry,
        const SimRenderSnapshot* snapshot,
        const std::vector<entt::entity>& selected_entities,
        entt::entity hover_unit,
        bool hover_unit_is_enemy,
        entt::entity hover_building,
        std::optional<core::GridPos> hover_resource_cell,
        std::optional<core::GridPos> selected_resource_cell,
        entt::entity selected_building,
        float interpolation_alpha) const;
    [[nodiscard]] float unit_depth_sort_key(float world_x, float world_z) const;
    [[nodiscard]] float cap_unit_sort_below_buildings(
        float sort_key,
        float world_x,
        float world_z,
        const std::vector<core::GridPos>& building_anchors,
        const std::vector<sim::components::BuildingFootprint>& building_footprints,
        const std::vector<OccluderSprite>& building_sprites) const;
    void iso_object_sprite_screen_rect(
        int grid_x,
        int grid_y,
        SceneTextureKind texture_kind,
        float width_scale,
        float offset_x_tiles,
        float offset_y_tiles,
        float& min_x,
        float& min_y,
        float& max_x,
        float& max_y) const;
    [[nodiscard]] bool unit_screen_overlaps_iso_sprite(
        float world_x,
        float world_z,
        int grid_x,
        int grid_y,
        SceneTextureKind texture_kind,
        float width_scale,
        float offset_x_tiles,
        float offset_y_tiles) const;
    [[nodiscard]] bool unit_screen_is_in_front_of_building(
        float world_x,
        float world_z,
        const core::GridPos& anchor,
        const sim::components::BuildingFootprint& footprint) const;
    [[nodiscard]] bool unit_draws_in_front_of_building(
        float world_x,
        float world_z,
        const core::GridPos& anchor,
        const sim::components::BuildingFootprint& footprint,
        const OccluderSprite& sprite) const;
    void apply_team_color(float base_r, float base_g, float base_b, float& r, float& g, float& b) const;
    [[nodiscard]] std::pair<float, float> unit_render_world_xz(
        const entt::registry& registry,
        entt::entity entity,
        float interpolation_alpha) const;
    void destroy_present_target() const;
    [[nodiscard]] bool ensure_present_target() const;
    void present_target_to_window() const;
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
    unsigned int textured_scene_shader_program_{0U};
    unsigned int scene_vao_{0U};
    unsigned int scene_vbo_{0U};
    unsigned int textured_scene_vao_{0U};
    unsigned int textured_scene_vbo_{0U};
    mutable std::vector<SceneVertex> scene_batch_{};
    mutable std::vector<TexturedSceneVertex> textured_scene_batch_{};
    mutable std::vector<PendingIsoSpriteDraw> pending_iso_sprites_{};
    mutable std::vector<PendingUnitDraw> pending_unit_draws_{};
    SceneTextureCatalog scene_textures_{};
    sf::Vector2u window_size_{0U, 0U};
    ClassicCamera camera_{};
    HudOverlay hud_overlay_{};
    bool map_framed_{false};
    bool show_grid_lines_{false};
    bool fog_of_war_enabled_{true};
    bool show_perf_hud_{false};
    bool show_selection_debug_{false};
    bool show_hitboxes_{false};
    std::uint8_t local_player_slot_{0U};
    constants::BuildingRangeDisplayMode building_range_display_{
        constants::BuildingRangeDisplayMode::Never};
    BuildingSightMemory building_sight_memory_{};
    mutable int biome_blend_map_width_{0};
    mutable int biome_blend_map_height_{0};
    mutable std::uint64_t biome_blend_ground_hash_{0U};
    mutable std::vector<float> biome_vertex_weight_{};
    mutable std::uint64_t fog_vertex_tick_{~0ULL};
    mutable std::uint8_t fog_vertex_player_slot_{255U};
    mutable int fog_vertex_map_width_{0};
    mutable int fog_vertex_map_height_{0};
    mutable std::vector<float> fog_vertex_cache_{};
    mutable std::vector<int> ground_draw_indices_{};
    mutable std::uint64_t ground_draw_tick_{~0ULL};
    mutable std::uint8_t ground_draw_player_slot_{255U};
    mutable int ground_draw_map_width_{0};
    mutable int ground_draw_map_height_{0};
    std::uint64_t cached_hud_snapshot_tick_{~0ULL};
    SimRenderSnapshot cached_hud_snapshot_{};
    mutable unsigned int present_fbo_{0U};
    mutable unsigned int present_color_{0U};
    mutable unsigned int present_depth_{0U};
    mutable int present_width_{0};
    mutable int present_height_{0};
};

bool init_gl_loader();

} // namespace aoa::render
