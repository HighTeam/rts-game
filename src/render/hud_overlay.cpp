#include "render/hud_overlay.hpp"

#include "app/command_panel.hpp"
#include "core/constants.hpp"
#include "core/grid.hpp"
#include "core/runtime_paths.hpp"
#include "data/content_types.hpp"
#include "net/lockstep_network_hud.hpp"
#include "render/building_sight_memory.hpp"
#include "render/hud_icons.hpp"
#include "render/minimap_math.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/content_pack.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/match_session.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"
#include "sim/player/player_economy.hpp"

#include <entt/entt.hpp>

#include <glad/glad.h>

#include <SFML/Window/Mouse.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace aoa::render {

namespace {

constexpr int GLYPH_WIDTH = 5;
constexpr int GLYPH_HEIGHT = 7;
std::array<std::uint8_t, 8> hud_player_color_indices{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};

void set_hud_player_color_indices(const std::array<std::uint8_t, 8>& color_indices)
{
    hud_player_color_indices = color_indices;
}

unsigned int hud_color_vao = 0U;
unsigned int hud_color_vbo = 0U;
unsigned int hud_textured_vao = 0U;
unsigned int hud_textured_vbo = 0U;
unsigned int hud_line_vao = 0U;
unsigned int hud_line_vbo = 0U;
unsigned int hud_color_batch_shader = 0U;
bool hud_color_batch_defer = false;
std::vector<float> hud_color_batch{};
std::vector<float> hud_overlay_color_batch{};
std::vector<float>* hud_active_color_batch = &hud_color_batch;
std::uint64_t hud_minimap_cached_tick = std::numeric_limits<std::uint64_t>::max();

struct HudTexturedQuad {
    unsigned int shader_program{0U};
    unsigned int texture_id{0U};
    float tint_a{1.0F};
    std::array<float, 16> vertices{};
};

std::vector<HudTexturedQuad> hud_textured_batch{};

void delete_hud_buffer(unsigned int& buffer)
{
    if (buffer == 0U) {
        return;
    }

    glDeleteBuffers(1, &buffer);
    buffer = 0U;
}

void delete_hud_vao(unsigned int& vao)
{
    if (vao == 0U) {
        return;
    }

    glDeleteVertexArrays(1, &vao);
    vao = 0U;
}

void destroy_hud_draw_geometry()
{
    delete_hud_buffer(hud_color_vbo);
    delete_hud_vao(hud_color_vao);
    delete_hud_buffer(hud_textured_vbo);
    delete_hud_vao(hud_textured_vao);
    delete_hud_buffer(hud_line_vbo);
    delete_hud_vao(hud_line_vao);
    hud_color_batch.clear();
    hud_overlay_color_batch.clear();
    hud_textured_batch.clear();
    hud_active_color_batch = &hud_color_batch;
    hud_color_batch_shader = 0U;
    hud_color_batch_defer = false;
    hud_minimap_cached_tick = std::numeric_limits<std::uint64_t>::max();
}

void ensure_hud_color_geometry()
{
    if (hud_color_vao != 0U) {
        return;
    }

    const GLsizei initial_bytes = static_cast<GLsizei>(
        constants::HUD_COLOR_BATCH_INITIAL_QUADS
        * constants::HUD_COLOR_QUAD_TRI_VERTICES
        * constants::HUD_COLOR_VERTEX_COMPONENTS
        * static_cast<int>(sizeof(float)));
    glGenVertexArrays(1, &hud_color_vao);
    glGenBuffers(1, &hud_color_vbo);
    glBindVertexArray(hud_color_vao);
    glBindBuffer(GL_ARRAY_BUFFER, hud_color_vbo);
    glBufferData(GL_ARRAY_BUFFER, initial_bytes, nullptr, GL_STREAM_DRAW);
    const GLsizei stride = static_cast<GLsizei>(
        constants::HUD_COLOR_VERTEX_COMPONENTS * static_cast<int>(sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        stride,
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
            static_cast<unsigned int>(constants::HUD_POSITION_COMPONENTS) * sizeof(float))));
    glBindVertexArray(0U);
}

void ensure_hud_textured_geometry()
{
    if (hud_textured_vao != 0U) {
        return;
    }

    glGenVertexArrays(1, &hud_textured_vao);
    glGenBuffers(1, &hud_textured_vbo);
    glBindVertexArray(hud_textured_vao);
    glBindBuffer(GL_ARRAY_BUFFER, hud_textured_vbo);
    const GLsizei initial_bytes = static_cast<GLsizei>(
        constants::HUD_TEXTURED_BATCH_INITIAL_QUADS
        * constants::HUD_TEXTURED_QUAD_TRI_VERTICES
        * constants::HUD_TEXTURED_VERTEX_COMPONENTS
        * static_cast<int>(sizeof(float)));
    glBufferData(GL_ARRAY_BUFFER, initial_bytes, nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(2U * sizeof(float))));
    glBindVertexArray(0U);
}

void ensure_hud_line_geometry()
{
    if (hud_line_vao != 0U) {
        return;
    }

    glGenVertexArrays(1, &hud_line_vao);
    glGenBuffers(1, &hud_line_vbo);
    glBindVertexArray(hud_line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, hud_line_vbo);
    const GLsizei stride = static_cast<GLsizei>(
        constants::HUD_COLOR_VERTEX_COMPONENTS * static_cast<int>(sizeof(float)));
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizei>(
            constants::HUD_LINE_VERTEX_COUNT
            * constants::HUD_COLOR_VERTEX_COMPONENTS
            * static_cast<int>(sizeof(float))),
        nullptr,
        GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        stride,
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
            static_cast<unsigned int>(constants::HUD_POSITION_COMPONENTS) * sizeof(float))));
    glBindVertexArray(0U);
}

void flush_hud_color_batch();
void flush_hud_overlay_color_batch();
void flush_hud_textured_batch();

void begin_hud_screen_pass()
{
    hud_color_batch.clear();
    hud_overlay_color_batch.clear();
    hud_textured_batch.clear();
    hud_active_color_batch = &hud_color_batch;
    hud_color_batch_defer = true;
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_ALWAYS);
    glDisable(GL_BLEND);
}

void end_hud_screen_pass()
{
    flush_hud_color_batch();
    flush_hud_textured_batch();
    flush_hud_overlay_color_batch();
    hud_color_batch_defer = false;
    hud_active_color_batch = &hud_color_batch;
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LESS);
}

void draw_screen_quad(
    sf::Vector2u window_size,
    unsigned int shader_program,
    float x,
    float y,
    float width,
    float height,
    float r,
    float g,
    float b,
    float a = 1.0F);

[[nodiscard]] std::uint8_t hud_color_index_for_slot(const std::uint8_t player_slot)
{
    if (player_slot >= hud_player_color_indices.size()) {
        return 0U;
    }

    return hud_player_color_indices[player_slot];
}

constexpr const char* HUD_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 vertex_color;
out vec4 frag_color;
void main()
{
    frag_color = vertex_color;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

constexpr const char* HUD_FRAGMENT_SHADER = R"(
#version 330 core
in vec4 frag_color;
out vec4 fragment_color;
void main()
{
    fragment_color = frag_color;
}
)";

constexpr const char* HUD_TEXTURED_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
out vec2 frag_uv;
void main()
{
    frag_uv = uv;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

constexpr const char* HUD_TEXTURED_FRAGMENT_SHADER = R"(
#version 330 core
in vec2 frag_uv;
uniform sampler2D icon_texture;
out vec4 fragment_color;
void main()
{
    fragment_color = texture(icon_texture, frag_uv);
}
)";

constexpr const char* HUD_TINTED_TEXTURE_FRAGMENT_SHADER = R"(
#version 330 core
in vec2 frag_uv;
uniform sampler2D icon_texture;
uniform vec4 tint;
out vec4 fragment_color;
void main()
{
    fragment_color = texture(icon_texture, frag_uv) * tint;
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
        throw std::runtime_error("HUD shader compile failed");
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
        throw std::runtime_error("HUD shader link failed");
    }

    return program;
}

