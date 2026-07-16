#pragma once

#include <SFML/Window/Window.hpp>

namespace aoa::render {

class GlRenderer {
public:
    GlRenderer();
    ~GlRenderer();

    GlRenderer(const GlRenderer&) = delete;
    GlRenderer& operator=(const GlRenderer&) = delete;

    void resize(sf::Vector2u size);
    void draw_triangle(float interpolation_alpha);

private:
    void create_shader_program();
    void destroy_gl_objects();

    unsigned int vertex_array_{0U};
    unsigned int vertex_buffer_{0U};
    unsigned int shader_program_{0U};
};

bool init_gl_loader();

} // namespace aoa::render
