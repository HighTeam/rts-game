#pragma once

#include "app/game_menu.hpp"
#include "net/lobby_wire.hpp"

#include <SFML/System/Vector2.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace aoa::app {

enum class MainMenuScreen : std::uint8_t {
    Main = 0,
    Settings,
    Multiplayer,
    HostSetup,
    Connect,
    Lobby,
};

enum class MainMenuAction : std::uint8_t {
    None = 0,
    Singleplayer,
    OpenMultiplayer,
    OpenSettings,
    ExitGame,
    MultiplayerBack,
    OpenHostSetup,
    OpenConnect,
    CyclePlayerCount,
    CycleCivilCap,
    ToggleFog,
    CreateLobby,
    HostSetupBack,
    ConnectStart,
    ConnectBack,
    LobbyLeave,
    LobbyReady,
    LobbyStart,
};

enum class MenuTextField : std::uint8_t {
    None = 0,
    PlayerName,
    JoinAddress,
    JoinPort,
    HostPort,
};

using MenuRect = GameMenuRect;

struct MenuButton {
    MainMenuAction action{MainMenuAction::None};
    std::string label{};
    MenuRect rect{};
    bool disabled{false};
};

struct MenuLabel {
    std::string text{};
    float x{0.0F};
    float y{0.0F};
    int pixel_scale{1};
};

struct MenuTextFieldEntry {
    MenuTextField field{MenuTextField::None};
    std::string label{};
    std::string value{};
    MenuRect rect{};
};

struct MenuLayout {
    MenuRect panel{};
    std::vector<MenuLabel> labels{};
    std::vector<MenuRect> split_lines{};
    std::vector<MenuButton> buttons{};
    std::vector<MenuTextFieldEntry> text_fields{};
};

struct MainMenuState {
    MainMenuScreen screen{MainMenuScreen::Main};
    // Game/Audio tabs are shared with the in-game menu layout.
    GameMenuState settings{};
    MenuTextField focused_field{MenuTextField::None};
    std::string player_name{};
    std::string join_address{};
    std::string join_port{};
    std::string host_port{};
    // Port the active lobby is hosted on / was joined through.
    std::uint16_t active_port{net::constants::DEFAULT_PORT};
    net::LobbySettings host_settings{};
    std::string status_message{};
};

[[nodiscard]] MainMenuState make_default_main_menu_state();

[[nodiscard]] MenuLayout build_menu_layout(
    sf::Vector2u window_size,
    const MainMenuState& state,
    const net::LobbyStateMessage& lobby,
    bool is_host,
    bool local_ready,
    bool can_start);

[[nodiscard]] MenuRect lobby_row_rect(const MenuRect& panel, int row_index);

[[nodiscard]] MainMenuAction hit_test_menu_buttons(
    const std::vector<MenuButton>& buttons,
    float mouse_x,
    float mouse_y);

[[nodiscard]] MenuTextField hit_test_menu_text_fields(
    const std::vector<MenuTextFieldEntry>& text_fields,
    float mouse_x,
    float mouse_y);

void append_focused_text(MainMenuState& state, char character);

void backspace_focused_text(MainMenuState& state);

void cycle_player_count(net::LobbySettings& settings);

void cycle_civil_population_cap(net::LobbySettings& settings);

} // namespace aoa::app
