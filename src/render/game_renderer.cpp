#include "render/game_renderer.hpp"

#include "core/constants.hpp"
#include "render/sim_render_snapshot.hpp"
#include "render/camera_settings.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/fog_of_war.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/systems/visibility_system.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"

#include "math/fixed.hpp"

#include <algorithm>
#include <cmath>

#include <SFML/Window/Context.hpp>

#include <glad/glad.h>

#include <array>
#include <stdexcept>
#include <string>

namespace aoa::render {

namespace {

constexpr const char* SCENE_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
out vec3 fragment_color;
void main()
{
    gl_Position = vec4(position, 1.0);
    fragment_color = color;
}
)";

constexpr const char* SCENE_FRAGMENT_SHADER = R"(
#version 330 core
in vec3 fragment_color;
out vec4 output_color;
void main()
{
    output_color = vec4(fragment_color, 1.0);
}
)";

unsigned int compile_shader(const unsigned int type, const char* source)
{
    const unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        std::array<char, 512> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        throw std::runtime_error(std::string("Shader compile failed: ") + log.data());
    }

    return shader;
}

unsigned int link_program(const unsigned int vertex_shader, const unsigned int fragment_shader)
{
    const unsigned int program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        std::array<char, 512> log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        throw std::runtime_error(std::string("Shader link failed: ") + log.data());
    }

    return program;
}

[[nodiscard]] bool fog_data_is_player_slice(
    const std::vector<std::uint8_t>& fog,
    const int map_width,
    const int map_height)
{
    if (map_width <= 0 || map_height <= 0 || fog.empty()) {
        return false;
    }

    return fog.size() == static_cast<std::size_t>(map_width * map_height);
}

[[nodiscard]] std::size_t fog_tile_index(
    const std::vector<std::uint8_t>& fog,
    const int map_width,
    const int map_height,
    const std::uint8_t player_slot,
    const int x,
    const int y)
{
    if (fog_data_is_player_slice(fog, map_width, map_height)) {
        return static_cast<std::size_t>(y * map_width + x);
    }

    return static_cast<std::size_t>(player_slot * map_width * map_height + y * map_width + x);
}

[[nodiscard]] bool is_fog_tile_unexplored(
    const std::vector<std::uint8_t>& fog_visible,
    const std::vector<std::uint8_t>& fog_explored,
    const int map_width,
    const int map_height,
    const std::uint8_t player_slot,
    const int x,
    const int y)
{
    if (fog_visible.empty() || fog_explored.empty() || map_width <= 0 || map_height <= 0) {
        return false;
    }

    const std::size_t index =
        fog_tile_index(fog_visible, map_width, map_height, player_slot, x, y);
    if (index >= fog_visible.size() || index >= fog_explored.size()) {
        return false;
    }

    return fog_visible[index] == 0U && fog_explored[index] == 0U;
}

float fog_tile_brightness(
    const std::vector<std::uint8_t>& fog_visible,
    const std::vector<std::uint8_t>& fog_explored,
    const int map_width,
    const int map_height,
    const std::uint8_t player_slot,
    const int x,
    const int y)
{
    if (fog_visible.empty() || fog_explored.empty() || map_width <= 0 || map_height <= 0) {
        return 1.0F;
    }

    const std::size_t index =
        fog_tile_index(fog_visible, map_width, map_height, player_slot, x, y);
    if (index >= fog_visible.size() || index >= fog_explored.size()) {
        return 1.0F;
    }

    if (fog_visible[index] != 0U) {
        return 1.0F;
    }

    if (fog_explored[index] != 0U) {
        return aoa::constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
    }

    return aoa::constants::FOG_UNEXPLORED_BRIGHTNESS;
}

[[nodiscard]] bool fog_tile_is_in_shroud(
    const std::vector<std::uint8_t>& fog_visible,
    const std::vector<std::uint8_t>& fog_explored,
    const int map_width,
    const int map_height,
    const std::uint8_t player_slot,
    const int x,
    const int y)
{
    if (fog_visible.empty() || fog_explored.empty()) {
        return false;
    }

    if (is_fog_tile_unexplored(fog_visible, fog_explored, map_width, map_height, player_slot, x, y)) {
        return false;
    }

    return fog_tile_brightness(fog_visible, fog_explored, map_width, map_height, player_slot, x, y)
        < 1.0F;
}

void resolve_tile_appearance(
    const sim::components::TileType live_tile,
    const int live_forest_wood,
    const bool use_memory,
    const sim::components::TileType memory_tile,
    const int memory_forest_wood,
    sim::components::TileType& tile,
    int& forest_wood,
    float& extrude_base_r,
    float& extrude_base_g,
    float& extrude_base_b,
    float& extrude)
{
    tile = use_memory ? memory_tile : live_tile;
    forest_wood = use_memory ? memory_forest_wood : live_forest_wood;

    extrude_base_r = 0.20F;
    extrude_base_g = 0.32F;
    extrude_base_b = 0.18F;
    extrude = 0.0F;
    if (tile == sim::components::TileType::Forest && forest_wood > 0) {
        extrude_base_r = 0.10F;
        extrude_base_g = 0.22F;
        extrude_base_b = 0.10F;
        extrude = constants::RENDER_FOREST_EXTRUDE;
    }
}

} // namespace

bool init_gl_loader()
{
    return gladLoadGLLoader(reinterpret_cast<GLADloadproc>(sf::Context::getFunction)) != 0;
}

GameRenderer::SceneVertex GameRenderer::make_scene_vertex(
    const float world_x,
    const float world_y,
    const float world_z,
    const float r,
    const float g,
    const float b) const
{
    const auto clip = camera_.world_to_clip(world_x, world_y, world_z);
    return SceneVertex{clip[0], clip[1], clip[2], r, g, b};
}

