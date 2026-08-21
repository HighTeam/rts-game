#pragma once

#include "app/game_menu.hpp"
#include "core/constants.hpp"

#include <SFML/System/Vector2.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace aoa::app {

struct HudGrid {
    float unit_x{1.0F};
    float unit_y{1.0F};

    [[nodiscard]] static HudGrid from(const sf::Vector2u window_size)
    {
        return HudGrid{
            static_cast<float>(window_size.x) / static_cast<float>(constants::HUD_GRID_COLUMNS),
            static_cast<float>(window_size.y) / static_cast<float>(constants::HUD_GRID_ROWS),
        };
    }

    [[nodiscard]] float x(const float units) const
    {
        return units * unit_x;
    }

    [[nodiscard]] float y(const float units) const
    {
        return units * unit_y;
    }

    [[nodiscard]] float w(const float units) const
    {
        return units * unit_x;
    }

    [[nodiscard]] float h(const float units) const
    {
        return units * unit_y;
    }

    [[nodiscard]] sf::Vector2f point(const float units_x, const float units_y) const
    {
        return sf::Vector2f{x(units_x), y(units_y)};
    }
};

[[nodiscard]] inline GameMenuRect default_minimap_rect(const sf::Vector2u window_size)
{
    const HudGrid grid = HudGrid::from(window_size);
    const float cell = std::min(grid.unit_x, grid.unit_y);
    const float size = constants::HUD_DEFAULT_MINIMAP_SIZE_U * cell;
    const float left =
        (static_cast<float>(window_size.x) - size) * 0.5F;
    const float top = static_cast<float>(window_size.y) - size;
    return GameMenuRect{left, top, size, size};
}

[[nodiscard]] inline GameMenuRect default_minimap_inner_rect(const sf::Vector2u window_size)
{
    const GameMenuRect outer = default_minimap_rect(window_size);
    const HudGrid grid = HudGrid::from(window_size);
    const float cell = std::min(grid.unit_x, grid.unit_y);
    const float inset = constants::HUD_DEFAULT_MINIMAP_INSET_U * cell;
    return GameMenuRect{
        outer.x + inset,
        outer.y + inset,
        std::max(0.0F, outer.width - inset * 2.0F),
        std::max(0.0F, outer.height - inset * 2.0F),
    };
}

[[nodiscard]] inline std::vector<sf::Vector2f> default_minimap_diamond_points(
    const GameMenuRect& rect)
{
    return {
        sf::Vector2f{rect.x + rect.width * 0.5F, rect.y},
        sf::Vector2f{rect.x + rect.width, rect.y + rect.height * 0.5F},
        sf::Vector2f{rect.x + rect.width * 0.5F, rect.y + rect.height},
        sf::Vector2f{rect.x, rect.y + rect.height * 0.5F},
    };
}

[[nodiscard]] inline float default_diamond_edge_x(
    const GameMenuRect& diamond,
    const float y,
    const bool left_side)
{
    const float half_w = diamond.width * 0.5F;
    const float half_h = diamond.height * 0.5F;
    if (half_h <= 0.0F) {
        return diamond.x + half_w;
    }

    const float cy = diamond.y + half_h;
    const float t = 1.0F - std::fabs(y - cy) / half_h;
    const float span = half_w * std::max(0.0F, t);
    const float cx = diamond.x + half_w;
    return left_side ? cx - span : cx + span;
}

[[nodiscard]] inline GameMenuRect default_left_decor_rect(const sf::Vector2u window_size)
{
    const HudGrid grid = HudGrid::from(window_size);
    return GameMenuRect{
        0.0F,
        grid.y(static_cast<float>(constants::HUD_GRID_ROWS)
            - constants::HUD_DEFAULT_DECOR_HEIGHT_U),
        grid.w(constants::HUD_DEFAULT_DECOR_WIDTH_U),
        grid.h(constants::HUD_DEFAULT_DECOR_HEIGHT_U),
    };
}

[[nodiscard]] inline GameMenuRect default_right_decor_rect(const sf::Vector2u window_size)
{
    const HudGrid grid = HudGrid::from(window_size);
    return GameMenuRect{
        grid.x(static_cast<float>(constants::HUD_GRID_COLUMNS)
            - constants::HUD_DEFAULT_DECOR_WIDTH_U),
        grid.y(static_cast<float>(constants::HUD_GRID_ROWS)
            - constants::HUD_DEFAULT_DECOR_HEIGHT_U),
        grid.w(constants::HUD_DEFAULT_DECOR_WIDTH_U),
        grid.h(constants::HUD_DEFAULT_DECOR_HEIGHT_U),
    };
}

