#include "render/hud_overlay.hpp"

#include "core/constants.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"

#include <entt/entt.hpp>

#include <glad/glad.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

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
    const float b)
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
    glUniform3f(glGetUniformLocation(shader_program, "color"), r, g, b);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glBindVertexArray(0U);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
}

} // namespace

unsigned int HudOverlay::hud_shader_program() const
{
    static unsigned int program = 0U;
    if (program != 0U) {
        return program;
    }

    const unsigned int vertex_shader = compile_shader(GL_VERTEX_SHADER, HUD_VERTEX_SHADER);
    const unsigned int fragment_shader = compile_shader(GL_FRAGMENT_SHADER, HUD_FRAGMENT_SHADER);
    program = link_program(vertex_shader, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return program;
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
    const unsigned int shader_program = hud_shader_program();
    const float pixel_scale = static_cast<float>(constants::HUD_PIXEL_SCALE);
    const float char_step = static_cast<float>(GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * pixel_scale;

    float cursor_x = x;
    for (const char character : text) {
        const auto& rows = glyph_rows(character);
        for (int row = 0; row < GLYPH_HEIGHT; ++row) {
            for (int column = 0; column < GLYPH_WIDTH; ++column) {
                const int bit = 1 << (GLYPH_WIDTH - 1 - column);
                if ((rows[static_cast<std::size_t>(row)] & bit) == 0) {
                    continue;
                }

                const float pixel_x = cursor_x + static_cast<float>(column) * pixel_scale;
                const float pixel_y = y + static_cast<float>(row) * pixel_scale;
                draw_screen_quad(
                    window_size,
                    shader_program,
                    pixel_x,
                    pixel_y,
                    pixel_scale,
                    pixel_scale,
                    r,
                    g,
                    b);
            }
        }

        cursor_x += char_step;
    }
}

void HudOverlay::draw(const sim::Simulation& simulation, const sf::Vector2u window_size) const
{
    const auto& registry = simulation.registry();

    int town_wood = 0;
    const auto town_center_view =
        registry.view<sim::components::TownCenterTag, sim::components::Stockpile>();
    for (const entt::entity entity : town_center_view) {
        town_wood = town_center_view.get<sim::components::Stockpile>(entity).wood;
        break;
    }

    int carried_wood = 0;
    const auto worker_view = registry.view<sim::components::WorkerUnitTag, sim::components::CarriedWood>();
    for (const entt::entity entity : worker_view) {
        carried_wood = worker_view.get<sim::components::CarriedWood>(entity).amount;
        break;
    }

    const std::string stockpile_line = "Wood: " + std::to_string(town_wood);
    const std::string carried_line = "Carry: " + std::to_string(carried_wood);

    const float line_height =
        static_cast<float>(GLYPH_HEIGHT * constants::HUD_PIXEL_SCALE + constants::HUD_LINE_SPACING);

    draw_string(
        window_size,
        constants::HUD_MARGIN_X,
        constants::HUD_MARGIN_Y,
        stockpile_line,
        constants::HUD_TEXT_R,
        constants::HUD_TEXT_G,
        constants::HUD_TEXT_B);

    draw_string(
        window_size,
        constants::HUD_MARGIN_X,
        constants::HUD_MARGIN_Y + line_height,
        carried_line,
        constants::HUD_TEXT_R,
        constants::HUD_TEXT_G * 0.9F,
        constants::HUD_TEXT_B * 0.7F);
}

} // namespace aoa::render