GameRenderer::GameRenderer()
{
    if (!init_gl_loader()) {
        throw std::runtime_error("Failed to initialize OpenGL loader");
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    create_shader_program();
    create_scene_batch_gl();
    scene_batch_.reserve(131072U);
}

GameRenderer::~GameRenderer()
{
    destroy_gl_objects();
}

void GameRenderer::create_shader_program()
{
    const unsigned int vertex_shader = compile_shader(GL_VERTEX_SHADER, SCENE_VERTEX_SHADER);
    const unsigned int fragment_shader = compile_shader(GL_FRAGMENT_SHADER, SCENE_FRAGMENT_SHADER);
    scene_shader_program_ = link_program(vertex_shader, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
}

void GameRenderer::create_scene_batch_gl()
{
    glGenVertexArrays(1, &scene_vao_);
    glGenBuffers(1, &scene_vbo_);

    glBindVertexArray(scene_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, scene_vbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(SceneVertex),
        reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0U);
}

void GameRenderer::destroy_gl_objects()
{
    if (scene_vbo_ != 0U) {
        glDeleteBuffers(1, &scene_vbo_);
        scene_vbo_ = 0U;
    }

    if (scene_vao_ != 0U) {
        glDeleteVertexArrays(1, &scene_vao_);
        scene_vao_ = 0U;
    }

    if (scene_shader_program_ != 0U) {
        glDeleteProgram(scene_shader_program_);
        scene_shader_program_ = 0U;
    }
}

void GameRenderer::resize(const sf::Vector2u window_size, const bool preserve_camera_view)
{
    window_size_ = window_size;
    camera_.set_window_size(window_size);
    if (!preserve_camera_view) {
        camera_.reset_view();
        map_framed_ = false;
    }
    glViewport(0, 0, static_cast<GLsizei>(window_size.x), static_cast<GLsizei>(window_size.y));
}

void GameRenderer::set_local_player_slot(const std::uint8_t player_slot)
{
    if (local_player_slot_ == player_slot) {
        return;
    }

    local_player_slot_ = player_slot;
    map_framed_ = false;
}

void GameRenderer::reset_camera_frame()
{
    map_framed_ = false;
}

void GameRenderer::update_camera(const float delta_seconds)
{
    camera_.update(delta_seconds);
}

void GameRenderer::toggle_grid_lines()
{
    show_grid_lines_ = !show_grid_lines_;
}

void GameRenderer::reset_graphics_context(const sf::Vector2u window_size)
{
    destroy_gl_objects();
    hud_overlay_.invalidate_gl_cache();

    if (!init_gl_loader()) {
        throw std::runtime_error("Failed to reinitialize OpenGL loader");
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    create_shader_program();
    create_scene_batch_gl();
    resize(window_size, true);
}

void GameRenderer::pan_camera(const float delta_x, const float delta_y)
{
    camera_.pan(delta_x, delta_y);
}

void GameRenderer::step_zoom_camera(
    const int direction,
    const float anchor_screen_x,
    const float anchor_screen_y)
{
    camera_.step_zoom(direction, anchor_screen_x, anchor_screen_y);
}

std::optional<core::GridPos> GameRenderer::screen_to_grid(
    const float screen_x,
    const float screen_y) const
{
    return camera_.screen_to_grid(screen_x, screen_y);
}

std::pair<float, float> GameRenderer::screen_to_world_xz(
    const float screen_x,
    const float screen_y) const
{
    return camera_.screen_to_world_xz(screen_x, screen_y);
}

sf::Vector2f GameRenderer::world_to_screen(
    const float world_x,
    const float world_y,
    const float world_z) const
{
    return camera_.world_to_screen(world_x, world_y, world_z);
}

std::pair<float, float> GameRenderer::unit_render_world_xz(
    const entt::registry& registry,
    const entt::entity entity,
    const float interpolation_alpha) const
{
    const float max_extrapolation_alpha = net::constants::LOCKSTEP_MAX_RENDER_EXTRAPOLATION_ALPHA;

    if (registry.all_of<sim::components::MoveSegment, sim::components::WorldPosition>(entity)) {
        const auto& segment = registry.get<sim::components::MoveSegment>(entity);
        if (segment.ticks_total > 0) {
            const float progress = (static_cast<float>(segment.ticks_elapsed)
                    + std::clamp(interpolation_alpha, 0.0F, 1.0F + max_extrapolation_alpha))
                / static_cast<float>(segment.ticks_total);
            const math::Fixed t = math::Fixed::from_float(std::clamp(progress, 0.0F, 1.0F));
            return {
                math::fixed_lerp(segment.from_x, segment.to_x, t).to_float(),
                math::fixed_lerp(segment.from_y, segment.to_y, t).to_float(),
            };
        }
    }

    if (registry.all_of<sim::components::PreviousWorldPosition, sim::components::WorldPosition>(entity)) {
        const auto& previous = registry.get<sim::components::PreviousWorldPosition>(entity);
        const auto& current = registry.get<sim::components::WorldPosition>(entity);

        if (interpolation_alpha <= 1.0F) {
            const math::Fixed t = math::Fixed::from_float(std::clamp(interpolation_alpha, 0.0F, 1.0F));
            return {
                math::fixed_lerp(previous.x, current.x, t).to_float(),
                math::fixed_lerp(previous.y, current.y, t).to_float(),
            };
        }

        const float extrapolation_alpha = std::min(interpolation_alpha - 1.0F, max_extrapolation_alpha);
        const math::Fixed delta_x = current.x - previous.x;
        const math::Fixed delta_y = current.y - previous.y;
        const math::Fixed extrapolation = math::Fixed::from_float(extrapolation_alpha);
        return {
            (current.x + delta_x * extrapolation).to_float(),
            (current.y + delta_y * extrapolation).to_float(),
        };
    }

    if (registry.any_of<sim::components::WorldPosition>(entity)) {
        const auto& world = registry.get<sim::components::WorldPosition>(entity);
        return {world.x.to_float(), world.y.to_float()};
    }

    if (registry.any_of<sim::components::GridPosition>(entity)) {
        const auto& cell = registry.get<sim::components::GridPosition>(entity).cell;
        return {
            static_cast<float>(cell.x) + 0.5F,
            static_cast<float>(cell.y) + 0.5F,
        };
    }

    return {0.0F, 0.0F};
}

sf::Vector2f GameRenderer::tile_center_screen(
    const int grid_x,
    const int grid_y,
    const float world_y) const
{
    return world_to_screen(
        static_cast<float>(grid_x) + 0.5F,
        world_y,
        static_cast<float>(grid_y) + 0.5F);
}

sf::Vector2f GameRenderer::unit_screen_position(
    const entt::registry& registry,
    const entt::entity entity,
    const float interpolation_alpha) const
{
    const auto [world_x, world_z] = unit_render_world_xz(registry, entity, interpolation_alpha);
    return world_to_screen(world_x, constants::RENDER_ENTITY_BASE_LIFT, world_z);
}

float GameRenderer::selection_pick_radius_px() const
{
    return constants::SELECTION_PICK_RADIUS_TILES * camera_.tile_width();
}

std::optional<core::GridPos> GameRenderer::find_local_town_center_cell(
    const sim::Simulation* simulation,
    const SimRenderSnapshot* snapshot) const
{
    if (snapshot != nullptr) {
        for (const RenderEntityPose& pose : snapshot->buildings) {
            if (pose.is_town_center && pose.player_slot == local_player_slot_) {
                return core::GridPos{pose.grid_x, pose.grid_y};
            }
        }
    }

    if (simulation != nullptr) {
        const entt::registry& registry = simulation->registry();
        const auto town_center_view = registry.view<
            sim::components::TownCenterTag,
            sim::components::PlayerOwnedTag,
            sim::components::GridPosition>();
        for (const entt::entity entity : town_center_view) {
            if (sim::components::entity_player_slot(registry, entity) != local_player_slot_) {
                continue;
            }

            return town_center_view.get<sim::components::GridPosition>(entity).cell;
        }
    }

    return std::nullopt;
}

std::optional<core::GridPos> GameRenderer::find_first_town_center_cell(
    const sim::Simulation* simulation,
    const SimRenderSnapshot* snapshot) const
{
    if (snapshot != nullptr) {
        for (const RenderEntityPose& pose : snapshot->buildings) {
            if (pose.is_town_center) {
                return core::GridPos{pose.grid_x, pose.grid_y};
            }
        }
    }

    if (simulation != nullptr) {
        const entt::registry& registry = simulation->registry();
        const auto town_center_view = registry.view<
            sim::components::TownCenterTag,
            sim::components::GridPosition>();
        if (town_center_view.begin() != town_center_view.end()) {
            return town_center_view.get<sim::components::GridPosition>(*town_center_view.begin()).cell;
        }
    }

    return std::nullopt;
}

void GameRenderer::center_camera_on_grid_cell(const core::GridPos& cell)
{
    camera_.center_on_world(
        static_cast<float>(cell.x) + 0.5F,
        static_cast<float>(cell.y) + 0.5F,
        constants::CAMERA_CLASSIC_START_ZOOM,
        constants::CAMERA_CLASSIC_START_ZOOM_LEVEL_INDEX);
}

void GameRenderer::center_camera_on_map_center(const int map_width, const int map_height)
{
    camera_.center_on_world(
        static_cast<float>(map_width) * 0.5F,
        static_cast<float>(map_height) * 0.5F,
        constants::CAMERA_CLASSIC_START_ZOOM,
        constants::CAMERA_CLASSIC_START_ZOOM_LEVEL_INDEX);
}

void GameRenderer::try_frame_player_start(
    const int map_width,
    const int map_height,
    const sim::Simulation* simulation,
    const SimRenderSnapshot* snapshot)
{
    if (map_framed_) {
        return;
    }

    const std::optional<core::GridPos> town_center_cell =
        find_local_town_center_cell(simulation, snapshot);
    if (town_center_cell.has_value()) {
        center_camera_on_grid_cell(*town_center_cell);
    }
    else {
        const std::optional<core::GridPos> first_town_center_cell =
            find_first_town_center_cell(simulation, snapshot);
        if (first_town_center_cell.has_value()) {
            center_camera_on_grid_cell(*first_town_center_cell);
        }
        else {
            center_camera_on_map_center(map_width, map_height);
        }
    }

    map_framed_ = true;
}

void GameRenderer::draw_screen_rect_outline(
    const float screen_x0,
    const float screen_y0,
    const float screen_x1,
    const float screen_y1,
    const float r,
    const float g,
    const float b) const
{
    draw_screen_rect_outline_immediate(screen_x0, screen_y0, screen_x1, screen_y1, r, g, b);
}

void GameRenderer::draw_screen_rect_outline_immediate(
    const float screen_x0,
    const float screen_y0,
    const float screen_x1,
    const float screen_y1,
    const float r,
    const float g,
    const float b) const
{
    const float window_width = static_cast<float>(window_size_.x);
    const float window_height = static_cast<float>(window_size_.y);
    if (window_width <= 0.0F || window_height <= 0.0F) {
        return;
    }

    const auto to_ndc_x = [window_width](const float screen_x) {
        return (screen_x / window_width) * 2.0F - 1.0F;
    };
    const auto to_ndc_y = [window_height](const float screen_y) {
        return 1.0F - (screen_y / window_height) * 2.0F;
    };

    const float left = to_ndc_x(std::min(screen_x0, screen_x1));
    const float right = to_ndc_x(std::max(screen_x0, screen_x1));
    const float top = to_ndc_y(std::min(screen_y0, screen_y1));
    const float bottom = to_ndc_y(std::max(screen_y0, screen_y1));
    const float depth = -0.99F;

    const std::array<SceneVertex, 8> line_vertices = {
        SceneVertex{left, top, depth, r, g, b},
        SceneVertex{right, top, depth, r, g, b},
        SceneVertex{right, top, depth, r, g, b},
        SceneVertex{right, bottom, depth, r, g, b},
        SceneVertex{right, bottom, depth, r, g, b},
        SceneVertex{left, bottom, depth, r, g, b},
        SceneVertex{left, bottom, depth, r, g, b},
        SceneVertex{left, top, depth, r, g, b},
    };

    glBindVertexArray(scene_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, scene_vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizei>(line_vertices.size() * sizeof(SceneVertex)),
        line_vertices.data(),
        GL_DYNAMIC_DRAW);
    glUseProgram(scene_shader_program_);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(line_vertices.size()));
    glBindVertexArray(0U);
}

void GameRenderer::apply_team_color(
    const float base_r,
    const float base_g,
    const float base_b,
    float& r,
    float& g,
    float& b) const
{
    r = base_r;
    g = base_g;
    b = base_b;
}

void GameRenderer::append_scene_vertices(
    const SceneVertex* vertices,
    const std::size_t vertex_count) const
{
    scene_batch_.insert(scene_batch_.end(), vertices, vertices + vertex_count);
}

void GameRenderer::flush_scene_batch() const
{
    if (scene_batch_.empty() || scene_vao_ == 0U || scene_vbo_ == 0U) {
        return;
    }

    glBindVertexArray(scene_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, scene_vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizei>(scene_batch_.size() * sizeof(SceneVertex)),
        scene_batch_.data(),
        GL_DYNAMIC_DRAW);
    glUseProgram(scene_shader_program_);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(scene_batch_.size()));
    glBindVertexArray(0U);
}

void GameRenderer::draw_scene_triangles(
    const std::array<SceneVertex, 6>& triangle_vertices,
    const int vertex_count) const
{
    append_scene_vertices(triangle_vertices.data(), static_cast<std::size_t>(vertex_count));
}

void GameRenderer::draw_scene_quad(
    const float wx0,
    const float wy0,
    const float wz0,
    const float wx1,
    const float wy1,
    const float wz1,
    const float wx2,
    const float wy2,
    const float wz2,
    const float wx3,
    const float wy3,
    const float wz3,
    const float r,
    const float g,
    const float b,
    const float light_factor) const
{
    const float lit_r = r * light_factor;
    const float lit_g = g * light_factor;
    const float lit_b = b * light_factor;

    const std::array<SceneVertex, 6> vertices = {
        make_scene_vertex(wx0, wy0, wz0, lit_r, lit_g, lit_b),
        make_scene_vertex(wx1, wy1, wz1, lit_r, lit_g, lit_b),
        make_scene_vertex(wx2, wy2, wz2, lit_r, lit_g, lit_b),
        make_scene_vertex(wx0, wy0, wz0, lit_r, lit_g, lit_b),
        make_scene_vertex(wx2, wy2, wz2, lit_r, lit_g, lit_b),
        make_scene_vertex(wx3, wy3, wz3, lit_r, lit_g, lit_b),
    };

    draw_scene_triangles(vertices, 6);
}

void GameRenderer::draw_extruded_tile(
    const int grid_x,
    const int grid_y,
    const float extrude_height,
    const float r,
    const float g,
    const float b) const
{
    const float gx = static_cast<float>(grid_x);
    const float gy = static_cast<float>(grid_y);
    const float top = extrude_height;
    const float top_light = constants::RENDER_AMBIENT_LIGHT;
    const float side_light = constants::RENDER_AMBIENT_LIGHT * constants::RENDER_SIDE_LIGHT_FACTOR;

    draw_scene_quad(
        gx,
        top,
        gy,
        gx + 1.0F,
        top,
        gy,
        gx + 1.0F,
        top,
        gy + 1.0F,
        gx,
        top,
        gy + 1.0F,
        r,
        g,
        b,
        top_light);

    if (extrude_height <= 0.0F) {
        return;
    }

    draw_scene_quad(gx, 0.0F, gy, gx + 1.0F, 0.0F, gy, gx + 1.0F, top, gy, gx, top, gy, r, g, b, side_light);
    draw_scene_quad(
        gx + 1.0F,
        0.0F,
        gy,
        gx + 1.0F,
        0.0F,
        gy + 1.0F,
        gx + 1.0F,
        top,
        gy + 1.0F,
        gx + 1.0F,
        top,
        gy,
        r,
        g,
        b,
        side_light);
    draw_scene_quad(
        gx,
        0.0F,
        gy + 1.0F,
        gx + 1.0F,
        0.0F,
        gy + 1.0F,
        gx + 1.0F,
        top,
        gy + 1.0F,
        gx,
        top,
        gy + 1.0F,
        r,
        g,
        b,
        side_light);
    draw_scene_quad(gx, 0.0F, gy, gx, 0.0F, gy + 1.0F, gx, top, gy + 1.0F, gx, top, gy, r, g, b, side_light);
}

void GameRenderer::draw_tile_grid_lines(const int grid_x, const int grid_y, const float extrude_height) const
{
    const float gx = static_cast<float>(grid_x);
    const float gy = static_cast<float>(grid_y);
    const float top = extrude_height + constants::RENDER_GRID_LINE_LIFT;
    const float width = constants::RENDER_GRID_LINE_WIDTH;
    const float r = constants::RENDER_GRID_LINE_R;
    const float g = constants::RENDER_GRID_LINE_G;
    const float b = constants::RENDER_GRID_LINE_B;

    draw_scene_quad(gx, top, gy, gx + 1.0F, top, gy, gx + 1.0F, top, gy + width, gx, top, gy + width, r, g, b, 1.0F);
    draw_scene_quad(
        gx,
        top,
        gy + 1.0F - width,
        gx + 1.0F,
        top,
        gy + 1.0F - width,
        gx + 1.0F,
        top,
        gy + 1.0F,
        gx,
        top,
        gy + 1.0F,
        r,
        g,
        b,
        1.0F);
    draw_scene_quad(gx, top, gy, gx, top, gy + 1.0F, gx + width, top, gy + 1.0F, gx + width, top, gy, r, g, b, 1.0F);
    draw_scene_quad(
        gx + 1.0F - width,
        top,
        gy,
        gx + 1.0F,
        top,
        gy,
        gx + 1.0F,
        top,
        gy + 1.0F,
        gx + 1.0F - width,
        top,
        gy + 1.0F,
        r,
        g,
        b,
        1.0F);
}

void GameRenderer::draw_entity_prism(
    const float world_x,
    const float world_z,
    const float height,
    const float r,
    const float g,
    const float b) const
{
    const float inset = 0.18F;
    const float fx = world_x - 0.5F + inset;
    const float fz = world_z - 0.5F + inset;
    const float size = 1.0F - inset * 2.0F;
    const float base = constants::RENDER_ENTITY_BASE_LIFT;
    const float top = base + height;
    const float top_light = constants::RENDER_AMBIENT_LIGHT;
    const float side_light = constants::RENDER_AMBIENT_LIGHT * constants::RENDER_SIDE_LIGHT_FACTOR;

    draw_scene_quad(
        fx,
        top,
        fz,
        fx + size,
        top,
        fz,
        fx + size,
        top,
        fz + size,
        fx,
        top,
        fz + size,
        r,
        g,
        b,
        top_light);
    draw_scene_quad(
        fx,
        base,
        fz,
        fx + size,
        base,
        fz,
        fx + size,
        top,
        fz,
        fx,
        top,
        fz,
        r,
        g,
        b,
        side_light);
    draw_scene_quad(
        fx + size,
        base,
        fz,
        fx + size,
        base,
        fz + size,
        fx + size,
        top,
        fz + size,
        fx + size,
        top,
        fz,
        r,
        g,
        b,
        side_light);
    draw_scene_quad(
        fx,
        base,
        fz + size,
        fx + size,
        base,
        fz + size,
        fx + size,
        top,
        fz + size,
        fx,
        top,
        fz + size,
        r,
        g,
        b,
        side_light);
    draw_scene_quad(
        fx,
        base,
        fz,
        fx,
        base,
        fz + size,
        fx,
        top,
        fz + size,
        fx,
        top,
        fz,
        r,
        g,
        b,
        side_light);
}

void GameRenderer::draw_entity_cylinder(
    const float world_x,
    const float world_z,
    const float height,
    const float radius,
    const float r,
    const float g,
    const float b) const
{
    const float base = constants::RENDER_ENTITY_BASE_LIFT;
    const float top = base + height;
    const float top_light = constants::RENDER_AMBIENT_LIGHT;
    const float side_light = constants::RENDER_AMBIENT_LIGHT * constants::RENDER_SIDE_LIGHT_FACTOR;
    const float lit_top_r = r * top_light;
    const float lit_top_g = g * top_light;
    const float lit_top_b = b * top_light;
    const float lit_side_r = r * side_light;
    const float lit_side_g = g * side_light;
    const float lit_side_b = b * side_light;

    constexpr float k_two_pi = 6.2831853F;
    const int segments = constants::RENDER_CYLINDER_SEGMENTS;
    const float angle_step = k_two_pi / static_cast<float>(segments);

    for (int segment_index = 0; segment_index < segments; ++segment_index) {
        const float angle_a = angle_step * static_cast<float>(segment_index);
        const float angle_b = angle_step * static_cast<float>(segment_index + 1);
        const float ax = world_x + std::cos(angle_a) * radius;
        const float az = world_z + std::sin(angle_a) * radius;
        const float bx = world_x + std::cos(angle_b) * radius;
        const float bz = world_z + std::sin(angle_b) * radius;

        const std::array<SceneVertex, 3> top_triangle = {
            make_scene_vertex(world_x, top, world_z, lit_top_r, lit_top_g, lit_top_b),
            make_scene_vertex(ax, top, az, lit_top_r, lit_top_g, lit_top_b),
            make_scene_vertex(bx, top, bz, lit_top_r, lit_top_g, lit_top_b),
        };
        append_scene_vertices(top_triangle.data(), top_triangle.size());

        const std::array<SceneVertex, 6> side_quad = {
            make_scene_vertex(ax, base, az, lit_side_r, lit_side_g, lit_side_b),
            make_scene_vertex(bx, base, bz, lit_side_r, lit_side_g, lit_side_b),
            make_scene_vertex(bx, top, bz, lit_side_r, lit_side_g, lit_side_b),
            make_scene_vertex(ax, base, az, lit_side_r, lit_side_g, lit_side_b),
            make_scene_vertex(bx, top, bz, lit_side_r, lit_side_g, lit_side_b),
            make_scene_vertex(ax, top, az, lit_side_r, lit_side_g, lit_side_b),
        };
        append_scene_vertices(side_quad.data(), side_quad.size());
    }
}

void GameRenderer::draw_ground_highlight(const SceneHighlight& highlight) const
{
    const float half = 0.5F * highlight.scale;
    const float outline_height = constants::RENDER_ENTITY_BASE_LIFT + 0.02F;

    draw_scene_quad(
        highlight.world_x - half,
        outline_height,
        highlight.world_z - half,
        highlight.world_x + half,
        outline_height,
        highlight.world_z - half,
        highlight.world_x + half,
        outline_height,
        highlight.world_z + half,
        highlight.world_x - half,
        outline_height,
        highlight.world_z + half,
        highlight.r,
        highlight.g,
        highlight.b,
        1.0F);
}

void GameRenderer::draw_unit_ground_highlight(const SceneHighlight& highlight) const
{
    const float radius = constants::RENDER_UNIT_RADIUS * highlight.scale;
    const float outline_height = constants::RENDER_ENTITY_BASE_LIFT + 0.02F;

    constexpr float k_two_pi = 6.2831853F;
    const int segments = constants::RENDER_CYLINDER_SEGMENTS;
    const float angle_step = k_two_pi / static_cast<float>(segments);

    for (int segment_index = 0; segment_index < segments; ++segment_index) {
        const float angle_a = angle_step * static_cast<float>(segment_index);
        const float angle_b = angle_step * static_cast<float>(segment_index + 1);
        const float ax = highlight.world_x + std::cos(angle_a) * radius;
        const float az = highlight.world_z + std::sin(angle_a) * radius;
        const float bx = highlight.world_x + std::cos(angle_b) * radius;
        const float bz = highlight.world_z + std::sin(angle_b) * radius;

        const std::array<SceneVertex, 3> triangle = {
            make_scene_vertex(highlight.world_x, outline_height, highlight.world_z, highlight.r, highlight.g, highlight.b),
            make_scene_vertex(ax, outline_height, az, highlight.r, highlight.g, highlight.b),
            make_scene_vertex(bx, outline_height, bz, highlight.r, highlight.g, highlight.b),
        };
        append_scene_vertices(triangle.data(), triangle.size());
    }
}

void GameRenderer::draw_selection_outline(const float world_x, const float world_z) const
{
    draw_ground_highlight(
        SceneHighlight{
            world_x,
            world_z,
            constants::RENDER_SELECTION_HIGHLIGHT_R,
            constants::RENDER_SELECTION_HIGHLIGHT_G,
            constants::RENDER_SELECTION_HIGHLIGHT_B,
            constants::RENDER_SELECTION_OUTLINE_SCALE,
        });
}

void GameRenderer::draw_unit_selection_outline(const float world_x, const float world_z) const
{
    draw_unit_ground_highlight(
        SceneHighlight{
            world_x,
            world_z,
            constants::RENDER_SELECTION_HIGHLIGHT_R,
            constants::RENDER_SELECTION_HIGHLIGHT_G,
            constants::RENDER_SELECTION_HIGHLIGHT_B,
            constants::RENDER_SELECTION_OUTLINE_SCALE,
        });
}

void GameRenderer::draw(
    const sim::Simulation& simulation,
    const std::vector<entt::entity>& selected_entities,
    const float interpolation_alpha,
    const SelectionBoxOverlay& selection_box,
    const entt::entity hover_unit,
    const bool hover_unit_is_enemy,
    const entt::entity hover_building,
    const std::optional<core::GridPos> hover_resource_cell,
    const std::optional<core::GridPos> selected_resource_cell,
    const entt::entity selected_building,
    const float fps,
    const net::LockstepNetworkHudStats& network_stats)
{
    if (active_camera_view() != CameraView::Classic) {
        return;
    }

    glClearColor(0.05F, 0.06F, 0.08F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto& registry = simulation.registry();
    const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
    if (world_view.begin() == world_view.end()) {
        return;
    }

    const entt::entity world = *world_view.begin();
    const auto& map = world_view.get<sim::components::MapGrid>(world);
    const sim::components::FogOfWarState* fog = nullptr;
    if (registry.any_of<sim::components::FogOfWarState>(world)) {
        fog = &registry.get<sim::components::FogOfWarState>(world);
    }

    if (!map_framed_) {
        try_frame_player_start(map.width, map.height, &simulation, nullptr);
    }

    scene_batch_.clear();

    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            if (fog != nullptr
                && is_fog_tile_unexplored(
                    fog->visible,
                    fog->explored,
                    map.width,
                    map.height,
                    local_player_slot_,
                    x,
                    y)) {
                draw_extruded_tile(x, y, 0.0F, 0.0F, 0.0F, 0.0F);
                continue;
            }

            const int index = y * map.width + x;
            const auto live_tile = map.tiles[static_cast<std::size_t>(index)];
            const int live_forest_wood = map.forest_wood[static_cast<std::size_t>(index)];
            const bool use_memory = fog != nullptr
                && fog_tile_is_in_shroud(
                    fog->visible,
                    fog->explored,
                    map.width,
                    map.height,
                    local_player_slot_,
                    x,
                    y);

            sim::components::TileType tile = live_tile;
            int forest_wood = live_forest_wood;
            float r = 0.20F;
            float g = 0.32F;
            float b = 0.18F;
            float extrude = 0.0F;
            resolve_tile_appearance(
                live_tile,
                live_forest_wood,
                use_memory,
                use_memory
                    ? sim::components::fog_memory_tile_type(*fog, map, x, y, local_player_slot_)
                    : live_tile,
                use_memory
                    ? sim::components::fog_memory_forest_wood(*fog, map, x, y, local_player_slot_)
                    : live_forest_wood,
                tile,
                forest_wood,
                r,
                g,
                b,
                extrude);

            if (fog != nullptr) {
                const float brightness = fog_tile_brightness(
                    fog->visible,
                    fog->explored,
                    map.width,
                    map.height,
                    local_player_slot_,
                    x,
                    y);
                r *= brightness;
                g *= brightness;
                b *= brightness;
            }

            draw_extruded_tile(x, y, extrude, r, g, b);
            if (show_grid_lines_ && (r > 0.0F || g > 0.0F || b > 0.0F)) {
                draw_tile_grid_lines(x, y, extrude);
            }
        }
    }

    for (const entt::entity entity : selected_entities) {
        if (!registry.valid(entity) || !registry.any_of<sim::components::GridPosition>(entity)) {
            continue;
        }

        const auto [world_x, world_z] = unit_render_world_xz(registry, entity, interpolation_alpha);
        if (registry.any_of<sim::components::UnitTag>(entity)) {
            draw_unit_selection_outline(world_x, world_z);
        }
        else {
            draw_selection_outline(world_x, world_z);
        }
    }

    if (selected_resource_cell.has_value()) {
        draw_selection_outline(
            static_cast<float>(selected_resource_cell->x) + 0.5F,
            static_cast<float>(selected_resource_cell->y) + 0.5F);
    }

    if (selected_building != entt::null && registry.valid(selected_building)
        && registry.any_of<sim::components::GridPosition>(selected_building)) {
        const auto& pos = registry.get<sim::components::GridPosition>(selected_building).cell;
        draw_selection_outline(
            static_cast<float>(pos.x) + 0.5F,
            static_cast<float>(pos.y) + 0.5F);
    }

    if (hover_unit != entt::null && registry.valid(hover_unit)
        && registry.any_of<sim::components::GridPosition>(hover_unit)) {
        const auto [world_x, world_z] = unit_render_world_xz(registry, hover_unit, interpolation_alpha);
        const bool is_selected =
            std::find(selected_entities.begin(), selected_entities.end(), hover_unit) != selected_entities.end();
        if (!is_selected) {
            draw_unit_ground_highlight(
                SceneHighlight{
                    world_x,
                    world_z,
                    hover_unit_is_enemy ? constants::RENDER_HOVER_ENEMY_R
                                        : constants::RENDER_HOVER_FRIENDLY_R,
                    hover_unit_is_enemy ? constants::RENDER_HOVER_ENEMY_G
                                        : constants::RENDER_HOVER_FRIENDLY_G,
                    hover_unit_is_enemy ? constants::RENDER_HOVER_ENEMY_B
                                        : constants::RENDER_HOVER_FRIENDLY_B,
                    constants::RENDER_HOVER_OUTLINE_SCALE,
                });
        }
    }
    else if (hover_building != entt::null && registry.valid(hover_building)
        && registry.any_of<sim::components::GridPosition>(hover_building)
        && hover_building != selected_building) {
        const auto& pos = registry.get<sim::components::GridPosition>(hover_building).cell;
        draw_ground_highlight(
            SceneHighlight{
                static_cast<float>(pos.x) + 0.5F,
                static_cast<float>(pos.y) + 0.5F,
                constants::RENDER_HOVER_FRIENDLY_R,
                constants::RENDER_HOVER_FRIENDLY_G,
                constants::RENDER_HOVER_FRIENDLY_B,
                constants::RENDER_HOVER_OUTLINE_SCALE,
            });
    }
    else if (hover_resource_cell.has_value()
        && (!selected_resource_cell.has_value()
            || *hover_resource_cell != *selected_resource_cell)) {
        draw_ground_highlight(
            SceneHighlight{
                static_cast<float>(hover_resource_cell->x) + 0.5F,
                static_cast<float>(hover_resource_cell->y) + 0.5F,
                constants::RENDER_HOVER_RESOURCE_R,
                constants::RENDER_HOVER_RESOURCE_G,
                constants::RENDER_HOVER_RESOURCE_B,
                constants::RENDER_HOVER_OUTLINE_SCALE,
            });
    }

    const auto building_view = registry.view<
        sim::components::BuildingTag,
        sim::components::GridPosition,
        sim::components::Health>();
    for (const entt::entity entity : building_view) {
        const bool visible = fog == nullptr
            || sim::systems::is_entity_visible_to_slot(registry, *fog, entity, local_player_slot_);
        const bool shrouded = !visible && fog != nullptr
            && sim::systems::is_building_renderable_in_shroud(registry, *fog, entity, local_player_slot_);
        if (!visible && !shrouded) {
            continue;
        }

        const auto& pos = building_view.get<sim::components::GridPosition>(entity).cell;
        float base_r = 0.55F;
        float base_g = 0.38F;
        float base_b = 0.18F;
        if (registry.any_of<sim::components::PlayerOwnedTag>(entity)
            && sim::components::entity_player_slot(registry, entity) != local_player_slot_) {
            base_r = 0.70F;
            base_g = 0.22F;
            base_b = 0.22F;
        }
        float r = base_r;
        float g = base_g;
        float b = base_b;
        apply_team_color(base_r, base_g, base_b, r, g, b);
        if (shrouded) {
            r *= constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
            g *= constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
            b *= constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
        }
        draw_entity_prism(
            static_cast<float>(pos.x) + 0.5F,
            static_cast<float>(pos.y) + 0.5F,
            constants::RENDER_BUILDING_HEIGHT,
            r,
            g,
            b);
    }

    const auto unit_view = registry.view<
        sim::components::UnitTag,
        sim::components::GridPosition,
        sim::components::Health>();
    for (const entt::entity entity : unit_view) {
        const auto& health = unit_view.get<sim::components::Health>(entity);
        if (health.current.raw() <= 0) {
            continue;
        }

        const auto [world_x, world_z] = unit_render_world_xz(registry, entity, interpolation_alpha);
        if (fog != nullptr) {
            const core::GridPos visibility_cell{
                static_cast<int>(std::floor(world_x)),
                static_cast<int>(std::floor(world_z)),
            };
            if (!sim::systems::is_cell_visible_to_slot(*fog, visibility_cell, local_player_slot_)
                && (!registry.any_of<sim::components::PlayerOwnedTag>(entity)
                    || sim::components::entity_player_slot(registry, entity) != local_player_slot_)) {
                continue;
            }
        }

        float base_r = 0.25F;
        float base_g = 0.55F;
        float base_b = 0.85F;
        if (registry.any_of<sim::components::EnemyTag>(entity)
            || (registry.any_of<sim::components::PlayerOwnedTag>(entity)
                && sim::components::entity_player_slot(registry, entity) != local_player_slot_)) {
            base_r = 0.85F;
            base_g = 0.25F;
            base_b = 0.25F;
        }
        else if (registry.any_of<sim::components::WorkerUnitTag>(entity)) {
            base_r = 0.85F;
            base_g = 0.75F;
            base_b = 0.20F;
        }

        float r = base_r;
        float g = base_g;
        float b = base_b;
        apply_team_color(base_r, base_g, base_b, r, g, b);
        draw_entity_cylinder(
            world_x,
            world_z,
            constants::RENDER_UNIT_HEIGHT,
            constants::RENDER_UNIT_RADIUS,
            r,
            g,
            b);
    }

    flush_scene_batch();

    glDisable(GL_DEPTH_TEST);
    hud_overlay_.draw(simulation, window_size_, fps, local_player_slot_, camera_.zoom(), network_stats);

    if (selection_box.active) {
        draw_screen_rect_outline(
            selection_box.start.x,
            selection_box.start.y,
            selection_box.current.x,
            selection_box.current.y,
            0.35F,
            0.95F,
            0.35F);
    }

    glEnable(GL_DEPTH_TEST);
}

