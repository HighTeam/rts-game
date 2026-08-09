#include "app/main_menu.hpp"

#include "core/constants.hpp"
#include "render/hud_overlay.hpp"

#include <cctype>

namespace aoa::app {

namespace {

constexpr float PADDING = static_cast<float>(constants::MAIN_MENU_PANEL_PADDING_PX);
constexpr float BUTTON_HEIGHT = static_cast<float>(constants::MAIN_MENU_BUTTON_HEIGHT_PX);
constexpr float BUTTON_GAP = static_cast<float>(constants::MAIN_MENU_BUTTON_GAP_PX);
constexpr float SPLIT_GAP = static_cast<float>(constants::MAIN_MENU_SPLIT_GAP_PX);
constexpr float SPLIT_THICKNESS = static_cast<float>(constants::MAIN_MENU_SPLIT_THICKNESS_PX);
constexpr float SPLIT_BLOCK = SPLIT_GAP * 2.0F + SPLIT_THICKNESS;
constexpr float ROW_HEIGHT = static_cast<float>(constants::MAIN_MENU_ROW_HEIGHT_PX);
constexpr float LABEL_GAP = static_cast<float>(constants::MAIN_MENU_LABEL_GAP_PX);
constexpr float WIDE_BUTTON_WIDTH = static_cast<float>(constants::MAIN_MENU_WIDE_BUTTON_WIDTH_PX);
constexpr float LOBBY_ROW_HEIGHT = static_cast<float>(constants::MAIN_MENU_LOBBY_ROW_HEIGHT_PX);

[[nodiscard]] float title_height()
{
    return render::HudOverlay::text_height_px(constants::MAIN_MENU_TITLE_PIXEL_SCALE);
}

[[nodiscard]] float label_height()
{
    return render::HudOverlay::text_height_px(constants::HUD_PIXEL_SCALE);
}

[[nodiscard]] MenuRect centered_panel(
    const sf::Vector2u window_size,
    const float width,
    const float height)
{
    return MenuRect{
        (static_cast<float>(window_size.x) - width) * 0.5F,
        (static_cast<float>(window_size.y) - height) * 0.5F,
        width,
        height,
    };
}

void push_title(MenuLayout& layout, const std::string& text, float& cursor_y)
{
    const float text_width = render::HudOverlay::text_width_px(
        text.size(),
        constants::MAIN_MENU_TITLE_PIXEL_SCALE);
    layout.labels.push_back(MenuLabel{
        text,
        layout.panel.x + (layout.panel.width - text_width) * 0.5F,
        cursor_y,
        constants::MAIN_MENU_TITLE_PIXEL_SCALE,
    });
    cursor_y += title_height();
}

void push_split_line(MenuLayout& layout, float& cursor_y)
{
    cursor_y += SPLIT_GAP;
    layout.split_lines.push_back(MenuRect{
        layout.panel.x + PADDING,
        cursor_y,
        layout.panel.width - PADDING * 2.0F,
        SPLIT_THICKNESS,
    });
    cursor_y += SPLIT_THICKNESS + SPLIT_GAP;
}

void push_full_width_button(
    MenuLayout& layout,
    const MainMenuAction action,
    const std::string& label,
    float& cursor_y,
    const bool disabled = false)
{
    layout.buttons.push_back(MenuButton{
        action,
        label,
        MenuRect{
            layout.panel.x + PADDING,
            cursor_y,
            layout.panel.width - PADDING * 2.0F,
            BUTTON_HEIGHT,
        },
        disabled,
    });
    cursor_y += BUTTON_HEIGHT + BUTTON_GAP;
}

void push_option_row(
    MenuLayout& layout,
    const MainMenuAction action,
    const std::string& label,
    float& cursor_y)
{
    layout.buttons.push_back(MenuButton{
        action,
        label,
        MenuRect{
            layout.panel.x + PADDING,
            cursor_y,
            layout.panel.width - PADDING * 2.0F,
            ROW_HEIGHT,
        },
        false,
    });
    cursor_y += ROW_HEIGHT + BUTTON_GAP;
}

void push_text_field(
    MenuLayout& layout,
    const MenuTextField field,
    const std::string& label,
    const std::string& value,
    float& cursor_y)
{
    layout.labels.push_back(MenuLabel{
        label,
        layout.panel.x + PADDING,
        cursor_y,
        constants::HUD_PIXEL_SCALE,
    });
    cursor_y += LABEL_GAP;
    layout.text_fields.push_back(MenuTextFieldEntry{
        field,
        label,
        value,
        MenuRect{
            layout.panel.x + PADDING,
            cursor_y,
            layout.panel.width - PADDING * 2.0F,
            ROW_HEIGHT,
        },
    });
    cursor_y += ROW_HEIGHT + BUTTON_GAP;
}

void push_bottom_buttons(MenuLayout& layout, std::vector<MenuButton> buttons)
{
    const float row_y = layout.panel.y + layout.panel.height - PADDING - BUTTON_HEIGHT;
    float right_edge = layout.panel.x + layout.panel.width - PADDING;
    for (std::size_t index = buttons.size(); index > 0U; --index) {
        MenuButton& button = buttons[index - 1U];
        button.rect = MenuRect{right_edge - WIDE_BUTTON_WIDTH, row_y, WIDE_BUTTON_WIDTH, BUTTON_HEIGHT};
        right_edge -= WIDE_BUTTON_WIDTH + BUTTON_GAP;
        layout.buttons.push_back(button);
    }
}

[[nodiscard]] std::string fog_label(const bool enabled)
{
    return enabled ? "Fog of War: Enabled" : "Fog of War: Disabled";
}

[[nodiscard]] float lobby_rows_top(const MenuRect& panel)
{
    return panel.y + PADDING + title_height() + SPLIT_BLOCK;
}

} // namespace

MainMenuState make_default_main_menu_state()
{
    MainMenuState state{};
    state.player_name = std::string(constants::MULTIPLAYER_DEFAULT_PLAYER_NAME);
    state.join_address = std::string(constants::MULTIPLAYER_DEFAULT_JOIN_ADDRESS);
    state.join_port = std::to_string(net::constants::DEFAULT_PORT);
    state.host_port = std::to_string(net::constants::DEFAULT_PORT);
    state.settings.screen = GameMenuScreen::SettingsGame;
    state.host_settings.player_count = constants::MULTIPLAYER_MIN_PLAYER_COUNT;
    state.host_settings.civil_population_map_cap =
        static_cast<std::uint8_t>(constants::CIVIL_POPULATION_MAP_CAP_OPTION_A);
    state.host_settings.fog_of_war_enabled = true;
    return state;
}

namespace {

[[nodiscard]] std::string map_name_for_player_count(const std::uint8_t player_count)
{
    return "Test Map (" + std::to_string(player_count) + "P)";
}

[[nodiscard]] MenuLayout build_main_menu_layout(const sf::Vector2u window_size)
{
    const float width = static_cast<float>(constants::MAIN_MENU_PANEL_WIDTH_PX);
    const float height = PADDING * 2.0F + title_height() + SPLIT_BLOCK * 2.0F
        + BUTTON_HEIGHT * 4.0F + BUTTON_GAP * 2.0F;
    const float margin = static_cast<float>(constants::MAIN_MENU_PANEL_MARGIN_PX);

    MenuLayout layout{};
    layout.panel = MenuRect{
        static_cast<float>(window_size.x) - width - margin,
        static_cast<float>(window_size.y) - height - margin,
        width,
        height,
    };

    float cursor_y = layout.panel.y + PADDING;
    push_title(layout, std::string(constants::MAIN_MENU_TITLE_TEXT), cursor_y);
    push_split_line(layout, cursor_y);
    push_full_width_button(layout, MainMenuAction::Singleplayer, "Singleplayer", cursor_y);
    push_full_width_button(layout, MainMenuAction::OpenMultiplayer, "Multiplayer", cursor_y);
    push_full_width_button(layout, MainMenuAction::OpenSettings, "Settings", cursor_y);
    cursor_y -= BUTTON_GAP;
    push_split_line(layout, cursor_y);
    push_full_width_button(layout, MainMenuAction::ExitGame, "Exit", cursor_y);
    return layout;
}

[[nodiscard]] MenuLayout build_multiplayer_layout(
    const sf::Vector2u window_size,
    const MainMenuState& state)
{
    const float width = static_cast<float>(constants::MAIN_MENU_DIALOG_WIDTH_PX);
    const float height = PADDING * 2.0F + title_height() + SPLIT_BLOCK * 2.0F + LABEL_GAP
        + ROW_HEIGHT + BUTTON_GAP + BUTTON_HEIGHT * 3.0F + BUTTON_GAP * 2.0F;

    MenuLayout layout{};
    layout.panel = centered_panel(window_size, width, height);

    float cursor_y = layout.panel.y + PADDING;
    push_title(layout, "Multiplayer", cursor_y);
    push_split_line(layout, cursor_y);
    push_text_field(layout, MenuTextField::PlayerName, "Player Name", state.player_name, cursor_y);
    push_full_width_button(layout, MainMenuAction::OpenHostSetup, "Host", cursor_y);
    push_full_width_button(layout, MainMenuAction::OpenConnect, "Connect", cursor_y);
    cursor_y -= BUTTON_GAP;
    push_split_line(layout, cursor_y);
    push_full_width_button(layout, MainMenuAction::MultiplayerBack, "Back", cursor_y);
    return layout;
}

[[nodiscard]] MenuLayout build_host_setup_layout(
    const sf::Vector2u window_size,
    const MainMenuState& state)
{
    const float width = static_cast<float>(constants::MAIN_MENU_DIALOG_WIDTH_PX);
    const float height = PADDING * 2.0F + title_height() + SPLIT_BLOCK * 2.0F
        + (ROW_HEIGHT + BUTTON_GAP) * 4.0F + LABEL_GAP + ROW_HEIGHT + BUTTON_GAP + BUTTON_HEIGHT;

    MenuLayout layout{};
    layout.panel = centered_panel(window_size, width, height);

    float cursor_y = layout.panel.y + PADDING;
    push_title(layout, "Host Game", cursor_y);
    push_split_line(layout, cursor_y);
    push_option_row(
        layout,
        MainMenuAction::CyclePlayerCount,
        "Max Players: " + std::to_string(state.host_settings.player_count),
        cursor_y);
    push_option_row(
        layout,
        MainMenuAction::CyclePlayerCount,
        "Map: " + map_name_for_player_count(state.host_settings.player_count),
        cursor_y);
    push_option_row(
        layout,
        MainMenuAction::CycleCivilCap,
        "Civil Population Cap: "
            + std::to_string(state.host_settings.civil_population_map_cap),
        cursor_y);
    push_option_row(
        layout,
        MainMenuAction::ToggleFog,
        fog_label(state.host_settings.fog_of_war_enabled),
        cursor_y);
    push_text_field(layout, MenuTextField::HostPort, "Port", state.host_port, cursor_y);
    cursor_y -= BUTTON_GAP;
    push_split_line(layout, cursor_y);
    push_bottom_buttons(
        layout,
        {
            MenuButton{MainMenuAction::HostSetupBack, "Back", MenuRect{}, false},
            MenuButton{MainMenuAction::CreateLobby, "Create", MenuRect{}, false},
        });
    return layout;
}

[[nodiscard]] MenuLayout build_connect_layout(
    const sf::Vector2u window_size,
    const MainMenuState& state)
{
    const float width = static_cast<float>(constants::MAIN_MENU_DIALOG_WIDTH_PX);
    const float height = PADDING * 2.0F + title_height() + SPLIT_BLOCK * 2.0F
        + (LABEL_GAP + ROW_HEIGHT + BUTTON_GAP) * 2.0F + BUTTON_HEIGHT;

    MenuLayout layout{};
    layout.panel = centered_panel(window_size, width, height);

    float cursor_y = layout.panel.y + PADDING;
    push_title(layout, "Connect", cursor_y);
    push_split_line(layout, cursor_y);
    push_text_field(layout, MenuTextField::JoinAddress, "Host Address", state.join_address, cursor_y);
    push_text_field(layout, MenuTextField::JoinPort, "Port", state.join_port, cursor_y);
    cursor_y -= BUTTON_GAP;
    push_split_line(layout, cursor_y);
    push_bottom_buttons(
        layout,
        {
            MenuButton{MainMenuAction::ConnectBack, "Back", MenuRect{}, false},
            MenuButton{MainMenuAction::ConnectStart, "Connect", MenuRect{}, false},
        });
    return layout;
}

} // namespace

MenuRect lobby_row_rect(const MenuRect& panel, const int row_index)
{
    return MenuRect{
        panel.x + PADDING,
        lobby_rows_top(panel) + static_cast<float>(row_index) * LOBBY_ROW_HEIGHT,
        panel.width - PADDING * 2.0F,
        LOBBY_ROW_HEIGHT,
    };
}

namespace {

[[nodiscard]] MenuLayout build_lobby_layout(
    const sf::Vector2u window_size,
    const net::LobbyStateMessage& lobby,
    const bool is_host,
    const bool local_ready,
    const bool can_start)
{
    const float row_count = static_cast<float>(lobby.settings.player_count);
    const float width = static_cast<float>(constants::MAIN_MENU_LOBBY_WIDTH_PX);
    const float height = PADDING * 2.0F + title_height() + SPLIT_BLOCK * 2.0F
        + row_count * LOBBY_ROW_HEIGHT + BUTTON_GAP + label_height() + BUTTON_HEIGHT;

    MenuLayout layout{};
    layout.panel = centered_panel(window_size, width, height);

    float cursor_y = layout.panel.y + PADDING;
    push_title(layout, "Lobby", cursor_y);
    push_split_line(layout, cursor_y);
    cursor_y += row_count * LOBBY_ROW_HEIGHT + BUTTON_GAP;

    layout.labels.push_back(MenuLabel{
        "Map: " + map_name_for_player_count(lobby.settings.player_count) + "  Cap: "
            + std::to_string(lobby.settings.civil_population_map_cap) + "  "
            + fog_label(lobby.settings.fog_of_war_enabled),
        layout.panel.x + PADDING,
        cursor_y,
        constants::HUD_PIXEL_SCALE,
    });
    cursor_y += label_height();
    push_split_line(layout, cursor_y);

    push_bottom_buttons(
        layout,
        {
            MenuButton{MainMenuAction::LobbyLeave, "Leave", MenuRect{}, false},
            MenuButton{
                MainMenuAction::LobbyReady,
                local_ready ? "Not Ready" : "Ready",
                MenuRect{},
                false,
            },
            MenuButton{MainMenuAction::LobbyStart, "Start", MenuRect{}, !is_host || !can_start},
        });
    return layout;
}

} // namespace

MenuLayout build_menu_layout(
    const sf::Vector2u window_size,
    const MainMenuState& state,
    const net::LobbyStateMessage& lobby,
    const bool is_host,
    const bool local_ready,
    const bool can_start)
{
    switch (state.screen) {
    case MainMenuScreen::Multiplayer:
        return build_multiplayer_layout(window_size, state);
    case MainMenuScreen::HostSetup:
        return build_host_setup_layout(window_size, state);
    case MainMenuScreen::Connect:
        return build_connect_layout(window_size, state);
    case MainMenuScreen::Lobby:
        return build_lobby_layout(window_size, lobby, is_host, local_ready, can_start);
    case MainMenuScreen::Settings:
    case MainMenuScreen::Main:
        break;
    }

    return build_main_menu_layout(window_size);
}

MainMenuAction hit_test_menu_buttons(
    const std::vector<MenuButton>& buttons,
    const float mouse_x,
    const float mouse_y)
{
    for (const MenuButton& button : buttons) {
        if (button.disabled || !button.rect.contains(mouse_x, mouse_y)) {
            continue;
        }

        return button.action;
    }

    return MainMenuAction::None;
}

MenuTextField hit_test_menu_text_fields(
    const std::vector<MenuTextFieldEntry>& text_fields,
    const float mouse_x,
    const float mouse_y)
{
    for (const MenuTextFieldEntry& entry : text_fields) {
        if (entry.rect.contains(mouse_x, mouse_y)) {
            return entry.field;
        }
    }

    return MenuTextField::None;
}

namespace {

[[nodiscard]] std::string* focused_text_value(MainMenuState& state)
{
    switch (state.focused_field) {
    case MenuTextField::PlayerName:
        return &state.player_name;
    case MenuTextField::JoinAddress:
        return &state.join_address;
    case MenuTextField::JoinPort:
        return &state.join_port;
    case MenuTextField::HostPort:
        return &state.host_port;
    case MenuTextField::None:
        break;
    }

    return nullptr;
}

} // namespace

void append_focused_text(MainMenuState& state, const char character)
{
    std::string* value = focused_text_value(state);
    if (value == nullptr) {
        return;
    }

    if (state.focused_field == MenuTextField::JoinPort
        || state.focused_field == MenuTextField::HostPort) {
        if (std::isdigit(static_cast<unsigned char>(character)) == 0
            || value->size() >= static_cast<std::size_t>(constants::MAIN_MENU_MAX_PORT_LENGTH)) {
            return;
        }

        value->push_back(character);
        return;
    }

    const std::size_t max_length = state.focused_field == MenuTextField::PlayerName
        ? static_cast<std::size_t>(constants::MAIN_MENU_MAX_NAME_LENGTH)
        : static_cast<std::size_t>(constants::MAIN_MENU_MAX_ADDRESS_LENGTH);
    if (value->size() >= max_length) {
        return;
    }

    value->push_back(character);
}

void backspace_focused_text(MainMenuState& state)
{
    std::string* value = focused_text_value(state);
    if (value == nullptr || value->empty()) {
        return;
    }

    value->pop_back();
}

void cycle_player_count(net::LobbySettings& settings)
{
    settings.player_count = settings.player_count == constants::MULTIPLAYER_MIN_PLAYER_COUNT
        ? constants::MULTIPLAYER_MAX_PLAYER_COUNT
        : constants::MULTIPLAYER_MIN_PLAYER_COUNT;
}

void cycle_civil_population_cap(net::LobbySettings& settings)
{
    if (settings.civil_population_map_cap == constants::CIVIL_POPULATION_MAP_CAP_OPTION_A) {
        settings.civil_population_map_cap =
            static_cast<std::uint8_t>(constants::CIVIL_POPULATION_MAP_CAP_OPTION_B);
        return;
    }

    if (settings.civil_population_map_cap == constants::CIVIL_POPULATION_MAP_CAP_OPTION_B) {
        settings.civil_population_map_cap =
            static_cast<std::uint8_t>(constants::CIVIL_POPULATION_MAP_CAP_OPTION_C);
        return;
    }

    settings.civil_population_map_cap =
        static_cast<std::uint8_t>(constants::CIVIL_POPULATION_MAP_CAP_OPTION_A);
}

} // namespace aoa::app
