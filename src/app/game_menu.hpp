#pragma once

#include "core/constants.hpp"

#include <SFML/System/Vector2.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aoa::app {

enum class GameMenuScreen : std::uint8_t {
    Closed = 0,
    Main,
    SettingsGame,
    SettingsVideo,
    SettingsAudio,
    Save,
    Load,
    ConfirmOverwrite,
    ConfirmLoad,
    ConfirmResign,
    ConfirmLeave,
    ErrorMissingSave,
};

enum class GameMenuAction : std::uint8_t {
    None = 0,
    ToggleMenu,
    Resume,
    Pause,
    Save,
    Load,
    OpenSettings,
    Resign,
    ExitToMainMenu,
    ExitGame,
    SettingsBack,
    SettingsTabGame,
    SettingsTabVideo,
    SettingsTabAudio,
    ToggleFullscreen,
    ToggleMouseCapture,
    ToggleVsync,
    CycleFps,
    CycleBuildingRangeDisplay,
    CycleHudStyle,
    BeginDragMaster,
    BeginDragMusic,
    BeginDragSfx,
    BeginDragScrollSpeed,
    SaveLoadConfirm,
    SaveLoadBack,
    SaveLoadFocusFilename,
    DialogYes,
    DialogCancel,
    DialogOk,
};

enum class GameMenuSlider : std::uint8_t {
    None = 0,
    Master,
    Music,
    Sfx,
    ScrollSpeed,
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
    std::string label{};
    GameMenuRect rect{};
    bool disabled{false};

    GameMenuButton() = default;
    GameMenuButton(
        const GameMenuAction action_in,
        const std::string_view label_in,
        const GameMenuRect rect_in,
        const bool disabled_in)
        : action(action_in)
        , label(label_in)
        , rect(rect_in)
        , disabled(disabled_in)
    {
    }
};

struct GameMenuState {
    GameMenuScreen screen{GameMenuScreen::Closed};
    float master_volume{constants::AUDIO_MASTER_VOLUME};
    float music_volume{constants::AUDIO_MUSIC_VOLUME};
    float sfx_volume{constants::AUDIO_SFX_VOLUME};
    float scroll_speed{constants::CAMERA_SCROLL_SPEED_DEFAULT};
    constants::BuildingRangeDisplayMode building_range_display{
        constants::BuildingRangeDisplayMode::Never};
    constants::HudStyle hud_style{constants::HudStyle::Default};
    bool center_settings_panel{false};
    GameMenuSlider dragging_slider{GameMenuSlider::None};
    bool fullscreen{false};
    bool mouse_capture{constants::RENDER_MOUSE_CAPTURE_DEFAULT};
    bool vsync{constants::RENDER_VERTICAL_SYNC};
    int fps_limit{constants::TARGET_DISPLAY_FPS};
    bool multiplayer{false};
    std::string filename_draft{};
    bool filename_focused{false};
    std::vector<std::string> save_entries{};
    int selected_save_index{-1};
    int save_list_scroll{0};
    GameMenuScreen dialog_return_screen{GameMenuScreen::Main};

    [[nodiscard]] bool is_open() const
    {
        return screen != GameMenuScreen::Closed;
    }

    [[nodiscard]] bool is_save_load_screen() const
    {
        return screen == GameMenuScreen::Save || screen == GameMenuScreen::Load;
    }

    [[nodiscard]] bool is_dialog_screen() const
    {
        return screen == GameMenuScreen::ConfirmOverwrite
            || screen == GameMenuScreen::ConfirmLoad
            || screen == GameMenuScreen::ConfirmResign
            || screen == GameMenuScreen::ConfirmLeave
            || screen == GameMenuScreen::ErrorMissingSave;
    }

    [[nodiscard]] bool is_settings_screen() const
    {
        return screen == GameMenuScreen::SettingsGame
            || screen == GameMenuScreen::SettingsVideo
            || screen == GameMenuScreen::SettingsAudio;
    }

    void open_main()
    {
        screen = GameMenuScreen::Main;
        dragging_slider = GameMenuSlider::None;
        filename_focused = false;
    }

