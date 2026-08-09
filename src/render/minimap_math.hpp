#pragma once

#include "app/command_panel.hpp"
#include "core/constants.hpp"

#include <SFML/System/Vector2.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace aoa::render {

struct MinimapIsoLayout {
    float offset_x{0.0F};
    float offset_y{0.0F};
    float scale{1.0F};
    float min_iso_u{0.0F};
    float min_iso_v{0.0F};
    float iso_span{1.0F};
    int map_width{0};
    int map_height{0};
};

[[nodiscard]] inline MinimapIsoLayout make_minimap_iso_layout(
    const app::CommandPanelFrame& content,
    const int map_width,
    const int map_height)
{
    MinimapIsoLayout layout{};
    layout.map_width = map_width;
    layout.map_height = map_height;
    if (map_width <= 0 || map_height <= 0 || content.width <= 0.0F || content.height <= 0.0F) {
        return layout;
    }

    layout.min_iso_u = -static_cast<float>(map_height);
    layout.min_iso_v = 0.0F;
    layout.iso_span = static_cast<float>(map_width + map_height);
    if (layout.iso_span <= 0.0F) {
        return layout;
    }

    layout.scale = std::min(content.width / layout.iso_span, content.height / layout.iso_span);
    float used = layout.iso_span * layout.scale;
    if (used > static_cast<float>(constants::MINIMAP_TEXTURE_MAX_EDGE_PX)) {
        layout.scale = static_cast<float>(constants::MINIMAP_TEXTURE_MAX_EDGE_PX) / layout.iso_span;
        used = layout.iso_span * layout.scale;
    }

    layout.offset_x = content.x + (content.width - used) * 0.5F;
    layout.offset_y = content.y + (content.height - used) * 0.5F;
    return layout;
}

[[nodiscard]] inline sf::Vector2f minimap_world_to_screen(
    const MinimapIsoLayout& layout,
    const float world_x,
    const float world_z)
{
    const float iso_u = world_x - world_z;
    const float iso_v = world_x + world_z;
    return {
        layout.offset_x + (iso_u - layout.min_iso_u) * layout.scale,
        layout.offset_y + (iso_v - layout.min_iso_v) * layout.scale,
    };
}

[[nodiscard]] inline std::optional<std::pair<float, float>> minimap_screen_to_world(
    const MinimapIsoLayout& layout,
    const float screen_x,
    const float screen_y)
{
    if (layout.scale <= 0.0F || layout.map_width <= 0 || layout.map_height <= 0) {
        return std::nullopt;
    }

    const float iso_u = (screen_x - layout.offset_x) / layout.scale + layout.min_iso_u;
    const float iso_v = (screen_y - layout.offset_y) / layout.scale + layout.min_iso_v;
    const float world_x = (iso_u + iso_v) * 0.5F;
    const float world_z = (iso_v - iso_u) * 0.5F;
    if (world_x < 0.0F || world_z < 0.0F || world_x >= static_cast<float>(layout.map_width)
        || world_z >= static_cast<float>(layout.map_height)) {
        return std::nullopt;
    }

    return std::pair<float, float>{world_x, world_z};
}

[[nodiscard]] inline bool minimap_screen_inside_diamond(
    const MinimapIsoLayout& layout,
    const float screen_x,
    const float screen_y)
{
    return minimap_screen_to_world(layout, screen_x, screen_y).has_value();
}

} // namespace aoa::render
