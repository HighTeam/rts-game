#include "render/game_renderer.hpp"

#include "core/constants.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"

#include <SFML/Window/Context.hpp>

#include <glad/glad.h>

#include <array>
#include <stdexcept>
#include <string>

namespace aoa::render {

namespace {

constexpr const char* VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec2 position;
void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

constexpr const char* FRAGMENT_SHADER = R"(
#version 330 core
uniform vec3 color;
out vec4 fragment_color;
void main()
{
    fragment_color = vec4(color, 1.0);
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

GameRenderer::GameRenderer()
{
    if (!init_gl_loader()) {
        throw std::runtime_error("Failed to initialize OpenGL loader");
    }

    glDisable(GL_DEPTH_TEST);
    create_shader_program();
}

GameRenderer::~GameRenderer()
{
    destroy_gl_objects();
}

void GameRenderer::create_shader_program()
{
    const unsigned int vertex_shader = compile_shader(GL_VERTEX_SHADER, VERTEX_SHADER);
    const unsigned int fragment_shader = compile_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
    shader_program_ = link_program(vertex_shader, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
}

void GameRenderer::destroy_gl_objects()
{
    if (shader_program_ != 0U) {
        glDeleteProgram(shader_program_);
        shader_program_ = 0U;
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

void GameRenderer::draw_colored_polygon(
    const std::array<sf::Vector2f, 4>& corners,
    const float r,
    const float g,
    const float b)
{
    if (window_size_.x == 0U || window_size_.y == 0U) {
        return;
    }

    const float window_width = static_cast<float>(window_size_.x);
    const float window_height = static_cast<float>(window_size_.y);

    const auto to_ndc = [&](const sf::Vector2f point) {
        const float ndc_x = (point.x / window_width) * 2.0F - 1.0F;
        const float ndc_y = 1.0F - (point.y / window_height) * 2.0F;
        return std::array<float, 2>{ndc_x, ndc_y};
    };

    std::array<float, 8> vertices{};
    for (std::size_t index = 0U; index < corners.size(); ++index) {
        const auto ndc = to_ndc(corners[index]);
        vertices[index * 2U] = ndc[0];
        vertices[(index * 2U) + 1U] = ndc[1];
    }

    unsigned int vao = 0U;
    unsigned int vbo = 0U;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizei>(vertices.size() * sizeof(float)),
        vertices.data(),
        GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glUseProgram(shader_program_);
    glUniform3f(glGetUniformLocation(shader_program_, "color"), r, g, b);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glBindVertexArray(0U);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
}

void GameRenderer::draw_iso_tile(const int grid_x, const int grid_y, const float r, const float g, const float b)
{
    const sf::Vector2f top = camera_.grid_top_corner(grid_x, grid_y);
    const float half_w = camera_.tile_half_width();
    const float half_h = camera_.tile_half_height();
    const float tile_h = camera_.tile_height();

    const std::array<sf::Vector2f, 4> corners = {
        top,
        sf::Vector2f{top.x + half_w, top.y + half_h},
        sf::Vector2f{top.x, top.y + tile_h},
        sf::Vector2f{top.x - half_w, top.y + half_h},
    };

    draw_colored_polygon(corners, r, g, b);
}

void GameRenderer::draw_iso_marker(const int grid_x, const int grid_y, const float r, const float g, const float b)
{
    const sf::Vector2f top = camera_.grid_top_corner(grid_x, grid_y);
    const float inset_w = camera_.tile_half_width() * 0.55F;
    const float inset_h = camera_.tile_half_height() * 0.55F;
    const float marker_h = camera_.tile_height() * 0.55F;

    const std::array<sf::Vector2f, 4> corners = {
        sf::Vector2f{top.x, top.y + inset_h * 0.5F},
        sf::Vector2f{top.x + inset_w, top.y + inset_h},
        sf::Vector2f{top.x, top.y + marker_h},
        sf::Vector2f{top.x - inset_w, top.y + inset_h},
    };

    draw_colored_polygon(corners, r, g, b);
}

void GameRenderer::draw(const sim::Simulation& simulation)
{
    glClearColor(0.05F, 0.06F, 0.08F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);

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
            if (tile == sim::components::TileType::Forest) {
                r = 0.10F;
                g = 0.22F;
                b = 0.10F;
            }

            draw_iso_tile(x, y, r, g, b);
        }
    }

    const auto building_view = registry.view<
        sim::components::BuildingTag,
        sim::components::GridPosition,
        sim::components::Health>();
    for (const entt::entity entity : building_view) {
        const auto& pos = building_view.get<sim::components::GridPosition>(entity).cell;
        draw_iso_marker(pos.x, pos.y, 0.55F, 0.38F, 0.18F);
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

        float r = 0.25F;
        float g = 0.55F;
        float b = 0.85F;
        if (registry.any_of<sim::components::EnemyTag>(entity)) {
            r = 0.85F;
            g = 0.25F;
            b = 0.25F;
        }
        else if (registry.any_of<sim::components::WorkerUnitTag>(entity)) {
            r = 0.85F;
            g = 0.75F;
            b = 0.20F;
        }

        draw_iso_marker(pos.x, pos.y, r, g, b);
    }
}

} // namespace aoa::render
