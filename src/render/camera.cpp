#include "render/camera.hpp"

#include "core/constants.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace aoa::render {

namespace {

struct Bounds {
    float min_x{0.0F};
    float min_y{0.0F};
    float max_x{0.0F};
    float max_y{0.0F};
};

Bounds compute_map_bounds(const int map_width, const int map_height, const float zoom)
{
    const float half_w =
        static_cast<float>(constants::RENDER_ISO_TILE_WIDTH) * 0.5F * zoom;
    const float half_h =
        static_cast<float>(constants::RENDER_ISO_TILE_HEIGHT) * 0.5F * zoom;
    const float tile_h = static_cast<float>(constants::RENDER_ISO_TILE_HEIGHT) * zoom;

    Bounds bounds{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };

    const auto extend = [&](const float x, const float y) {
        bounds.min_x = std::min(bounds.min_x, x);
        bounds.min_y = std::min(bounds.min_y, y);
        bounds.max_x = std::max(bounds.max_x, x);
        bounds.max_y = std::max(bounds.max_y, y);
    };

    for (int grid_y = 0; grid_y < map_height; ++grid_y) {
        for (int grid_x = 0; grid_x < map_width; ++grid_x) {
            const float iso_x = static_cast<float>(grid_x - grid_y) * half_w;
            const float iso_y = static_cast<float>(grid_x + grid_y) * half_h;

            extend(iso_x, iso_y);
            extend(iso_x + half_w, iso_y + half_h);
            extend(iso_x, iso_y + tile_h);
            extend(iso_x - half_w, iso_y + half_h);
        }
    }

    return bounds;
}

} // namespace

void ClassicCamera::set_window_size(const sf::Vector2u window_size)
{
    window_size_ = window_size;
}

void ClassicCamera::reset_view()
{
    user_adjusted_ = false;
    pan_ = {0.0F, 0.0F};
    zoom_ = 1.0F;
}

void ClassicCamera::frame_map(const int map_width, const int map_height)
{
    if (window_size_.x == 0U || window_size_.y == 0U) {
        return;
    }

    if (user_adjusted_) {
        return;
    }

    zoom_ = 1.0F;
    const Bounds bounds = compute_map_bounds(map_width, map_height, zoom_);

    const float map_width_px = bounds.max_x - bounds.min_x;
    const float map_height_px = bounds.max_y - bounds.min_y;

    const float window_width = static_cast<float>(window_size_.x);
    const float window_height = static_cast<float>(window_size_.y);

    const float fit_zoom = std::min(
                               window_width / map_width_px,
                               window_height / map_height_px)
        * constants::CAMERA_CLASSIC_FRAME_PADDING;

    zoom_ = std::clamp(
        fit_zoom,
        constants::CAMERA_CLASSIC_MIN_ZOOM,
        constants::CAMERA_CLASSIC_MAX_ZOOM);

    const Bounds fitted_bounds = compute_map_bounds(map_width, map_height, zoom_);
    const float center_x = (fitted_bounds.min_x + fitted_bounds.max_x) * 0.5F;
    const float center_y = (fitted_bounds.min_y + fitted_bounds.max_y) * 0.5F;

    pan_.x = window_width * 0.5F - center_x;
    pan_.y = window_height * 0.5F - center_y;
}

void ClassicCamera::pan(const float delta_x, const float delta_y)
{
    pan_.x += delta_x;
    pan_.y += delta_y;
    user_adjusted_ = true;
}

void ClassicCamera::add_zoom(const float delta)
{
    zoom_ = std::clamp(
        zoom_ + delta,
        constants::CAMERA_CLASSIC_MIN_ZOOM,
        constants::CAMERA_CLASSIC_MAX_ZOOM);
    user_adjusted_ = true;
}

sf::Vector2f ClassicCamera::grid_top_corner(const int grid_x, const int grid_y) const
{
    const float half_w = tile_half_width();
    const float half_h = tile_half_height();

    return {
        static_cast<float>(grid_x - grid_y) * half_w + pan_.x,
        static_cast<float>(grid_x + grid_y) * half_h + pan_.y,
    };
}

float ClassicCamera::tile_width() const
{
    return static_cast<float>(constants::RENDER_ISO_TILE_WIDTH) * zoom_;
}

float ClassicCamera::tile_height() const
{
    return static_cast<float>(constants::RENDER_ISO_TILE_HEIGHT) * zoom_;
}

float ClassicCamera::tile_half_width() const
{
    return tile_width() * 0.5F;
}

float ClassicCamera::tile_half_height() const
{
    return tile_height() * 0.5F;
}

} // namespace aoa::render