void GameRenderer::draw_snapshot(
    const SimRenderSnapshot& snapshot,
    const std::vector<entt::entity>& selected_entities,
    const float interpolation_alpha,
    const SelectionBoxOverlay& selection_box,
    const entt::entity hover_unit,
    const bool hover_unit_is_enemy,
    const entt::entity hover_building,
    const std::optional<core::GridPos> hover_resource_cell,
    const std::optional<core::GridPos> selected_resource_cell,
    const entt::entity selected_building,
    const float fps,
    const net::LockstepNetworkHudStats& network_stats)
{
    if (active_camera_view() != CameraView::Classic) {
        return;
    }

    glClearColor(0.05F, 0.06F, 0.08F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (snapshot.map_width <= 0 || snapshot.map_height <= 0) {
        return;
    }

    if (!map_framed_) {
        try_frame_player_start(snapshot.map_width, snapshot.map_height, nullptr, &snapshot);
    }

    scene_batch_.clear();

    for (int y = 0; y < snapshot.map_height; ++y) {
        for (int x = 0; x < snapshot.map_width; ++x) {
            if (!snapshot.fog_visible.empty()
                && is_fog_tile_unexplored(
                    snapshot.fog_visible,
                    snapshot.fog_explored,
                    snapshot.map_width,
                    snapshot.map_height,
                    local_player_slot_,
                    x,
                    y)) {
                draw_extruded_tile(x, y, 0.0F, 0.0F, 0.0F, 0.0F);
                continue;
            }

            const int index = y * snapshot.map_width + x;
            const auto live_tile = snapshot.tiles[static_cast<std::size_t>(index)];
            const int live_forest_wood = snapshot.forest_wood[static_cast<std::size_t>(index)];
            const bool use_memory = !snapshot.fog_visible.empty()
                && !snapshot.fog_memory_tiles.empty()
                && fog_tile_is_in_shroud(
                    snapshot.fog_visible,
                    snapshot.fog_explored,
                    snapshot.map_width,
                    snapshot.map_height,
                    local_player_slot_,
                    x,
                    y);
            const sim::components::TileType memory_tile =
                use_memory
                ? static_cast<sim::components::TileType>(
                    snapshot.fog_memory_tiles[static_cast<std::size_t>(index)])
                : live_tile;
            const int memory_forest_wood =
                use_memory && static_cast<std::size_t>(index) < snapshot.fog_memory_forest_wood.size()
                ? snapshot.fog_memory_forest_wood[static_cast<std::size_t>(index)]
                : live_forest_wood;

            sim::components::TileType tile = live_tile;
            int forest_wood = live_forest_wood;
            float r = 0.20F;
            float g = 0.32F;
            float b = 0.18F;
            float extrude = 0.0F;
            resolve_tile_appearance(
                live_tile,
                live_forest_wood,
                use_memory,
                memory_tile,
                memory_forest_wood,
                tile,
                forest_wood,
                r,
                g,
                b,
                extrude);

            if (!snapshot.fog_visible.empty()) {
                const float brightness = fog_tile_brightness(
                    snapshot.fog_visible,
                    snapshot.fog_explored,
                    snapshot.map_width,
                    snapshot.map_height,
                    local_player_slot_,
                    x,
                    y);
                r *= brightness;
                g *= brightness;
                b *= brightness;
            }

            draw_extruded_tile(x, y, extrude, r, g, b);
            if (show_grid_lines_ && (r > 0.0F || g > 0.0F || b > 0.0F)) {
                draw_tile_grid_lines(x, y, extrude);
            }
        }
    }

    const auto find_unit_pose = [&](const entt::entity entity) -> const RenderEntityPose* {
        for (const RenderEntityPose& pose : snapshot.units) {
            if (pose.entity == entity) {
                return &pose;
            }
        }

        return nullptr;
    };

    for (const entt::entity entity : selected_entities) {
        const RenderEntityPose* pose = find_unit_pose(entity);
        if (pose == nullptr) {
            continue;
        }

        const auto [world_x, world_z] = interpolate_render_pose(*pose, interpolation_alpha);
        if (!snapshot.fog_visible.empty()) {
            const core::GridPos visibility_cell =
                snapshot_world_visibility_cell(world_x, world_z);
            if (!snapshot_cell_is_visible(snapshot, visibility_cell)) {
                continue;
            }
        }

        draw_unit_selection_outline(world_x, world_z);
    }

    if (selected_resource_cell.has_value()) {
        draw_selection_outline(
            static_cast<float>(selected_resource_cell->x) + 0.5F,
            static_cast<float>(selected_resource_cell->y) + 0.5F);
    }

    if (selected_building != entt::null) {
        for (const RenderEntityPose& pose : snapshot.buildings) {
            if (pose.entity != selected_building) {
                continue;
            }

            draw_selection_outline(
                static_cast<float>(pose.grid_x) + 0.5F,
                static_cast<float>(pose.grid_y) + 0.5F);
            break;
        }
    }

    if (hover_unit != entt::null) {
        const RenderEntityPose* pose = find_unit_pose(hover_unit);
        if (pose != nullptr) {
            const bool is_selected = std::find(selected_entities.begin(), selected_entities.end(), hover_unit)
                != selected_entities.end();
            if (!is_selected) {
                const auto [world_x, world_z] = interpolate_render_pose(*pose, interpolation_alpha);
                const bool render_hover = snapshot.fog_visible.empty()
                    || snapshot_cell_is_visible(
                        snapshot,
                        snapshot_world_visibility_cell(world_x, world_z));
                if (render_hover) {
                    draw_unit_ground_highlight(
                        SceneHighlight{
                            world_x,
                            world_z,
                            hover_unit_is_enemy ? constants::RENDER_HOVER_ENEMY_R
                                                : constants::RENDER_HOVER_FRIENDLY_R,
                            hover_unit_is_enemy ? constants::RENDER_HOVER_ENEMY_G
                                                : constants::RENDER_HOVER_FRIENDLY_G,
                            hover_unit_is_enemy ? constants::RENDER_HOVER_ENEMY_B
                                                : constants::RENDER_HOVER_FRIENDLY_B,
                            constants::RENDER_HOVER_OUTLINE_SCALE,
                        });
                }
            }
        }
    }
    else if (hover_building != entt::null && hover_building != selected_building) {
        for (const RenderEntityPose& pose : snapshot.buildings) {
            if (pose.entity != hover_building) {
                continue;
            }

            draw_ground_highlight(
                SceneHighlight{
                    static_cast<float>(pose.grid_x) + 0.5F,
                    static_cast<float>(pose.grid_y) + 0.5F,
                    constants::RENDER_HOVER_FRIENDLY_R,
                    constants::RENDER_HOVER_FRIENDLY_G,
                    constants::RENDER_HOVER_FRIENDLY_B,
                    constants::RENDER_HOVER_OUTLINE_SCALE,
                });
            break;
        }
    }
    else if (hover_resource_cell.has_value()
        && (!selected_resource_cell.has_value()
            || *hover_resource_cell != *selected_resource_cell)) {
        draw_ground_highlight(
            SceneHighlight{
                static_cast<float>(hover_resource_cell->x) + 0.5F,
                static_cast<float>(hover_resource_cell->y) + 0.5F,
                constants::RENDER_HOVER_RESOURCE_R,
                constants::RENDER_HOVER_RESOURCE_G,
                constants::RENDER_HOVER_RESOURCE_B,
                constants::RENDER_HOVER_OUTLINE_SCALE,
            });
    }

    for (const RenderEntityPose& pose : snapshot.buildings) {
        float base_r = 0.55F;
        float base_g = 0.38F;
        float base_b = 0.18F;
        if (pose.player_slot != local_player_slot_) {
            base_r = 0.70F;
            base_g = 0.22F;
            base_b = 0.22F;
        }

        float r = base_r;
        float g = base_g;
        float b = base_b;
        apply_team_color(base_r, base_g, base_b, r, g, b);
        if (pose.shrouded) {
            r *= constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
            g *= constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
            b *= constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
        }
        draw_entity_prism(
            static_cast<float>(pose.grid_x) + 0.5F,
            static_cast<float>(pose.grid_y) + 0.5F,
            constants::RENDER_BUILDING_HEIGHT,
            r,
            g,
            b);
    }

    for (const RenderEntityPose& pose : snapshot.units) {
        if (pose.health_current <= 0) {
            continue;
        }

        const auto [world_x, world_z] = interpolate_render_pose(pose, interpolation_alpha);
        if (!snapshot.fog_visible.empty()) {
            const core::GridPos visibility_cell =
                snapshot_world_visibility_cell(world_x, world_z);
            if (!snapshot_cell_is_visible(snapshot, visibility_cell)) {
                continue;
            }
        }

        float base_r = 0.25F;
        float base_g = 0.55F;
        float base_b = 0.85F;
        if (pose.is_enemy) {
            base_r = 0.85F;
            base_g = 0.25F;
            base_b = 0.25F;
        }
        else if (pose.is_worker) {
            base_r = 0.85F;
            base_g = 0.75F;
            base_b = 0.20F;
        }

        float r = base_r;
        float g = base_g;
        float b = base_b;
        apply_team_color(base_r, base_g, base_b, r, g, b);
        draw_entity_cylinder(
            world_x,
            world_z,
            constants::RENDER_UNIT_HEIGHT,
            constants::RENDER_UNIT_RADIUS,
            r,
            g,
            b);
    }

    flush_scene_batch();

    glDisable(GL_DEPTH_TEST);
    hud_overlay_.draw_snapshot(
        snapshot,
        window_size_,
        fps,
        local_player_slot_,
        camera_.zoom(),
        network_stats);

    if (selection_box.active) {
        draw_screen_rect_outline(
            selection_box.start.x,
            selection_box.start.y,
            selection_box.current.x,
            selection_box.current.y,
            0.35F,
            0.95F,
            0.35F);
    }

    glEnable(GL_DEPTH_TEST);
}

void GameRenderer::clear_frame() const
{
    if (active_camera_view() != CameraView::Classic) {
        return;
    }

    glClearColor(0.05F, 0.06F, 0.08F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GameRenderer::draw_waiting_overlay(
    const std::string& title,
    const std::string& subtitle) const
{
    if (active_camera_view() != CameraView::Classic) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    hud_overlay_.draw_waiting_overlay(window_size_, title, subtitle);
    glEnable(GL_DEPTH_TEST);
}

} // namespace aoa::render
