#pragma once

#include "core/constants.hpp"

#include <SFML/System/Vector2.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace aoa::app {

enum class GameMenuScreen : std::uint8_t {
    Closed = 0,
    Main,
    SettingsGame,
    SettingsAudio,
};

enum class GameMenuAction : std::uint8_t {
    None = 0,
    ToggleMenu,
    Resume,
    Save,
    Load,
    OpenSettings,
    ExitToMainMenu,
    ExitGame,
    SettingsBack,
    SettingsTabGame,
    SettingsTabAudio,
    ToggleFullscreen,
    BeginDragMaster,
    BeginDragMusic,
    BeginDragSfx,
};

enum class GameMenuSlider : std::uint8_t {
    None = 0,
    Master,
    Music,
    Sfx,
};

struct GameMenuRect {
    float x{0.0F};
    float y{0.0F};
    float width{0.0F};
    float height{0.0F};

    [[nodiscard]] bool contains(const float px, const float py) const
    {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }
};

struct GameMenuButton {
    GameMenuAction action{GameMenuAction::None};
    std::string_view label{};
    GameMenuRect rect{};
    bool disabled{false};
};

struct GameMenuState {
    GameMenuScreen screen{GameMenuScreen::Closed};
    float master_volume{constants::AUDIO_MASTER_VOLUME};
    float music_volume{constants::AUDIO_MUSIC_VOLUME};
    float sfx_volume{constants::AUDIO_SFX_VOLUME};
    GameMenuSlider dragging_slider{GameMenuSlider::None};
    bool fullscreen{false};

    [[nodiscard]] bool is_open() const
    {
        return screen != GameMenuScreen::Closed;
    }

    void open_main()
    {
        screen = GameMenuScreen::Main;
        dragging_slider = GameMenuSlider::None;
    }

    void close()
    {
        screen = GameMenuScreen::Closed;
        dragging_slider = GameMenuSlider::None;
    }

    void toggle()
    {
        if (is_open()) {
            close();
            return;
        }
        open_main();
    }
};

[[nodiscard]] inline GameMenuRect menu_button_rect(const sf::Vector2u window_size)
{
    const float width =
        static_cast<float>(window_size.x) * constants::HUD_MENU_BUTTON_WIDTH_FRACTION;
    const float height =
        static_cast<float>(window_size.y) * constants::HUD_MENU_BUTTON_HEIGHT_FRACTION;
    const float margin = static_cast<float>(constants::HUD_MENU_BUTTON_MARGIN_PX);
    return GameMenuRect{
        static_cast<float>(window_size.x) - width - margin,
        margin,
        width,
        height,
    };
}

[[nodiscard]] inline GameMenuRect game_menu_panel_rect(const sf::Vector2u window_size)
{
    const float width =
        static_cast<float>(window_size.x) * constants::HUD_GAME_MENU_WIDTH_FRACTION;
    const float height =
        static_cast<float>(window_size.y) * constants::HUD_GAME_MENU_HEIGHT_FRACTION;
    return GameMenuRect{
        (static_cast<float>(window_size.x) - width) * 0.5F,
        (static_cast<float>(window_size.y) - height) * 0.5F,
        width,
        height,
    };
}

[[nodiscard]] inline GameMenuRect settings_panel_rect(const sf::Vector2u window_size)
{
    const float width =
        static_cast<float>(window_size.x) * constants::HUD_SETTINGS_WIDTH_FRACTION;
    const float height =
        static_cast<float>(window_size.y) * constants::HUD_SETTINGS_HEIGHT_FRACTION;
    return GameMenuRect{
        (static_cast<float>(window_size.x) - width) * 0.5F,
        (static_cast<float>(window_size.y) - height) * 0.5F,
        width,
        height,
    };
}

[[nodiscard]] inline std::vector<GameMenuButton> build_main_menu_buttons(
    const sf::Vector2u window_size)
{
    const GameMenuRect panel = game_menu_panel_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    const int count = constants::HUD_GAME_MENU_BUTTON_COUNT;
    const float content_height = panel.height - padding * 2.0F;
    const float button_height =
        (content_height - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
    const float button_width = panel.width - padding * 2.0F;

    struct Entry {
        GameMenuAction action;
        std::string_view label;
        bool disabled;
    };
    const Entry entries[] = {
        {GameMenuAction::Resume, "Resume", false},
        {GameMenuAction::Save, "Save", true},
        {GameMenuAction::Load, "Load", true},
        {GameMenuAction::OpenSettings, "Settings", false},
        {GameMenuAction::ExitToMainMenu, "Exit to Main Menu", false},
        {GameMenuAction::ExitGame, "Exit the Game", false},
    };

    std::vector<GameMenuButton> buttons{};
    buttons.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        buttons.push_back(GameMenuButton{
            entries[index].action,
            entries[index].label,
            GameMenuRect{
                panel.x + padding,
                panel.y + padding + static_cast<float>(index) * (button_height + gap),
                button_width,
                button_height,
            },
            entries[index].disabled,
        });
    }
    return buttons;
}

