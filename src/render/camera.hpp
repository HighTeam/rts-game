#pragma once

#include <array>

#include <SFML/System/Vector2.hpp>

namespace aoa::render {

enum class CameraView {
    Classic,
    Full3D,
};

class ClassicCamera {
public:
    void set_window_size(sf::Vector2u window_size);
    void frame_map(int map_width, int map_height);
    void reset_view();

    void pan(float delta_x, float delta_y);
    void add_zoom(float delta);

    [[nodiscard]] sf::Vector2f grid_top_corner(int grid_x, int grid_y) const;
    [[nodiscard]] std::array<float, 3> world_to_clip(float world_x, float world_y, float world_z) const;
    [[nodiscard]] float tile_width() const;
    [[nodiscard]] float tile_height() const;
    [[nodiscard]] float tile_half_width() const;
    [[nodiscard]] float tile_half_height() const;

private:
    sf::Vector2u window_size_{0U, 0U};
    sf::Vector2f pan_{0.0F, 0.0F};
    float zoom_{1.0F};
    bool user_adjusted_{false};
};

} // namespace aoa::render