    void close()
    {
        screen = GameMenuScreen::Closed;
        dragging_slider = GameMenuSlider::None;
        filename_focused = false;
        selected_save_index = -1;
        save_list_scroll = 0;
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

[[nodiscard]] inline GameMenuRect game_menu_rail_rect(const sf::Vector2u window_size)
{
    const float width =
        static_cast<float>(window_size.x) * constants::HUD_GAME_MENU_WIDTH_FRACTION;
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    const int count = constants::HUD_GAME_MENU_BUTTON_COUNT;
    const float height = padding * 2.0F
        + constants::HUD_GAME_MENU_BUTTON_HEIGHT_PX * static_cast<float>(count)
        + gap * static_cast<float>(count - 1);
    const float margin = constants::HUD_GAME_MENU_RAIL_MARGIN_PX;
    return GameMenuRect{
        static_cast<float>(window_size.x) - width - margin,
        margin,
        width,
        height,
    };
}

[[nodiscard]] inline GameMenuRect game_menu_panel_rect(const sf::Vector2u window_size)
{
    return game_menu_rail_rect(window_size);
}

[[nodiscard]] inline GameMenuRect settings_panel_rect(
    const sf::Vector2u window_size,
    const bool centered = false)
{
    const float width =
        static_cast<float>(window_size.x) * constants::HUD_SETTINGS_WIDTH_FRACTION;
    const float height =
        static_cast<float>(window_size.y) * constants::HUD_SETTINGS_HEIGHT_FRACTION;
    if (centered) {
        return GameMenuRect{
            (static_cast<float>(window_size.x) - width) * 0.5F,
            (static_cast<float>(window_size.y) - height) * 0.5F,
            width,
            height,
        };
    }

    const GameMenuRect rail = game_menu_rail_rect(window_size);
    return GameMenuRect{
        rail.x - constants::HUD_GAME_MENU_SIDE_GAP_PX - width,
        rail.y,
        width,
        height,
    };
}

[[nodiscard]] inline GameMenuRect save_load_panel_rect(const sf::Vector2u window_size)
{
    const GameMenuRect rail = game_menu_rail_rect(window_size);
    const float width =
        static_cast<float>(window_size.x) * constants::HUD_SAVE_LOAD_WIDTH_FRACTION;
    const float height =
        static_cast<float>(window_size.y) * constants::HUD_SAVE_LOAD_HEIGHT_FRACTION;
    return GameMenuRect{
        rail.x - constants::HUD_GAME_MENU_SIDE_GAP_PX - width,
        rail.y,
        width,
        height,
    };
}

[[nodiscard]] inline GameMenuRect confirm_panel_rect(const sf::Vector2u window_size)
{
    const float width =
        static_cast<float>(window_size.x) * constants::HUD_SAVE_CONFIRM_WIDTH_FRACTION;
    const float height =
        static_cast<float>(window_size.y) * constants::HUD_SAVE_CONFIRM_HEIGHT_FRACTION;
    return GameMenuRect{
        (static_cast<float>(window_size.x) - width) * 0.5F,
        (static_cast<float>(window_size.y) - height) * 0.5F,
        width,
        height,
    };
}

[[nodiscard]] inline GameMenuRect save_load_filename_rect(const sf::Vector2u window_size)
{
    const GameMenuRect panel = save_load_panel_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    return GameMenuRect{
        panel.x + padding,
        panel.y + padding,
        panel.width - padding * 2.0F,
        constants::HUD_SAVE_LOAD_INPUT_HEIGHT_PX,
    };
}

[[nodiscard]] inline GameMenuRect save_load_list_rect(const sf::Vector2u window_size)
{
    const GameMenuRect panel = save_load_panel_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    const float top = panel.y + padding + constants::HUD_SAVE_LOAD_INPUT_HEIGHT_PX + gap;
    const float bottom = panel.y + panel.height - padding
        - constants::HUD_SAVE_LOAD_ACTION_HEIGHT_PX - gap;
    return GameMenuRect{
        panel.x + padding,
        top,
        panel.width - padding * 2.0F,
        std::max(0.0F, bottom - top),
    };
}

[[nodiscard]] inline std::vector<GameMenuButton> build_main_menu_buttons(
    const sf::Vector2u window_size,
    const bool load_disabled = false,
    const bool pause_disabled = false)
{
    const GameMenuRect panel = game_menu_rail_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    const int count = constants::HUD_GAME_MENU_BUTTON_COUNT;
    const float button_height = constants::HUD_GAME_MENU_BUTTON_HEIGHT_PX;
    const float button_width = panel.width - padding * 2.0F;

    struct Entry {
        GameMenuAction action;
        std::string_view label;
        bool disabled;
    };
    const Entry entries[] = {
        {GameMenuAction::Resume, "Resume", false},
        {GameMenuAction::Pause, constants::GAME_MENU_PAUSE_LABEL, pause_disabled},
        {GameMenuAction::Save, "Save", false},
        {GameMenuAction::Load, "Load", load_disabled},
        {GameMenuAction::OpenSettings, "Settings", false},
        {GameMenuAction::Resign, constants::GAME_MENU_RESIGN_LABEL, false},
        {GameMenuAction::ExitToMainMenu, constants::GAME_MENU_EXIT_LABEL, false},
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

[[nodiscard]] inline GameMenuRect settings_tab_rect(
    const sf::Vector2u window_size,
    const int tab_index,
    const bool centered = false)
{
    const GameMenuRect panel = settings_panel_rect(window_size, centered);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    const float tab_width =
        (panel.width - padding * 2.0F
            - gap * static_cast<float>(constants::HUD_SETTINGS_TAB_COUNT - 1))
        / static_cast<float>(constants::HUD_SETTINGS_TAB_COUNT);
    return GameMenuRect{
        panel.x + padding + static_cast<float>(tab_index) * (tab_width + gap),
        panel.y + padding,
        tab_width,
        constants::HUD_SETTINGS_TAB_HEIGHT_PX,
    };
}

[[nodiscard]] inline GameMenuRect settings_option_row_rect(
    const sf::Vector2u window_size,
    const int row_index,
    const bool centered = false)
{
    const GameMenuRect panel = settings_panel_rect(window_size, centered);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    const float y = panel.y + padding + constants::HUD_SETTINGS_TAB_HEIGHT_PX + gap * 2.0F
        + constants::HUD_SETTINGS_LABEL_GAP_PX
        + static_cast<float>(row_index)
            * (constants::HUD_SETTINGS_ROW_HEIGHT_PX + gap);
    return GameMenuRect{
        panel.x + padding,
        y,
        panel.width - padding * 2.0F,
        constants::HUD_SETTINGS_ROW_HEIGHT_PX,
    };
}

[[nodiscard]] inline std::string_view video_fps_button_label(const int fps_limit)
{
    if (fps_limit == constants::VIDEO_FPS_30) {
        return "FPS: 30";
    }
    if (fps_limit == constants::VIDEO_FPS_60) {
        return "FPS: 60";
    }
    if (fps_limit == constants::VIDEO_FPS_120) {
        return "FPS: 120";
    }
    return "FPS: Unlimited";
}

[[nodiscard]] inline int next_video_fps_limit(const int fps_limit)
{
    const auto& presets = constants::VIDEO_FPS_PRESETS;
    for (std::size_t index = 0; index < presets.size(); ++index) {
        if (presets[index] == fps_limit) {
            return presets[(index + 1U) % presets.size()];
        }
    }
    return constants::TARGET_DISPLAY_FPS;
}

[[nodiscard]] inline std::string_view building_range_display_label(
    const constants::BuildingRangeDisplayMode mode)
{
    const auto index = static_cast<std::size_t>(mode);
    if (index >= constants::BUILDING_RANGE_DISPLAY_LABELS.size()) {
        return constants::BUILDING_RANGE_DISPLAY_LABELS[0];
    }

    return constants::BUILDING_RANGE_DISPLAY_LABELS[index];
}

[[nodiscard]] inline constants::BuildingRangeDisplayMode next_building_range_display(
    const constants::BuildingRangeDisplayMode mode)
{
    const auto raw = static_cast<std::uint8_t>(mode);
    return static_cast<constants::BuildingRangeDisplayMode>((raw + 1U) % 3U);
}

[[nodiscard]] inline std::string_view hud_style_button_label(const constants::HudStyle style)
{
    return style == constants::HudStyle::Default
        ? constants::HUD_STYLE_DEFAULT_LABEL
        : constants::HUD_STYLE_AOE_LABEL;
}

[[nodiscard]] inline constants::HudStyle next_hud_style(const constants::HudStyle style)
{
    return style == constants::HudStyle::Aoe
        ? constants::HudStyle::Default
        : constants::HudStyle::Aoe;
}

[[nodiscard]] inline GameMenuRect shooting_range_option_rect(
    const sf::Vector2u window_size,
    const bool centered = false);

[[nodiscard]] inline GameMenuRect hud_style_option_rect(
    const sf::Vector2u window_size,
    const bool centered = false)
{
    const GameMenuRect shooting = shooting_range_option_rect(window_size, centered);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    return GameMenuRect{
        shooting.x,
        shooting.y + shooting.height + gap,
        shooting.width,
        constants::HUD_SETTINGS_ROW_HEIGHT_PX,
    };
}

[[nodiscard]] inline std::vector<GameMenuButton> build_settings_buttons(
    const GameMenuState& state,
    const sf::Vector2u window_size)
{
    std::vector<GameMenuButton> buttons{};
    const bool centered = state.center_settings_panel;
    buttons.push_back(GameMenuButton{
        GameMenuAction::SettingsTabGame,
        "Game",
        settings_tab_rect(window_size, 0, centered),
        false,
    });
    buttons.push_back(GameMenuButton{
        GameMenuAction::SettingsTabVideo,
        "Video",
        settings_tab_rect(window_size, 1, centered),
        false,
    });
    buttons.push_back(GameMenuButton{
        GameMenuAction::SettingsTabAudio,
        "Audio",
        settings_tab_rect(window_size, 2, centered),
        false,
    });

    if (state.screen == GameMenuScreen::SettingsVideo) {
        buttons.push_back(GameMenuButton{
            GameMenuAction::ToggleFullscreen,
            state.fullscreen ? "Fullscreen: Fullscreen" : "Fullscreen: Windowed",
            settings_option_row_rect(window_size, 0, centered),
            false,
        });
        buttons.push_back(GameMenuButton{
            GameMenuAction::ToggleMouseCapture,
            state.mouse_capture
                ? constants::VIDEO_MOUSE_CAPTURE_ENABLED_LABEL
                : constants::VIDEO_MOUSE_CAPTURE_DISABLED_LABEL,
            settings_option_row_rect(window_size, 1, centered),
            state.fullscreen,
        });
        buttons.push_back(GameMenuButton{
            GameMenuAction::ToggleVsync,
            state.vsync ? "Vsync: Enabled" : "Vsync: Disabled",
            settings_option_row_rect(window_size, 2, centered),
            false,
        });
        buttons.push_back(GameMenuButton{
            GameMenuAction::CycleFps,
            video_fps_button_label(state.fps_limit),
            settings_option_row_rect(window_size, 3, centered),
            state.vsync,
        });
    }

    if (state.screen == GameMenuScreen::SettingsGame) {
        buttons.push_back(GameMenuButton{
            GameMenuAction::CycleBuildingRangeDisplay,
            building_range_display_label(state.building_range_display),
            shooting_range_option_rect(window_size, centered),
            false,
        });
        buttons.push_back(GameMenuButton{
            GameMenuAction::CycleHudStyle,
            hud_style_button_label(state.hud_style),
            hud_style_option_rect(window_size, centered),
            false,
        });
    }

    const GameMenuRect panel = settings_panel_rect(window_size, centered);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
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
    const int row_index,
    const bool centered = false)
{
    const GameMenuRect panel = settings_panel_rect(window_size, centered);
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

[[nodiscard]] inline GameMenuRect scroll_speed_slider_rect(
    const sf::Vector2u window_size,
    const bool centered = false)
{
    return volume_slider_rect(window_size, 0, centered);
}

[[nodiscard]] inline GameMenuRect shooting_range_option_rect(
    const sf::Vector2u window_size,
    const bool centered)
{
    const GameMenuRect slider = scroll_speed_slider_rect(window_size, centered);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    return GameMenuRect{
        slider.x,
        slider.y + slider.height + gap + constants::HUD_SETTINGS_LABEL_GAP_PX,
        slider.width,
        constants::HUD_SETTINGS_ROW_HEIGHT_PX,
    };
}

[[nodiscard]] inline GameMenuRect match_result_panel_rect(const sf::Vector2u window_size)
{
    const float width =
        static_cast<float>(window_size.x) * constants::HUD_MATCH_RESULT_WIDTH_FRACTION;
    const float height =
        static_cast<float>(window_size.y) * constants::HUD_MATCH_RESULT_HEIGHT_FRACTION;
    return GameMenuRect{
        (static_cast<float>(window_size.x) - width) * 0.5F,
        (static_cast<float>(window_size.y) - height) * 0.5F,
        width,
        height,
    };
}

[[nodiscard]] inline GameMenuRect match_result_exit_button_rect(const sf::Vector2u window_size)
{
    const GameMenuRect panel = match_result_panel_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float width = constants::HUD_MATCH_RESULT_EXIT_WIDTH_PX;
    const float height = constants::HUD_SETTINGS_BACK_HEIGHT_PX;
    return GameMenuRect{
        panel.x + (panel.width - width) * 0.5F,
        panel.y + panel.height - padding - height,
        width,
        height,
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
    if (state.dragging_slider == GameMenuSlider::None) {
        return;
    }

    if (state.dragging_slider == GameMenuSlider::ScrollSpeed) {
        if (state.screen != GameMenuScreen::SettingsGame) {
            return;
        }

        const GameMenuRect slider = scroll_speed_slider_rect(
            window_size, state.center_settings_panel);
        if (slider.width <= 0.0F) {
            return;
        }

        const float t = std::clamp((mouse_x - slider.x) / slider.width, 0.0F, 1.0F);
        state.scroll_speed = constants::CAMERA_SCROLL_SPEED_MIN
            + t * (constants::CAMERA_SCROLL_SPEED_MAX - constants::CAMERA_SCROLL_SPEED_MIN);
        return;
    }

    if (state.screen != GameMenuScreen::SettingsAudio) {
        return;
    }

    int row = 0;
    if (state.dragging_slider == GameMenuSlider::Music) {
        row = 1;
    }
    else if (state.dragging_slider == GameMenuSlider::Sfx) {
        row = 2;
    }

    const GameMenuRect slider = volume_slider_rect(
        window_size, row, state.center_settings_panel);
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
    const float mouse_y,
    const bool centered = false)
{
    if (volume_slider_rect(window_size, 0, centered).contains(mouse_x, mouse_y)) {
        return GameMenuSlider::Master;
    }
    if (volume_slider_rect(window_size, 1, centered).contains(mouse_x, mouse_y)) {
        return GameMenuSlider::Music;
    }
    if (volume_slider_rect(window_size, 2, centered).contains(mouse_x, mouse_y)) {
        return GameMenuSlider::Sfx;
    }
    return GameMenuSlider::None;
}

[[nodiscard]] inline std::vector<GameMenuButton> build_save_load_buttons(
    const GameMenuState& state,
    const sf::Vector2u window_size)
{
    const GameMenuRect panel = save_load_panel_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float action_w = constants::HUD_SAVE_LOAD_ACTION_WIDTH_PX;
    const float action_h = constants::HUD_SAVE_LOAD_ACTION_HEIGHT_PX;
    const float y = panel.y + panel.height - padding - action_h;

    std::vector<GameMenuButton> buttons{};
    buttons.push_back(GameMenuButton{
        GameMenuAction::SaveLoadConfirm,
        state.screen == GameMenuScreen::Load ? "Load" : "Save",
        GameMenuRect{panel.x + padding, y, action_w, action_h},
        state.filename_draft.empty(),
    });
    buttons.push_back(GameMenuButton{
        GameMenuAction::SaveLoadBack,
        "Back",
        GameMenuRect{panel.x + panel.width - padding - action_w, y, action_w, action_h},
        false,
    });
    return buttons;
}

[[nodiscard]] inline std::vector<GameMenuButton> build_dialog_buttons(
    const GameMenuState& state,
    const sf::Vector2u window_size)
{
    const GameMenuRect panel = confirm_panel_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_CONFIRM_DIALOG_PADDING_PX);
    const float action_w = constants::HUD_DIALOG_BUTTON_WIDTH_PX;
    const float action_h = constants::HUD_DIALOG_BUTTON_HEIGHT_PX;
    const float y = panel.y + panel.height - padding - action_h;

    std::vector<GameMenuButton> buttons{};
    if (state.screen == GameMenuScreen::ErrorMissingSave) {
        buttons.push_back(GameMenuButton{
            GameMenuAction::DialogOk,
            "Ok",
            GameMenuRect{
                panel.x + (panel.width - action_w) * 0.5F,
                y,
                action_w,
                action_h,
            },
            false,
        });
        return buttons;
    }

    const bool yes_no = state.screen == GameMenuScreen::ConfirmResign
        || state.screen == GameMenuScreen::ConfirmLeave;
    buttons.push_back(GameMenuButton{
        GameMenuAction::DialogYes,
        yes_no ? constants::GAME_MENU_DIALOG_YES_LABEL : "Yes",
        GameMenuRect{panel.x + padding, y, action_w, action_h},
        false,
    });
    buttons.push_back(GameMenuButton{
        GameMenuAction::DialogCancel,
        yes_no ? constants::GAME_MENU_DIALOG_NO_LABEL : "Cancel",
        GameMenuRect{panel.x + panel.width - padding - action_w, y, action_w, action_h},
        false,
    });
    return buttons;
}

[[nodiscard]] inline int hit_test_save_list_row(
    const GameMenuState& state,
    const sf::Vector2u window_size,
    const float mouse_x,
    const float mouse_y)
{
    const GameMenuRect list = save_load_list_rect(window_size);
    if (!list.contains(mouse_x, mouse_y)) {
        return -1;
    }

    const float row_h = constants::HUD_SAVE_LOAD_ROW_HEIGHT_PX;
    const int local_row = static_cast<int>((mouse_y - list.y) / row_h);
    if (local_row < 0 || local_row >= constants::HUD_SAVE_LOAD_VISIBLE_ROWS) {
        return -1;
    }

    const int index = state.save_list_scroll + local_row;
    if (index < 0 || index >= static_cast<int>(state.save_entries.size())) {
        return -1;
    }

    return index;
}

} // namespace aoa::app