const std::array<std::uint8_t, GLYPH_HEIGHT>& glyph_rows(const char character)
{
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_space = {
        0U, 0U, 0U, 0U, 0U, 0U, 0U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_colon = {
        0U, 0x04U, 0U, 0U, 0x04U, 0U, 0U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_w_upper = {
        0x11U, 0x11U, 0x11U, 0x15U, 0x15U, 0x1BU, 0x11U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_o_lower = {
        0U, 0x0EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_d_lower = {
        0x01U, 0x0DU, 0x13U, 0x11U, 0x11U, 0x13U, 0x0DU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_c_upper = {
        0U, 0x0EU, 0x11U, 0x10U, 0x10U, 0x11U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_a_lower = {
        0U, 0x0EU, 0x01U, 0x0FU, 0x11U, 0x11U, 0x0FU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_r_lower = {
        0U, 0x16U, 0x19U, 0x10U, 0x10U, 0x10U, 0x10U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_y_lower = {
        0U, 0x11U, 0x11U, 0x0FU, 0x01U, 0x0EU, 0x08U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_digit_0 = {
        0U, 0x0EU, 0x11U, 0x13U, 0x15U, 0x19U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_digit_1 = {
        0U, 0x04U, 0x0CU, 0x04U, 0x04U, 0x04U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_digit_2 = {
        0U, 0x0EU, 0x11U, 0x02U, 0x04U, 0x08U, 0x1FU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_digit_3 = {
        0U, 0x0EU, 0x11U, 0x06U, 0x01U, 0x11U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_digit_4 = {
        0U, 0x02U, 0x06U, 0x0AU, 0x12U, 0x1FU, 0x02U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_digit_5 = {
        0U, 0x1FU, 0x10U, 0x1EU, 0x01U, 0x11U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_digit_6 = {
        0U, 0x0EU, 0x10U, 0x1EU, 0x11U, 0x11U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_digit_7 = {
        0U, 0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x08U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_digit_8 = {
        0U, 0x0EU, 0x11U, 0x0EU, 0x11U, 0x11U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_digit_9 = {
        0U, 0x0EU, 0x11U, 0x11U, 0x0FU, 0x01U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_f_upper = {
        0U, 0x1FU, 0x10U, 0x1EU, 0x10U, 0x10U, 0x10U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_s_upper = {
        0U, 0x0EU, 0x11U, 0x0EU, 0x01U, 0x11U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_h_upper = {
        0U, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_p_upper = {
        0U, 0x1EU, 0x11U, 0x1EU, 0x10U, 0x10U, 0x10U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_i_upper = {
        0U, 0x04U, 0U, 0x04U, 0x04U, 0x04U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_n_upper = {
        0U, 0x11U, 0x19U, 0x15U, 0x13U, 0x11U, 0x11U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_g_upper = {
        0U, 0x0EU, 0x11U, 0x17U, 0x11U, 0x11U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_m_lower = {
        0U, 0U, 0x1AU, 0x15U, 0x15U, 0x11U, 0x11U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_s_lower = {
        0U, 0U, 0x0EU, 0x10U, 0x0EU, 0x01U, 0x1EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_e_upper = {
        0U, 0x1EU, 0x10U, 0x1CU, 0x10U, 0x10U, 0x1EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_slash = {
        0U, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x00U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_l_upper = {
        0U, 0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x1EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_o_upper = {
        0U, 0x0EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_a_upper = {
        0U, 0x0EU, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_t_upper = {
        0U, 0x1FU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_r_upper = {
        0U, 0x1EU, 0x11U, 0x1EU, 0x14U, 0x12U, 0x11U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_y_upper = {
        0U, 0x11U, 0x11U, 0x0AU, 0x04U, 0x04U, 0x04U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_u_upper = {
        0U, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_z_upper = {
        0U, 0x1EU, 0x02U, 0x04U, 0x08U, 0x10U, 0x1EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_z_lower = {
        0U, 0U, 0x1FU, 0x02U, 0x04U, 0x08U, 0x1FU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_period = {
        0U, 0U, 0U, 0U, 0U, 0x04U, 0U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_e_lower = {
        0U, 0x0EU, 0x11U, 0x1EU, 0x10U, 0x10U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_i_lower = {
        0U, 0x04U, 0U, 0x0CU, 0x04U, 0x04U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_n_lower = {
        0U, 0U, 0x16U, 0x19U, 0x11U, 0x11U, 0x11U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_g_lower = {
        0U, 0x0EU, 0x11U, 0x17U, 0x11U, 0x11U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_w_lower = {
        0U, 0U, 0x11U, 0x11U, 0x15U, 0x15U, 0x0AU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_m_upper = {
        0U, 0x11U, 0x1BU, 0x15U, 0x11U, 0x11U, 0x11U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_b_upper = {
        0U, 0x1EU, 0x11U, 0x1EU, 0x11U, 0x11U, 0x1EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_k_upper = {
        0U, 0x11U, 0x12U, 0x1CU, 0x12U, 0x11U, 0x11U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_d_upper = {
        0U, 0x1EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x1EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_p_lower = {
        0U, 0U, 0x1EU, 0x11U, 0x1EU, 0x10U, 0x10U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_c_lower = {
        0U, 0U, 0x0EU, 0x10U, 0x10U, 0x10U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_u_lower = {
        0U, 0U, 0x11U, 0x11U, 0x11U, 0x13U, 0x0DU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_l_lower = {
        0U, 0x08U, 0x08U, 0x08U, 0x08U, 0x08U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_t_lower = {
        0U, 0x08U, 0x1EU, 0x08U, 0x08U, 0x08U, 0x06U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_k_lower = {
        0U, 0x10U, 0x12U, 0x14U, 0x18U, 0x14U, 0x12U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_b_lower = {
        0U, 0x10U, 0x1EU, 0x11U, 0x11U, 0x11U, 0x1EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_f_lower = {
        0U, 0x06U, 0x08U, 0x1EU, 0x08U, 0x08U, 0x08U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_comma = {
        0U, 0U, 0U, 0U, 0U, 0x04U, 0x08U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_h_lower = {
        0x10U, 0x10U, 0x16U, 0x19U, 0x11U, 0x11U, 0x11U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_paren_open = {
        0U, 0x02U, 0x04U, 0x04U, 0x04U, 0x04U, 0x02U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_paren_close = {
        0U, 0x08U, 0x04U, 0x04U, 0x04U, 0x04U, 0x08U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_q_upper = {
        0U, 0x0EU, 0x11U, 0x11U, 0x15U, 0x12U, 0x0DU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_q_lower = {
        0U, 0U, 0x0FU, 0x11U, 0x0FU, 0x01U, 0x01U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_j_upper = {
        0U, 0x07U, 0x02U, 0x02U, 0x02U, 0x12U, 0x0CU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_j_lower = {
        0U, 0x02U, 0U, 0x06U, 0x02U, 0x12U, 0x0CU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_x_upper = {
        0U, 0x11U, 0x0AU, 0x04U, 0x0AU, 0x11U, 0x11U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_x_lower = {
        0U, 0U, 0x11U, 0x0AU, 0x04U, 0x0AU, 0x11U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_v_upper = {
        0U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0AU, 0x04U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_v_lower = {
        0U, 0U, 0x11U, 0x11U, 0x11U, 0x0AU, 0x04U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_bracket_open = {
        0U, 0x0EU, 0x08U, 0x08U, 0x08U, 0x08U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_bracket_close = {
        0U, 0x0EU, 0x02U, 0x02U, 0x02U, 0x02U, 0x0EU,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_semicolon = {
        0U, 0x04U, 0U, 0U, 0x04U, 0x04U, 0x08U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_apostrophe = {
        0U, 0x04U, 0x04U, 0U, 0U, 0U, 0U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_minus = {
        0U, 0U, 0U, 0x1FU, 0U, 0U, 0U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_equals = {
        0U, 0U, 0x1FU, 0U, 0x1FU, 0U, 0U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_asterisk = {
        0U, 0x04U, 0x15U, 0x0EU, 0x15U, 0x04U, 0U,
    };
    static const std::array<std::uint8_t, GLYPH_HEIGHT> glyph_plus = {
        0U, 0x04U, 0x04U, 0x1FU, 0x04U, 0x04U, 0U,
    };

    switch (character) {
    case ' ':
        return glyph_space;
    case ':':
        return glyph_colon;
    case 'W':
        return glyph_w_upper;
    case 'o':
        return glyph_o_lower;
    case 'd':
        return glyph_d_lower;
    case 'C':
        return glyph_c_upper;
    case 'a':
        return glyph_a_lower;
    case 'r':
        return glyph_r_lower;
    case 'y':
        return glyph_y_lower;
    case '0':
        return glyph_digit_0;
    case '1':
        return glyph_digit_1;
    case '2':
        return glyph_digit_2;
    case '3':
        return glyph_digit_3;
    case '4':
        return glyph_digit_4;
    case '5':
        return glyph_digit_5;
    case '6':
        return glyph_digit_6;
    case '7':
        return glyph_digit_7;
    case '8':
        return glyph_digit_8;
    case '9':
        return glyph_digit_9;
    case 'H':
        return glyph_h_upper;
    case 'F':
        return glyph_f_upper;
    case 'S':
        return glyph_s_upper;
    case 'P':
        return glyph_p_upper;
    case 'I':
        return glyph_i_upper;
    case 'N':
        return glyph_n_upper;
    case 'G':
        return glyph_g_upper;
    case 'm':
        return glyph_m_lower;
    case 's':
        return glyph_s_lower;
    case 'E':
        return glyph_e_upper;
    case 'L':
        return glyph_l_upper;
    case 'O':
        return glyph_o_upper;
    case 'A':
        return glyph_a_upper;
    case 'T':
        return glyph_t_upper;
    case 'R':
        return glyph_r_upper;
    case 'Y':
        return glyph_y_upper;
    case 'U':
        return glyph_u_upper;
    case 'Z':
        return glyph_z_upper;
    case 'z':
        return glyph_z_lower;
    case '.':
        return glyph_period;
    case 'e':
        return glyph_e_lower;
    case 'i':
        return glyph_i_lower;
    case 'n':
        return glyph_n_lower;
    case 'g':
        return glyph_g_lower;
    case 'w':
        return glyph_w_lower;
    case 'M':
        return glyph_m_upper;
    case 'B':
        return glyph_b_upper;
    case 'K':
        return glyph_k_upper;
    case 'D':
        return glyph_d_upper;
    case 'p':
        return glyph_p_lower;
    case 'c':
        return glyph_c_lower;
    case 'u':
        return glyph_u_lower;
    case 'l':
        return glyph_l_lower;
    case 't':
        return glyph_t_lower;
    case 'k':
        return glyph_k_lower;
    case 'b':
        return glyph_b_lower;
    case 'f':
        return glyph_f_lower;
    case ',':
        return glyph_comma;
    case '/':
        return glyph_slash;
    case 'h':
        return glyph_h_lower;
    case '(':
        return glyph_paren_open;
    case ')':
        return glyph_paren_close;
    case 'Q':
        return glyph_q_upper;
    case 'q':
        return glyph_q_lower;
    case 'J':
        return glyph_j_upper;
    case 'j':
        return glyph_j_lower;
    case 'X':
        return glyph_x_upper;
    case 'x':
        return glyph_x_lower;
    case 'V':
        return glyph_v_upper;
    case 'v':
        return glyph_v_lower;
    case '[':
        return glyph_bracket_open;
    case ']':
        return glyph_bracket_close;
    case ';':
        return glyph_semicolon;
    case '\'':
        return glyph_apostrophe;
    case '-':
        return glyph_minus;
    case '=':
        return glyph_equals;
    case '*':
        return glyph_asterisk;
    case '+':
        return glyph_plus;
    default:
        return glyph_space;
    }
}

void draw_screen_quad(
    const sf::Vector2u window_size,
    const unsigned int shader_program,
    const float x,
    const float y,
    const float width,
    const float height,
    const float r,
    const float g,
    const float b,
    const float a)
{
    if (window_size.x == 0U || window_size.y == 0U) {
        return;
    }

    const float window_width = static_cast<float>(window_size.x);
    const float window_height = static_cast<float>(window_size.y);

    const auto to_ndc = [&](const float px, const float py) {
        const float ndc_x = (px / window_width) * 2.0F - 1.0F;
        const float ndc_y = 1.0F - (py / window_height) * 2.0F;
        return std::array<float, 2>{ndc_x, ndc_y};
    };

    const auto top_left = to_ndc(x, y);
    const auto top_right = to_ndc(x + width, y);
    const auto bottom_right = to_ndc(x + width, y + height);
    const auto bottom_left = to_ndc(x, y + height);

    const auto push_vertex = [&](const std::array<float, 2>& point) {
        hud_active_color_batch->push_back(point[0]);
        hud_active_color_batch->push_back(point[1]);
        hud_active_color_batch->push_back(r);
        hud_active_color_batch->push_back(g);
        hud_active_color_batch->push_back(b);
        hud_active_color_batch->push_back(a);
    };
    push_vertex(top_left);
    push_vertex(top_right);
    push_vertex(bottom_right);
    push_vertex(top_left);
    push_vertex(bottom_right);
    push_vertex(bottom_left);
    hud_color_batch_shader = shader_program;
}

void flush_hud_color_buffer(std::vector<float>& batch)
{
    if (batch.empty() || hud_color_batch_shader == 0U) {
        batch.clear();
        return;
    }

    ensure_hud_color_geometry();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(hud_color_batch_shader);
    glBindVertexArray(hud_color_vao);
    glBindBuffer(GL_ARRAY_BUFFER, hud_color_vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizei>(batch.size() * sizeof(float)),
        batch.data(),
        GL_STREAM_DRAW);
    enable_rgb_blend_keep_framebuffer_opaque();
    glDrawArrays(
        GL_TRIANGLES,
        0,
        static_cast<GLsizei>(
            batch.size() / static_cast<std::size_t>(constants::HUD_COLOR_VERTEX_COMPONENTS)));
    glDisable(GL_BLEND);
    glBindVertexArray(0U);
    batch.clear();
}

void flush_hud_color_batch()
{
    flush_hud_color_buffer(hud_color_batch);
}

void flush_hud_overlay_color_batch()
{
    flush_hud_color_buffer(hud_overlay_color_batch);
}

void flush_hud_textured_batch()
{
    if (hud_textured_batch.empty()) {
        return;
    }

    ensure_hud_textured_geometry();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    enable_rgb_blend_keep_framebuffer_opaque();

    std::vector<float> vertices{};
    vertices.reserve(
        hud_textured_batch.size()
        * static_cast<std::size_t>(constants::HUD_TEXTURED_QUAD_TRI_VERTICES)
        * static_cast<std::size_t>(constants::HUD_TEXTURED_VERTEX_COMPONENTS));

    const auto emit_quad = [&](const HudTexturedQuad& quad) {
        const auto& src = quad.vertices;
        const int corners[6] = {0, 1, 2, 0, 2, 3};
        for (const int corner : corners) {
            const std::size_t src_index = static_cast<std::size_t>(corner)
                * static_cast<std::size_t>(constants::HUD_TEXTURED_VERTEX_COMPONENTS);
            vertices.push_back(src[src_index]);
            vertices.push_back(src[src_index + 1U]);
            vertices.push_back(src[src_index + 2U]);
            vertices.push_back(src[src_index + 3U]);
        }
    };

    const auto draw_group = [&](
                                const unsigned int shader_program,
                                const unsigned int texture_id,
                                const float tint_a) {
        if (vertices.empty() || shader_program == 0U || texture_id == 0U) {
            vertices.clear();
            return;
        }

        glUseProgram(shader_program);
        glUniform4f(glGetUniformLocation(shader_program, "tint"), 1.0F, 1.0F, 1.0F, tint_a);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glUniform1i(glGetUniformLocation(shader_program, "icon_texture"), 0);
        glBindVertexArray(hud_textured_vao);
        glBindBuffer(GL_ARRAY_BUFFER, hud_textured_vbo);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizei>(vertices.size() * sizeof(float)),
            vertices.data(),
            GL_STREAM_DRAW);
        glDrawArrays(
            GL_TRIANGLES,
            0,
            static_cast<GLsizei>(
                vertices.size()
                / static_cast<std::size_t>(constants::HUD_TEXTURED_VERTEX_COMPONENTS)));
        vertices.clear();
    };

    unsigned int group_shader = hud_textured_batch.front().shader_program;
    unsigned int group_texture = hud_textured_batch.front().texture_id;
    float group_tint = hud_textured_batch.front().tint_a;
    for (const HudTexturedQuad& quad : hud_textured_batch) {
        if (quad.shader_program != group_shader || quad.texture_id != group_texture
            || quad.tint_a != group_tint) {
            draw_group(group_shader, group_texture, group_tint);
            group_shader = quad.shader_program;
            group_texture = quad.texture_id;
            group_tint = quad.tint_a;
        }
        emit_quad(quad);
    }
    draw_group(group_shader, group_texture, group_tint);

    glBindTexture(GL_TEXTURE_2D, 0U);
    glBindVertexArray(0U);
    glDisable(GL_BLEND);
    hud_textured_batch.clear();
}

std::string format_zoom_line(const float camera_zoom)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "Zoom: %.2f", static_cast<double>(camera_zoom));
    return buffer;
}

[[nodiscard]] HudIcon icon_for_command_action(const app::CommandPanelAction action)
{
    switch (action) {
    case app::CommandPanelAction::Kill:
    case app::CommandPanelAction::Destroy:
        return HudIcon::Death;
    case app::CommandPanelAction::BuildTownCenter:
        return HudIcon::Firecamp;
    case app::CommandPanelAction::BuildHouse:
        return HudIcon::House;
    case app::CommandPanelAction::BuildLumberjack:
        return HudIcon::Axe;
    case app::CommandPanelAction::BuildExtractor:
        return HudIcon::ManaPlus;
    case app::CommandPanelAction::Attack:
        return HudIcon::Sword;
    case app::CommandPanelAction::Stop:
        return HudIcon::Boots;
    case app::CommandPanelAction::Build:
        return HudIcon::Hammer;
    case app::CommandPanelAction::Back:
        return HudIcon::ArrowSe;
    case app::CommandPanelAction::SpawnWorker:
        return HudIcon::BlueTShirt;
    case app::CommandPanelAction::SpawnMilitia:
        return HudIcon::MilitiaHat;
    case app::CommandPanelAction::Deselect:
        return HudIcon::HandDown;
    case app::CommandPanelAction::None:
        break;
    }

    return HudIcon::HandDown;
}

void apply_player_title_color(HudInfoPanel& panel, const std::uint8_t player_slot)
{
    const std::uint8_t color_index = hud_color_index_for_slot(player_slot);
    if (color_index >= constants::PLAYER_SLOT_COLOR_RGB.size()) {
        panel.title_r = constants::HUD_TEXT_R;
        panel.title_g = constants::HUD_TEXT_G;
        panel.title_b = constants::HUD_TEXT_B;
        return;
    }

    const auto& color = constants::PLAYER_SLOT_COLOR_RGB[color_index];
    panel.title_r = color[0];
    panel.title_g = color[1];
    panel.title_b = color[2];
}

void apply_nature_title_color(HudInfoPanel& panel)
{
    panel.title_r = constants::HUD_TEXT_R;
    panel.title_g = constants::HUD_TEXT_G;
    panel.title_b = constants::HUD_TEXT_B;
}

// Cooldown counts down, so an empty cooldown means the next mana drop is imminent.
[[nodiscard]] int extraction_percent_from_ticks(const int ticks_remaining)
{
    constexpr int FULL_PERCENT = 100;
    constexpr int interval = constants::EXTRACTOR_MANA_GEN_INTERVAL_TICKS;
    if constexpr (interval <= 0) {
        return FULL_PERCENT;
    }

    const int clamped = std::clamp(ticks_remaining, 0, interval);
    return ((interval - clamped) * FULL_PERCENT) / interval;
}

[[nodiscard]] const char* ground_type_debug_name(const sim::components::GroundType ground)
{
    if (ground == sim::components::GroundType::Snow) {
        return "snow";
    }

    if (ground == sim::components::GroundType::Sand) {
        return "sand";
    }

    return "grass";
}

[[nodiscard]] const char* tile_type_debug_name(const sim::components::TileType tile)
{
    switch (tile) {
    case sim::components::TileType::Forest:
        return "forest";
    case sim::components::TileType::Berries:
        return "berries";
    case sim::components::TileType::Blueberries:
        return "blueberries";
    case sim::components::TileType::GoldMine:
        return "gold_mine";
    default:
        return "grass";
    }
}

[[nodiscard]] const char* tree_visual_class_name(
    const int grid_x,
    const int grid_y,
    const sim::components::GroundType ground)
{
    const bool snow = ground == sim::components::GroundType::Snow;
    const int size_index =
        std::abs(grid_x * 3 + grid_y * 5) % constants::TREE_SIZE_VARIANT_COUNT;
    if (snow) {
        if (size_index == 0) {
            return "pines_forest_small";
        }

        if (size_index == 1) {
            return "pines_forest_medium";
        }

        return "pines_forest_large";
    }

    if (size_index == 0) {
        return "oak_forest_small";
    }

    if (size_index == 1) {
        return "oak_forest_medium";
    }

    return "oak_forest_large";
}

void append_tile_debug_lines(
    HudInfoPanel& panel,
    const core::GridPos cell,
    const sim::components::TileType tile,
    const sim::components::GroundType ground)
{
    panel.debug_lines.push_back(
        "id: tile " + std::to_string(cell.x) + "," + std::to_string(cell.y));
    if (tile == sim::components::TileType::Forest) {
        panel.debug_lines.push_back(
            std::string("class: ") + tree_visual_class_name(cell.x, cell.y, ground));
    }
    else {
        panel.debug_lines.push_back(std::string("class: ") + tile_type_debug_name(tile));
    }

    panel.debug_lines.push_back(std::string("ground: ") + ground_type_debug_name(ground));
    panel.debug_lines.push_back(std::string("tile: ") + tile_type_debug_name(tile));
}

void append_entity_debug_lines(
    HudInfoPanel& panel,
    const entt::entity entity,
    const std::string& class_name,
    const core::GridPos cell,
    const int path_remaining_steps = -1)
{
    panel.debug_lines.push_back("id: " + std::to_string(entt::to_integral(entity)));
    panel.debug_lines.push_back("class: " + class_name);
    panel.debug_lines.push_back(
        "cell: " + std::to_string(cell.x) + "," + std::to_string(cell.y));
    if (path_remaining_steps >= 0) {
        panel.debug_lines.push_back("path: " + std::to_string(path_remaining_steps) + " steps");
    }
}

void fill_combat_stats(
    HudInfoPanel& panel,
    const int melee_attack,
    const int melee_armor,
    const int pierce_attack,
    const int pierce_armor)
{
    panel.has_combat_stats = true;
    panel.melee_attack = melee_attack;
    panel.melee_armor = melee_armor;
    panel.pierce_attack = pierce_attack;
    panel.pierce_armor = pierce_armor;
}

void fill_combat_from_definition(HudInfoPanel& panel, const data::ArchetypeDefinition& definition)
{
    fill_combat_stats(
        panel,
        definition.melee_attack,
        definition.melee_armor,
        definition.pierce_attack,
        definition.pierce_armor);
}

void fill_combat_from_pose(HudInfoPanel& panel, const RenderEntityPose& pose)
{
    fill_combat_stats(
        panel,
        pose.melee_attack,
        pose.melee_armor,
        pose.pierce_attack,
        pose.pierce_armor);
}

[[nodiscard]] std::string player_suffix(
    const std::uint8_t player_slot,
    const std::uint8_t local_player_slot,
    const bool multiplayer,
    const std::array<std::string, net::constants::LOCKSTEP_MAX_PLAYER_SLOTS>* player_names)
{
    if (!multiplayer && player_slot == local_player_slot) {
        return " (You)";
    }

    if (player_names != nullptr && player_slot < player_names->size()
        && !(*player_names)[player_slot].empty()) {
        return " (" + (*player_names)[player_slot] + ")";
    }

    return " (Player " + std::to_string(static_cast<int>(player_slot) + 1) + ")";
}

[[nodiscard]] const data::ArchetypeDefinition* find_entity_definition(
    const entt::registry& registry,
    const entt::entity entity)
{
    if (!registry.any_of<sim::components::DefinitionRef>(entity)) {
        return nullptr;
    }

    const auto world_view =
        registry.view<sim::components::WorldTag, sim::components::ContentPack>();
    if (world_view.begin() == world_view.end()) {
        return nullptr;
    }

    const auto& content = world_view.get<sim::components::ContentPack>(*world_view.begin()).content;
    const auto& definition_ref = registry.get<sim::components::DefinitionRef>(entity);
    return data::find_archetype(content, definition_ref.id);
}

[[nodiscard]] const data::ArchetypeDefinition* find_town_center_definition(
    const entt::registry& registry)
{
    const auto world_view =
        registry.view<sim::components::WorldTag, sim::components::ContentPack>();
    if (world_view.begin() == world_view.end()) {
        return nullptr;
    }

    const auto& content = world_view.get<sim::components::ContentPack>(*world_view.begin()).content;
    return data::find_structure_archetype(content, std::string{constants::TOWN_CENTER_BUILDING_ID});
}

void fill_info_panel_from_live_entity(
    HudInfoPanel& panel,
    const entt::registry& registry,
    const entt::entity entity,
    const HudUnitContext& unit_context,
    const std::uint8_t local_player_slot)
{
    if (!registry.valid(entity)) {
        return;
    }

    panel.active = true;

    const bool is_nature = !registry.any_of<sim::components::PlayerOwnedTag>(entity);
    const std::uint8_t player_slot = is_nature
        ? 0U
        : sim::components::entity_player_slot(registry, entity);
    const std::string suffix = is_nature
        ? std::string{}
        : player_suffix(
              player_slot,
              local_player_slot,
              unit_context.multiplayer,
              &unit_context.player_names);

    if (registry.any_of<sim::components::MilitiaUnitTag>(entity)) {
        panel.title = "Militia" + suffix;
        apply_player_title_color(panel, player_slot);
    }
    else if (registry.any_of<sim::components::WorkerUnitTag>(entity)) {
        panel.title = "Worker" + suffix;
        apply_player_title_color(panel, player_slot);
    }
    else if (registry.any_of<sim::components::TownCenterTag>(entity)) {
        panel.title = "Town Center" + suffix;
        apply_player_title_color(panel, player_slot);
    }
    else if (registry.any_of<sim::components::HouseTag>(entity)) {
        panel.title = "House" + suffix;
        apply_player_title_color(panel, player_slot);
    }
    else if (registry.any_of<sim::components::LumberjackTag>(entity)) {
        panel.title = "Lumberjack" + suffix;
        apply_player_title_color(panel, player_slot);
    }
    else if (registry.any_of<sim::components::ExtractorTag>(entity)) {
        panel.title = "Extractor" + suffix;
        apply_player_title_color(panel, player_slot);
        if (!registry.any_of<sim::components::UnderConstructionTag>(entity)) {
            panel.has_mana_rate = true;
            panel.mana_rate = constants::EXTRACTOR_MANA_GEN_AMOUNT;
            panel.has_extraction_progress = true;
            const int remaining = registry.any_of<sim::components::ManaGenerationCooldown>(entity)
                ? registry.get<sim::components::ManaGenerationCooldown>(entity).ticks_remaining
                : 0;
            panel.extraction_percent = extraction_percent_from_ticks(remaining);
        }
    }
    else if (registry.any_of<sim::components::ManaLakeTag>(entity)) {
        panel.title = "Mana lake (Nature)";
        apply_nature_title_color(panel);
        panel.has_mana_rate = true;
        panel.mana_rate = constants::EXTRACTOR_MANA_GEN_AMOUNT;
        return;
    }
    else if (is_nature) {
        panel.title = "Other (Nature)";
        apply_nature_title_color(panel);
    }
    else {
        panel.title = "Other" + suffix;
        apply_player_title_color(panel, player_slot);
    }

    if (registry.any_of<sim::components::Health>(entity)) {
        const auto& health = registry.get<sim::components::Health>(entity);
        if (health.current.raw() > 0) {
            panel.has_health = true;
            panel.health_current = health.current.to_int();
            panel.health_max = health.max.to_int();
        }
    }

    if (const data::ArchetypeDefinition* definition = find_entity_definition(registry, entity);
        definition != nullptr) {
        fill_combat_from_definition(panel, *definition);
    }

    if (registry.any_of<sim::components::CarriedFood>(entity)) {
        const int carried_food = registry.get<sim::components::CarriedFood>(entity).amount;
        if (carried_food > 0) {
            panel.carry_amount = carried_food;
            panel.carry_icon = HudIcon::Food;
        }
    }

    if (panel.carry_amount <= 0 && registry.any_of<sim::components::CarriedMoney>(entity)) {
        const int carried_money = registry.get<sim::components::CarriedMoney>(entity).amount;
        if (carried_money > 0) {
            panel.carry_amount = carried_money;
            panel.carry_icon = HudIcon::Money;
        }
    }

    if (panel.carry_amount <= 0 && registry.any_of<sim::components::CarriedWood>(entity)) {
        const int carried = registry.get<sim::components::CarriedWood>(entity).amount;
        if (carried > 0) {
            panel.carry_amount = carried;
            panel.carry_icon = HudIcon::Wood;
        }
    }
}

void fill_info_panel_from_pose(
    HudInfoPanel& panel,
    const RenderEntityPose& pose,
    const HudUnitContext& unit_context,
    const std::uint8_t local_player_slot)
{
    panel.active = true;
    const std::string suffix = pose.is_nature
        ? std::string{}
        : player_suffix(
              pose.player_slot,
              local_player_slot,
              unit_context.multiplayer,
              &unit_context.player_names);

    if (pose.is_militia) {
        panel.title = "Militia" + suffix;
        apply_player_title_color(panel, pose.player_slot);
    }
    else if (pose.is_worker) {
        panel.title = "Worker" + suffix;
        apply_player_title_color(panel, pose.player_slot);
    }
    else if (pose.is_town_center) {
        panel.title = "Town Center" + suffix;
        apply_player_title_color(panel, pose.player_slot);
    }
    else if (pose.is_house) {
        panel.title = "House" + suffix;
        apply_player_title_color(panel, pose.player_slot);
    }
    else if (pose.is_extractor) {
        panel.title = "Extractor" + suffix;
        apply_player_title_color(panel, pose.player_slot);
        if (!pose.under_construction) {
            panel.has_mana_rate = true;
            panel.mana_rate = constants::EXTRACTOR_MANA_GEN_AMOUNT;
            panel.has_extraction_progress = true;
            panel.extraction_percent =
                extraction_percent_from_ticks(pose.mana_gen_ticks_remaining);
        }
    }
    else if (pose.is_mana_lake) {
        panel.title = "Mana lake (Nature)";
        apply_nature_title_color(panel);
        panel.has_mana_rate = true;
        panel.mana_rate = constants::EXTRACTOR_MANA_GEN_AMOUNT;
        return;
    }
    else if (pose.is_lumberjack) {
        panel.title = "Lumberjack" + suffix;
        apply_player_title_color(panel, pose.player_slot);
    }
    else if (pose.is_nature) {
        panel.title = "Other (Nature)";
        apply_nature_title_color(panel);
    }
    else {
        panel.title = "Other" + suffix;
        apply_player_title_color(panel, pose.player_slot);
    }

    if (pose.health_current > 0) {
        panel.has_health = true;
        panel.health_current = pose.health_current;
        panel.health_max = pose.health_max;
    }

    fill_combat_from_pose(panel, pose);

    if (pose.carried_food > 0) {
        panel.carry_amount = pose.carried_food;
        panel.carry_icon = HudIcon::Food;
    }
    else if (pose.carried_money > 0) {
        panel.carry_amount = pose.carried_money;
        panel.carry_icon = HudIcon::Money;
    }
    else if (pose.carried_wood > 0) {
        panel.carry_amount = pose.carried_wood;
        panel.carry_icon = HudIcon::Wood;
    }
}

[[nodiscard]] HudInfoPanel build_info_panel_live(
    const entt::registry& registry,
    const HudUnitContext& unit_context,
    const std::uint8_t local_player_slot,
    const bool show_selection_debug)
{
    HudInfoPanel panel{};

    if (unit_context.selected_single_unit != entt::null) {
        fill_info_panel_from_live_entity(
            panel,
            registry,
            unit_context.selected_single_unit,
            unit_context,
            local_player_slot);
        if (show_selection_debug && panel.active) {
            std::string class_name = "unknown";
            if (registry.any_of<sim::components::DefinitionRef>(unit_context.selected_single_unit)) {
                class_name =
                    registry.get<sim::components::DefinitionRef>(unit_context.selected_single_unit).id;
            }

            core::GridPos cell{0, 0};
            if (registry.any_of<sim::components::GridPosition>(unit_context.selected_single_unit)) {
                cell = registry.get<sim::components::GridPosition>(unit_context.selected_single_unit).cell;
            }

            int path_remaining = -1;
            if (registry.any_of<sim::components::UnitTag>(unit_context.selected_single_unit)
                || registry.any_of<sim::components::WorkerUnitTag>(
                    unit_context.selected_single_unit)) {
                path_remaining = 0;
                if (registry.any_of<sim::components::MovePath>(
                        unit_context.selected_single_unit)) {
                    const auto& path = registry.get<sim::components::MovePath>(
                        unit_context.selected_single_unit);
                    path_remaining = std::max(
                        0,
                        static_cast<int>(path.cells.size()) - path.next_index);
                }
            }

            append_entity_debug_lines(
                panel, unit_context.selected_single_unit, class_name, cell, path_remaining);
        }
        return panel;
    }

    if (unit_context.selected_resource_cell.has_value()) {
        const auto world_view =
            registry.view<sim::components::WorldTag, sim::components::MapGrid>();
        if (world_view.begin() != world_view.end()) {
            const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
            const core::GridPos& cell = *unit_context.selected_resource_cell;
            if (core::is_inside_grid(cell, map.width, map.height)) {
                const int index = core::grid_index(cell, map.width);
                const int remaining_wood = map.forest_wood[static_cast<std::size_t>(index)];
                if (remaining_wood > 0) {
                    panel.active = true;
                    panel.title = "Tree (Nature)";
                    apply_nature_title_color(panel);
                    panel.carry_amount = remaining_wood;
                    panel.carry_icon = HudIcon::Wood;
                    panel.carry_is_remaining = true;
                    if (show_selection_debug) {
                        append_tile_debug_lines(
                            panel,
                            cell,
                            sim::components::TileType::Forest,
                            sim::components::ground_at(map, static_cast<std::size_t>(index)));
                    }
                    return panel;
                }

                const int remaining_food = map.bush_food[static_cast<std::size_t>(index)];
                if (remaining_food > 0) {
                    panel.active = true;
                    const auto tile = map.tiles[static_cast<std::size_t>(index)];
                    panel.title = tile == sim::components::TileType::Blueberries
                        ? "Blueberries (Nature)"
                        : "Berries (Nature)";
                    apply_nature_title_color(panel);
                    panel.carry_amount = remaining_food;
                    panel.carry_icon = HudIcon::Food;
                    panel.carry_is_remaining = true;
                    if (show_selection_debug) {
                        append_tile_debug_lines(
                            panel,
                            cell,
                            tile,
                            sim::components::ground_at(map, static_cast<std::size_t>(index)));
                    }
                    return panel;
                }

                const int remaining_money =
                    static_cast<std::size_t>(index) < map.mine_money.size()
                    ? map.mine_money[static_cast<std::size_t>(index)]
                    : 0;
                if (remaining_money > 0) {
                    panel.active = true;
                    panel.title = "Gold Mine (Nature)";
                    apply_nature_title_color(panel);
                    panel.carry_amount = remaining_money;
                    panel.carry_icon = HudIcon::Money;
                    panel.carry_is_remaining = true;
                    if (show_selection_debug) {
                        append_tile_debug_lines(
                            panel,
                            cell,
                            sim::components::TileType::GoldMine,
                            sim::components::ground_at(map, static_cast<std::size_t>(index)));
                    }
                    return panel;
                }
            }
        }
    }

    if (unit_context.selected_building_is_mana_lake
        || unit_context.command_panel_mode == app::CommandPanelMode::ManaLakeInfo) {
        panel.active = true;
        panel.title = "Mana lake (Nature)";
        apply_nature_title_color(panel);
        panel.has_mana_rate = true;
        panel.mana_rate = constants::EXTRACTOR_MANA_GEN_AMOUNT;
        return panel;
    }

    if (unit_context.has_selected_building_health) {
        panel.active = true;
        panel.has_health = true;
        panel.health_current = unit_context.selected_building_health_current;
        panel.health_max = unit_context.selected_building_health_max;

        const std::uint8_t owner_slot = unit_context.has_selected_building_owner
            ? unit_context.selected_building_player_slot
            : local_player_slot;
        const std::string suffix = player_suffix(
            owner_slot,
            local_player_slot,
            unit_context.multiplayer,
            &unit_context.player_names);
        if (unit_context.selected_building_is_house) {
            panel.title = "House" + suffix;
            apply_player_title_color(panel, owner_slot);
        }
        else if (unit_context.selected_building_is_lumberjack) {
            panel.title = "Lumberjack" + suffix;
            apply_player_title_color(panel, owner_slot);
        }
        else if (unit_context.selected_building_is_extractor) {
            panel.title = "Extractor" + suffix;
            apply_player_title_color(panel, owner_slot);
            panel.has_mana_rate = true;
            panel.mana_rate = constants::EXTRACTOR_MANA_GEN_AMOUNT;
        }
        else {
            panel.title = "Town Center" + suffix;
            apply_player_title_color(panel, owner_slot);
        }

        if (unit_context.selected_single_unit != entt::null) {
            if (const data::ArchetypeDefinition* definition =
                    find_entity_definition(registry, unit_context.selected_single_unit);
                definition != nullptr) {
                fill_combat_from_definition(panel, *definition);
            }
        }
        else if (const data::ArchetypeDefinition* definition = find_town_center_definition(registry);
                 definition != nullptr) {
            fill_combat_from_definition(panel, *definition);
        }
    }

    return panel;
}

[[nodiscard]] const RenderEntityPose* find_pose_by_entity(
    const std::vector<RenderEntityPose>& poses,
    const entt::entity entity)
{
    for (const RenderEntityPose& pose : poses) {
        if (pose.entity == entity) {
            return &pose;
        }
    }

    return nullptr;
}

[[nodiscard]] HudInfoPanel build_info_panel_snapshot(
    const SimRenderSnapshot& snapshot,
    const HudUnitContext& unit_context,
    const std::uint8_t local_player_slot,
    const bool show_selection_debug)
{
    HudInfoPanel panel{};

    if (unit_context.selected_single_unit != entt::null) {
        if (const RenderEntityPose* unit_pose =
                find_pose_by_entity(snapshot.units, unit_context.selected_single_unit);
            unit_pose != nullptr) {
            fill_info_panel_from_pose(panel, *unit_pose, unit_context, local_player_slot);
            if (show_selection_debug) {
                const std::string class_name =
                    unit_pose->archetype_id.empty() ? "unknown" : unit_pose->archetype_id;
                append_entity_debug_lines(
                    panel,
                    unit_pose->entity,
                    class_name,
                    {unit_pose->grid_x, unit_pose->grid_y},
                    std::max(
                        0,
                        static_cast<int>(unit_pose->debug_path_cells.size())
                            - unit_pose->debug_path_next_index));
            }
            return panel;
        }

        if (const RenderEntityPose* building_pose =
                find_pose_by_entity(snapshot.buildings, unit_context.selected_single_unit);
            building_pose != nullptr) {
            fill_info_panel_from_pose(panel, *building_pose, unit_context, local_player_slot);
            if (show_selection_debug) {
                const std::string class_name =
                    building_pose->archetype_id.empty() ? "unknown" : building_pose->archetype_id;
                append_entity_debug_lines(
                    panel,
                    building_pose->entity,
                    class_name,
                    {building_pose->grid_x, building_pose->grid_y});
            }
            return panel;
        }
    }

    if (unit_context.selected_building_is_mana_lake
        || unit_context.command_panel_mode == app::CommandPanelMode::ManaLakeInfo) {
        panel.active = true;
        panel.title = "Mana lake (Nature)";
        apply_nature_title_color(panel);
        panel.has_mana_rate = true;
        panel.mana_rate = constants::EXTRACTOR_MANA_GEN_AMOUNT;
        return panel;
    }

    if (unit_context.selected_resource_cell.has_value()
        && snapshot.forest_wood.size()
            == static_cast<std::size_t>(snapshot.map_width * snapshot.map_height)) {
        const core::GridPos& cell = *unit_context.selected_resource_cell;
        if (core::is_inside_grid(cell, snapshot.map_width, snapshot.map_height)) {
            const int index = core::grid_index(cell, snapshot.map_width);
            const int remaining_wood = snapshot.forest_wood[static_cast<std::size_t>(index)];
            if (remaining_wood > 0) {
                panel.active = true;
                panel.title = "Tree (Nature)";
                apply_nature_title_color(panel);
                panel.carry_amount = remaining_wood;
                panel.carry_icon = HudIcon::Wood;
                panel.carry_is_remaining = true;
                if (show_selection_debug) {
                    append_tile_debug_lines(
                        panel,
                        cell,
                        sim::components::TileType::Forest,
                        static_cast<std::size_t>(index) < snapshot.ground.size()
                            ? snapshot.ground[static_cast<std::size_t>(index)]
                            : sim::components::GroundType::Grass);
                }
                return panel;
            }

            if (index < snapshot.bush_food.size()) {
                const int remaining_food = snapshot.bush_food[static_cast<std::size_t>(index)];
                if (remaining_food > 0) {
                    panel.active = true;
                    const auto tile = snapshot.tiles[static_cast<std::size_t>(index)];
                    panel.title = tile == sim::components::TileType::Blueberries
                        ? "Blueberries (Nature)"
                        : "Berries (Nature)";
                    apply_nature_title_color(panel);
                    panel.carry_amount = remaining_food;
                    panel.carry_icon = HudIcon::Food;
                    panel.carry_is_remaining = true;
                    if (show_selection_debug) {
                        append_tile_debug_lines(
                            panel,
                            cell,
                            tile,
                            static_cast<std::size_t>(index) < snapshot.ground.size()
                                ? snapshot.ground[static_cast<std::size_t>(index)]
                                : sim::components::GroundType::Grass);
                    }
                    return panel;
                }
            }

            if (index < snapshot.mine_money.size()) {
                const int remaining_money = snapshot.mine_money[static_cast<std::size_t>(index)];
                if (remaining_money > 0) {
                    panel.active = true;
                    panel.title = "Gold Mine (Nature)";
                    apply_nature_title_color(panel);
                    panel.carry_amount = remaining_money;
                    panel.carry_icon = HudIcon::Money;
                    panel.carry_is_remaining = true;
                    if (show_selection_debug) {
                        append_tile_debug_lines(
                            panel,
                            cell,
                            sim::components::TileType::GoldMine,
                            static_cast<std::size_t>(index) < snapshot.ground.size()
                                ? snapshot.ground[static_cast<std::size_t>(index)]
                                : sim::components::GroundType::Grass);
                    }
                    return panel;
                }
            }
        }
    }

    if (unit_context.has_selected_building_health) {
        panel.active = true;
        panel.has_health = true;
        panel.health_current = unit_context.selected_building_health_current;
        panel.health_max = unit_context.selected_building_health_max;

        if (unit_context.selected_single_unit != entt::null) {
            if (const RenderEntityPose* building_pose =
                    find_pose_by_entity(snapshot.buildings, unit_context.selected_single_unit);
                building_pose != nullptr) {
                fill_info_panel_from_pose(panel, *building_pose, unit_context, local_player_slot);
                return panel;
            }
        }

        const std::uint8_t owner_slot = unit_context.has_selected_building_owner
            ? unit_context.selected_building_player_slot
            : local_player_slot;
        const std::string suffix = player_suffix(
            owner_slot,
            local_player_slot,
            unit_context.multiplayer,
            &unit_context.player_names);
        if (unit_context.selected_building_is_house) {
            panel.title = "House" + suffix;
            apply_player_title_color(panel, owner_slot);
            return panel;
        }

        if (unit_context.selected_building_is_lumberjack) {
            panel.title = "Lumberjack" + suffix;
            apply_player_title_color(panel, owner_slot);
            return panel;
        }

        if (unit_context.selected_building_is_extractor) {
            panel.title = "Extractor" + suffix;
            apply_player_title_color(panel, owner_slot);
            panel.has_mana_rate = true;
            panel.mana_rate = constants::EXTRACTOR_MANA_GEN_AMOUNT;
            return panel;
        }

        panel.title = "Town Center" + suffix;
        apply_player_title_color(panel, owner_slot);
    }

    return panel;
}

void draw_panel_frame(
    const sf::Vector2u window_size,
    const unsigned int shader_program,
    const app::CommandPanelFrame& frame)
{
    if (frame.width <= 0.0F || frame.height <= 0.0F) {
        return;
    }

    draw_screen_quad(
        window_size,
        shader_program,
        frame.x,
        frame.y,
        frame.width,
        frame.height,
        constants::HUD_OPTIONS_FRAME_BORDER_R,
        constants::HUD_OPTIONS_FRAME_BORDER_G,
        constants::HUD_OPTIONS_FRAME_BORDER_B);
    draw_screen_quad(
        window_size,
        shader_program,
        frame.x + 1.0F,
        frame.y + 1.0F,
        frame.width - 2.0F,
        frame.height - 2.0F,
        constants::HUD_OPTIONS_FRAME_R,
        constants::HUD_OPTIONS_FRAME_G,
        constants::HUD_OPTIONS_FRAME_B);
}

void submit_hud_textured_quad(
    const std::array<float, 16>& vertices,
    const unsigned int shader_program,
    const unsigned int texture_id,
    const float tint_a = 1.0F)
{
    hud_textured_batch.push_back(HudTexturedQuad{
        .shader_program = shader_program,
        .texture_id = texture_id,
        .tint_a = tint_a,
        .vertices = vertices,
    });
    if (hud_color_batch_defer) {
        return;
    }

    flush_hud_color_batch();
    flush_hud_textured_batch();
}

void draw_textured_screen_quad(
    const sf::Vector2u window_size,
    const unsigned int shader_program,
    const unsigned int texture_id,
    const float x,
    const float y,
    const float width,
    const float height)
{
    if (window_size.x == 0U || window_size.y == 0U || width <= 0.0F || height <= 0.0F
        || texture_id == 0U) {
        return;
    }

    const float window_width = static_cast<float>(window_size.x);
    const float window_height = static_cast<float>(window_size.y);
    const auto to_ndc = [&](const float px, const float py) {
        return std::array<float, 2>{
            (px / window_width) * 2.0F - 1.0F,
            1.0F - (py / window_height) * 2.0F,
        };
    };

    const auto top_left = to_ndc(x, y);
    const auto top_right = to_ndc(x + width, y);
    const auto bottom_right = to_ndc(x + width, y + height);
    const auto bottom_left = to_ndc(x, y + height);
    const std::array<float, 16> vertices = {
        top_left[0],
        top_left[1],
        0.0F,
        0.0F,
        top_right[0],
        top_right[1],
        1.0F,
        0.0F,
        bottom_right[0],
        bottom_right[1],
        1.0F,
        1.0F,
        bottom_left[0],
        bottom_left[1],
        0.0F,
        1.0F,
    };

    submit_hud_textured_quad(vertices, shader_program, texture_id);
}

void draw_screen_line(
    const sf::Vector2u window_size,
    const unsigned int shader_program,
    const float x0,
    const float y0,
    const float x1,
    const float y1,
    const float r,
    const float g,
    const float b)
{
    if (window_size.x == 0U || window_size.y == 0U) {
        return;
    }

    const float window_width = static_cast<float>(window_size.x);
    const float window_height = static_cast<float>(window_size.y);
    const auto to_ndc = [&](const float px, const float py) {
        return std::array<float, 2>{
            (px / window_width) * 2.0F - 1.0F,
            1.0F - (py / window_height) * 2.0F,
        };
    };

    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0F) {
        return;
    }

    const float half = constants::HUD_LINE_THICKNESS_PX * 0.5F;
    const float nx = (-dy / length) * half;
    const float ny = (dx / length) * half;
    const auto p0 = to_ndc(x0 + nx, y0 + ny);
    const auto p1 = to_ndc(x1 + nx, y1 + ny);
    const auto p2 = to_ndc(x1 - nx, y1 - ny);
    const auto p3 = to_ndc(x0 - nx, y0 - ny);
    hud_color_batch_shader = shader_program;
    const auto push_vertex = [&](const std::array<float, 2>& point) {
        hud_overlay_color_batch.push_back(point[0]);
        hud_overlay_color_batch.push_back(point[1]);
        hud_overlay_color_batch.push_back(r);
        hud_overlay_color_batch.push_back(g);
        hud_overlay_color_batch.push_back(b);
        hud_overlay_color_batch.push_back(1.0F);
    };
    push_vertex(p0);
    push_vertex(p1);
    push_vertex(p2);
    push_vertex(p0);
    push_vertex(p2);
    push_vertex(p3);
    if (!hud_color_batch_defer) {
        flush_hud_overlay_color_batch();
    }
}

void draw_screen_poly_quad(
    const sf::Vector2u window_size,
    const unsigned int shader_program,
    const sf::Vector2f& p0,
    const sf::Vector2f& p1,
    const sf::Vector2f& p2,
    const sf::Vector2f& p3,
    const float r,
    const float g,
    const float b)
{
    if (window_size.x == 0U || window_size.y == 0U || hud_active_color_batch == nullptr) {
        return;
    }

    const float window_width = static_cast<float>(window_size.x);
    const float window_height = static_cast<float>(window_size.y);
    const auto to_ndc = [&](const sf::Vector2f& point) {
        return std::array<float, 2>{
            (point.x / window_width) * 2.0F - 1.0F,
            1.0F - (point.y / window_height) * 2.0F,
        };
    };

    hud_color_batch_shader = shader_program;
    const auto push_vertex = [&](const std::array<float, 2>& point) {
        hud_active_color_batch->push_back(point[0]);
        hud_active_color_batch->push_back(point[1]);
        hud_active_color_batch->push_back(r);
        hud_active_color_batch->push_back(g);
        hud_active_color_batch->push_back(b);
        hud_active_color_batch->push_back(1.0F);
    };
    const auto n0 = to_ndc(p0);
    const auto n1 = to_ndc(p1);
    const auto n2 = to_ndc(p2);
    const auto n3 = to_ndc(p3);
    push_vertex(n0);
    push_vertex(n1);
    push_vertex(n2);
    push_vertex(n0);
    push_vertex(n2);
    push_vertex(n3);
}

void draw_screen_rect_border(
    const sf::Vector2u window_size,
    const unsigned int shader_program,
    const float x,
    const float y,
    const float width,
    const float height,
    const float line_px,
    const float r,
    const float g,
    const float b)
{
    if (width <= 0.0F || height <= 0.0F || line_px <= 0.0F) {
        return;
    }

    draw_screen_quad(window_size, shader_program, x, y, width, line_px, r, g, b);
    draw_screen_quad(
        window_size,
        shader_program,
        x,
        y + height - line_px,
        width,
        line_px,
        r,
        g,
        b);
    draw_screen_quad(window_size, shader_program, x, y, line_px, height, r, g, b);
    draw_screen_quad(
        window_size,
        shader_program,
        x + width - line_px,
        y,
        line_px,
        height,
        r,
        g,
        b);
}

[[nodiscard]] std::array<float, 3> minimap_tile_color(
    const SimRenderSnapshot& snapshot,
    const int x,
    const int y)
{
    const core::GridPos cell{x, y};
    if (snapshot_cell_is_unexplored(snapshot, cell)) {
        return {
            constants::MINIMAP_FOG_UNEXPLORED_R,
            constants::MINIMAP_FOG_UNEXPLORED_G,
            constants::MINIMAP_FOG_UNEXPLORED_B,
        };
    }

    const std::size_t index = static_cast<std::size_t>(y * snapshot.map_width + x);
    const bool visible = snapshot_cell_is_visible(snapshot, cell);
    const auto dim_if_explored = [visible](const std::array<float, 3>& rgb) {
        if (visible) {
            return rgb;
        }

        return std::array<float, 3>{
            rgb[0] * constants::MINIMAP_FOG_EXPLORED_DIM,
            rgb[1] * constants::MINIMAP_FOG_EXPLORED_DIM,
            rgb[2] * constants::MINIMAP_FOG_EXPLORED_DIM,
        };
    };

    sim::components::TileType tile = sim::components::TileType::Grass;
    int forest_wood = 0;
    int bush_food = 0;
    int mine_money = 0;
    if (visible) {
        if (index < snapshot.tiles.size()) {
            tile = snapshot.tiles[index];
        }
        if (index < snapshot.forest_wood.size()) {
            forest_wood = snapshot.forest_wood[index];
        }
        if (index < snapshot.bush_food.size()) {
            bush_food = snapshot.bush_food[index];
        }
        if (index < snapshot.mine_money.size()) {
            mine_money = snapshot.mine_money[index];
        }
    }
    else {
        if (index < snapshot.fog_memory_tiles.size()) {
            tile = static_cast<sim::components::TileType>(snapshot.fog_memory_tiles[index]);
        }
        else if (index < snapshot.tiles.size()) {
            tile = snapshot.tiles[index];
        }
        if (index < snapshot.fog_memory_forest_wood.size()) {
            forest_wood = snapshot.fog_memory_forest_wood[index];
        }
        if (index < snapshot.fog_memory_bush_food.size()) {
            bush_food = snapshot.fog_memory_bush_food[index];
        }
        if (index < snapshot.fog_memory_mine_money.size()) {
            mine_money = snapshot.fog_memory_mine_money[index];
        }
    }

    if (tile == sim::components::TileType::Forest && forest_wood > 0) {
        return dim_if_explored({
            constants::MINIMAP_FOG_VISIBLE_FOREST_R,
            constants::MINIMAP_FOG_VISIBLE_FOREST_G,
            constants::MINIMAP_FOG_VISIBLE_FOREST_B,
        });
    }

    if (tile == sim::components::TileType::GoldMine && mine_money > 0) {
        return dim_if_explored({
            constants::MINIMAP_FOG_VISIBLE_GOLD_R,
            constants::MINIMAP_FOG_VISIBLE_GOLD_G,
            constants::MINIMAP_FOG_VISIBLE_GOLD_B,
        });
    }

    if ((tile == sim::components::TileType::Berries
            || tile == sim::components::TileType::Blueberries)
        && bush_food > 0) {
        return dim_if_explored({
            constants::MINIMAP_FOG_VISIBLE_BERRY_R,
            constants::MINIMAP_FOG_VISIBLE_BERRY_G,
            constants::MINIMAP_FOG_VISIBLE_BERRY_B,
        });
    }

    if (index < snapshot.ground.size()
        && snapshot.ground[index] == sim::components::GroundType::Snow) {
        return dim_if_explored({
            constants::MINIMAP_FOG_VISIBLE_SNOW_R,
            constants::MINIMAP_FOG_VISIBLE_SNOW_G,
            constants::MINIMAP_FOG_VISIBLE_SNOW_B,
        });
    }

    if (index < snapshot.ground.size()
        && snapshot.ground[index] == sim::components::GroundType::Sand) {
        return dim_if_explored({
            constants::MINIMAP_FOG_VISIBLE_SAND_R,
            constants::MINIMAP_FOG_VISIBLE_SAND_G,
            constants::MINIMAP_FOG_VISIBLE_SAND_B,
        });
    }

    return dim_if_explored({
        constants::MINIMAP_FOG_VISIBLE_GRASS_R,
        constants::MINIMAP_FOG_VISIBLE_GRASS_G,
        constants::MINIMAP_FOG_VISIBLE_GRASS_B,
    });
}

void draw_minimap_contents(
    const sf::Vector2u window_size,
    const SimRenderSnapshot& snapshot,
    const std::uint8_t local_player_slot,
    const HudUnitContext& unit_context,
    const unsigned int color_shader,
    const unsigned int textured_shader,
    unsigned int& minimap_texture,
    int& minimap_texture_width,
    int& minimap_texture_height)
{
    if (snapshot.map_width <= 0 || snapshot.map_height <= 0) {
        return;
    }

    const app::CommandPanelFrame content = app::minimap_content_rect(window_size);
    if (content.width <= 0.0F || content.height <= 0.0F) {
        return;
    }

    const int map_width = snapshot.map_width;
    const int map_height = snapshot.map_height;
    const MinimapIsoLayout layout = make_minimap_iso_layout(content, map_width, map_height);
    if (layout.scale <= 0.0F) {
        return;
    }

    const float used = layout.iso_span * layout.scale;
    const int tex_w = std::max(1, static_cast<int>(std::ceil(used)));
    const int tex_h = tex_w;
    const bool rebuild_minimap = minimap_texture == 0U
        || minimap_texture_width != tex_w
        || minimap_texture_height != tex_h
        || hud_minimap_cached_tick != snapshot.tick_count;
    if (rebuild_minimap) {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(tex_w * tex_h * 4), 0U);
        const float inv_scale = 1.0F / layout.scale;
        for (int py = 0; py < tex_h; ++py) {
            for (int px = 0; px < tex_w; ++px) {
                const float screen_x = layout.offset_x + (static_cast<float>(px) + 0.5F);
                const float screen_y = layout.offset_y + (static_cast<float>(py) + 0.5F);
                const float iso_u = (screen_x - layout.offset_x) * inv_scale + layout.min_iso_u;
                const float iso_v = (screen_y - layout.offset_y) * inv_scale + layout.min_iso_v;
                const float world_x = (iso_u + iso_v) * 0.5F;
                const float world_z = (iso_v - iso_u) * 0.5F;
                const std::size_t pixel_index = static_cast<std::size_t>((py * tex_w + px) * 4);
                if (world_x < 0.0F || world_z < 0.0F || world_x >= static_cast<float>(map_width)
                    || world_z >= static_cast<float>(map_height)) {
                    pixels[pixel_index + 3U] = 0U;
                    continue;
                }

                const int tile_x = static_cast<int>(std::floor(world_x));
                const int tile_y = static_cast<int>(std::floor(world_z));
                const auto color = minimap_tile_color(snapshot, tile_x, tile_y);
                pixels[pixel_index + 0U] =
                    static_cast<std::uint8_t>(std::clamp(color[0], 0.0F, 1.0F) * 255.0F);
                pixels[pixel_index + 1U] =
                    static_cast<std::uint8_t>(std::clamp(color[1], 0.0F, 1.0F) * 255.0F);
                pixels[pixel_index + 2U] =
                    static_cast<std::uint8_t>(std::clamp(color[2], 0.0F, 1.0F) * 255.0F);
                pixels[pixel_index + 3U] = 255U;
            }
        }

        if (minimap_texture == 0U) {
            glGenTextures(1, &minimap_texture);
        }

        GLint previous_unpack_alignment = 4;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_unpack_alignment);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_2D, minimap_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (minimap_texture_width != tex_w || minimap_texture_height != tex_h) {
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA8,
                tex_w,
                tex_h,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels.data());
            minimap_texture_width = tex_w;
            minimap_texture_height = tex_h;
        }
        else {
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                tex_w,
                tex_h,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels.data());
        }
        glBindTexture(GL_TEXTURE_2D, 0U);
        glPixelStorei(GL_UNPACK_ALIGNMENT, previous_unpack_alignment);
        hud_minimap_cached_tick = snapshot.tick_count;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    draw_textured_screen_quad(
        window_size,
        textured_shader,
        minimap_texture,
        layout.offset_x,
        layout.offset_y,
        used,
        used);

    std::vector<float>* const previous_color_target = hud_active_color_batch;
    hud_active_color_batch = &hud_overlay_color_batch;

    const auto draw_marker = [&](
                                 const float world_x,
                                 const float world_z,
                                 const int footprint_w,
                                 const int footprint_h,
                                 const std::uint8_t player_slot,
                                 const bool is_building) {
        const std::uint8_t color_index = hud_color_index_for_slot(player_slot);
        if (color_index >= constants::PLAYER_SLOT_COLOR_RGB.size()) {
            return;
        }

        const auto& rgb = constants::PLAYER_SLOT_COLOR_RGB[color_index];
        const float marker_w = std::max(
            static_cast<float>(
                is_building ? constants::MINIMAP_BUILDING_MARKER_MIN_PX
                            : constants::MINIMAP_UNIT_MARKER_MIN_PX),
            static_cast<float>(std::max(1, footprint_w)) * layout.scale * 0.7F);
        const float marker_h = std::max(
            static_cast<float>(
                is_building ? constants::MINIMAP_BUILDING_MARKER_MIN_PX
                            : constants::MINIMAP_UNIT_MARKER_MIN_PX),
            static_cast<float>(std::max(1, footprint_h)) * layout.scale * 0.7F);
        const sf::Vector2f center = minimap_world_to_screen(layout, world_x, world_z);
        draw_screen_quad(
            window_size,
            color_shader,
            center.x - marker_w * 0.5F,
            center.y - marker_h * 0.5F,
            marker_w,
            marker_h,
            rgb[0],
            rgb[1],
            rgb[2]);
    };

    const auto should_draw_pose = [&](const RenderEntityPose& pose) {
        if (pose.health_current <= 0) {
            return false;
        }

        const core::GridPos cell{pose.grid_x, pose.grid_y};
        if (snapshot_cell_is_unexplored(snapshot, cell)) {
            return false;
        }

        if (pose.is_nature || pose.is_mana_lake) {
            return true;
        }

        if (pose.player_slot == local_player_slot) {
            return true;
        }

        return snapshot_cell_is_visible(snapshot, cell) && !pose.shrouded;
    };

    for (const RenderEntityPose& building : snapshot.buildings) {
        if (!should_draw_pose(building)) {
            continue;
        }

        draw_marker(
            static_cast<float>(building.grid_x)
                + static_cast<float>(building.footprint_width) * 0.5F,
            static_cast<float>(building.grid_y)
                + static_cast<float>(building.footprint_height) * 0.5F,
            building.footprint_width,
            building.footprint_height,
            building.player_slot,
            true);
    }

    for (const RenderEntityPose& unit : snapshot.units) {
        if (!should_draw_pose(unit)) {
            continue;
        }

        draw_marker(unit.cur_x, unit.cur_y, 1, 1, unit.player_slot, false);
    }

    const sf::Vector2f c00 = minimap_world_to_screen(layout, 0.0F, 0.0F);
    const sf::Vector2f c10 =
        minimap_world_to_screen(layout, static_cast<float>(map_width), 0.0F);
    const sf::Vector2f c11 = minimap_world_to_screen(
        layout, static_cast<float>(map_width), static_cast<float>(map_height));
    const sf::Vector2f c01 =
        minimap_world_to_screen(layout, 0.0F, static_cast<float>(map_height));
    draw_screen_line(
        window_size,
        color_shader,
        c00.x,
        c00.y,
        c10.x,
        c10.y,
        constants::HUD_OPTIONS_FRAME_BORDER_R,
        constants::HUD_OPTIONS_FRAME_BORDER_G,
        constants::HUD_OPTIONS_FRAME_BORDER_B);
    draw_screen_line(
        window_size,
        color_shader,
        c10.x,
        c10.y,
        c11.x,
        c11.y,
        constants::HUD_OPTIONS_FRAME_BORDER_R,
        constants::HUD_OPTIONS_FRAME_BORDER_G,
        constants::HUD_OPTIONS_FRAME_BORDER_B);
    draw_screen_line(
        window_size,
        color_shader,
        c11.x,
        c11.y,
        c01.x,
        c01.y,
        constants::HUD_OPTIONS_FRAME_BORDER_R,
        constants::HUD_OPTIONS_FRAME_BORDER_G,
        constants::HUD_OPTIONS_FRAME_BORDER_B);
    draw_screen_line(
        window_size,
        color_shader,
        c01.x,
        c01.y,
        c00.x,
        c00.y,
        constants::HUD_OPTIONS_FRAME_BORDER_R,
        constants::HUD_OPTIONS_FRAME_BORDER_G,
        constants::HUD_OPTIONS_FRAME_BORDER_B);

    if (unit_context.has_camera_view) {
        const float min_x =
            std::clamp(unit_context.camera_world_min_x, 0.0F, static_cast<float>(map_width));
        const float max_x =
            std::clamp(unit_context.camera_world_max_x, 0.0F, static_cast<float>(map_width));
        const float min_z =
            std::clamp(unit_context.camera_world_min_z, 0.0F, static_cast<float>(map_height));
        const float max_z =
            std::clamp(unit_context.camera_world_max_z, 0.0F, static_cast<float>(map_height));
        if (max_x > min_x && max_z > min_z) {
            const sf::Vector2f cam00 = minimap_world_to_screen(layout, min_x, min_z);
            const sf::Vector2f cam10 = minimap_world_to_screen(layout, max_x, min_z);
            const sf::Vector2f cam11 = minimap_world_to_screen(layout, max_x, max_z);
            const sf::Vector2f cam01 = minimap_world_to_screen(layout, min_x, max_z);
            draw_screen_line(
                window_size,
                color_shader,
                cam00.x,
                cam00.y,
                cam10.x,
                cam10.y,
                constants::MINIMAP_CAMERA_BOX_R,
                constants::MINIMAP_CAMERA_BOX_G,
                constants::MINIMAP_CAMERA_BOX_B);
            draw_screen_line(
                window_size,
                color_shader,
                cam10.x,
                cam10.y,
                cam11.x,
                cam11.y,
                constants::MINIMAP_CAMERA_BOX_R,
                constants::MINIMAP_CAMERA_BOX_G,
                constants::MINIMAP_CAMERA_BOX_B);
            draw_screen_line(
                window_size,
                color_shader,
                cam11.x,
                cam11.y,
                cam01.x,
                cam01.y,
                constants::MINIMAP_CAMERA_BOX_R,
                constants::MINIMAP_CAMERA_BOX_G,
                constants::MINIMAP_CAMERA_BOX_B);
            draw_screen_line(
                window_size,
                color_shader,
                cam01.x,
                cam01.y,
                cam00.x,
                cam00.y,
                constants::MINIMAP_CAMERA_BOX_R,
                constants::MINIMAP_CAMERA_BOX_G,
                constants::MINIMAP_CAMERA_BOX_B);
        }
    }

    hud_active_color_batch = previous_color_target;
}

} // namespace

void enable_rgb_blend_keep_framebuffer_opaque()
{
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
}

void clear_opaque_framebuffer(const float r, const float g, const float b, const float a)
{
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

unsigned int HudOverlay::hud_shader_program() const
{
    if (hud_shader_program_ != 0U) {
        return hud_shader_program_;
    }

    const unsigned int vertex_shader = compile_shader(GL_VERTEX_SHADER, HUD_VERTEX_SHADER);
    const unsigned int fragment_shader = compile_shader(GL_FRAGMENT_SHADER, HUD_FRAGMENT_SHADER);
    hud_shader_program_ = link_program(vertex_shader, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return hud_shader_program_;
}

unsigned int HudOverlay::hud_textured_shader_program() const
{
    if (hud_textured_shader_program_ != 0U) {
        return hud_textured_shader_program_;
    }

    const unsigned int vertex_shader =
        compile_shader(GL_VERTEX_SHADER, HUD_TEXTURED_VERTEX_SHADER);
    const unsigned int fragment_shader =
        compile_shader(GL_FRAGMENT_SHADER, HUD_TEXTURED_FRAGMENT_SHADER);
    hud_textured_shader_program_ = link_program(vertex_shader, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return hud_textured_shader_program_;
}

unsigned int HudOverlay::hud_tinted_texture_shader_program() const
{
    if (hud_tinted_texture_shader_program_ != 0U) {
        return hud_tinted_texture_shader_program_;
    }

    const unsigned int vertex_shader =
        compile_shader(GL_VERTEX_SHADER, HUD_TEXTURED_VERTEX_SHADER);
    const unsigned int fragment_shader =
        compile_shader(GL_FRAGMENT_SHADER, HUD_TINTED_TEXTURE_FRAGMENT_SHADER);
    hud_tinted_texture_shader_program_ = link_program(vertex_shader, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return hud_tinted_texture_shader_program_;
}

float HudOverlay::text_width_px(const std::size_t character_count, const int pixel_scale)
{
    if (character_count == 0U) {
        return 0.0F;
    }

    const float scale = static_cast<float>(pixel_scale);
    const float char_step = static_cast<float>(GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * scale;
    return static_cast<float>(character_count) * char_step
        - static_cast<float>(constants::HUD_CHAR_SPACING) * scale;
}

float HudOverlay::text_height_px(const int pixel_scale)
{
    return static_cast<float>(GLYPH_HEIGHT * pixel_scale);
}

void HudOverlay::draw_text(
    const sf::Vector2u window_size,
    const float x,
    const float y,
    const std::string& text,
    const int pixel_scale,
    const float r,
    const float g,
    const float b) const
{
    draw_string_scaled(window_size, x, y, text, pixel_scale, r, g, b);
}

void HudOverlay::draw_rect(
    const sf::Vector2u window_size,
    const float x,
    const float y,
    const float width,
    const float height,
    const float r,
    const float g,
    const float b,
    const float a) const
{
    draw_screen_quad(window_size, hud_shader_program(), x, y, width, height, r, g, b, a);
    if (!hud_color_batch_defer) {
        flush_hud_color_batch();
    }
}

void HudOverlay::draw_line(
    const sf::Vector2u window_size,
    const float x0,
    const float y0,
    const float x1,
    const float y1,
    const float r,
    const float g,
    const float b) const
{
    draw_screen_line(window_size, hud_shader_program(), x0, y0, x1, y1, r, g, b);
}

void HudOverlay::draw_quad(
    const sf::Vector2u window_size,
    const sf::Vector2f& p0,
    const sf::Vector2f& p1,
    const sf::Vector2f& p2,
    const sf::Vector2f& p3,
    const float r,
    const float g,
    const float b) const
{
    draw_screen_poly_quad(window_size, hud_shader_program(), p0, p1, p2, p3, r, g, b);
    if (!hud_color_batch_defer) {
        flush_hud_color_batch();
    }
}

void HudOverlay::begin_batch() const
{
    hud_color_batch_defer = true;
}

void HudOverlay::end_batch() const
{
    flush_hud_color_batch();
    flush_hud_overlay_color_batch();
    hud_color_batch_defer = false;
}

void HudOverlay::draw_cover_texture(
    const sf::Vector2u window_size,
    const unsigned int texture_id,
    const float texture_aspect_ratio,
    const float alpha) const
{
    if (window_size.x == 0U || window_size.y == 0U || texture_id == 0U || alpha <= 0.0F
        || texture_aspect_ratio <= 0.0F) {
        return;
    }

    const float window_width = static_cast<float>(window_size.x);
    const float window_height = static_cast<float>(window_size.y);
    const float window_aspect_ratio = window_height / window_width;

    float draw_width = window_width;
    float draw_height = window_height;
    if (texture_aspect_ratio > window_aspect_ratio) {
        draw_height = window_width * texture_aspect_ratio;
    }
    else {
        draw_width = window_height / texture_aspect_ratio;
    }

    const float draw_x = (window_width - draw_width) * 0.5F;
    const float draw_y = (window_height - draw_height) * 0.5F;

    const auto to_ndc = [&](const float px, const float py) {
        return std::array<float, 2>{
            (px / window_width) * 2.0F - 1.0F,
            1.0F - (py / window_height) * 2.0F,
        };
    };

    const auto top_left = to_ndc(draw_x, draw_y);
    const auto top_right = to_ndc(draw_x + draw_width, draw_y);
    const auto bottom_right = to_ndc(draw_x + draw_width, draw_y + draw_height);
    const auto bottom_left = to_ndc(draw_x, draw_y + draw_height);
    const std::array<float, 16> vertices = {
        top_left[0], top_left[1], 0.0F, 0.0F,
        top_right[0], top_right[1], 1.0F, 0.0F,
        bottom_right[0], bottom_right[1], 1.0F, 1.0F,
        bottom_left[0], bottom_left[1], 0.0F, 1.0F,
    };

    submit_hud_textured_quad(
        vertices, hud_tinted_texture_shader_program(), texture_id, alpha);
}

HudIconAtlas& HudOverlay::icon_atlas() const
{
    if (!icon_atlas_load_attempted_) {
        icon_atlas_load_attempted_ = true;
        (void)icon_atlas_.load(core::default_assets_directory());
    }

    return icon_atlas_;
}

void HudOverlay::invalidate_gl_cache()
{
    if (hud_shader_program_ != 0U) {
        glDeleteProgram(hud_shader_program_);
        hud_shader_program_ = 0U;
    }
    if (hud_textured_shader_program_ != 0U) {
        glDeleteProgram(hud_textured_shader_program_);
        hud_textured_shader_program_ = 0U;
    }
    if (hud_tinted_texture_shader_program_ != 0U) {
        glDeleteProgram(hud_tinted_texture_shader_program_);
        hud_tinted_texture_shader_program_ = 0U;
    }
    if (minimap_texture_ != 0U) {
        glDeleteTextures(1, &minimap_texture_);
        minimap_texture_ = 0U;
        minimap_texture_width_ = 0;
        minimap_texture_height_ = 0;
    }
    icon_atlas_.destroy_gl_resources();
    icon_atlas_load_attempted_ = false;
    destroy_hud_draw_geometry();
}

void HudOverlay::draw_icon(
    const sf::Vector2u window_size,
    const float x,
    const float y,
    const float size,
    const HudIcon icon) const
{
    if (window_size.x == 0U || window_size.y == 0U || size <= 0.0F) {
        return;
    }

    HudIconAtlas& atlas = icon_atlas();
    if (!atlas.ready()) {
        return;
    }

    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;
    atlas.icon_uv(icon, u0, v0, u1, v1);

    const float window_width = static_cast<float>(window_size.x);
    const float window_height = static_cast<float>(window_size.y);
    const auto to_ndc = [&](const float px, const float py) {
        const float ndc_x = (px / window_width) * 2.0F - 1.0F;
        const float ndc_y = 1.0F - (py / window_height) * 2.0F;
        return std::array<float, 2>{ndc_x, ndc_y};
    };

    const auto top_left = to_ndc(x, y);
    const auto top_right = to_ndc(x + size, y);
    const auto bottom_right = to_ndc(x + size, y + size);
    const auto bottom_left = to_ndc(x, y + size);

    // pos.xy, uv.xy per vertex
    const std::array<float, 16> vertices = {
        top_left[0], top_left[1], u0, v0,
        top_right[0], top_right[1], u1, v0,
        bottom_right[0], bottom_right[1], u1, v1,
        bottom_left[0], bottom_left[1], u0, v1,
    };

    submit_hud_textured_quad(vertices, hud_textured_shader_program(), atlas.texture_id());
}

void HudOverlay::draw_resource_bar(
    const sf::Vector2u window_size,
    const float x,
    const float y,
    const int wood,
    const int food,
    const int money,
    const int mana,
    const int mana_max,
    const int cap_current,
    const int cap_max) const
{
    const float icon_size = static_cast<float>(constants::HUD_ICON_DRAW_SIZE_PX);
    const float gap = static_cast<float>(constants::HUD_ICON_TEXT_GAP_PX);
    const float group_gap = icon_size;
    const float char_step = static_cast<float>(
        (GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * constants::HUD_PIXEL_SCALE);
    const float text_y = y
        + (icon_size - static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE)) * 0.5F;

    float cursor_x = x;
    const auto draw_item = [&](const HudIcon icon, const std::string& value) {
        draw_icon(window_size, cursor_x, y, icon_size, icon);
        cursor_x += icon_size + gap;
        draw_string(
            window_size,
            cursor_x,
            text_y,
            value,
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);
        cursor_x += static_cast<float>(value.size()) * char_step + group_gap;
    };

    draw_item(HudIcon::Wood, std::to_string(wood));
    draw_item(HudIcon::Food, std::to_string(food));
    draw_item(HudIcon::Money, std::to_string(money));
    draw_item(HudIcon::Mana, std::to_string(mana) + "/" + std::to_string(mana_max));
    draw_item(HudIcon::GreenHat, std::to_string(cap_current) + "/" + std::to_string(cap_max));
}

void HudOverlay::draw_string(
    const sf::Vector2u window_size,
    const float x,
    const float y,
    const std::string& text,
    const float r,
    const float g,
    const float b) const
{
    draw_string_scaled(window_size, x, y, text, constants::HUD_PIXEL_SCALE, r, g, b);
}

void HudOverlay::draw_minimap(
    const sf::Vector2u window_size,
    const SimRenderSnapshot& snapshot,
    const std::uint8_t local_player_slot,
    const HudUnitContext& unit_context) const
{
    draw_minimap_contents(
        window_size,
        snapshot,
        local_player_slot,
        unit_context,
        hud_shader_program(),
        hud_textured_shader_program(),
        minimap_texture_,
        minimap_texture_width_,
        minimap_texture_height_);
}

void HudOverlay::draw_command_panel(
    const sf::Vector2u window_size,
    const app::CommandPanelMode mode,
    const app::CommandPanelBuildOptions& build_options,
    const sf::Vector2f mouse_screen_position,
    const int pressed_slot) const
{
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    const unsigned int shader_program = hud_shader_program();
    const app::CommandPanelFrame frame = app::command_panel_frame_rect(window_size);
    draw_panel_frame(window_size, shader_program, frame);
    draw_panel_frame(window_size, shader_program, app::minimap_panel_frame_rect(window_size));
    draw_panel_frame(window_size, shader_program, app::status_panel_frame_rect(window_size));

    const bool mouse_down = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    std::string hover_name_label{};
    std::vector<std::pair<int, HudIcon>> hover_cost_parts{};
    for (const app::CommandPanelButton& button :
         app::build_command_panel_buttons(mode, window_size, build_options)) {
        const bool hovered = mouse_screen_position.x >= button.x
            && mouse_screen_position.x <= button.x + button.width
            && mouse_screen_position.y >= button.y
            && mouse_screen_position.y <= button.y + button.height;
        if (hovered) {
            if (button.action == app::CommandPanelAction::SpawnWorker) {
                hover_name_label = "Worker";
            }
            else if (button.action == app::CommandPanelAction::SpawnMilitia) {
                hover_name_label = "Militia";
            }
            else if (button.action == app::CommandPanelAction::BuildHouse) {
                hover_name_label = "House";
            }
            else if (button.action == app::CommandPanelAction::BuildLumberjack) {
                hover_name_label = "Lumberjack";
            }
            else if (button.action == app::CommandPanelAction::BuildExtractor) {
                hover_name_label = "Extractor";
            }
            else if (button.action == app::CommandPanelAction::BuildTownCenter) {
                hover_name_label = "Town Center";
            }

            hover_cost_parts.clear();
            if (button.cost_food > 0) {
                hover_cost_parts.emplace_back(button.cost_food, HudIcon::Food);
            }
            if (button.cost_wood > 0) {
                hover_cost_parts.emplace_back(button.cost_wood, HudIcon::Wood);
            }
            if (button.cost_money > 0) {
                hover_cost_parts.emplace_back(button.cost_money, HudIcon::Money);
            }
        }

        const bool pressed = !button.disabled
            && ((hovered && mouse_down) || (pressed_slot >= 0 && button.slot == pressed_slot));
        float button_r = button.disabled
            ? constants::HUD_UNAFFORDABLE_R
            : constants::HUD_OPTIONS_BUTTON_R;
        float button_g = button.disabled
            ? constants::HUD_UNAFFORDABLE_G
            : constants::HUD_OPTIONS_BUTTON_G;
        float button_b = button.disabled
            ? constants::HUD_UNAFFORDABLE_B
            : constants::HUD_OPTIONS_BUTTON_B;
        if (!button.disabled && hovered && !pressed) {
            button_r = std::min(1.0F, button_r * constants::HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN);
            button_g = std::min(1.0F, button_g * constants::HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN);
            button_b = std::min(1.0F, button_b * constants::HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN);
        }
        if (pressed) {
            button_r *= constants::HUD_OPTIONS_BUTTON_PRESS_DARKEN;
            button_g *= constants::HUD_OPTIONS_BUTTON_PRESS_DARKEN;
            button_b *= constants::HUD_OPTIONS_BUTTON_PRESS_DARKEN;
        }

        const float press_offset = pressed
            ? static_cast<float>(constants::HUD_OPTIONS_BUTTON_PRESS_OFFSET_PX)
            : 0.0F;
        const float draw_x = button.x + press_offset;
        const float draw_y = button.y + press_offset;
        const float draw_w = button.width - press_offset;
        const float draw_h = button.height - press_offset;

        draw_screen_quad(
            window_size,
            shader_program,
            draw_x,
            draw_y,
            draw_w,
            draw_h,
            button_r * 0.6F,
            button_g * 0.6F,
            button_b * 0.6F);
        draw_screen_quad(
            window_size,
            shader_program,
            draw_x + 1.0F,
            draw_y + 1.0F,
            draw_w - 2.0F,
            draw_h - 2.0F,
            button_r,
            button_g,
            button_b);

        const float inset = static_cast<float>(constants::HUD_ICON_OPTION_INSET_PX);
        const float icon_size = std::max(1.0F, std::min(draw_w, draw_h) - inset * 2.0F);
        const float icon_x = draw_x + (draw_w - icon_size) * 0.5F;
        const float icon_y = draw_y + (draw_h - icon_size) * 0.5F;
        draw_icon(
            window_size,
            icon_x,
            icon_y,
            icon_size,
            icon_for_command_action(button.action));
    }

    const float hover_text_height =
        static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE);
    const float hover_line_height =
        hover_text_height + static_cast<float>(constants::HUD_LINE_SPACING);
    const float cost_icon_size = static_cast<float>(constants::HUD_ICON_TILE_SIZE_PX);
    const float cost_line_height =
        std::max(hover_line_height, cost_icon_size + static_cast<float>(constants::HUD_LINE_SPACING));
    float hover_y = frame.y - cost_line_height;
    if (!hover_cost_parts.empty() || !hover_name_label.empty()) {
        const float char_step = static_cast<float>(
            (GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * constants::HUD_PIXEL_SCALE);
        const float icon_gap = static_cast<float>(constants::HUD_ICON_TEXT_GAP_PX);
        const float pad = static_cast<float>(constants::HUD_FLOATING_LABEL_PAD_PX);
        float backing_width = 0.0F;
        float backing_height = 0.0F;
        float backing_y = hover_y;
        if (!hover_cost_parts.empty()) {
            float cost_width = static_cast<float>(std::string("Cost: ").size()) * char_step;
            for (std::size_t index = 0; index < hover_cost_parts.size(); ++index) {
                if (index > 0U) {
                    cost_width += icon_gap;
                }
                cost_width += static_cast<float>(
                    std::to_string(hover_cost_parts[index].first).size()) * char_step
                    + icon_gap + cost_icon_size;
            }
            backing_width = std::max(backing_width, cost_width);
            backing_height += cost_line_height;
        }
        if (!hover_name_label.empty()) {
            backing_width = std::max(
                backing_width,
                static_cast<float>(hover_name_label.size()) * char_step);
            if (!hover_cost_parts.empty()) {
                backing_y -= hover_line_height;
                backing_height += hover_line_height;
            }
            else {
                backing_height += hover_text_height;
            }
        }
        draw_screen_quad(
            window_size,
            shader_program,
            frame.x - pad,
            backing_y - pad,
            backing_width + pad * 2.0F,
            backing_height + pad * 2.0F,
            constants::HUD_OPTIONS_FRAME_R,
            constants::HUD_OPTIONS_FRAME_G,
            constants::HUD_OPTIONS_FRAME_B);
    }
    if (!hover_cost_parts.empty()) {
        const float char_step = static_cast<float>(
            (GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * constants::HUD_PIXEL_SCALE);
        const float icon_gap = static_cast<float>(constants::HUD_ICON_TEXT_GAP_PX);
        const float text_y = hover_y + (cost_icon_size - hover_text_height) * 0.5F;
        float cursor_x = frame.x;
        const std::string cost_prefix = "Cost: ";
        draw_string(
            window_size,
            cursor_x,
            text_y,
            cost_prefix,
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);
        cursor_x += static_cast<float>(cost_prefix.size()) * char_step;
        for (std::size_t index = 0; index < hover_cost_parts.size(); ++index) {
            if (index > 0U) {
                cursor_x += icon_gap;
            }

            const std::string amount = std::to_string(hover_cost_parts[index].first);
            draw_string(
                window_size,
                cursor_x,
                text_y,
                amount,
                constants::HUD_TEXT_R,
                constants::HUD_TEXT_G,
                constants::HUD_TEXT_B);
            cursor_x += static_cast<float>(amount.size()) * char_step + icon_gap;
            draw_icon(
                window_size,
                cursor_x,
                hover_y,
                cost_icon_size,
                hover_cost_parts[index].second);
            cursor_x += cost_icon_size;
        }
        hover_y -= hover_line_height;
    }

    if (!hover_name_label.empty()) {
        draw_string(
            window_size,
            frame.x,
            hover_y,
            hover_name_label,
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);
    }
}

void HudOverlay::draw_info_panel(const sf::Vector2u window_size, const HudInfoPanel& panel) const
{
    if (!panel.active) {
        return;
    }

    const app::CommandPanelFrame frame = app::status_panel_frame_rect(window_size);
    if (frame.width <= 0.0F || frame.height <= 0.0F) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float line_height =
        static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE + constants::HUD_LINE_SPACING);
    const float icon_gap = static_cast<float>(constants::HUD_ICON_TEXT_GAP_PX);
    float text_y = frame.y + padding;
    const float text_x = frame.x + padding;
    const float bottom_limit = frame.y + frame.height - padding;

    const auto can_draw_line = [&](const float height) {
        return text_y + height <= bottom_limit;
    };

    if (can_draw_line(line_height)) {
        draw_string(
            window_size,
            text_x,
            text_y,
            panel.title,
            panel.title_r,
            panel.title_g,
            panel.title_b);
        text_y += line_height;
    }

    if (panel.has_health && can_draw_line(line_height)) {
        draw_string(
            window_size,
            text_x,
            text_y,
            "HP: " + std::to_string(panel.health_current) + "/"
                + std::to_string(panel.health_max),
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);
        text_y += line_height;
    }

    if (panel.carry_amount > 0) {
        const float carry_icon_size = static_cast<float>(constants::HUD_ICON_TILE_SIZE_PX);
        const float carry_line_height =
            std::max(line_height, carry_icon_size + static_cast<float>(constants::HUD_LINE_SPACING));
        if (can_draw_line(carry_line_height)) {
            const char* remaining_prefix = "Wood: ";
            if (panel.carry_icon == HudIcon::Food) {
                remaining_prefix = "Food: ";
            }
            else if (panel.carry_icon == HudIcon::Money) {
                remaining_prefix = "Gold: ";
            }
            const std::string carry_label =
                (panel.carry_is_remaining ? remaining_prefix : "Carry: ")
                + std::to_string(panel.carry_amount);
            const float char_step = static_cast<float>(
                (GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * constants::HUD_PIXEL_SCALE);
            const float label_width = static_cast<float>(carry_label.size()) * char_step;
            const float label_y =
                text_y
                + (carry_icon_size - static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE))
                    * 0.5F;
            draw_string(
                window_size,
                text_x,
                label_y,
                carry_label,
                constants::HUD_TEXT_R,
                constants::HUD_TEXT_G,
                constants::HUD_TEXT_B);
            draw_icon(
                window_size,
                text_x + label_width + icon_gap,
                text_y,
                carry_icon_size,
                panel.carry_icon);
            text_y += carry_line_height;
        }
    }

    if (panel.has_mana_rate) {
        const float rate_icon_size = static_cast<float>(constants::HUD_ICON_TILE_SIZE_PX);
        const float rate_line_height =
            std::max(line_height, rate_icon_size + static_cast<float>(constants::HUD_LINE_SPACING));
        if (can_draw_line(rate_line_height)) {
            const std::string rate_label = "Charge Rate: +" + std::to_string(panel.mana_rate);
            const float char_step = static_cast<float>(
                (GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * constants::HUD_PIXEL_SCALE);
            const float label_width = static_cast<float>(rate_label.size()) * char_step;
            const float label_y =
                text_y
                + (rate_icon_size - static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE))
                    * 0.5F;
            draw_string(
                window_size,
                text_x,
                label_y,
                rate_label,
                constants::HUD_TEXT_R,
                constants::HUD_TEXT_G,
                constants::HUD_TEXT_B);
            draw_icon(
                window_size,
                text_x + label_width + icon_gap,
                text_y,
                rate_icon_size,
                HudIcon::Mana);
            text_y += rate_line_height;
        }
    }

    if (panel.has_extraction_progress && can_draw_line(line_height)) {
        draw_string(
            window_size,
            text_x,
            text_y,
            "Extracting: " + std::to_string(panel.extraction_percent) + "%",
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);
        text_y += line_height;
    }

    if (panel.has_combat_stats) {
        if (can_draw_line(line_height)) {
            draw_string(
                window_size,
                text_x,
                text_y,
                "Melee Atk " + std::to_string(panel.melee_attack) + " Def "
                    + std::to_string(panel.melee_armor),
                constants::HUD_TEXT_R,
                constants::HUD_TEXT_G,
                constants::HUD_TEXT_B);
            text_y += line_height;
        }
        if (can_draw_line(line_height)) {
            draw_string(
                window_size,
                text_x,
                text_y,
                "Pierce Atk " + std::to_string(panel.pierce_attack) + " Def "
                    + std::to_string(panel.pierce_armor),
                constants::HUD_TEXT_R,
                constants::HUD_TEXT_G,
                constants::HUD_TEXT_B);
            text_y += line_height;
        }
    }

    for (const std::string& line : panel.debug_lines) {
        if (!can_draw_line(line_height)) {
            break;
        }

        draw_string(
            window_size,
            text_x,
            text_y,
            line,
            constants::HUD_TEXT_R * 0.85F,
            constants::HUD_TEXT_G * 0.95F,
            constants::HUD_TEXT_B * 0.75F);
        text_y += line_height;
    }
}

void HudOverlay::draw_chat(
    const sf::Vector2u window_size,
    const float top_y,
    const HudUnitContext& unit_context) const
{
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    const float x = constants::HUD_MARGIN_X;
    const float y = top_y + static_cast<float>(constants::CHAT_FRAME_MARGIN_TOP_PX);
    const float line_height =
        static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE + constants::HUD_LINE_SPACING);
    const float char_step = static_cast<float>(
        (GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * constants::HUD_PIXEL_SCALE);
    const float pad = static_cast<float>(constants::HUD_FLOATING_LABEL_PAD_PX);
    float longest_line_width = 0.0F;
    int visible_lines = 0;
    for (const app::ChatLine& line : unit_context.chat_lines) {
        if (visible_lines >= constants::CHAT_MAX_VISIBLE_LINES) {
            break;
        }
        const std::string text = line.system
            ? line.text
            : ("P" + std::to_string(static_cast<int>(line.player_slot) + 1) + ": " + line.text);
        longest_line_width = std::max(longest_line_width, static_cast<float>(text.size()) * char_step);
        ++visible_lines;
    }
    if (visible_lines > 0) {
        draw_screen_quad(
            window_size,
            hud_shader_program(),
            x - pad,
            y - pad,
            longest_line_width + pad * 2.0F,
            static_cast<float>(visible_lines) * line_height + pad * 2.0F,
            constants::HUD_OPTIONS_FRAME_R,
            constants::HUD_OPTIONS_FRAME_G,
            constants::HUD_OPTIONS_FRAME_B);
    }
    float text_y = y;
    int drawn_lines = 0;
    for (const app::ChatLine& line : unit_context.chat_lines) {
        if (drawn_lines >= constants::CHAT_MAX_VISIBLE_LINES) {
            break;
        }

        const std::string text = line.system
            ? line.text
            : ("P" + std::to_string(static_cast<int>(line.player_slot) + 1) + ": " + line.text);
        draw_string(
            window_size,
            x,
            text_y,
            text,
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);
        text_y += line_height;
        ++drawn_lines;
    }

    if (unit_context.chat_composing) {
        const float input_width =
            static_cast<float>(window_size.x) * constants::CHAT_INPUT_WIDTH_FRACTION;
        const float input_height = static_cast<float>(constants::CHAT_INPUT_HEIGHT_PX);
        const float input_x = (static_cast<float>(window_size.x) - input_width) * 0.5F;
        const float input_y = (static_cast<float>(window_size.y) - input_height) * 0.5F;
        const unsigned int shader_program = hud_shader_program();
        draw_panel_frame(
            window_size,
            shader_program,
            app::CommandPanelFrame{
                .x = input_x,
                .y = input_y,
                .width = input_width,
                .height = input_height,
            });

        std::string draft = unit_context.chat_draft;
        if (draft.empty()) {
            draft = "...";
        }
        else if (draft.size() > static_cast<std::size_t>(constants::CHAT_INPUT_VISIBLE_CHARS)) {
            draft = draft.substr(draft.size() - static_cast<std::size_t>(constants::CHAT_INPUT_VISIBLE_CHARS));
        }

        draw_string(
            window_size,
            input_x + 6.0F,
            input_y + 8.0F,
            draft,
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);
    }
}

void HudOverlay::draw_string_scaled(
    const sf::Vector2u window_size,
    const float x,
    const float y,
    const std::string& text,
    const int pixel_scale,
    const float r,
    const float g,
    const float b) const
{
    const unsigned int shader_program = hud_shader_program();
    const float scale = static_cast<float>(pixel_scale);
    const float char_step =
        static_cast<float>(GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * scale;

    float cursor_x = x;
    for (const char character : text) {
        const auto& rows = glyph_rows(character);
        for (int row = 0; row < GLYPH_HEIGHT; ++row) {
            for (int column = 0; column < GLYPH_WIDTH; ++column) {
                const int bit = 1 << (GLYPH_WIDTH - 1 - column);
                if ((rows[static_cast<std::size_t>(row)] & bit) == 0) {
                    continue;
                }

                const float pixel_x = cursor_x + static_cast<float>(column) * scale;
                const float pixel_y = y + static_cast<float>(row) * scale;
                draw_screen_quad(
                    window_size,
                    shader_program,
                    pixel_x,
                    pixel_y,
                    scale,
                    scale,
                    r,
                    g,
                    b);
            }
        }

        cursor_x += char_step;
    }

    if (!hud_color_batch_defer) {
        flush_hud_color_batch();
    }
}

void HudOverlay::draw_game_menu(
    const sf::Vector2u window_size,
    const HudUnitContext& unit_context) const
{
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    const unsigned int shader_program = hud_shader_program();
    const sf::Vector2f mouse = unit_context.mouse_screen_position;
    const bool mouse_down = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    const float char_step = static_cast<float>(
        (GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * constants::HUD_PIXEL_SCALE);
    const float text_height =
        static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE);

    const auto draw_labeled_button = [&](const app::GameMenuButton& button, const bool active_tab) {
        const bool hovered = button.rect.contains(mouse.x, mouse.y);
        const bool pressed = !button.disabled && hovered && mouse_down;
        float button_r = constants::HUD_OPTIONS_BUTTON_R;
        float button_g = constants::HUD_OPTIONS_BUTTON_G;
        float button_b = constants::HUD_OPTIONS_BUTTON_B;
        if (button.disabled) {
            button_r *= constants::HUD_MENU_DISABLED_DIM;
            button_g *= constants::HUD_MENU_DISABLED_DIM;
            button_b *= constants::HUD_MENU_DISABLED_DIM;
        }
        if (active_tab) {
            button_r = std::min(1.0F, button_r * constants::HUD_SETTINGS_ACTIVE_TAB_BRIGHTEN);
            button_g = std::min(1.0F, button_g * constants::HUD_SETTINGS_ACTIVE_TAB_BRIGHTEN);
            button_b = std::min(1.0F, button_b * constants::HUD_SETTINGS_ACTIVE_TAB_BRIGHTEN);
        }
        if (!button.disabled && hovered && !pressed) {
            button_r = std::min(1.0F, button_r * constants::HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN);
            button_g = std::min(1.0F, button_g * constants::HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN);
            button_b = std::min(1.0F, button_b * constants::HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN);
        }
        if (pressed) {
            button_r *= constants::HUD_OPTIONS_BUTTON_PRESS_DARKEN;
            button_g *= constants::HUD_OPTIONS_BUTTON_PRESS_DARKEN;
            button_b *= constants::HUD_OPTIONS_BUTTON_PRESS_DARKEN;
        }

        const float press_offset = pressed
            ? static_cast<float>(constants::HUD_OPTIONS_BUTTON_PRESS_OFFSET_PX)
            : 0.0F;
        const float draw_x = button.rect.x + press_offset;
        const float draw_y = button.rect.y + press_offset;
        const float draw_w = button.rect.width - press_offset;
        const float draw_h = button.rect.height - press_offset;

        draw_screen_quad(
            window_size,
            shader_program,
            draw_x,
            draw_y,
            draw_w,
            draw_h,
            button_r * 0.6F,
            button_g * 0.6F,
            button_b * 0.6F);
        draw_screen_quad(
            window_size,
            shader_program,
            draw_x + 1.0F,
            draw_y + 1.0F,
            draw_w - 2.0F,
            draw_h - 2.0F,
            button_r,
            button_g,
            button_b);

        const std::string label{button.label};
        const float text_width = static_cast<float>(label.size()) * char_step;
        const float text_x = draw_x + (draw_w - text_width) * 0.5F;
        const float text_y = draw_y + (draw_h - text_height) * 0.5F;
        const float text_r = button.disabled
            ? constants::HUD_TEXT_R * constants::HUD_MENU_DISABLED_DIM
            : constants::HUD_TEXT_R;
        const float text_g = button.disabled
            ? constants::HUD_TEXT_G * constants::HUD_MENU_DISABLED_DIM
            : constants::HUD_TEXT_G;
        const float text_b = button.disabled
            ? constants::HUD_TEXT_B * constants::HUD_MENU_DISABLED_DIM
            : constants::HUD_TEXT_B;
        draw_string(window_size, text_x, text_y, label, text_r, text_g, text_b);
    };

    const app::GameMenuButton menu_button{
        app::GameMenuAction::ToggleMenu,
        "Menu",
        app::menu_button_rect(window_size),
        false,
    };
    draw_labeled_button(menu_button, false);

    if (!unit_context.game_menu.is_open()) {
        return;
    }

    draw_screen_quad(
        window_size,
        shader_program,
        0.0F,
        0.0F,
        static_cast<float>(window_size.x),
        static_cast<float>(window_size.y),
        constants::HUD_MENU_SCRIM_R,
        constants::HUD_MENU_SCRIM_G,
        constants::HUD_MENU_SCRIM_B,
        constants::HUD_MENU_SCRIM_A);

    if (unit_context.game_menu.screen == app::GameMenuScreen::Main) {
        const app::GameMenuRect panel = app::game_menu_panel_rect(window_size);
        draw_panel_frame(
            window_size,
            shader_program,
            app::CommandPanelFrame{panel.x, panel.y, panel.width, panel.height});
        for (const app::GameMenuButton& button :
             app::build_main_menu_buttons(window_size, unit_context.game_menu.multiplayer)) {
            draw_labeled_button(button, false);
        }
        return;
    }

    if (unit_context.game_menu.is_save_load_screen()) {
        const app::GameMenuRect panel = app::save_load_panel_rect(window_size);
        draw_panel_frame(
            window_size,
            shader_program,
            app::CommandPanelFrame{panel.x, panel.y, panel.width, panel.height});

        const app::GameMenuRect filename = app::save_load_filename_rect(window_size);
        draw_screen_quad(
            window_size,
            shader_program,
            filename.x,
            filename.y,
            filename.width,
            filename.height,
            unit_context.game_menu.filename_focused
                ? constants::MAIN_MENU_FIELD_FOCUS_R
                : constants::MAIN_MENU_FIELD_BG_R,
            unit_context.game_menu.filename_focused
                ? constants::MAIN_MENU_FIELD_FOCUS_G
                : constants::MAIN_MENU_FIELD_BG_G,
            unit_context.game_menu.filename_focused
                ? constants::MAIN_MENU_FIELD_FOCUS_B
                : constants::MAIN_MENU_FIELD_BG_B);

        const float field_pad = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX) * 0.5F;
        const std::string filename_label = unit_context.game_menu.filename_draft.empty()
            ? "Filename"
            : unit_context.game_menu.filename_draft;
        draw_string(
            window_size,
            filename.x + field_pad,
            filename.y
                + (filename.height
                    - static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE))
                    * 0.5F,
            filename_label,
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);

        const app::GameMenuRect list = app::save_load_list_rect(window_size);
        draw_screen_quad(
            window_size,
            shader_program,
            list.x,
            list.y,
            list.width,
            list.height,
            constants::MAIN_MENU_FIELD_BG_R,
            constants::MAIN_MENU_FIELD_BG_G,
            constants::MAIN_MENU_FIELD_BG_B);

        const float row_h = constants::HUD_SAVE_LOAD_ROW_HEIGHT_PX;
        for (int visible = 0; visible < constants::HUD_SAVE_LOAD_VISIBLE_ROWS; ++visible) {
            const int index = unit_context.game_menu.save_list_scroll + visible;
            if (index < 0
                || index >= static_cast<int>(unit_context.game_menu.save_entries.size())) {
                break;
            }

            const float row_y = list.y + static_cast<float>(visible) * row_h;
            if (index == unit_context.game_menu.selected_save_index) {
                draw_screen_quad(
                    window_size,
                    shader_program,
                    list.x,
                    row_y,
                    list.width,
                    row_h,
                    constants::MAIN_MENU_FIELD_FOCUS_R,
                    constants::MAIN_MENU_FIELD_FOCUS_G,
                    constants::MAIN_MENU_FIELD_FOCUS_B);
            }

            draw_string(
                window_size,
                list.x + field_pad,
                row_y
                    + (row_h - static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE))
                        * 0.5F,
                unit_context.game_menu.save_entries[static_cast<std::size_t>(index)],
                constants::HUD_TEXT_R,
                constants::HUD_TEXT_G,
                constants::HUD_TEXT_B);
        }

        for (const app::GameMenuButton& button :
             app::build_save_load_buttons(unit_context.game_menu, window_size)) {
            draw_labeled_button(button, false);
        }
        return;
    }

    if (unit_context.game_menu.is_dialog_screen()) {
        const app::GameMenuRect panel = app::confirm_panel_rect(window_size);
        draw_panel_frame(
            window_size,
            shader_program,
            app::CommandPanelFrame{panel.x, panel.y, panel.width, panel.height});

        std::string message = "Confirm?";
        if (unit_context.game_menu.screen == app::GameMenuScreen::ConfirmOverwrite) {
            message = "Overwrite existing save?";
        }
        else if (unit_context.game_menu.screen == app::GameMenuScreen::ConfirmLoad) {
            message = "End current game and load?";
        }
        else if (unit_context.game_menu.screen == app::GameMenuScreen::ErrorMissingSave) {
            message = "Save file not found.";
        }

        const float message_y =
            panel.y + static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
        draw_string(
            window_size,
            panel.x + static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX),
            message_y,
            message,
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);

        for (const app::GameMenuButton& button :
             app::build_dialog_buttons(unit_context.game_menu, window_size)) {
            draw_labeled_button(button, false);
        }
        return;
    }

    const app::GameMenuRect panel = app::settings_panel_rect(window_size);
    draw_panel_frame(
        window_size,
        shader_program,
        app::CommandPanelFrame{panel.x, panel.y, panel.width, panel.height});

    for (const app::GameMenuButton& button :
         app::build_settings_buttons(unit_context.game_menu, window_size)) {
        const bool active_tab =
            (button.action == app::GameMenuAction::SettingsTabGame
                && unit_context.game_menu.screen == app::GameMenuScreen::SettingsGame)
            || (button.action == app::GameMenuAction::SettingsTabVideo
                && unit_context.game_menu.screen == app::GameMenuScreen::SettingsVideo)
            || (button.action == app::GameMenuAction::SettingsTabAudio
                && unit_context.game_menu.screen == app::GameMenuScreen::SettingsAudio);
        draw_labeled_button(button, active_tab);
    }

    if (unit_context.game_menu.screen == app::GameMenuScreen::SettingsGame) {
        const app::GameMenuRect slider = app::scroll_speed_slider_rect(window_size);
        const int percent = static_cast<int>(
            (unit_context.game_menu.scroll_speed / constants::CAMERA_SCROLL_SPEED_DEFAULT) * 100.0F
            + 0.5F);
        draw_string(
            window_size,
            slider.x,
            slider.y - constants::HUD_SETTINGS_LABEL_GAP_PX,
            "Scroll Speed: " + std::to_string(percent) + "%",
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);
        draw_screen_quad(
            window_size,
            shader_program,
            slider.x,
            slider.y,
            slider.width,
            slider.height,
            constants::HUD_VOLUME_SLIDER_TRACK_R,
            constants::HUD_VOLUME_SLIDER_TRACK_G,
            constants::HUD_VOLUME_SLIDER_TRACK_B);
        const float fill_t = std::clamp(
            (unit_context.game_menu.scroll_speed - constants::CAMERA_SCROLL_SPEED_MIN)
                / (constants::CAMERA_SCROLL_SPEED_MAX - constants::CAMERA_SCROLL_SPEED_MIN),
            0.0F,
            1.0F);
        draw_screen_quad(
            window_size,
            shader_program,
            slider.x,
            slider.y,
            slider.width * fill_t,
            slider.height,
            constants::HUD_VOLUME_SLIDER_FILL_R,
            constants::HUD_VOLUME_SLIDER_FILL_G,
            constants::HUD_VOLUME_SLIDER_FILL_B);
        return;
    }

    if (unit_context.game_menu.screen != app::GameMenuScreen::SettingsAudio) {
        return;
    }

    const auto draw_volume_slider = [&](
                                        const int row,
                                        const std::string& label,
                                        const float value) {
        const app::GameMenuRect slider = app::volume_slider_rect(window_size, row);
        const float label_y = slider.y - constants::HUD_SETTINGS_LABEL_GAP_PX;
        draw_string(
            window_size,
            slider.x,
            label_y,
            label,
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);

        draw_screen_quad(
            window_size,
            shader_program,
            slider.x,
            slider.y,
            slider.width,
            slider.height,
            constants::HUD_VOLUME_SLIDER_TRACK_R,
            constants::HUD_VOLUME_SLIDER_TRACK_G,
            constants::HUD_VOLUME_SLIDER_TRACK_B);

        const float fill_t = std::clamp(
            (value - constants::AUDIO_VOLUME_MIN)
                / (constants::AUDIO_VOLUME_MAX - constants::AUDIO_VOLUME_MIN),
            0.0F,
            1.0F);
        draw_screen_quad(
            window_size,
            shader_program,
            slider.x,
            slider.y,
            slider.width * fill_t,
            slider.height,
            constants::HUD_VOLUME_SLIDER_FILL_R,
            constants::HUD_VOLUME_SLIDER_FILL_G,
            constants::HUD_VOLUME_SLIDER_FILL_B);
    };

    draw_volume_slider(0, "Master", unit_context.game_menu.master_volume);
    draw_volume_slider(1, "Music", unit_context.game_menu.music_volume);
    draw_volume_slider(2, "SFX", unit_context.game_menu.sfx_volume);
}

void HudOverlay::draw_waiting_overlay(
    const sf::Vector2u window_size,
    const std::string& title,
    const std::string& subtitle) const
{
    if (window_size.x == 0U || window_size.y == 0U) {
        return;
    }

    const float title_scale = static_cast<float>(constants::LOCKSTEP_WAITING_TITLE_PIXEL_SCALE);
    const float subtitle_scale = static_cast<float>(constants::HUD_PIXEL_SCALE);
    const float title_char_step =
        static_cast<float>(GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * title_scale;
    const float subtitle_char_step =
        static_cast<float>(GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * subtitle_scale;
    const float title_width =
        static_cast<float>(title.size()) * title_char_step
        - static_cast<float>(constants::HUD_CHAR_SPACING) * title_scale;
    const float subtitle_width =
        static_cast<float>(subtitle.size()) * subtitle_char_step
        - static_cast<float>(constants::HUD_CHAR_SPACING) * subtitle_scale;
    const float title_height = static_cast<float>(GLYPH_HEIGHT) * title_scale;
    const float subtitle_height = static_cast<float>(GLYPH_HEIGHT) * subtitle_scale;
    const float line_gap = 20.0F;
    const float block_height = title_height + line_gap + subtitle_height;
    const float block_top = (static_cast<float>(window_size.y) - block_height) * 0.5F;

    const float title_x = (static_cast<float>(window_size.x) - title_width) * 0.5F;
    const float subtitle_x = (static_cast<float>(window_size.x) - subtitle_width) * 0.5F;

    draw_string_scaled(
        window_size,
        title_x,
        block_top,
        title,
        constants::LOCKSTEP_WAITING_TITLE_PIXEL_SCALE,
        1.0F,
        1.0F,
        1.0F);
    draw_string_scaled(
        window_size,
        subtitle_x,
        block_top + title_height + line_gap,
        subtitle,
        constants::HUD_PIXEL_SCALE,
        constants::HUD_TEXT_R,
        constants::HUD_TEXT_G,
        constants::HUD_TEXT_B);
}

void HudOverlay::draw_match_result(
    const sf::Vector2u window_size,
    const sim::components::MatchSession& session,
    const std::uint8_t local_player_slot,
    const HudUnitContext& unit_context) const
{
    if (!session.match_finished || window_size.x == 0U || window_size.y == 0U) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    const unsigned int shader_program = hud_shader_program();
    draw_screen_quad(
        window_size,
        shader_program,
        0.0F,
        0.0F,
        static_cast<float>(window_size.x),
        static_cast<float>(window_size.y),
        constants::HUD_MENU_SCRIM_R,
        constants::HUD_MENU_SCRIM_G,
        constants::HUD_MENU_SCRIM_B,
        constants::HUD_MENU_SCRIM_A);

    const app::GameMenuRect panel = app::match_result_panel_rect(window_size);
    draw_panel_frame(
        window_size,
        shader_program,
        app::CommandPanelFrame{panel.x, panel.y, panel.width, panel.height});

    const bool local_won =
        sim::components::slot_is_on_winning_side(session, local_player_slot);
    const char* title = local_won ? "VICTORY" : "DEFEAT";
    const float title_r =
        local_won ? constants::HUD_VICTORY_TITLE_R : constants::HUD_DEFEAT_TITLE_R;
    const float title_g =
        local_won ? constants::HUD_VICTORY_TITLE_G : constants::HUD_DEFEAT_TITLE_G;
    const float title_b =
        local_won ? constants::HUD_VICTORY_TITLE_B : constants::HUD_DEFEAT_TITLE_B;
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    float cursor_y = panel.y + padding;
    const float title_width = HudOverlay::text_width_px(
        std::string(title).size(),
        constants::HUD_MATCH_RESULT_TITLE_PIXEL_SCALE);
    draw_string_scaled(
        window_size,
        panel.x + (panel.width - title_width) * 0.5F,
        cursor_y,
        title,
        constants::HUD_MATCH_RESULT_TITLE_PIXEL_SCALE,
        title_r,
        title_g,
        title_b);
    cursor_y += HudOverlay::text_height_px(constants::HUD_MATCH_RESULT_TITLE_PIXEL_SCALE)
        + constants::HUD_MATCH_RESULT_LINE_GAP_PX * 2.0F;

    const sim::components::PlayerMatchStats& stats =
        local_player_slot < session.player_stats.size()
        ? session.player_stats[local_player_slot]
        : sim::components::PlayerMatchStats{};
    const int total_seconds = static_cast<int>(
        session.finished_tick / static_cast<std::uint64_t>(constants::SIM_TICKS_PER_SECOND));
    const int minutes = total_seconds / 60;
    const int seconds = total_seconds % 60;
    std::string time_line = "Time: " + std::to_string(minutes) + ":";
    if (seconds < 10) {
        time_line += "0";
    }
    time_line += std::to_string(seconds);

    const std::array<std::string, 9> lines{
        time_line,
        "Units killed: " + std::to_string(stats.units_killed),
        "Units lost: " + std::to_string(stats.units_lost),
        "Buildings destroyed: " + std::to_string(stats.buildings_destroyed),
        "Buildings lost: " + std::to_string(stats.buildings_lost),
        "Wood: " + std::to_string(stats.wood_collected),
        "Food: " + std::to_string(stats.food_collected),
        "Money: " + std::to_string(stats.money_collected),
        "Mana: " + std::to_string(stats.mana_collected),
    };
    const float line_step = HudOverlay::text_height_px(constants::HUD_PIXEL_SCALE)
        + constants::HUD_MATCH_RESULT_LINE_GAP_PX;
    for (const std::string& line : lines) {
        draw_string(
            window_size,
            panel.x + padding,
            cursor_y,
            line,
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);
        cursor_y += line_step;
    }

    const app::GameMenuButton exit_button{
        app::GameMenuAction::ExitToMainMenu,
        "Exit to Main Menu",
        app::match_result_exit_button_rect(window_size),
        false,
    };
    const sf::Vector2f mouse = unit_context.mouse_screen_position;
    const bool hovered = exit_button.rect.contains(mouse.x, mouse.y);
    const bool pressed = hovered && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    float button_r = constants::HUD_OPTIONS_BUTTON_R;
    float button_g = constants::HUD_OPTIONS_BUTTON_G;
    float button_b = constants::HUD_OPTIONS_BUTTON_B;
    if (hovered && !pressed) {
        button_r = std::min(1.0F, button_r * constants::HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN);
        button_g = std::min(1.0F, button_g * constants::HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN);
        button_b = std::min(1.0F, button_b * constants::HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN);
    }
    if (pressed) {
        button_r *= constants::HUD_OPTIONS_BUTTON_PRESS_DARKEN;
        button_g *= constants::HUD_OPTIONS_BUTTON_PRESS_DARKEN;
        button_b *= constants::HUD_OPTIONS_BUTTON_PRESS_DARKEN;
    }
    draw_screen_quad(
        window_size,
        shader_program,
        exit_button.rect.x,
        exit_button.rect.y,
        exit_button.rect.width,
        exit_button.rect.height,
        button_r,
        button_g,
        button_b);
    const float label_width = HudOverlay::text_width_px(
        exit_button.label.size(),
        constants::HUD_PIXEL_SCALE);
    const float label_height = HudOverlay::text_height_px(constants::HUD_PIXEL_SCALE);
    draw_string(
        window_size,
        exit_button.rect.x + (exit_button.rect.width - label_width) * 0.5F,
        exit_button.rect.y + (exit_button.rect.height - label_height) * 0.5F,
        std::string(exit_button.label),
        constants::HUD_TEXT_R,
        constants::HUD_TEXT_G,
        constants::HUD_TEXT_B);
}

void HudOverlay::draw(
    const sim::Simulation& simulation,
    const sf::Vector2u window_size,
    const float fps,
    const float tps,
    const std::uint8_t local_player_slot,
    const float camera_zoom,
    const HudUnitContext& unit_context,
    const net::LockstepNetworkHudStats& network_stats,
    const bool show_perf_hud,
    const bool show_selection_debug,
    BuildingSightMemory* building_sight_memory,
    const SimRenderSnapshot* minimap_snapshot) const
{
    begin_hud_screen_pass();
    const auto& registry = simulation.registry();
    const auto session_view = registry.view<sim::components::WorldTag, sim::components::MatchSession>();
    if (session_view.begin() != session_view.end()) {
        set_hud_player_color_indices(
            session_view.get<sim::components::MatchSession>(*session_view.begin())
                .player_color_indices);
    }

    const sim::components::Stockpile stockpile =
        sim::player::sum_player_stockpile(registry, local_player_slot);
    const int civil_cap_current = sim::player::count_player_units(registry, local_player_slot);
    const int civil_cap_max = sim::player::player_civil_cap_max(registry, local_player_slot);

    const float line_height =
        static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE + constants::HUD_LINE_SPACING);
    const float resource_line_height = std::max(
        line_height,
        static_cast<float>(constants::HUD_ICON_DRAW_SIZE_PX)
            + static_cast<float>(constants::HUD_LINE_SPACING));
    float left_y = constants::HUD_MARGIN_Y;

    draw_resource_bar(
        window_size,
        constants::HUD_MARGIN_X,
        left_y,
        stockpile.wood,
        stockpile.food,
        stockpile.money,
        stockpile.mana,
        sim::player::player_mana_cap_max(registry, local_player_slot),
        civil_cap_current,
        civil_cap_max);
    left_y += resource_line_height;
    draw_chat(window_size, left_y, unit_context);

    const HudInfoPanel info_panel =
        build_info_panel_live(registry, unit_context, local_player_slot, show_selection_debug);

    const float char_step = static_cast<float>(
        (GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * constants::HUD_PIXEL_SCALE);
    const app::GameMenuRect menu_rect = app::menu_button_rect(window_size);
    float top_right_y = menu_rect.y + menu_rect.height
        + static_cast<float>(constants::HUD_MENU_BUTTON_MARGIN_PX);
    const auto draw_top_right_line = [&](const std::string& line, const float r, const float g, const float b) {
        const float text_width = static_cast<float>(line.size()) * char_step;
        const float x = static_cast<float>(window_size.x) - constants::HUD_MARGIN_X - text_width;
        draw_string(window_size, x, top_right_y, line, r, g, b);
        top_right_y += line_height;
    };

    if (show_perf_hud) {
        draw_top_right_line(
            "FPS: " + std::to_string(static_cast<int>(fps + 0.5F)),
            constants::HUD_TEXT_R * 0.85F,
            constants::HUD_TEXT_G * 0.85F,
            constants::HUD_TEXT_B * 0.85F);
        draw_top_right_line(
            "TPS: " + std::to_string(static_cast<int>(tps + 0.5F)),
            constants::HUD_TEXT_R * 0.85F,
            constants::HUD_TEXT_G * 0.85F,
            constants::HUD_TEXT_B * 0.85F);
        if (show_selection_debug) {
            draw_top_right_line(
                format_zoom_line(camera_zoom),
                constants::HUD_TEXT_R * 0.75F,
                constants::HUD_TEXT_G * 0.95F,
                constants::HUD_TEXT_B * 0.75F);
        }
    }

    if (network_stats.active) {
        const int ping_ms = std::max(0, network_stats.local_ping_ms);
        draw_top_right_line(
            "PING: " + std::to_string(ping_ms) + "ms",
            constants::HUD_TEXT_R * 0.75F,
            constants::HUD_TEXT_G * 0.95F,
            constants::HUD_TEXT_B * 0.75F);
    }

    draw_command_panel(
        window_size,
        unit_context.command_panel_mode,
        unit_context.build_options,
        unit_context.mouse_screen_position,
        unit_context.command_panel_pressed_slot);
    {
        std::optional<SimRenderSnapshot> owned_snapshot{};
        const SimRenderSnapshot* snapshot_ptr = minimap_snapshot;
        if (snapshot_ptr == nullptr) {
            owned_snapshot = capture_sim_render_snapshot(
                registry, local_player_slot, building_sight_memory, simulation.tick_count());
            snapshot_ptr = &*owned_snapshot;
        }
        draw_minimap(window_size, *snapshot_ptr, local_player_slot, unit_context);
    }
    draw_info_panel(window_size, info_panel);
    draw_game_menu(window_size, unit_context);
    if (unit_context.has_match_session) {
        draw_match_result(
            window_size,
            unit_context.match_session,
            local_player_slot,
            unit_context);
    }
    end_hud_screen_pass();
}

void HudOverlay::draw_snapshot(
    const SimRenderSnapshot& snapshot,
    const sf::Vector2u window_size,
    const float fps,
    const float tps,
    const std::uint8_t local_player_slot,
    const float camera_zoom,
    const HudUnitContext& unit_context,
    const net::LockstepNetworkHudStats& network_stats,
    const bool show_perf_hud,
    const bool show_selection_debug) const
{
    begin_hud_screen_pass();
    set_hud_player_color_indices(snapshot.player_color_indices);
    const RenderHudPlayerStats& local_stats = snapshot.hud_by_player[local_player_slot];

    const float line_height =
        static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE + constants::HUD_LINE_SPACING);
    const float resource_line_height = std::max(
        line_height,
        static_cast<float>(constants::HUD_ICON_DRAW_SIZE_PX)
            + static_cast<float>(constants::HUD_LINE_SPACING));
    float left_y = constants::HUD_MARGIN_Y;

    draw_resource_bar(
        window_size,
        constants::HUD_MARGIN_X,
        left_y,
        local_stats.town_wood,
        local_stats.town_food,
        local_stats.town_money,
        local_stats.town_mana,
        local_stats.town_mana_max,
        local_stats.civil_cap_current,
        local_stats.civil_cap_max);
    left_y += resource_line_height;
    draw_chat(window_size, left_y, unit_context);

    const HudInfoPanel info_panel =
        build_info_panel_snapshot(snapshot, unit_context, local_player_slot, show_selection_debug);

    const float char_step = static_cast<float>(
        (GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * constants::HUD_PIXEL_SCALE);
    const app::GameMenuRect menu_rect = app::menu_button_rect(window_size);
    float top_right_y = menu_rect.y + menu_rect.height
        + static_cast<float>(constants::HUD_MENU_BUTTON_MARGIN_PX);
    const auto draw_top_right_line = [&](const std::string& line, const float r, const float g, const float b) {
        const float text_width = static_cast<float>(line.size()) * char_step;
        const float x = static_cast<float>(window_size.x) - constants::HUD_MARGIN_X - text_width;
        draw_string(window_size, x, top_right_y, line, r, g, b);
        top_right_y += line_height;
    };

    if (show_perf_hud) {
        draw_top_right_line(
            "FPS: " + std::to_string(static_cast<int>(fps + 0.5F)),
            constants::HUD_TEXT_R * 0.85F,
            constants::HUD_TEXT_G * 0.85F,
            constants::HUD_TEXT_B * 0.85F);
        draw_top_right_line(
            "TPS: " + std::to_string(static_cast<int>(tps + 0.5F)),
            constants::HUD_TEXT_R * 0.85F,
            constants::HUD_TEXT_G * 0.85F,
            constants::HUD_TEXT_B * 0.85F);
        if (show_selection_debug) {
            draw_top_right_line(
                format_zoom_line(camera_zoom),
                constants::HUD_TEXT_R * 0.75F,
                constants::HUD_TEXT_G * 0.95F,
                constants::HUD_TEXT_B * 0.75F);
        }
    }

    if (network_stats.active) {
        const int ping_ms = std::max(0, network_stats.local_ping_ms);
        draw_top_right_line(
            "PING: " + std::to_string(ping_ms) + "ms",
            constants::HUD_TEXT_R * 0.75F,
            constants::HUD_TEXT_G * 0.95F,
            constants::HUD_TEXT_B * 0.75F);
    }

    draw_command_panel(
        window_size,
        unit_context.command_panel_mode,
        unit_context.build_options,
        unit_context.mouse_screen_position,
        unit_context.command_panel_pressed_slot);
    draw_minimap(window_size, snapshot, local_player_slot, unit_context);
    draw_info_panel(window_size, info_panel);
    draw_game_menu(window_size, unit_context);
    if (unit_context.has_match_session) {
        draw_match_result(
            window_size,
            unit_context.match_session,
            local_player_slot,
            unit_context);
    }
    end_hud_screen_pass();
}

} // namespace aoa::render
