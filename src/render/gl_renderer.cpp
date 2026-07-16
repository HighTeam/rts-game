#include "render/gl_renderer.hpp"

#include <SFML/Window/Context.hpp>

#include <glad/glad.h>

#include <array>
#include <stdexcept>
#include <string>

namespace aoa::render {

namespace {

constexpr const char* VERTEX_SHADER_SOURCE = R"(
#version 330 core
layout(location = 0) in vec2 position;
void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

constexpr const char* FRAGMENT_SHADER_SOURCE = R"(
#version 330 core
uniform vec3 triangle_color;
out vec4 fragment_color;
void main()
{
    fragment_color = vec4(triangle_color, 1.0);
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

GlRenderer::GlRenderer()
{
    if (!init_gl_loader()) {
        throw std::runtime_error("Failed to initialize OpenGL loader");
    }

    create_shader_program();

    glGenVertexArrays(1, &vertex_array_);
    glGenBuffers(1, &vertex_buffer_);

    glBindVertexArray(vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);

    constexpr std::array<float, 6> triangle_vertices = {
        -0.5F, -0.5F,
         0.5F, -0.5F,
         0.0F,  0.5F,
    };

    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizei>(triangle_vertices.size() * sizeof(float)),
        triangle_vertices.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0U);
}

GlRenderer::~GlRenderer()
{
    destroy_gl_objects();
}

void GlRenderer::create_shader_program()
{
    const unsigned int vertex_shader = compile_shader(GL_VERTEX_SHADER, VERTEX_SHADER_SOURCE);
    const unsigned int fragment_shader = compile_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER_SOURCE);
    shader_program_ = link_program(vertex_shader, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
}

void GlRenderer::destroy_gl_objects()
{
    if (shader_program_ != 0U) {
        glDeleteProgram(shader_program_);
        shader_program_ = 0U;
    }

    if (vertex_buffer_ != 0U) {
        glDeleteBuffers(1, &vertex_buffer_);
        vertex_buffer_ = 0U;
    }

    if (vertex_array_ != 0U) {
        glDeleteVertexArrays(1, &vertex_array_);
        vertex_array_ = 0U;
    }
}

void GlRenderer::resize(const sf::Vector2u size)
{
    glViewport(0, 0, static_cast<GLsizei>(size.x), static_cast<GLsizei>(size.y));
}

void GlRenderer::draw_triangle(const float interpolation_alpha)
{
    const float red = 0.12F + (0.25F * interpolation_alpha);
    const float green = 0.14F;
    const float blue = 0.18F;

    glClearColor(red, green, blue, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shader_program_);
    const int color_location = glGetUniformLocation(shader_program_, "triangle_color");
    glUniform3f(color_location, 0.85F, 0.55F, 0.20F);

    glBindVertexArray(vertex_array_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0U);
}

} // namespace aoa::render
