#include "app/game_cursor.hpp"

#include "core/asset_io.hpp"
#include "core/constants.hpp"

#include <SFML/Graphics/Image.hpp>

#include <algorithm>
#include <array>
#include <cstdio>

namespace aoa::app {

namespace {

// Color file indices per shape, ordered as CursorPlayerColor:
// Red, Blue, Green, Yellow, Purple, Gray, Cyan(unused→Blue base), Pink
// Cyan uses the Blue PNG as a tint base (index ignored here).
using ColorFiles = std::array<int, static_cast<std::size_t>(CursorPlayerColor::Count)>;

[[nodiscard]] int shape_folder_index(const CursorShape shape)
{
    switch (shape) {
    case CursorShape::Normal:
        return constants::CURSOR_SHAPE_FOLDER_NORMAL;
    case CursorShape::Restricted:
        return constants::CURSOR_SHAPE_FOLDER_RESTRICTED;
    case CursorShape::Blocked:
        return constants::CURSOR_SHAPE_FOLDER_BLOCKED;
    case CursorShape::Attack:
        return constants::CURSOR_SHAPE_FOLDER_ATTACK;
    case CursorShape::AttackRestricted:
        return constants::CURSOR_SHAPE_FOLDER_ATTACK_RESTRICTED;
    case CursorShape::Target:
        return constants::CURSOR_SHAPE_FOLDER_TARGET;
    case CursorShape::Question:
        return constants::CURSOR_SHAPE_FOLDER_QUESTION;
    case CursorShape::Exclamation:
        return constants::CURSOR_SHAPE_FOLDER_EXCLAMATION;
    case CursorShape::Check:
        return constants::CURSOR_SHAPE_FOLDER_CHECK;
    case CursorShape::Cross:
        return constants::CURSOR_SHAPE_FOLDER_CROSS;
    case CursorShape::Rally:
        return constants::CURSOR_SHAPE_FOLDER_RALLY;
    case CursorShape::Count:
        break;
    }

    return constants::CURSOR_SHAPE_FOLDER_NORMAL;
}

[[nodiscard]] const ColorFiles& color_files_for_shape(const CursorShape shape)
{
    // Cyan slot stores Blue's file; load path tints it to cyan.
    static constexpr ColorFiles question_files{11, 2, 9, 8, 3, 10, 2, 5};
    static constexpr ColorFiles exclamation_files{6, 2, 10, 9, 3, 11, 2, 5};
    static constexpr ColorFiles check_files{4, 10, 9, 1, 2, 11, 10, 3};
    static constexpr ColorFiles cross_files{4, 10, 9, 1, 2, 11, 10, 3};
    static constexpr ColorFiles rally_files{6, 2, 10, 9, 3, 11, 2, 5};
    static constexpr ColorFiles attack_files{6, 2, 10, 9, 3, 11, 2, 5};
    static constexpr ColorFiles attack_restricted_files{6, 2, 10, 9, 3, 11, 2, 5};
    static constexpr ColorFiles restricted_files{4, 10, 9, 1, 2, 11, 10, 3};
    static constexpr ColorFiles target_files{4, 10, 9, 1, 2, 11, 10, 3};
    static constexpr ColorFiles normal_files{4, 10, 9, 1, 2, 11, 10, 3};
    // Blocked pack was not listed; reuse restricted coloring.
    static constexpr ColorFiles blocked_files{4, 10, 9, 1, 2, 11, 10, 3};

    switch (shape) {
    case CursorShape::Question:
        return question_files;
    case CursorShape::Exclamation:
        return exclamation_files;
    case CursorShape::Check:
        return check_files;
    case CursorShape::Cross:
        return cross_files;
    case CursorShape::Rally:
        return rally_files;
    case CursorShape::Attack:
        return attack_files;
    case CursorShape::AttackRestricted:
        return attack_restricted_files;
    case CursorShape::Restricted:
        return restricted_files;
    case CursorShape::Target:
        return target_files;
    case CursorShape::Normal:
        return normal_files;
    case CursorShape::Blocked:
        return blocked_files;
    case CursorShape::Count:
        break;
    }

    return normal_files;
}

[[nodiscard]] bool shape_blue_needs_blueish_shift(const CursorShape shape)
{
    switch (shape) {
    case CursorShape::Check:
    case CursorShape::Cross:
    case CursorShape::Restricted:
    case CursorShape::Target:
    case CursorShape::Normal:
    case CursorShape::Blocked:
        return true;
    default:
        return false;
    }
}

void scale_rgb(sf::Color& pixel, const float r_scale, const float g_scale, const float b_scale)
{
    const auto clamp_channel = [](const float value) {
        return static_cast<std::uint8_t>(std::clamp(value, 0.0F, 255.0F));
    };
    pixel.r = clamp_channel(static_cast<float>(pixel.r) * r_scale);
    pixel.g = clamp_channel(static_cast<float>(pixel.g) * g_scale);
    pixel.b = clamp_channel(static_cast<float>(pixel.b) * b_scale);
}

void prepare_cursor_pixels(
    sf::Image& image,
    const CursorShape shape,
    const CursorPlayerColor color)
{
    const bool apply_blueish =
        color == CursorPlayerColor::Blue && shape_blue_needs_blueish_shift(shape);
    const bool apply_cyan = color == CursorPlayerColor::Cyan;

    const sf::Vector2u size = image.getSize();
    for (unsigned int y = 0U; y < size.y; ++y) {
        for (unsigned int x = 0U; x < size.x; ++x) {
            sf::Color pixel = image.getPixel({x, y});
            if (pixel.r < 16 && pixel.g < 16 && pixel.b < 16) {
                pixel.a = 0;
                image.setPixel({x, y}, pixel);
                continue;
            }

            if (apply_blueish) {
                scale_rgb(
                    pixel,
                    constants::CURSOR_BLUEISH_RED_SCALE,
                    constants::CURSOR_BLUEISH_GREEN_SCALE,
                    constants::CURSOR_BLUEISH_BLUE_SCALE);
            }
            else if (apply_cyan) {
                scale_rgb(
                    pixel,
                    constants::CURSOR_CYAN_RED_SCALE,
                    constants::CURSOR_CYAN_GREEN_SCALE,
                    constants::CURSOR_CYAN_BLUE_SCALE);
            }

            image.setPixel({x, y}, pixel);
        }
    }
}

} // namespace

CursorPlayerColor cursor_color_for_player_slot(const std::uint8_t player_slot)
{
    static constexpr std::array<CursorPlayerColor, 8> slot_colors{
        CursorPlayerColor::Red,
        CursorPlayerColor::Blue,
        CursorPlayerColor::Green,
        CursorPlayerColor::Yellow,
        CursorPlayerColor::Purple,
        CursorPlayerColor::Gray,
        CursorPlayerColor::Cyan,
        CursorPlayerColor::Pink,
    };

    if (player_slot >= slot_colors.size()) {
        return CursorPlayerColor::Gray;
    }

    return slot_colors[player_slot];
}

bool GameCursor::load_shape_color(
    const std::filesystem::path& assets_directory,
    const CursorShape shape,
    const CursorPlayerColor color)
{
    const ColorFiles& files = color_files_for_shape(shape);
    const int color_file = files[static_cast<std::size_t>(color)];

    char relative[128]{};
    std::snprintf(
        relative,
        sizeof(relative),
        "textures/CursorCrystal/PNG/%02d/%02d.png",
        shape_folder_index(shape),
        color_file);

    (void)assets_directory;
    sf::Image image{};
    if (!core::load_image_asset(image, relative)) {
        return false;
    }

    prepare_cursor_pixels(image, shape, color);
    const sf::Vector2u size = image.getSize();
    if (size.x == 0U || size.y == 0U) {
        return false;
    }

    const sf::Vector2u hotspot{
        static_cast<unsigned int>(constants::CURSOR_HOTSPOT_X),
        static_cast<unsigned int>(constants::CURSOR_HOTSPOT_Y),
    };
    auto cursor = sf::Cursor::createFromPixels(image.getPixelsPtr(), size, hotspot);
    if (!cursor.has_value()) {
        return false;
    }

    cursors_[static_cast<std::size_t>(color)][static_cast<std::size_t>(shape)] = std::move(*cursor);
    return true;
}

bool GameCursor::load(const std::filesystem::path& assets_directory)
{
    bool any_loaded = false;
    for (std::uint8_t color = 0U; color < static_cast<std::uint8_t>(CursorPlayerColor::Count); ++color) {
        for (std::uint8_t shape = 0U; shape < static_cast<std::uint8_t>(CursorShape::Count); ++shape) {
            if (load_shape_color(
                    assets_directory,
                    static_cast<CursorShape>(shape),
                    static_cast<CursorPlayerColor>(color))) {
                any_loaded = true;
            }
        }
    }

    dirty_ = true;
    return any_loaded;
}

void GameCursor::set_player_color(const CursorPlayerColor color)
{
    if (player_color_ == color) {
        return;
    }

    player_color_ = color;
    dirty_ = true;
}

void GameCursor::set_shape(const CursorShape shape)
{
    if (shape_ == shape) {
        return;
    }

    shape_ = shape;
    dirty_ = true;
}

void GameCursor::apply(sf::Window& window)
{
    if (!dirty_) {
        return;
    }

    dirty_ = false;
    const auto& color_row = cursors_[static_cast<std::size_t>(player_color_)];
    const std::optional<sf::Cursor>& cursor = color_row[static_cast<std::size_t>(shape_)];
    if (cursor.has_value()) {
        window.setMouseCursor(*cursor);
        return;
    }

    const std::optional<sf::Cursor>& fallback =
        color_row[static_cast<std::size_t>(CursorShape::Normal)];
    if (fallback.has_value()) {
        window.setMouseCursor(*fallback);
    }
}

void GameCursor::force_reapply(sf::Window& window)
{
    dirty_ = true;
    apply(window);
}

} // namespace aoa::app
