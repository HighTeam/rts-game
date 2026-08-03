#include "render/game_renderer.hpp"

#include "core/constants.hpp"
#include "render/camera_settings.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
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

void GameRenderer::resize(const sf::Vector2u window_size)
{
    window_size_ = window_size;
    camera_.set_window_size(window_size);
    camera_.reset_view();
    map_framed_ = false;
    glViewport(0, 0, static_cast<GLsizei>(window_size.x), static_cast<GLsizei>(window_size.y));
}

void GameRenderer::pan_camera(const float delta_x, const float delta_y)
{
    camera_.pan(delta_x, delta_y);
}

void GameRenderer::zoom_camera(const float delta, const float anchor_screen_x, const float anchor_screen_y)
{
    camera_.add_zoom(delta, anchor_screen_x, anchor_screen_y);
}

std::optional<core::GridPos> GameRenderer::screen_to_grid(
    const float screen_x,
    const float screen_y) const
{
    return camera_.screen_to_grid(screen_x, screen_y);
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
    if (registry.all_of<sim::components::PreviousWorldPosition, sim::components::WorldPosition>(entity)) {
        const auto& previous = registry.get<sim::components::PreviousWorldPosition>(entity);
        const auto& current = registry.get<sim::components::WorldPosition>(entity);
        const math::Fixed t = math::Fixed::from_float(std::clamp(interpolation_alpha, 0.0F, 1.0F));
        return {
            math::fixed_lerp(previous.x, current.x, t).to_float(),
            math::fixed_lerp(previous.y, current.y, t).to_float(),
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
    const float fps)
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

    if (!map_framed_) {
        camera_.frame_map(map.width, map.height);
        map_framed_ = true;
    }

    scene_batch_.clear();

    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const int index = y * map.width + x;
            const auto tile = map.tiles[static_cast<std::size_t>(index)];
            const int forest_wood = map.forest_wood[static_cast<std::size_t>(index)];

            float r = 0.20F;
            float g = 0.32F;
            float b = 0.18F;
            float extrude = 0.0F;
            if (tile == sim::components::TileType::Forest && forest_wood > 0) {
                r = 0.10F;
                g = 0.22F;
                b = 0.10F;
                extrude = constants::RENDER_FOREST_EXTRUDE;
            }

            draw_extruded_tile(x, y, extrude, r, g, b);
        }
    }

    for (const entt::entity entity : selected_entities) {
        if (!registry.valid(entity) || !registry.any_of<sim::components::GridPosition>(entity)) {
            continue;
        }

        const auto [world_x, world_z] = unit_render_world_xz(registry, entity, interpolation_alpha);
        draw_selection_outline(world_x, world_z);
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
            draw_ground_highlight(
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
        const auto& pos = building_view.get<sim::components::GridPosition>(entity).cell;
        const float base_r = 0.55F;
        const float base_g = 0.38F;
        const float base_b = 0.18F;
        float r = base_r;
        float g = base_g;
        float b = base_b;
        apply_team_color(base_r, base_g, base_b, r, g, b);
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

        float base_r = 0.25F;
        float base_g = 0.55F;
        float base_b = 0.85F;
        if (registry.any_of<sim::components::EnemyTag>(entity)) {
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
        draw_entity_prism(world_x, world_z, constants::RENDER_UNIT_HEIGHT, r, g, b);
    }

    flush_scene_batch();

    glDisable(GL_DEPTH_TEST);
    hud_overlay_.draw(simulation, window_size_, fps);

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

} // namespace aoa::render
