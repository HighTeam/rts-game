#include "render/game_renderer.hpp"

#include "core/constants.hpp"
#include "render/camera_settings.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/tags.hpp"

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

void GameRenderer::destroy_gl_objects()
{
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

void GameRenderer::zoom_camera(const float delta)
{
    camera_.add_zoom(delta);
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

void GameRenderer::draw_scene_triangles(
    const std::array<SceneVertex, 6>& triangle_vertices,
    const int vertex_count) const
{
    unsigned int vao = 0U;
    unsigned int vbo = 0U;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizei>(static_cast<std::size_t>(vertex_count) * sizeof(SceneVertex)),
        triangle_vertices.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(SceneVertex),
        reinterpret_cast<void*>(3 * sizeof(float)));

    glUseProgram(scene_shader_program_);
    glDrawArrays(GL_TRIANGLES, 0, vertex_count);

    glBindVertexArray(0U);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
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
    const int grid_x,
    const int grid_y,
    const float height,
    const float r,
    const float g,
    const float b) const
{
    const float inset = 0.22F;
    const float fx = static_cast<float>(grid_x) + inset;
    const float fy = static_cast<float>(grid_y) + inset;
    const float size = 1.0F - inset * 2.0F;
    const float top = height;
    const float top_light = constants::RENDER_AMBIENT_LIGHT;
    const float side_light = constants::RENDER_AMBIENT_LIGHT * constants::RENDER_SIDE_LIGHT_FACTOR;

    draw_scene_quad(
        fx,
        top,
        fy,
        fx + size,
        top,
        fy,
        fx + size,
        top,
        fy + size,
        fx,
        top,
        fy + size,
        r,
        g,
        b,
        top_light);
    draw_scene_quad(
        fx,
        0.0F,
        fy,
        fx + size,
        0.0F,
        fy,
        fx + size,
        top,
        fy,
        fx,
        top,
        fy,
        r,
        g,
        b,
        side_light);
    draw_scene_quad(
        fx + size,
        0.0F,
        fy,
        fx + size,
        0.0F,
        fy + size,
        fx + size,
        top,
        fy + size,
        fx + size,
        top,
        fy,
        r,
        g,
        b,
        side_light);
    draw_scene_quad(
        fx,
        0.0F,
        fy + size,
        fx + size,
        0.0F,
        fy + size,
        fx + size,
        top,
        fy + size,
        fx,
        top,
        fy + size,
        r,
        g,
        b,
        side_light);
    draw_scene_quad(
        fx,
        0.0F,
        fy,
        fx,
        0.0F,
        fy + size,
        fx,
        top,
        fy + size,
        fx,
        top,
        fy,
        r,
        g,
        b,
        side_light);
}

void GameRenderer::draw_selection_outline(const int grid_x, const int grid_y) const
{
    const float scale = constants::RENDER_SELECTION_OUTLINE_SCALE;
    const float center_x = static_cast<float>(grid_x) + 0.5F;
    const float center_z = static_cast<float>(grid_y) + 0.5F;
    const float half = 0.5F * scale;
    const float outline_height = 0.03F;

    draw_scene_quad(
        center_x - half,
        outline_height,
        center_z - half,
        center_x + half,
        outline_height,
        center_z - half,
        center_x + half,
        outline_height,
        center_z + half,
        center_x - half,
        outline_height,
        center_z + half,
        0.95F,
        0.82F,
        0.18F,
        1.0F);
}

void GameRenderer::draw(const sim::Simulation& simulation)
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

    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const int index = y * map.width + x;
            const auto tile = map.tiles[static_cast<std::size_t>(index)];

            float r = 0.20F;
            float g = 0.32F;
            float b = 0.18F;
            float extrude = 0.0F;
            if (tile == sim::components::TileType::Forest) {
                r = 0.10F;
                g = 0.22F;
                b = 0.10F;
                extrude = constants::RENDER_FOREST_EXTRUDE;
            }

            draw_extruded_tile(x, y, extrude, r, g, b);
        }
    }

    const auto town_center_view = registry.view<
        sim::components::TownCenterTag,
        sim::components::GridPosition>();
    for (const entt::entity entity : town_center_view) {
        const auto& pos = town_center_view.get<sim::components::GridPosition>(entity).cell;
        draw_selection_outline(pos.x, pos.y);
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
        draw_entity_prism(pos.x, pos.y, constants::RENDER_BUILDING_HEIGHT, r, g, b);
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

        const auto& pos = unit_view.get<sim::components::GridPosition>(entity).cell;

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
        draw_entity_prism(pos.x, pos.y, constants::RENDER_UNIT_HEIGHT, r, g, b);
    }

    glDisable(GL_DEPTH_TEST);
    hud_overlay_.draw(simulation, window_size_);
    glEnable(GL_DEPTH_TEST);
}

} // namespace aoa::render
