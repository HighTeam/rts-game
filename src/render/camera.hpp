#pragma once

#include "core/grid.hpp"

#include <array>
#include <optional>

#include <SFML/System/Vector2.hpp>

namespace aoa::render {

enum class CameraView {
    Classic,
    Full3D,
};

struct IsoTileScreenCorners {
    sf::Vector2f top{};
    sf::Vector2f right{};
    sf::Vector2f bottom{};
    sf::Vector2f left{};
};

struct VisibleGridRange {
    int min_x{0};
    int min_y{0};
    int max_x{0};
    int max_y{0};
};

class ClassicCamera {
public:
    void set_window_size(sf::Vector2u window_size);
    void set_map_size(int map_width, int map_height);
    void frame_map(int map_width, int map_height);
    void center_on_world(float world_x, float world_z, float zoom, int zoom_level_index);
    void center_on_world_keep_zoom(float world_x, float world_z);
    void reset_view();

    void update(float delta_seconds);
    void pan(float delta_x, float delta_y);
    void step_zoom(int direction, float anchor_screen_x, float anchor_screen_y);
    void clamp_to_map_bounds();

    [[nodiscard]] sf::Vector2f grid_top_corner(int grid_x, int grid_y) const;
    [[nodiscard]] IsoTileScreenCorners grid_iso_corners(int grid_x, int grid_y) const;
    [[nodiscard]] VisibleGridRange visible_grid_range(
        int map_width,
        int map_height,
        int pad_tiles) const;
    [[nodiscard]] std::optional<core::GridPos> screen_to_grid(float screen_x, float screen_y) const;
    [[nodiscard]] sf::Vector2f world_to_screen(float world_x, float world_y, float world_z) const;
    [[nodiscard]] std::pair<float, float> screen_to_world_xz(float screen_x, float screen_y) const;
    [[nodiscard]] std::array<float, 3> world_to_clip(float world_x, float world_y, float world_z) const;
    [[nodiscard]] float tile_width() const;
    [[nodiscard]] float tile_height() const;
    [[nodiscard]] float tile_half_width() const;
    [[nodiscard]] float tile_half_height() const;
    [[nodiscard]] float zoom() const { return zoom_; }

private:
    void apply_zoom_pan(float old_zoom, float new_zoom, float anchor_screen_x, float anchor_screen_y);
    void snap_zoom_to_level(int zoom_level_index);

    sf::Vector2u window_size_{0U, 0U};
    sf::Vector2f pan_{0.0F, 0.0F};
    float zoom_{1.0F};
    float zoom_target_{1.0F};
    int zoom_level_index_{0};
    float zoom_anchor_x_{0.0F};
    float zoom_anchor_y_{0.0F};
    bool user_adjusted_{false};
    int map_width_{0};
    int map_height_{0};
};

} // namespace aoa::render
