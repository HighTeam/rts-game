#include "render/hud_overlay.hpp"

#include "app/command_panel.hpp"
#include "core/constants.hpp"
#include "core/grid.hpp"
#include "core/runtime_paths.hpp"
#include "data/content_types.hpp"
#include "net/lockstep_network_hud.hpp"
#include "render/hud_icons.hpp"
#include "render/minimap_math.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/content_pack.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
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
#include <stdexcept>
#include <string>
#include <vector>

namespace aoa::render {

namespace {

constexpr int GLYPH_WIDTH = 5;
constexpr int GLYPH_HEIGHT = 7;

constexpr const char* HUD_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec2 position;
void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

constexpr const char* HUD_FRAGMENT_SHADER = R"(
#version 330 core
uniform vec4 color;
out vec4 fragment_color;
void main()
{
    fragment_color = color;
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
    const float a = 1.0F)
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

    const std::array<float, 8> vertices = {
        top_left[0], top_left[1],
        top_right[0], top_right[1],
        bottom_right[0], bottom_right[1],
        bottom_left[0], bottom_left[1],
    };

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

    glUseProgram(shader_program);
    glUniform4f(glGetUniformLocation(shader_program, "color"), r, g, b, a);
    if (a < 1.0F) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    if (a < 1.0F) {
        glDisable(GL_BLEND);
    }

    glBindVertexArray(0U);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
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
    if (player_slot >= constants::PLAYER_SLOT_COLOR_RGB.size()) {
        panel.title_r = constants::HUD_TEXT_R;
        panel.title_g = constants::HUD_TEXT_G;
        panel.title_b = constants::HUD_TEXT_B;
        return;
    }

    const auto& color = constants::PLAYER_SLOT_COLOR_RGB[player_slot];
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

[[nodiscard]] std::string player_suffix(const std::uint8_t player_slot)
{
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
    const entt::entity entity)
{
    if (!registry.valid(entity)) {
        return;
    }

    panel.active = true;

    const bool is_nature = !registry.any_of<sim::components::PlayerOwnedTag>(entity);
    const std::uint8_t player_slot = is_nature
        ? 0U
        : sim::components::entity_player_slot(registry, entity);

    if (registry.any_of<sim::components::MilitiaUnitTag>(entity)) {
        panel.title = "Militia" + player_suffix(player_slot);
        apply_player_title_color(panel, player_slot);
    }
    else if (registry.any_of<sim::components::WorkerUnitTag>(entity)) {
        panel.title = "Worker" + player_suffix(player_slot);
        apply_player_title_color(panel, player_slot);
    }
    else if (registry.any_of<sim::components::TownCenterTag>(entity)) {
        panel.title = "Town Center" + player_suffix(player_slot);
        apply_player_title_color(panel, player_slot);
    }
    else if (registry.any_of<sim::components::HouseTag>(entity)) {
        panel.title = "House" + player_suffix(player_slot);
        apply_player_title_color(panel, player_slot);
    }
    else if (is_nature) {
        panel.title = "Other (Nature)";
        apply_nature_title_color(panel);
    }
    else {
        panel.title = "Other" + player_suffix(player_slot);
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
            return;
        }
    }

    if (registry.any_of<sim::components::CarriedMoney>(entity)) {
        const int carried_money = registry.get<sim::components::CarriedMoney>(entity).amount;
        if (carried_money > 0) {
            panel.carry_amount = carried_money;
            panel.carry_icon = HudIcon::Money;
            return;
        }
    }