[[nodiscard]] inline GameMenuRect default_left_hatch_rect(const sf::Vector2u window_size)
{
    const HudGrid grid = HudGrid::from(window_size);
    return GameMenuRect{
        0.0F,
        grid.y(static_cast<float>(constants::HUD_GRID_ROWS)
            - constants::HUD_DEFAULT_DECOR_HATCH_HEIGHT_U),
        grid.w(constants::HUD_DEFAULT_DECOR_WIDTH_U),
        grid.h(constants::HUD_DEFAULT_DECOR_HATCH_HEIGHT_U),
    };
}

[[nodiscard]] inline GameMenuRect default_right_hatch_rect(const sf::Vector2u window_size)
{
    const HudGrid grid = HudGrid::from(window_size);
    return GameMenuRect{
        grid.x(static_cast<float>(constants::HUD_GRID_COLUMNS)
            - constants::HUD_DEFAULT_DECOR_WIDTH_U),
        grid.y(static_cast<float>(constants::HUD_GRID_ROWS)
            - constants::HUD_DEFAULT_DECOR_HATCH_HEIGHT_U),
        grid.w(constants::HUD_DEFAULT_DECOR_WIDTH_U),
        grid.h(constants::HUD_DEFAULT_DECOR_HATCH_HEIGHT_U),
    };
}

[[nodiscard]] inline std::vector<sf::Vector2f> default_option_panel_points(
    const sf::Vector2u window_size)
{
    const HudGrid grid = HudGrid::from(window_size);
    const float left = constants::HUD_DEFAULT_DECOR_WIDTH_U;
    const float top = static_cast<float>(constants::HUD_GRID_ROWS)
        - constants::HUD_DEFAULT_SIDE_BOX_HEIGHT_U;
    const float bottom = static_cast<float>(constants::HUD_GRID_ROWS);
    const float chamfer = constants::HUD_DEFAULT_CHAMFER_U;
    const GameMenuRect diamond = default_minimap_rect(window_size);
    const float top_px = grid.y(top);
    const float bottom_px = grid.y(bottom);
    return {
        grid.point(left + chamfer, top),
        sf::Vector2f{default_diamond_edge_x(diamond, top_px, true), top_px},
        sf::Vector2f{diamond.x, diamond.y + diamond.height * 0.5F},
        sf::Vector2f{default_diamond_edge_x(diamond, bottom_px, true), bottom_px},
        grid.point(left, bottom),
        grid.point(left, top + chamfer),
    };
}

[[nodiscard]] inline std::vector<sf::Vector2f> default_info_panel_points(
    const sf::Vector2u window_size)
{
    const HudGrid grid = HudGrid::from(window_size);
    const float right = static_cast<float>(constants::HUD_GRID_COLUMNS)
        - constants::HUD_DEFAULT_DECOR_WIDTH_U;
    const float top = static_cast<float>(constants::HUD_GRID_ROWS)
        - constants::HUD_DEFAULT_SIDE_BOX_HEIGHT_U;
    const float bottom = static_cast<float>(constants::HUD_GRID_ROWS);
    const float chamfer = constants::HUD_DEFAULT_CHAMFER_U;
    const GameMenuRect diamond = default_minimap_rect(window_size);
    const float top_px = grid.y(top);
    const float bottom_px = grid.y(bottom);
    return {
        sf::Vector2f{default_diamond_edge_x(diamond, top_px, false), top_px},
        grid.point(right - chamfer, top),
        grid.point(right, top + chamfer),
        grid.point(right, bottom),
        sf::Vector2f{default_diamond_edge_x(diamond, bottom_px, false), bottom_px},
        sf::Vector2f{diamond.x + diamond.width, diamond.y + diamond.height * 0.5F},
    };
}

[[nodiscard]] inline std::vector<sf::Vector2f> default_left_top_bar_points(
    const sf::Vector2u window_size)
{
    const HudGrid grid = HudGrid::from(window_size);
    const float width = constants::HUD_DEFAULT_TOP_BAR_WIDTH_U;
    const float height = constants::HUD_DEFAULT_TOP_BAR_HEIGHT_U;
    return {
        grid.point(0.0F, 0.0F),
        grid.point(width, 0.0F),
        grid.point(width - height, height),
        grid.point(0.0F, height),
    };
}

