#pragma once

#include <SFML/Window/Cursor.hpp>
#include <SFML/Window/Window.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace aoa::app {

enum class CursorPlayerColor : std::uint8_t {
    Red = 0,
    Blue = 1,
    Green = 2,
    Yellow = 3,
    Purple = 4,
    Gray = 5,
    Cyan = 6,
    Pink = 7,
    Count = 8,
};

enum class CursorShape : std::uint8_t {
    Normal = 0,
    Restricted = 1,
    Blocked = 2,
    Attack = 3,
    AttackRestricted = 4,
    Target = 5,
    Question = 6,
    Exclamation = 7,
    Check = 8,
    Cross = 9,
    Rally = 10,
    Count = 11,
};

[[nodiscard]] CursorPlayerColor cursor_color_for_player_slot(std::uint8_t player_slot);

class GameCursor {
public:
    [[nodiscard]] bool load(const std::filesystem::path& assets_directory);
    void set_player_color(CursorPlayerColor color);
    void set_shape(CursorShape shape);
    void apply(sf::Window& window);
    void force_reapply(sf::Window& window);

private:
    [[nodiscard]] bool load_shape_color(
        const std::filesystem::path& assets_directory,
        CursorShape shape,
        CursorPlayerColor color);

    std::array<
        std::array<std::optional<sf::Cursor>, static_cast<std::size_t>(CursorShape::Count)>,
        static_cast<std::size_t>(CursorPlayerColor::Count)>
        cursors_{};
    CursorPlayerColor player_color_{CursorPlayerColor::Red};
    CursorShape shape_{CursorShape::Normal};
    bool dirty_{true};
};

} // namespace aoa::app
