#pragma once

#include "app/game_menu.hpp"
#include "core/constants.hpp"
#include "net/lobby_wire.hpp"
#include "sim/components/match_session.hpp"

#include <SFML/System/Vector2.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
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
    Singleplayer,
    MapPatternSelect,
    BrowseGames,
    Notice,
    About,
};

enum class SingleplayerGameStyle : std::uint8_t {
    RandomGame = 0,
    Scenarios = 1,
};

enum class SingleplayerSlotKind : std::uint8_t {
    Host = 0,
    Ai = 1,
    Disabled = 2,
    Spectator = 3,
    Enabled = 4,
};

enum class BrowseFilter : std::uint8_t {
    All = 0,
    Lan = 1,
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
    CycleGameStyle,
    SelectScenario,
    SelectMapPattern,
    HighlightMapPattern,
    ConfirmMapPattern,
    MapPatternBack,
    CycleMapSize,
    CycleFogMode,
    CycleCheats,
    CycleVictoryCondition,
    CycleSlotKind,
    CycleSlotColor,
    CycleSlotTeam,
    SingleplayerStart,
    SingleplayerBack,
    OpenBrowseGames,
    BrowseBack,
    BrowseRefresh,
    CycleBrowseFilter,
    HighlightBrowseGame,
    BrowseHost,
    BrowseConnect,
    NoticeOk,
    OpenAbout,
    AboutOpenWebsite,
    AboutClose,
    CycleBlockTeamChanges,
    CycleAllowSpectators,
    ReconnectMatch,
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
    bool selected{false};
    std::uint8_t slot{0U};
    std::uint8_t extra{0U};
};

struct MenuButtonHit {
    MainMenuAction action{MainMenuAction::None};
    std::uint8_t slot{0U};
    std::uint8_t extra{0U};
};

struct MenuLabel {
    std::string text{};
    float x{0.0F};
    float y{0.0F};
    int pixel_scale{1};
    float r{constants::HUD_TEXT_R};
    float g{constants::HUD_TEXT_G};
    float b{constants::HUD_TEXT_B};
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
    bool show_pattern_preview{false};
    MenuRect pattern_preview{};
};

struct BrowseGameEntry {
    std::string label{};
    std::string address{};
    std::uint16_t port{net::constants::DEFAULT_PORT};
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
    SingleplayerGameStyle game_style{SingleplayerGameStyle::RandomGame};
    std::filesystem::path selected_scenario_path{};
    std::string selected_scenario_name{};
    std::uint8_t required_player_count{0U};
    std::array<SingleplayerSlotKind, constants::MAX_PLAYER_SLOTS> slot_kinds{};
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> slot_colors{};
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> slot_teams{};
    sim::components::FogOfWarMode fog_mode{sim::components::FogOfWarMode::Enabled};
    bool cheats_enabled{false};
    std::uint8_t map_pattern{constants::MAP_PATTERN_COMMONS_INDEX};
    std::uint8_t pattern_highlight{constants::MAP_PATTERN_COMMONS_INDEX};
    bool pattern_return_to_lobby{false};
    std::filesystem::path selected_pattern_path{};
    std::string selected_pattern_name{};
    std::string selected_pattern_payload{};
    std::string notice_message{};
    MainMenuScreen notice_return_screen{MainMenuScreen::Singleplayer};
    BrowseFilter browse_filter{BrowseFilter::All};
    std::uint8_t browse_highlight{0U};
    std::vector<BrowseGameEntry> browse_games{};
    bool reconnect_available{false};
    std::string reconnect_address{};
    std::uint16_t reconnect_port{net::constants::DEFAULT_PORT};
};

[[nodiscard]] bool player_name_is_acceptable(const std::string& player_name);

[[nodiscard]] MainMenuState make_default_main_menu_state();

[[nodiscard]] MenuLayout build_menu_layout(
    sf::Vector2u window_size,
    const MainMenuState& state,
    const net::LobbyStateMessage& lobby,
    bool is_host,
    bool local_ready,
    bool can_start);

[[nodiscard]] MenuRect lobby_row_rect(const MenuRect& panel, int row_index);

[[nodiscard]] MenuButtonHit hit_test_menu_buttons(
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

void cycle_map_size(net::LobbySettings& settings);

void cycle_civil_population_cap(net::LobbySettings& settings);

void cycle_singleplayer_game_style(MainMenuState& state);

void apply_selected_scenario_path(MainMenuState& state, const std::filesystem::path& path);

void reset_singleplayer_slots(MainMenuState& state);

void apply_required_player_count(MainMenuState& state, std::uint8_t player_count);

void cycle_singleplayer_slot_kind(MainMenuState& state, std::uint8_t slot);

void cycle_singleplayer_slot_color(MainMenuState& state, std::uint8_t slot);

void cycle_singleplayer_slot_team(MainMenuState& state, std::uint8_t slot);

void cycle_fog_mode(MainMenuState& state);

[[nodiscard]] std::uint8_t occupied_singleplayer_slots(const MainMenuState& state);

[[nodiscard]] std::string singleplayer_scenario_button_label(const MainMenuState& state);

[[nodiscard]] std::string singleplayer_pattern_button_label(const MainMenuState& state);

[[nodiscard]] std::string map_size_button_label(const net::LobbySettings& settings);

[[nodiscard]] std::string singleplayer_fog_button_label(const MainMenuState& state);

[[nodiscard]] std::string singleplayer_cheats_button_label(const MainMenuState& state);

[[nodiscard]] std::string victory_condition_button_label();

} // namespace aoa::app