[[nodiscard]] inline std::vector<sf::Vector2f> default_right_top_bar_points(
    const sf::Vector2u window_size)
{
    const HudGrid grid = HudGrid::from(window_size);
    const float left = static_cast<float>(constants::HUD_GRID_COLUMNS)
        - constants::HUD_DEFAULT_TOP_BAR_WIDTH_U;
    const float right = static_cast<float>(constants::HUD_GRID_COLUMNS);
    const float height = constants::HUD_DEFAULT_TOP_BAR_HEIGHT_U;
    return {
        grid.point(left, 0.0F),
        grid.point(right, 0.0F),
        grid.point(right, height),
        grid.point(left + height, height),
    };
}

[[nodiscard]] inline std::vector<sf::Vector2f> default_civ_logo_points(
    const sf::Vector2u window_size)
{
    const HudGrid grid = HudGrid::from(window_size);
    const float size = constants::HUD_DEFAULT_LOGO_SIZE_U;
    const float cut = constants::HUD_DEFAULT_CHAMFER_U;
    return {
        grid.point(cut, 0.0F),
        grid.point(size - cut, 0.0F),
        grid.point(size, cut),
        grid.point(size, size - cut),
        grid.point(size - cut, size),
        grid.point(cut, size),
        grid.point(0.0F, size - cut),
        grid.point(0.0F, cut),
    };
}

[[nodiscard]] inline GameMenuRect default_menu_button_rect(const sf::Vector2u window_size)
{
    const HudGrid grid = HudGrid::from(window_size);
    const float width = constants::HUD_DEFAULT_MENU_BUTTON_WIDTH_U;
    return GameMenuRect{
        grid.x(static_cast<float>(constants::HUD_GRID_COLUMNS) - width),
        0.0F,
        grid.w(width),
        grid.h(constants::HUD_DEFAULT_TOP_BAR_HEIGHT_U),
    };
}

[[nodiscard]] inline GameMenuRect default_diplomacy_button_rect(const sf::Vector2u window_size)
{
    const HudGrid grid = HudGrid::from(window_size);
    const float menu_w = constants::HUD_DEFAULT_MENU_BUTTON_WIDTH_U;
    const float width = constants::HUD_DEFAULT_DIPLOMACY_BUTTON_WIDTH_U;
    return GameMenuRect{
        grid.x(static_cast<float>(constants::HUD_GRID_COLUMNS) - menu_w - width),
        0.0F,
        grid.w(width),
        grid.h(constants::HUD_DEFAULT_TOP_BAR_HEIGHT_U),
    };
}

[[nodiscard]] inline GameMenuRect default_unit_icon_rect(const sf::Vector2u window_size)
{
    const HudGrid grid = HudGrid::from(window_size);
    const float half = constants::HUD_DEFAULT_UNIT_ICON_HALF_U;
    return GameMenuRect{
        grid.x(constants::HUD_DEFAULT_UNIT_ICON_CENTER_X_U - half),
        grid.y(constants::HUD_DEFAULT_UNIT_ICON_CENTER_Y_U - half),
        grid.w(half * 2.0F),
        grid.h(half * 2.0F),
    };
}

[[nodiscard]] inline GameMenuRect default_circle_button_rect(
    const sf::Vector2u window_size,
    const float center_x_u,
    const float center_y_u)
{
    const HudGrid grid = HudGrid::from(window_size);
    const float radius = constants::HUD_DEFAULT_CIRCLE_BUTTON_RADIUS_U;
    return GameMenuRect{
        grid.x(center_x_u - radius),
        grid.y(center_y_u - radius),
        grid.w(radius * 2.0F),
        grid.h(radius * 2.0F),
    };
}

[[nodiscard]] inline GameMenuRect hud_menu_button_rect(
    const sf::Vector2u window_size,
    const constants::HudStyle hud_style)
{
    if (hud_style == constants::HudStyle::Aoe) {
        return menu_button_rect(window_size);
    }

    return default_menu_button_rect(window_size);
}

[[nodiscard]] inline GameMenuRect hud_diplomacy_button_rect(
    const sf::Vector2u window_size,
    const constants::HudStyle hud_style)
{
    if (hud_style == constants::HudStyle::Aoe) {
        const GameMenuRect menu = menu_button_rect(window_size);
        const float width =
            static_cast<float>(window_size.x) * constants::HUD_DIPLOMACY_BUTTON_WIDTH_FRACTION;
        return GameMenuRect{
            menu.x - constants::HUD_DIPLOMACY_BUTTON_GAP_PX - width,
            menu.y,
            width,
            menu.height,
        };
    }

    return default_diplomacy_button_rect(window_size);
}

} // namespace aoa::app