    if (registry.any_of<sim::components::CarriedWood>(entity)) {
        const int carried = registry.get<sim::components::CarriedWood>(entity).amount;
        if (carried > 0) {
            panel.carry_amount = carried;
            panel.carry_icon = HudIcon::Wood;
        }
    }
}

void fill_info_panel_from_pose(HudInfoPanel& panel, const RenderEntityPose& pose)
{
    panel.active = true;

    if (pose.is_militia) {
        panel.title = "Militia" + player_suffix(pose.player_slot);
        apply_player_title_color(panel, pose.player_slot);
    }
    else if (pose.is_worker) {
        panel.title = "Worker" + player_suffix(pose.player_slot);
        apply_player_title_color(panel, pose.player_slot);
    }
    else if (pose.is_town_center) {
        panel.title = "Town Center" + player_suffix(pose.player_slot);
        apply_player_title_color(panel, pose.player_slot);
    }
    else if (pose.is_house) {
        panel.title = "House" + player_suffix(pose.player_slot);
        apply_player_title_color(panel, pose.player_slot);
    }
    else if (pose.is_nature) {
        panel.title = "Other (Nature)";
        apply_nature_title_color(panel);
    }
    else {
        panel.title = "Other" + player_suffix(pose.player_slot);
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
    const std::uint8_t local_player_slot)
{
    HudInfoPanel panel{};

    if (unit_context.selected_single_unit != entt::null) {
        fill_info_panel_from_live_entity(panel, registry, unit_context.selected_single_unit);
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

        const std::uint8_t owner_slot = unit_context.has_selected_building_owner
            ? unit_context.selected_building_player_slot
            : local_player_slot;
        if (unit_context.selected_building_is_house) {
            panel.title = "House" + player_suffix(owner_slot);
            apply_player_title_color(panel, owner_slot);
        }
        else {
            panel.title = "Town Center" + player_suffix(owner_slot);
            apply_player_title_color(panel, owner_slot);
        }

        if (const data::ArchetypeDefinition* definition = find_town_center_definition(registry);
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
    const std::uint8_t local_player_slot)
{
    HudInfoPanel panel{};

    if (unit_context.selected_single_unit != entt::null) {
        if (const RenderEntityPose* unit_pose =
                find_pose_by_entity(snapshot.units, unit_context.selected_single_unit);
            unit_pose != nullptr) {
            fill_info_panel_from_pose(panel, *unit_pose);
            return panel;
        }

        if (const RenderEntityPose* building_pose =
                find_pose_by_entity(snapshot.buildings, unit_context.selected_single_unit);
            building_pose != nullptr) {
            fill_info_panel_from_pose(panel, *building_pose);
            return panel;
        }
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
                fill_info_panel_from_pose(panel, *building_pose);
                return panel;
            }
        }

        const std::uint8_t owner_slot = unit_context.has_selected_building_owner
            ? unit_context.selected_building_player_slot
            : local_player_slot;
        if (unit_context.selected_building_is_house) {
            panel.title = "House" + player_suffix(owner_slot);
            apply_player_title_color(panel, owner_slot);
            return panel;
        }

        panel.title = "Town Center" + player_suffix(owner_slot);
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
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(2U * sizeof(float))));

    glUseProgram(shader_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glUniform1i(glGetUniformLocation(shader_program, "icon_texture"), 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, 0U);
    glBindVertexArray(0U);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
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

    const auto a = to_ndc(x0, y0);
    const auto b_ndc = to_ndc(x1, y1);
    const std::array<float, 4> vertices = {a[0], a[1], b_ndc[0], b_ndc[1]};

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
    glUseProgram(shader_program);
    glUniform4f(glGetUniformLocation(shader_program, "color"), r, g, b, 1.0F);
    glDrawArrays(GL_LINES, 0, 2);
    glBindVertexArray(0U);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
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
    if (!visible) {
        return {
            constants::MINIMAP_FOG_EXPLORED_R,
            constants::MINIMAP_FOG_EXPLORED_G,
            constants::MINIMAP_FOG_EXPLORED_B,
        };
    }

    if (index < snapshot.tiles.size()
        && snapshot.tiles[index] == sim::components::TileType::Forest) {
        return {
            constants::MINIMAP_FOG_VISIBLE_FOREST_R,
            constants::MINIMAP_FOG_VISIBLE_FOREST_G,
            constants::MINIMAP_FOG_VISIBLE_FOREST_B,
        };
    }

    return {
        constants::MINIMAP_FOG_VISIBLE_GRASS_R,
        constants::MINIMAP_FOG_VISIBLE_GRASS_G,
        constants::MINIMAP_FOG_VISIBLE_GRASS_B,
    };
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

    const GLboolean previous_depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean previous_depth_mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previous_depth_mask);
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

    const auto draw_marker = [&](
                                 const float world_x,
                                 const float world_z,
                                 const int footprint_w,
                                 const int footprint_h,
                                 const std::uint8_t player_slot,
                                 const bool is_building) {
        if (player_slot >= constants::PLAYER_SLOT_COLOR_RGB.size()) {
            return;
        }

        const auto& rgb = constants::PLAYER_SLOT_COLOR_RGB[player_slot];
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

    // Diamond outline of the map extents.
    const sf::Vector2f c00 = minimap_world_to_screen(layout, 0.0F, 0.0F);
    const sf::Vector2f c10 = minimap_world_to_screen(layout, static_cast<float>(map_width), 0.0F);
    const sf::Vector2f c11 =
        minimap_world_to_screen(layout, static_cast<float>(map_width), static_cast<float>(map_height));
    const sf::Vector2f c01 = minimap_world_to_screen(layout, 0.0F, static_cast<float>(map_height));
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

    glDepthMask(previous_depth_mask);
    if (previous_depth_test) {
        glEnable(GL_DEPTH_TEST);
    }
    else {
        glDisable(GL_DEPTH_TEST);
    }
}

} // namespace

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
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(2U * sizeof(float))));

    const unsigned int shader_program = hud_tinted_texture_shader_program();
    glUseProgram(shader_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glUniform1i(glGetUniformLocation(shader_program, "icon_texture"), 0);
    glUniform4f(glGetUniformLocation(shader_program, "tint"), 1.0F, 1.0F, 1.0F, alpha);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, 0U);
    glBindVertexArray(0U);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
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

    const unsigned int shader_program = hud_textured_shader_program();
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
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(2U * sizeof(float))));

    glUseProgram(shader_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas.texture_id());
    glUniform1i(glGetUniformLocation(shader_program, "icon_texture"), 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisable(GL_BLEND);

    glBindTexture(GL_TEXTURE_2D, 0U);
    glBindVertexArray(0U);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
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
        const float char_step = static_cast<float>(
            (GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * constants::HUD_PIXEL_SCALE);
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
    std::string hover_cost_label{};
    for (const app::CommandPanelButton& button :
         app::build_command_panel_buttons(mode, window_size, build_options)) {
        const bool hovered = mouse_screen_position.x >= button.x
            && mouse_screen_position.x <= button.x + button.width
            && mouse_screen_position.y >= button.y
            && mouse_screen_position.y <= button.y + button.height;
        if (hovered && button.cost_food > 0) {
            hover_cost_label = "Cost: " + std::to_string(button.cost_food) + " Food";
        }
        else if (hovered && button.cost_wood > 0) {
            hover_cost_label = "Cost: " + std::to_string(button.cost_wood) + " Wood";
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

    if (!hover_cost_label.empty()) {
        draw_string(
            window_size,
            frame.x,
            frame.y - static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE)
                - static_cast<float>(constants::HUD_LINE_SPACING),
            hover_cost_label,
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
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
    const float icon_size = static_cast<float>(constants::HUD_ICON_DRAW_SIZE_PX);
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

    if (panel.carry_amount > 0) {
        const float carry_line_height = std::max(line_height, icon_size + static_cast<float>(constants::HUD_LINE_SPACING));
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
                text_y + (icon_size - static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE))
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
                icon_size,
                panel.carry_icon);
            text_y += carry_line_height;
        }
    }

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
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
    float text_y = y;
    int drawn_lines = 0;
    for (const app::ChatLine& line : unit_context.chat_lines) {
        if (drawn_lines >= constants::CHAT_MAX_VISIBLE_LINES) {
            break;
        }

        const std::string text =
            "P" + std::to_string(static_cast<int>(line.player_slot) + 1) + ": " + line.text;
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

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
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
        for (const app::GameMenuButton& button : app::build_main_menu_buttons(window_size)) {
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
            || (button.action == app::GameMenuAction::SettingsTabAudio
                && unit_context.game_menu.screen == app::GameMenuScreen::SettingsAudio);
        draw_labeled_button(button, active_tab);
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

void HudOverlay::draw(
    const sim::Simulation& simulation,
    const sf::Vector2u window_size,
    const float fps,
    const float tps,
    const std::uint8_t local_player_slot,
    const float camera_zoom,
    const HudUnitContext& unit_context,
    const net::LockstepNetworkHudStats& network_stats,
    const bool show_perf_hud) const
{
    const auto& registry = simulation.registry();

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
        constants::PLAYER_MANA_MAX,
        civil_cap_current,
        civil_cap_max);
    left_y += resource_line_height;
    draw_chat(window_size, left_y, unit_context);

    const HudInfoPanel info_panel =
        build_info_panel_live(registry, unit_context, local_player_slot);

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
        draw_top_right_line(
            format_zoom_line(camera_zoom),
            constants::HUD_TEXT_R * 0.75F,
            constants::HUD_TEXT_G * 0.95F,
            constants::HUD_TEXT_B * 0.75F);
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
        const SimRenderSnapshot snapshot =
            capture_sim_render_snapshot(registry, local_player_slot);
        draw_minimap(window_size, snapshot, local_player_slot, unit_context);
    }
    draw_info_panel(window_size, info_panel);
    draw_game_menu(window_size, unit_context);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
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
    const bool show_perf_hud) const
{
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
        local_stats.town_mana_max > 0 ? local_stats.town_mana_max : constants::PLAYER_MANA_MAX,
        local_stats.civil_cap_current,
        local_stats.civil_cap_max);
    left_y += resource_line_height;
    draw_chat(window_size, left_y, unit_context);

    const HudInfoPanel info_panel =
        build_info_panel_snapshot(snapshot, unit_context, local_player_slot);

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
        draw_top_right_line(
            format_zoom_line(camera_zoom),
            constants::HUD_TEXT_R * 0.75F,
            constants::HUD_TEXT_G * 0.95F,
            constants::HUD_TEXT_B * 0.75F);
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
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

} // namespace aoa::render
