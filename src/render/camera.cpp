#include "render/camera.hpp"

#include "core/constants.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace aoa::render {

namespace {

struct Bounds {
    float min_x{0.0F};
    float min_y{0.0F};
    float max_x{0.0F};
    float max_y{0.0F};
};

Bounds compute_map_bounds(
    const int map_width,
    const int map_height,
    const float zoom,
    const int padding_tiles = 0)
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

    const int min_x = -padding_tiles;
    const int min_y = -padding_tiles;
    const int max_x = map_width + padding_tiles;
    const int max_y = map_height + padding_tiles;
    for (int grid_y = min_y; grid_y < max_y; ++grid_y) {
        for (int grid_x = min_x; grid_x < max_x; ++grid_x) {
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
    clamp_to_map_bounds();
}

void ClassicCamera::set_map_size(const int map_width, const int map_height)
{
    map_width_ = map_width;
    map_height_ = map_height;
    clamp_to_map_bounds();
}

void ClassicCamera::clamp_to_map_bounds()
{
    if (window_size_.x == 0U || window_size_.y == 0U || map_width_ <= 0 || map_height_ <= 0) {
        return;
    }

    const Bounds bounds = compute_map_bounds(
        map_width_,
        map_height_,
        zoom_,
        constants::CAMERA_MAP_BOUNDS_PADDING_TILES);
    const float window_width = static_cast<float>(window_size_.x);
    const float window_height = static_cast<float>(window_size_.y);
    const float map_width_px = bounds.max_x - bounds.min_x;
    const float map_height_px = bounds.max_y - bounds.min_y;

    if (map_width_px <= window_width) {
        pan_.x = window_width * 0.5F - (bounds.min_x + bounds.max_x) * 0.5F;
    }
    else {
        const float min_pan_x = window_width - bounds.max_x;
        const float max_pan_x = -bounds.min_x;
        pan_.x = std::clamp(pan_.x, min_pan_x, max_pan_x);
    }

    if (map_height_px <= window_height) {
        pan_.y = window_height * 0.5F - (bounds.min_y + bounds.max_y) * 0.5F;
    }
    else {
        const float min_pan_y = window_height - bounds.max_y;
        const float max_pan_y = -bounds.min_y;
        pan_.y = std::clamp(pan_.y, min_pan_y, max_pan_y);
    }
}

void ClassicCamera::reset_view()
{
    user_adjusted_ = false;
    pan_ = {0.0F, 0.0F};
    zoom_ = 1.0F;
    zoom_target_ = 1.0F;
    zoom_level_index_ = 0;
}

void ClassicCamera::snap_zoom_to_level(const int zoom_level_index)
{
    zoom_level_index_ = std::clamp(
        zoom_level_index,
        0,
        static_cast<int>(constants::CAMERA_CLASSIC_ZOOM_LEVELS.size()) - 1);
    zoom_ = constants::CAMERA_CLASSIC_ZOOM_LEVELS[static_cast<std::size_t>(zoom_level_index_)];
    zoom_target_ = zoom_;
}

void ClassicCamera::center_on_world(
    const float world_x,
    const float world_z,
    const float zoom,
    const int zoom_level_index)
{
    if (window_size_.x == 0U || window_size_.y == 0U) {
        return;
    }

    snap_zoom_to_level(zoom_level_index);
    zoom_ = std::clamp(
        zoom,
        constants::CAMERA_CLASSIC_MIN_ZOOM,
        constants::CAMERA_CLASSIC_MAX_ZOOM);
    zoom_target_ = zoom_;

    const float half_w = tile_half_width();
    const float half_h = tile_half_height();
    const float screen_x = (world_x - world_z) * half_w;
    const float screen_y = (world_x + world_z) * half_h;

    const float window_width = static_cast<float>(window_size_.x);
    const float window_height = static_cast<float>(window_size_.y);

    pan_.x = window_width * 0.5F - screen_x;
    pan_.y = window_height * 0.5F - screen_y;

    zoom_anchor_x_ = window_width * 0.5F;
    zoom_anchor_y_ = window_height * 0.5F;
    user_adjusted_ = false;
    clamp_to_map_bounds();
}

void ClassicCamera::center_on_world_keep_zoom(const float world_x, const float world_z)
{
    if (window_size_.x == 0U || window_size_.y == 0U) {
        return;
    }

    const float half_w = tile_half_width();
    const float half_h = tile_half_height();
    const float screen_x = (world_x - world_z) * half_w;
    const float screen_y = (world_x + world_z) * half_h;

    const float window_width = static_cast<float>(window_size_.x);
    const float window_height = static_cast<float>(window_size_.y);

    pan_.x = window_width * 0.5F - screen_x;
    pan_.y = window_height * 0.5F - screen_y;

    zoom_anchor_x_ = window_width * 0.5F;
    zoom_anchor_y_ = window_height * 0.5F;
    user_adjusted_ = true;
    clamp_to_map_bounds();
}

void ClassicCamera::frame_map(const int map_width, const int map_height)
{
    if (window_size_.x == 0U || window_size_.y == 0U) {
        return;
    }

    map_width_ = map_width;
    map_height_ = map_height;

    if (user_adjusted_) {
        clamp_to_map_bounds();
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

    const float clamped_zoom = std::clamp(
        fit_zoom,
        constants::CAMERA_CLASSIC_MIN_ZOOM,
        constants::CAMERA_CLASSIC_MAX_ZOOM);

    int closest_level_index = 0;
    float closest_distance = std::numeric_limits<float>::max();
    for (std::size_t index = 0U; index < constants::CAMERA_CLASSIC_ZOOM_LEVELS.size(); ++index) {
        const float distance = std::abs(constants::CAMERA_CLASSIC_ZOOM_LEVELS[index] - clamped_zoom);
        if (distance < closest_distance) {
            closest_distance = distance;
            closest_level_index = static_cast<int>(index);
        }
    }

    snap_zoom_to_level(closest_level_index);

    const Bounds fitted_bounds = compute_map_bounds(map_width, map_height, zoom_);
    const float center_x = (fitted_bounds.min_x + fitted_bounds.max_x) * 0.5F;
    const float center_y = (fitted_bounds.min_y + fitted_bounds.max_y) * 0.5F;

    pan_.x = window_width * 0.5F - center_x;
    pan_.y = window_height * 0.5F - center_y;

    zoom_anchor_x_ = window_width * 0.5F;
    zoom_anchor_y_ = window_height * 0.5F;
    clamp_to_map_bounds();
}

void ClassicCamera::apply_zoom_pan(
    const float old_zoom,
    const float new_zoom,
    const float anchor_screen_x,
    const float anchor_screen_y)
{
    if (old_zoom == new_zoom) {
        return;
    }

    const float zoom_ratio = new_zoom / old_zoom;
    pan_.x = anchor_screen_x - (anchor_screen_x - pan_.x) * zoom_ratio;
    pan_.y = anchor_screen_y - (anchor_screen_y - pan_.y) * zoom_ratio;
}

void ClassicCamera::update(const float delta_seconds)
{
    if (window_size_.x == 0U || window_size_.y == 0U) {
        return;
    }

    const float zoom_delta = zoom_target_ - zoom_;
    if (std::abs(zoom_delta) <= 0.0001F) {
        zoom_ = zoom_target_;
        return;
    }

    const float blend = 1.0F
        - std::exp(-constants::CAMERA_CLASSIC_ZOOM_SMOOTH_RATE * delta_seconds);
    const float old_zoom = zoom_;
    const float new_zoom = zoom_ + zoom_delta * blend;

    apply_zoom_pan(old_zoom, new_zoom, zoom_anchor_x_, zoom_anchor_y_);
    zoom_ = new_zoom;
    clamp_to_map_bounds();
}

void ClassicCamera::pan(const float delta_x, const float delta_y)
{
    pan_.x += delta_x;
    pan_.y += delta_y;
    user_adjusted_ = true;
    clamp_to_map_bounds();
}

void ClassicCamera::step_zoom(
    const int direction,
    const float anchor_screen_x,
    const float anchor_screen_y)
{
    if (direction == 0) {
        return;
    }

    const int level_count = static_cast<int>(constants::CAMERA_CLASSIC_ZOOM_LEVELS.size());
    const int next_level_index = std::clamp(
        zoom_level_index_ + (direction > 0 ? 1 : -1),
        0,
        level_count - 1);

    if (next_level_index == zoom_level_index_
        && std::abs(zoom_target_ - zoom_) <= 0.0001F) {
        return;
    }

    zoom_level_index_ = next_level_index;
    zoom_target_ = constants::CAMERA_CLASSIC_ZOOM_LEVELS[static_cast<std::size_t>(zoom_level_index_)];
    zoom_anchor_x_ = anchor_screen_x;
    zoom_anchor_y_ = anchor_screen_y;
    user_adjusted_ = true;
}

std::optional<core::GridPos> ClassicCamera::screen_to_grid(
    const float screen_x,
    const float screen_y) const
{
    const auto [world_x, world_z] = screen_to_world_xz(screen_x, screen_y);
    return core::GridPos{
        static_cast<int>(std::floor(world_x)),
        static_cast<int>(std::floor(world_z)),
    };
}

std::pair<float, float> ClassicCamera::screen_to_world_xz(
    const float screen_x,
    const float screen_y) const
{
    if (window_size_.x == 0U || window_size_.y == 0U) {
        return {0.0F, 0.0F};
    }

    const float local_x = screen_x - pan_.x;
    const float local_y = screen_y - pan_.y;
    const float half_w = tile_half_width();
    const float half_h = tile_half_height();

    if (half_w <= 0.0F || half_h <= 0.0F) {
        return {0.0F, 0.0F};
    }

    const float world_x = (local_x / half_w + local_y / half_h) * 0.5F;
    const float world_z = (local_y / half_h - local_x / half_w) * 0.5F;
    return {world_x, world_z};
}

sf::Vector2f ClassicCamera::world_to_screen(
    const float world_x,
    const float world_y,
    const float world_z) const
{
    const auto clip = world_to_clip(world_x, world_y, world_z);
    const float window_width = static_cast<float>(window_size_.x);
    const float window_height = static_cast<float>(window_size_.y);

    return {
        (clip[0] + 1.0F) * 0.5F * window_width,
        (1.0F - clip[1]) * 0.5F * window_height,
    };
}

sf::Vector2f ClassicCamera::grid_top_corner(const int grid_x, const int grid_y) const
{
    return grid_iso_corners(grid_x, grid_y).top;
}

IsoTileScreenCorners ClassicCamera::grid_iso_corners(const int grid_x, const int grid_y) const
{
    const float half_w = tile_half_width();
    const float half_h = tile_half_height();
    const float tile_h = tile_height();
    const float iso_x = static_cast<float>(grid_x - grid_y) * half_w + pan_.x;
    const float iso_y = static_cast<float>(grid_x + grid_y) * half_h + pan_.y;

    return IsoTileScreenCorners{
        {iso_x, iso_y},
        {iso_x + half_w, iso_y + half_h},
        {iso_x, iso_y + tile_h},
        {iso_x - half_w, iso_y + half_h},
    };
}

std::array<float, 3> ClassicCamera::world_to_clip(
    const float world_x,
    const float world_y,
    const float world_z) const
{
    const float half_w = tile_half_width();
    const float half_h = tile_half_height();

    const float screen_x = (world_x - world_z) * half_w + pan_.x;
    const float screen_y = (world_x + world_z) * half_h
        - world_y * constants::RENDER_HEIGHT_SCREEN_SCALE * zoom_ + pan_.y;

    const float window_width = static_cast<float>(window_size_.x);
    const float window_height = static_cast<float>(window_size_.y);

    const float ndc_x = (screen_x / window_width) * 2.0F - 1.0F;
    const float ndc_y = 1.0F - (screen_y / window_height) * 2.0F;
    const float depth = (world_x + world_z) * constants::RENDER_DEPTH_GRID_SCALE
        - world_y * constants::RENDER_DEPTH_HEIGHT_SCALE;

    return {ndc_x, ndc_y, depth};
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