[[nodiscard]] inline std::vector<GameMenuButton> build_settings_buttons(
    const GameMenuState& state,
    const sf::Vector2u window_size)
{
    const GameMenuRect panel = settings_panel_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    const float tab_height = constants::HUD_SETTINGS_TAB_HEIGHT_PX;
    const float tab_width = (panel.width - padding * 2.0F - gap) * 0.5F;

    std::vector<GameMenuButton> buttons{};
    buttons.push_back(GameMenuButton{
        GameMenuAction::SettingsTabGame,
        "Game",
        GameMenuRect{panel.x + padding, panel.y + padding, tab_width, tab_height},
        false,
    });
    buttons.push_back(GameMenuButton{
        GameMenuAction::SettingsTabAudio,
        "Audio",
        GameMenuRect{
            panel.x + padding + tab_width + gap,
            panel.y + padding,
            tab_width,
            tab_height,
        },
        false,
    });

    if (state.screen == GameMenuScreen::SettingsGame) {
        const float row_y = panel.y + padding + tab_height + gap * 2.0F
            + constants::HUD_SETTINGS_LABEL_GAP_PX;
        buttons.push_back(GameMenuButton{
            GameMenuAction::ToggleFullscreen,
            state.fullscreen ? "Fullscreen: Fullscreen" : "Fullscreen: Windowed",
            GameMenuRect{
                panel.x + padding,
                row_y,
                panel.width - padding * 2.0F,
                constants::HUD_SETTINGS_ROW_HEIGHT_PX,
            },
            false,
        });
    }

    const float back_width = constants::HUD_SETTINGS_BACK_WIDTH_PX;
    const float back_height = constants::HUD_SETTINGS_BACK_HEIGHT_PX;
    buttons.push_back(GameMenuButton{
        GameMenuAction::SettingsBack,
        "Back",
        GameMenuRect{
            panel.x + panel.width - padding - back_width,
            panel.y + panel.height - padding - back_height,
            back_width,
            back_height,
        },
        false,
    });
    return buttons;
}

[[nodiscard]] inline GameMenuRect volume_slider_rect(
    const sf::Vector2u window_size,
    const int row_index)
{
    const GameMenuRect panel = settings_panel_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    const float tab_height = constants::HUD_SETTINGS_TAB_HEIGHT_PX;
    const float label_gap = constants::HUD_SETTINGS_LABEL_GAP_PX;
    const float row_step = constants::HUD_VOLUME_SLIDER_HEIGHT_PX + label_gap
        + constants::HUD_SETTINGS_SLIDER_ROW_EXTRA_PX;
    const float y = panel.y + padding + tab_height + gap * 2.0F + label_gap
        + static_cast<float>(row_index) * row_step;
    return GameMenuRect{
        panel.x + padding,
        y,
        panel.width - padding * 2.0F,
        constants::HUD_VOLUME_SLIDER_HEIGHT_PX,
    };
}

[[nodiscard]] inline GameMenuAction hit_test_menu_button(
    const std::vector<GameMenuButton>& buttons,
    const float mouse_x,
    const float mouse_y)
{
    for (const GameMenuButton& button : buttons) {
        if (button.disabled || !button.rect.contains(mouse_x, mouse_y)) {
            continue;
        }
        return button.action;
    }
    return GameMenuAction::None;
}

inline void apply_slider_drag(
    GameMenuState& state,
    const sf::Vector2u window_size,
    const float mouse_x)
{
    if (state.dragging_slider == GameMenuSlider::None
        || state.screen != GameMenuScreen::SettingsAudio) {
        return;
    }

    int row = 0;
    if (state.dragging_slider == GameMenuSlider::Music) {
        row = 1;
    }
    else if (state.dragging_slider == GameMenuSlider::Sfx) {
        row = 2;
    }

    const GameMenuRect slider = volume_slider_rect(window_size, row);
    if (slider.width <= 0.0F) {
        return;
    }

    const float t = std::clamp((mouse_x - slider.x) / slider.width, 0.0F, 1.0F);
    const float value = constants::AUDIO_VOLUME_MIN
        + t * (constants::AUDIO_VOLUME_MAX - constants::AUDIO_VOLUME_MIN);
    if (state.dragging_slider == GameMenuSlider::Master) {
        state.master_volume = value;
    }
    else if (state.dragging_slider == GameMenuSlider::Music) {
        state.music_volume = value;
    }
    else {
        state.sfx_volume = value;
    }
}

[[nodiscard]] inline GameMenuSlider hit_test_volume_slider(
    const sf::Vector2u window_size,
    const float mouse_x,
    const float mouse_y)
{
    if (volume_slider_rect(window_size, 0).contains(mouse_x, mouse_y)) {
        return GameMenuSlider::Master;
    }
    if (volume_slider_rect(window_size, 1).contains(mouse_x, mouse_y)) {
        return GameMenuSlider::Music;
    }
    if (volume_slider_rect(window_size, 2).contains(mouse_x, mouse_y)) {
        return GameMenuSlider::Sfx;
    }
    return GameMenuSlider::None;
}

} // namespace aoa::app
