#include "app/main_menu.hpp"

#include "core/constants.hpp"
#include "core/runtime_paths.hpp"
#include "render/hud_overlay.hpp"
#include "sim/map/map_pattern.hpp"
#include "sim/scenario/scenario_file.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>
#include <system_error>

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

void attach_pattern_preview(MenuLayout& layout, const sf::Vector2u window_size)
{
    const float size = static_cast<float>(constants::MENU_PATTERN_PREVIEW_SIZE_PX);
    const float gap = static_cast<float>(constants::MENU_PATTERN_PREVIEW_GAP_PX);
    const float window_w = static_cast<float>(window_size.x);
    float x = layout.panel.x + layout.panel.width + gap;
    if (x + size > window_w - gap) {
        x = layout.panel.x - gap - size;
    }
    if (x < gap) {
        x = std::max(gap, window_w - gap - size);
    }

    layout.show_pattern_preview = true;
    layout.pattern_preview = MenuRect{x, layout.panel.y, size, size};
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
            ROW_HEIGHT,
        },
        disabled,
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

void push_left_right_buttons(MenuLayout& layout, MenuButton left, MenuButton right)
{
    const float row_y = layout.panel.y + layout.panel.height - PADDING - BUTTON_HEIGHT;
    left.rect = MenuRect{
        layout.panel.x + PADDING,
        row_y,
        WIDE_BUTTON_WIDTH,
        BUTTON_HEIGHT,
    };
    right.rect = MenuRect{
        layout.panel.x + layout.panel.width - PADDING - WIDE_BUTTON_WIDTH,
        row_y,
        WIDE_BUTTON_WIDTH,
        BUTTON_HEIGHT,
    };
    layout.buttons.push_back(left);
    layout.buttons.push_back(right);
}

void push_wrapped_label(MenuLayout& layout, const std::string& text, float& cursor_y)
{
    const float inner_width = layout.panel.width - PADDING * 2.0F;
    int max_chars = 1;
    while (max_chars < constants::ABOUT_DESCRIPTION_WRAP_MAX_CHARS
        && render::HudOverlay::text_width_px(
               static_cast<std::size_t>(max_chars + 1),
               constants::HUD_PIXEL_SCALE)
            <= inner_width) {
        ++max_chars;
    }

    std::string line{};
    std::string word{};
    const auto flush_line = [&]() {
        if (line.empty()) {
            return;
        }

        layout.labels.push_back(MenuLabel{
            line,
            layout.panel.x + PADDING,
            cursor_y,
            constants::HUD_PIXEL_SCALE,
        });
        cursor_y += label_height() + static_cast<float>(constants::ABOUT_DESCRIPTION_LINE_GAP_PX);
        line.clear();
    };

    for (const char character : text) {
        if (character == ' ') {
            if (word.empty()) {
                continue;
            }

            const std::string candidate = line.empty() ? word : line + " " + word;
            if (static_cast<int>(candidate.size()) > max_chars && !line.empty()) {
                flush_line();
                line = word;
            }
            else {
                line = candidate;
            }
            word.clear();
            continue;
        }

        word.push_back(character);
    }

    if (!word.empty()) {
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (static_cast<int>(candidate.size()) > max_chars && !line.empty()) {
            flush_line();
            line = word;
        }
        else {
            line = candidate;
        }
    }

    flush_line();
}

[[nodiscard]] std::string fog_label(const bool enabled)
{
    return enabled ? "Fog of War: Enabled" : "Fog of War: Disabled";
}

[[nodiscard]] float lobby_rows_top(const MenuRect& panel)
{
    return panel.y + PADDING + title_height() + SPLIT_BLOCK;
}

[[nodiscard]] bool path_has_scenario_extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    for (char& character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    return extension == constants::SCENARIO_FILE_EXTENSION;
}

[[nodiscard]] std::filesystem::path first_scenario_in_directory()
{
    const std::filesystem::path directory = core::default_playable_scenarios_directory();
    std::error_code error{};
    std::filesystem::create_directories(directory, error);
    if (!std::filesystem::is_directory(directory, error)) {
        return {};
    }

    std::filesystem::path first{};
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file(error) || error) {
            continue;
        }

        const std::filesystem::path& path = entry.path();
        if (!path_has_scenario_extension(path)) {
            continue;
        }

        if (first.empty() || path.filename() < first.filename()) {
            first = path;
        }
    }

    return first;
}

} // namespace

void reset_singleplayer_slots(MainMenuState& state)
{
    state.required_player_count = 0U;
    for (int slot = 0; slot < constants::MAX_PLAYER_SLOTS; ++slot) {
        state.slot_colors[static_cast<std::size_t>(slot)] = static_cast<std::uint8_t>(slot);
        state.slot_teams[static_cast<std::size_t>(slot)] = constants::LOBBY_TEAM_NONE;
        if (slot == 0) {
            state.slot_kinds[static_cast<std::size_t>(slot)] = SingleplayerSlotKind::Host;
            continue;
        }

        state.slot_kinds[static_cast<std::size_t>(slot)] =
            slot == 1 ? SingleplayerSlotKind::Ai : SingleplayerSlotKind::Disabled;
    }
}

void apply_required_player_count(MainMenuState& state, const std::uint8_t player_count)
{
    state.required_player_count = player_count;
    for (int slot = 0; slot < constants::MAX_PLAYER_SLOTS; ++slot) {
        if (slot == 0) {
            state.slot_kinds[static_cast<std::size_t>(slot)] = SingleplayerSlotKind::Host;
            continue;
        }

        if (player_count > 0U && slot < static_cast<int>(player_count)) {
            state.slot_kinds[static_cast<std::size_t>(slot)] = SingleplayerSlotKind::Ai;
            continue;
        }

        state.slot_kinds[static_cast<std::size_t>(slot)] = SingleplayerSlotKind::Disabled;
    }
}

std::uint8_t occupied_singleplayer_slots(const MainMenuState& state)
{
    std::uint8_t count = 0U;
    for (const SingleplayerSlotKind kind : state.slot_kinds) {
        if (kind != SingleplayerSlotKind::Disabled) {
            ++count;
        }
    }

    return count;
}

void cycle_singleplayer_slot_kind(MainMenuState& state, const std::uint8_t slot)
{
    if (slot == 0U || slot >= static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)) {
        return;
    }

    if (state.required_player_count > 0U) {
        return;
    }

    const std::uint8_t occupied = occupied_singleplayer_slots(state);
    const auto& kind_ref = state.slot_kinds[slot];
    if (kind_ref == SingleplayerSlotKind::Disabled
        && occupied >= state.host_settings.pattern_max_players) {
        return;
    }

    if (kind_ref == SingleplayerSlotKind::Ai
        && occupied <= state.host_settings.pattern_min_players) {
        return;
    }

    auto& kind = state.slot_kinds[slot];
    kind = kind == SingleplayerSlotKind::Ai ? SingleplayerSlotKind::Disabled
                                           : SingleplayerSlotKind::Ai;
    if (kind != SingleplayerSlotKind::Ai) {
        return;
    }

    const std::uint8_t current = state.slot_colors[slot];
    bool current_taken = false;
    for (int other = 0; other < constants::MAX_PLAYER_SLOTS; ++other) {
        if (other == static_cast<int>(slot)
            || state.slot_kinds[static_cast<std::size_t>(other)] == SingleplayerSlotKind::Disabled) {
            continue;
        }

        if (state.slot_colors[static_cast<std::size_t>(other)] == current) {
            current_taken = true;
            break;
        }
    }

    if (!current_taken) {
        return;
    }

    cycle_singleplayer_slot_color(state, slot);
}

void cycle_singleplayer_slot_color(MainMenuState& state, const std::uint8_t slot)
{
    if (slot >= static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)) {
        return;
    }

    if (state.slot_kinds[slot] == SingleplayerSlotKind::Disabled) {
        return;
    }

    const std::uint8_t current = state.slot_colors[slot];
    for (int step = 1; step <= constants::MAX_PLAYER_SLOTS; ++step) {
        const std::uint8_t candidate = static_cast<std::uint8_t>(
            (static_cast<int>(current) + step) % constants::MAX_PLAYER_SLOTS);
        bool taken = false;
        for (int other = 0; other < constants::MAX_PLAYER_SLOTS; ++other) {
            if (other == static_cast<int>(slot)
                || state.slot_kinds[static_cast<std::size_t>(other)]
                    == SingleplayerSlotKind::Disabled) {
                continue;
            }

            if (state.slot_colors[static_cast<std::size_t>(other)] == candidate) {
                taken = true;
                break;
            }
        }

        if (!taken) {
            state.slot_colors[slot] = candidate;
            return;
        }
    }
}

void cycle_singleplayer_slot_team(MainMenuState& state, const std::uint8_t slot)
{
    if (slot >= static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)) {
        return;
    }

    if (state.slot_kinds[slot] == SingleplayerSlotKind::Disabled) {
        return;
    }

    state.slot_teams[slot] = sim::components::next_lobby_team(state.slot_teams[slot]);
}

void cycle_fog_mode(MainMenuState& state)
{
    if (state.fog_mode == sim::components::FogOfWarMode::Enabled) {
        state.fog_mode = sim::components::FogOfWarMode::Explored;
        return;
    }

    if (state.fog_mode == sim::components::FogOfWarMode::Explored) {
        state.fog_mode = sim::components::FogOfWarMode::Disabled;
        return;
    }

    state.fog_mode = sim::components::FogOfWarMode::Enabled;
}

void apply_selected_scenario_path(MainMenuState& state, const std::filesystem::path& path)
{
    state.selected_scenario_path = path;
    state.selected_scenario_name = path.empty() ? std::string{} : path.stem().string();
    state.required_player_count = 0U;
    if (path.empty()) {
        return;
    }

    const std::optional<sim::scenario::ScenarioFile> scenario = sim::scenario::load_scenario_file(path);
    if (!scenario.has_value()) {
        return;
    }

    if (!scenario->name.empty()) {
        state.selected_scenario_name = scenario->name;
    }

    if (scenario->required_player_count > 0U) {
        apply_required_player_count(state, scenario->required_player_count);
    }
}

void cycle_singleplayer_game_style(MainMenuState& state)
{
    if (state.game_style == SingleplayerGameStyle::RandomGame) {
        state.game_style = SingleplayerGameStyle::Scenarios;
        if (state.selected_scenario_path.empty()) {
            apply_selected_scenario_path(state, first_scenario_in_directory());
        }
        else {
            apply_selected_scenario_path(state, state.selected_scenario_path);
        }
        return;
    }

    state.game_style = SingleplayerGameStyle::RandomGame;
    state.required_player_count = 0U;
}

std::string singleplayer_scenario_button_label(const MainMenuState& state)
{
    if (state.selected_scenario_name.empty()) {
        return std::string(constants::SINGLEPLAYER_SELECT_SCENARIO_EMPTY_LABEL);
    }

    return std::string(constants::SINGLEPLAYER_SELECT_SCENARIO_PREFIX) + state.selected_scenario_name;
}

std::string singleplayer_pattern_button_label(const MainMenuState& state)
{
    return std::string(constants::SINGLEPLAYER_MAP_PATTERN_PREFIX)
        + sim::map::map_pattern_display_name(state.map_pattern, state.selected_pattern_name);
}

std::string map_size_button_label(const net::LobbySettings& settings)
{
    return std::string(constants::PATTERN_MAP_SIZE_PREFIX)
        + std::to_string(settings.map_width) + "x" + std::to_string(settings.map_height);
}

std::string singleplayer_fog_button_label(const MainMenuState& state)
{
    std::string_view mode_label = constants::SINGLEPLAYER_FOG_ENABLED_LABEL;
    if (state.fog_mode == sim::components::FogOfWarMode::Explored) {
        mode_label = constants::SINGLEPLAYER_FOG_EXPLORED_LABEL;
    }
    else if (state.fog_mode == sim::components::FogOfWarMode::Disabled) {
        mode_label = constants::SINGLEPLAYER_FOG_DISABLED_LABEL;
    }

    return std::string(constants::SINGLEPLAYER_FOG_PREFIX) + std::string(mode_label);
}

std::string singleplayer_cheats_button_label(const MainMenuState& state)
{
    const std::string_view value = state.cheats_enabled
        ? constants::SINGLEPLAYER_CHEATS_ENABLED_LABEL
        : constants::SINGLEPLAYER_CHEATS_DISABLED_LABEL;
    return std::string(constants::SINGLEPLAYER_CHEATS_PREFIX) + std::string(value);
}

std::string victory_condition_button_label()
{
    return std::string(constants::VICTORY_CONDITION_PREFIX)
        + std::string(constants::VICTORY_CONDITION_NORMAL_LABEL);
}

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
    state.host_settings.victory_condition =
        static_cast<std::uint8_t>(sim::components::VictoryCondition::Normal);
    reset_singleplayer_slots(state);
    state.map_pattern = constants::MAP_PATTERN_COMMONS_INDEX;
    state.pattern_highlight = constants::MAP_PATTERN_COMMONS_INDEX;
    state.host_settings.map_pattern = constants::MAP_PATTERN_COMMONS_INDEX;
    const sim::map::MapPattern default_pattern =
        sim::map::make_builtin_pattern(constants::MAP_PATTERN_COMMONS_INDEX);
    state.selected_pattern_name = default_pattern.name;
    state.selected_pattern_payload = sim::map::serialize_map_pattern(default_pattern);
    state.host_settings.pattern_name = default_pattern.name;
    state.host_settings.pattern_payload = state.selected_pattern_payload;
    state.host_settings.pattern_min_players = default_pattern.min_players;
    state.host_settings.pattern_max_players = default_pattern.max_players;
    state.host_settings.map_width = static_cast<std::int16_t>(default_pattern.map_width);
    state.host_settings.map_height = static_cast<std::int16_t>(default_pattern.map_height);
    state.host_settings.map_size_locked = default_pattern.fixed_map_size;
    const std::filesystem::path first_scenario = first_scenario_in_directory();
    state.selected_scenario_path = first_scenario;
    if (!first_scenario.empty()) {
        const std::optional<sim::scenario::ScenarioFile> scenario =
            sim::scenario::load_scenario_file(first_scenario);
        state.selected_scenario_name = scenario.has_value() && !scenario->name.empty()
            ? scenario->name
            : first_scenario.stem().string();
    }
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
        + BUTTON_HEIGHT * 5.0F + BUTTON_GAP * 3.0F;
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
    push_full_width_button(layout, MainMenuAction::OpenAbout, "About", cursor_y);
    cursor_y -= BUTTON_GAP;
    push_split_line(layout, cursor_y);
    push_full_width_button(layout, MainMenuAction::ExitGame, "Exit", cursor_y);
    return layout;
}

[[nodiscard]] bool name_blocks_multiplayer(const MainMenuState& state)
{
    return state.player_name.empty();
}

[[nodiscard]] MenuLayout build_multiplayer_layout(
    const sf::Vector2u window_size,
    const MainMenuState& state)
{
    const float width = static_cast<float>(constants::MAIN_MENU_DIALOG_WIDTH_PX);
    const float height = PADDING * 2.0F + title_height() + SPLIT_BLOCK * 4.0F + LABEL_GAP
        + ROW_HEIGHT + BUTTON_GAP + BUTTON_HEIGHT * 4.0F + BUTTON_GAP * 3.0F + label_height();

    MenuLayout layout{};
    layout.panel = centered_panel(window_size, width, height);

    float cursor_y = layout.panel.y + PADDING;
    push_title(layout, "Multiplayer", cursor_y);
    push_split_line(layout, cursor_y);
    push_text_field(layout, MenuTextField::PlayerName, "Player Name", state.player_name, cursor_y);
    push_split_line(layout, cursor_y);
    const bool name_empty = name_blocks_multiplayer(state);
    push_full_width_button(
        layout,
        MainMenuAction::OpenBrowseGames,
        std::string(constants::MULTIPLAYER_BROWSE_GAMES_LABEL),
        cursor_y,
        name_empty);
    push_split_line(layout, cursor_y);
    layout.labels.push_back(MenuLabel{
        std::string(constants::MULTIPLAYER_LAN_TCP_IP_LABEL),
        layout.panel.x + PADDING,
        cursor_y,
        constants::HUD_PIXEL_SCALE,
    });
    cursor_y += label_height() + BUTTON_GAP;
    push_full_width_button(layout, MainMenuAction::OpenHostSetup, "Host", cursor_y, name_empty);
    push_full_width_button(layout, MainMenuAction::OpenConnect, "Connect", cursor_y, name_empty);
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

[[nodiscard]] std::string game_style_button_label(const SingleplayerGameStyle style)
{
    const std::string_view style_label = style == SingleplayerGameStyle::Scenarios
        ? constants::SINGLEPLAYER_GAME_STYLE_SCENARIOS_LABEL
        : constants::SINGLEPLAYER_GAME_STYLE_RANDOM_LABEL;
    return std::string(constants::SINGLEPLAYER_GAME_STYLE_PREFIX) + std::string(style_label);
}

[[nodiscard]] std::string slot_kind_label(const SingleplayerSlotKind kind)
{
    if (kind == SingleplayerSlotKind::Host) {
        return std::string(constants::SINGLEPLAYER_SLOT_HOST_KIND_LABEL);
    }

    if (kind == SingleplayerSlotKind::Spectator) {
        return std::string(constants::SINGLEPLAYER_SLOT_SPECTATOR_LABEL);
    }

    if (kind == SingleplayerSlotKind::Ai) {
        return std::string(constants::SINGLEPLAYER_SLOT_AI_LABEL);
    }

    if (kind == SingleplayerSlotKind::Enabled) {
        return std::string(constants::SINGLEPLAYER_SLOT_ENABLED_LABEL);
    }

    return std::string(constants::SINGLEPLAYER_SLOT_DISABLED_LABEL);
}

[[nodiscard]] std::string lobby_slot_kind_label(const net::LobbySlotKind kind)
{
    if (kind == net::LobbySlotKind::Host) {
        return std::string(constants::SINGLEPLAYER_SLOT_HOST_KIND_LABEL);
    }

    if (kind == net::LobbySlotKind::Spectator) {
        return std::string(constants::SINGLEPLAYER_SLOT_SPECTATOR_LABEL);
    }

    if (kind == net::LobbySlotKind::Ai) {
        return std::string(constants::SINGLEPLAYER_SLOT_AI_LABEL);
    }

    if (kind == net::LobbySlotKind::Enabled) {
        return std::string(constants::SINGLEPLAYER_SLOT_ENABLED_LABEL);
    }

    return std::string(constants::SINGLEPLAYER_SLOT_DISABLED_LABEL);
}

[[nodiscard]] std::string color_name_for_index(const std::uint8_t color_index)
{
    if (color_index >= constants::PLAYER_COLOR_NAMES.size()) {
        return std::string(constants::PLAYER_COLOR_NAMES[0]);
    }

    return std::string(constants::PLAYER_COLOR_NAMES[color_index]);
}

[[nodiscard]] MenuLayout build_singleplayer_layout(
    const sf::Vector2u window_size,
    const MainMenuState& state)
{
    const float option_rows = static_cast<float>(constants::SINGLEPLAYER_OPTION_ROW_COUNT);
    const float width = static_cast<float>(constants::MAIN_MENU_LOBBY_WIDTH_PX);
    const float height = PADDING * 2.0F + title_height() + SPLIT_BLOCK * 3.0F
        + static_cast<float>(constants::MAX_PLAYER_SLOTS) * LOBBY_ROW_HEIGHT + BUTTON_GAP
        + (ROW_HEIGHT + BUTTON_GAP) * option_rows + BUTTON_HEIGHT;

    MenuLayout layout{};
    layout.panel = centered_panel(window_size, width, height);

    float cursor_y = layout.panel.y + PADDING;
    push_title(layout, "Singleplayer", cursor_y);
    push_split_line(layout, cursor_y);

    const float color_width = static_cast<float>(constants::SINGLEPLAYER_COLOR_BUTTON_WIDTH_PX);
    const float kind_width = static_cast<float>(constants::SINGLEPLAYER_KIND_BUTTON_WIDTH_PX);
    const float team_width = static_cast<float>(constants::SINGLEPLAYER_TEAM_BUTTON_WIDTH_PX);
    const float row_left = layout.panel.x + PADDING;
    const float row_width = layout.panel.width - PADDING * 2.0F;
    for (int slot = 0; slot < constants::MAX_PLAYER_SLOTS; ++slot) {
        const MenuRect row = lobby_row_rect(layout.panel, slot);
        const auto kind = state.slot_kinds[static_cast<std::size_t>(slot)];
        const std::string name = slot == 0
            ? ("P1 " + std::string(constants::SINGLEPLAYER_SLOT_HOST_LABEL))
            : ("P" + std::to_string(slot + 1));
        layout.labels.push_back(MenuLabel{
            name,
            row.x,
            row.y + (row.height - label_height()) * 0.5F,
            constants::HUD_PIXEL_SCALE,
        });

        const bool occupied = kind != SingleplayerSlotKind::Disabled;
        const float kind_x = row_left + row_width - kind_width;
        const float team_x = kind_x - BUTTON_GAP - team_width;
        const float color_x = team_x - BUTTON_GAP - color_width;
        MenuButton color_button{};
        color_button.action = MainMenuAction::CycleSlotColor;
        color_button.label = color_name_for_index(state.slot_colors[static_cast<std::size_t>(slot)]);
        color_button.rect = MenuRect{
            color_x,
            row.y,
            color_width,
            row.height,
        };
        color_button.disabled = !occupied;
        color_button.slot = static_cast<std::uint8_t>(slot);
        color_button.extra = state.slot_colors[static_cast<std::size_t>(slot)];
        layout.buttons.push_back(color_button);

        MenuButton team_button{};
        team_button.action = MainMenuAction::CycleSlotTeam;
        team_button.label = std::string(
            sim::components::lobby_team_label(state.slot_teams[static_cast<std::size_t>(slot)]));
        team_button.rect = MenuRect{
            team_x,
            row.y,
            team_width,
            row.height,
        };
        team_button.disabled = !occupied;
        team_button.slot = static_cast<std::uint8_t>(slot);
        layout.buttons.push_back(team_button);

        MenuButton kind_button{};
        kind_button.action = MainMenuAction::CycleSlotKind;
        kind_button.label = slot_kind_label(kind);
        kind_button.rect = MenuRect{
            row_left + row_width - kind_width,
            row.y,
            kind_width,
            row.height,
        };
        const std::uint8_t occupied_count = occupied_singleplayer_slots(state);
        const bool at_pattern_max = occupied_count >= state.host_settings.pattern_max_players;
        const bool at_pattern_min = occupied_count <= state.host_settings.pattern_min_players;
        kind_button.disabled = slot == 0
            || state.required_player_count > 0U
            || (kind == SingleplayerSlotKind::Disabled && at_pattern_max)
            || (kind == SingleplayerSlotKind::Ai && at_pattern_min);
        kind_button.slot = static_cast<std::uint8_t>(slot);
        layout.buttons.push_back(kind_button);
    }

    cursor_y += static_cast<float>(constants::MAX_PLAYER_SLOTS) * LOBBY_ROW_HEIGHT + BUTTON_GAP;
    push_split_line(layout, cursor_y);
    push_option_row(
        layout,
        MainMenuAction::CycleGameStyle,
        game_style_button_label(state.game_style),
        cursor_y);
    if (state.game_style == SingleplayerGameStyle::Scenarios) {
        push_option_row(
            layout,
            MainMenuAction::SelectScenario,
            singleplayer_scenario_button_label(state),
            cursor_y);
    }
    else {
        push_option_row(
            layout,
            MainMenuAction::SelectMapPattern,
            singleplayer_pattern_button_label(state),
            cursor_y);
        push_option_row(
            layout,
            MainMenuAction::CycleMapSize,
            map_size_button_label(state.host_settings),
            cursor_y,
            state.host_settings.map_size_locked);
    }

    push_option_row(layout, MainMenuAction::CycleFogMode, singleplayer_fog_button_label(state), cursor_y);
    push_option_row(
        layout,
        MainMenuAction::CycleCivilCap,
        std::string(constants::SINGLEPLAYER_CIVIL_CAP_PREFIX)
            + std::to_string(state.host_settings.civil_population_map_cap),
        cursor_y);
    push_option_row(
        layout,
        MainMenuAction::CycleCheats,
        singleplayer_cheats_button_label(state),
        cursor_y);
    push_option_row(
        layout,
        MainMenuAction::CycleVictoryCondition,
        victory_condition_button_label(),
        cursor_y,
        true);

    cursor_y -= BUTTON_GAP;
    push_split_line(layout, cursor_y);
    const bool start_disabled = state.game_style == SingleplayerGameStyle::Scenarios
        && state.selected_scenario_path.empty();
    push_bottom_buttons(
        layout,
        {
            MenuButton{MainMenuAction::SingleplayerBack, "Back", MenuRect{}, false},
            MenuButton{MainMenuAction::SingleplayerStart, "Start", MenuRect{}, start_disabled},
        });
    if (state.game_style == SingleplayerGameStyle::RandomGame) {
        attach_pattern_preview(layout, window_size);
    }
    return layout;
}

[[nodiscard]] MenuLayout build_pattern_picker_layout(
    const sf::Vector2u window_size,
    const MainMenuState& state)
{
    const float width = static_cast<float>(constants::MAIN_MENU_DIALOG_WIDTH_PX);
    const float height = PADDING * 2.0F + title_height() + SPLIT_BLOCK * 2.0F
        + (BUTTON_HEIGHT + BUTTON_GAP) * static_cast<float>(constants::MAP_PATTERN_COUNT)
        + BUTTON_HEIGHT;

    MenuLayout layout{};
    layout.panel = centered_panel(window_size, width, height);

    float cursor_y = layout.panel.y + PADDING;
    push_title(layout, std::string(constants::SINGLEPLAYER_PATTERN_PICKER_TITLE), cursor_y);
    push_split_line(layout, cursor_y);
    for (int index = 0; index < constants::MAP_PATTERN_COUNT; ++index) {
        MenuButton button{};
        button.action = MainMenuAction::HighlightMapPattern;
        button.label = std::string(constants::MAP_PATTERN_NAMES[static_cast<std::size_t>(index)]);
        button.rect = MenuRect{
            layout.panel.x + PADDING,
            cursor_y,
            layout.panel.width - PADDING * 2.0F,
            BUTTON_HEIGHT,
        };
        button.selected = state.pattern_highlight == static_cast<std::uint8_t>(index);
        button.extra = static_cast<std::uint8_t>(index);
        layout.buttons.push_back(button);
        cursor_y += BUTTON_HEIGHT + BUTTON_GAP;
    }

    cursor_y -= BUTTON_GAP;
    push_split_line(layout, cursor_y);
    push_bottom_buttons(
        layout,
        {
            MenuButton{MainMenuAction::ConfirmMapPattern, "Select", MenuRect{}, false},
            MenuButton{MainMenuAction::MapPatternBack, "Back", MenuRect{}, false},
        });
    attach_pattern_preview(layout, window_size);
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
    const MainMenuState& state,
    const net::LobbyStateMessage& lobby,
    const bool is_host,
    const bool local_ready,
    const bool can_start)
{
    const float option_rows = static_cast<float>(constants::SINGLEPLAYER_OPTION_ROW_COUNT);
    const float width = static_cast<float>(constants::MAIN_MENU_LOBBY_WIDTH_PX);
    const float height = PADDING * 2.0F + title_height() + SPLIT_BLOCK * 3.0F
        + static_cast<float>(constants::MAX_PLAYER_SLOTS) * LOBBY_ROW_HEIGHT + BUTTON_GAP
        + (ROW_HEIGHT + BUTTON_GAP) * option_rows + BUTTON_HEIGHT;

    MenuLayout layout{};
    layout.panel = centered_panel(window_size, width, height);

    float cursor_y = layout.panel.y + PADDING;
    push_title(layout, "Lobby", cursor_y);
    push_split_line(layout, cursor_y);

    const float color_width = static_cast<float>(constants::SINGLEPLAYER_COLOR_BUTTON_WIDTH_PX);
    const float kind_width = static_cast<float>(constants::SINGLEPLAYER_KIND_BUTTON_WIDTH_PX);
    const float team_width = static_cast<float>(constants::SINGLEPLAYER_TEAM_BUTTON_WIDTH_PX);
    const float row_left = layout.panel.x + PADDING;
    const float row_width = layout.panel.width - PADDING * 2.0F;
    const bool settings_locked = !is_host;
    for (int slot = 0; slot < constants::MAX_PLAYER_SLOTS; ++slot) {
        const MenuRect row = lobby_row_rect(layout.panel, slot);
        const net::LobbySlotInfo& info = lobby.slots[static_cast<std::size_t>(slot)];
        std::string name = "P" + std::to_string(slot + 1);
        if (slot == 0) {
            name += " " + std::string(constants::SINGLEPLAYER_SLOT_HOST_LABEL);
        }
        else if (info.occupied && !info.name.empty()) {
            name += " " + info.name;
        }

        const float label_y = row.y + (row.height - label_height()) * 0.5F;
        const std::uint8_t color_index =
            info.color < constants::PLAYER_SLOT_COLOR_RGB.size() ? info.color : 0U;
        const auto& name_rgb = constants::PLAYER_SLOT_COLOR_RGB[color_index];
        layout.labels.push_back(MenuLabel{
            name,
            row.x,
            label_y,
            constants::HUD_PIXEL_SCALE,
            name_rgb[0],
            name_rgb[1],
            name_rgb[2],
        });

        const float kind_x = row_left + row_width - kind_width;
        const float team_x = kind_x - BUTTON_GAP - team_width;
        const float color_x = team_x - BUTTON_GAP - color_width;
        const float ready_width = static_cast<float>(constants::MAIN_MENU_LOBBY_READY_WIDTH_PX);
        const float ready_x = color_x - BUTTON_GAP - ready_width;
        const float ping_x =
            ready_x - BUTTON_GAP - static_cast<float>(constants::MAIN_MENU_LOBBY_PING_WIDTH_PX);
        const bool human_occupied =
            info.occupied && info.kind != net::LobbySlotKind::Ai;
        if (human_occupied && slot != 0) {
            layout.labels.push_back(MenuLabel{
                std::to_string(info.ping_ms) + "ms",
                ping_x,
                label_y,
                constants::HUD_PIXEL_SCALE,
            });
        }

        if (human_occupied) {
            const bool ready = info.ready;
            layout.labels.push_back(MenuLabel{
                ready ? "READY" : "NOT READY",
                ready_x,
                label_y,
                constants::HUD_PIXEL_SCALE,
                ready ? constants::MAIN_MENU_READY_R : constants::HUD_UNAFFORDABLE_R,
                ready ? constants::MAIN_MENU_READY_G : constants::HUD_UNAFFORDABLE_G,
                ready ? constants::MAIN_MENU_READY_B : constants::HUD_UNAFFORDABLE_B,
            });
        }

        const bool color_enabled = info.kind != net::LobbySlotKind::Disabled
            && (is_host
                ? (slot == 0 || info.kind == net::LobbySlotKind::Ai
                    || !info.occupied)
                : static_cast<std::uint8_t>(slot) == lobby.recipient_slot);
        MenuButton color_button{};
        color_button.action = MainMenuAction::CycleSlotColor;
        color_button.label = color_name_for_index(info.color);
        color_button.rect = MenuRect{
            color_x,
            row.y,
            color_width,
            row.height,
        };
        color_button.disabled = !color_enabled;
        color_button.slot = static_cast<std::uint8_t>(slot);
        color_button.extra = info.color;
        layout.buttons.push_back(color_button);

        MenuButton team_button{};
        team_button.action = MainMenuAction::CycleSlotTeam;
        team_button.label = std::string(sim::components::lobby_team_label(info.team));
        team_button.rect = MenuRect{
            team_x,
            row.y,
            team_width,
            row.height,
        };
        team_button.disabled = !color_enabled;
        team_button.slot = static_cast<std::uint8_t>(slot);
        layout.buttons.push_back(team_button);

        MenuButton kind_button{};
        kind_button.action = MainMenuAction::CycleSlotKind;
        kind_button.label = lobby_slot_kind_label(info.kind);
        kind_button.rect = MenuRect{
            kind_x,
            row.y,
            kind_width,
            row.height,
        };
        const std::uint8_t playing = net::lobby_playing_slot_count(lobby);
        const bool at_pattern_max = playing >= lobby.settings.pattern_max_players;
        const bool at_pattern_min = playing <= lobby.settings.pattern_min_players;
        kind_button.disabled = !is_host
            || (slot != 0 && lobby.settings.required_player_count > 0U)
            || (info.kind == net::LobbySlotKind::Disabled && at_pattern_max)
            || (info.kind == net::LobbySlotKind::Ai && at_pattern_min);
        kind_button.slot = static_cast<std::uint8_t>(slot);
        layout.buttons.push_back(kind_button);
    }

    cursor_y += static_cast<float>(constants::MAX_PLAYER_SLOTS) * LOBBY_ROW_HEIGHT + BUTTON_GAP;
    push_split_line(layout, cursor_y);

    const bool scenarios = lobby.settings.game_style != 0U
        || state.game_style == SingleplayerGameStyle::Scenarios;
    push_option_row(
        layout,
        MainMenuAction::CycleGameStyle,
        game_style_button_label(
            scenarios ? SingleplayerGameStyle::Scenarios : SingleplayerGameStyle::RandomGame),
        cursor_y,
        settings_locked);
    if (scenarios) {
        const std::string scenario_label = lobby.settings.scenario_name.empty()
            ? std::string(constants::SINGLEPLAYER_SELECT_SCENARIO_EMPTY_LABEL)
            : (std::string(constants::SINGLEPLAYER_SELECT_SCENARIO_PREFIX)
                + lobby.settings.scenario_name);
        push_option_row(
            layout,
            MainMenuAction::SelectScenario,
            scenario_label,
            cursor_y,
            settings_locked);
    }
    else {
        push_option_row(
            layout,
            MainMenuAction::SelectMapPattern,
            singleplayer_pattern_button_label(state),
            cursor_y,
            settings_locked);
        push_option_row(
            layout,
            MainMenuAction::CycleMapSize,
            map_size_button_label(lobby.settings),
            cursor_y,
            settings_locked || lobby.settings.map_size_locked);
    }

    push_option_row(
        layout,
        MainMenuAction::CycleFogMode,
        singleplayer_fog_button_label(state),
        cursor_y,
        settings_locked);
    push_option_row(
        layout,
        MainMenuAction::CycleCivilCap,
        std::string(constants::SINGLEPLAYER_CIVIL_CAP_PREFIX)
            + std::to_string(lobby.settings.civil_population_map_cap),
        cursor_y,
        settings_locked);
    push_option_row(
        layout,
        MainMenuAction::CycleCheats,
        singleplayer_cheats_button_label(state),
        cursor_y,
        settings_locked);
    push_option_row(
        layout,
        MainMenuAction::CycleVictoryCondition,
        victory_condition_button_label(),
        cursor_y,
        true);

    cursor_y -= BUTTON_GAP;
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
    attach_pattern_preview(layout, window_size);
    return layout;
}

[[nodiscard]] MenuLayout build_browse_layout(
    const sf::Vector2u window_size,
    const MainMenuState& state)
{
    const float list_rows = static_cast<float>(constants::MULTIPLAYER_BROWSE_LIST_ROWS);
    const float width = static_cast<float>(constants::MAIN_MENU_LOBBY_WIDTH_PX);
    const float height = PADDING * 2.0F + title_height() + SPLIT_BLOCK * 2.0F
        + (ROW_HEIGHT + BUTTON_GAP) * 2.0F + (BUTTON_HEIGHT + BUTTON_GAP)
        + list_rows * (BUTTON_HEIGHT + BUTTON_GAP) + BUTTON_HEIGHT;

    MenuLayout layout{};
    layout.panel = centered_panel(window_size, width, height);

    float cursor_y = layout.panel.y + PADDING;
    push_title(layout, std::string(constants::MULTIPLAYER_BROWSE_TITLE), cursor_y);
    push_split_line(layout, cursor_y);
    const std::string filter_label = state.browse_filter == BrowseFilter::Lan
        ? std::string(constants::MULTIPLAYER_FILTER_LAN_LABEL)
        : std::string(constants::MULTIPLAYER_FILTER_ALL_LABEL);
    push_option_row(layout, MainMenuAction::CycleBrowseFilter, filter_label, cursor_y);
    push_option_row(
        layout,
        MainMenuAction::None,
        std::string(constants::MULTIPLAYER_FILTER_PUBLIC_LABEL),
        cursor_y,
        true);
    push_full_width_button(
        layout,
        MainMenuAction::BrowseRefresh,
        std::string(constants::MULTIPLAYER_REFRESH_LABEL),
        cursor_y);

    if (state.browse_games.empty()) {
        layout.labels.push_back(MenuLabel{
            std::string(constants::MULTIPLAYER_NO_GAMES_LABEL),
            layout.panel.x + PADDING,
            cursor_y,
            constants::HUD_PIXEL_SCALE,
        });
        cursor_y += list_rows * (BUTTON_HEIGHT + BUTTON_GAP);
    }
    else {
        const int visible = std::min(
            static_cast<int>(state.browse_games.size()),
            constants::MULTIPLAYER_BROWSE_LIST_ROWS);
        for (int index = 0; index < visible; ++index) {
            MenuButton button{};
            button.action = MainMenuAction::HighlightBrowseGame;
            button.label = state.browse_games[static_cast<std::size_t>(index)].label;
            button.rect = MenuRect{
                layout.panel.x + PADDING,
                cursor_y,
                layout.panel.width - PADDING * 2.0F,
                BUTTON_HEIGHT,
            };
            button.selected = state.browse_highlight == static_cast<std::uint8_t>(index);
            button.extra = static_cast<std::uint8_t>(index);
            layout.buttons.push_back(button);
            cursor_y += BUTTON_HEIGHT + BUTTON_GAP;
        }

        cursor_y += (list_rows - static_cast<float>(visible)) * (BUTTON_HEIGHT + BUTTON_GAP);
    }

    const float button_y = layout.panel.y + layout.panel.height - PADDING - BUTTON_HEIGHT;
    float split_y = button_y - SPLIT_GAP - SPLIT_THICKNESS;
    layout.split_lines.push_back(MenuRect{
        layout.panel.x + PADDING,
        split_y,
        layout.panel.width - PADDING * 2.0F,
        SPLIT_THICKNESS,
    });
    const bool name_empty = name_blocks_multiplayer(state);
    push_bottom_buttons(
        layout,
        {
            MenuButton{MainMenuAction::BrowseBack, "Back", MenuRect{}, false},
            MenuButton{MainMenuAction::BrowseHost, "Host", MenuRect{}, name_empty},
            MenuButton{MainMenuAction::BrowseConnect, "Connect", MenuRect{}, name_empty},
        });
    return layout;
}

[[nodiscard]] MenuLayout build_notice_layout(
    const sf::Vector2u window_size,
    const MainMenuState& state)
{
    const float width = static_cast<float>(constants::MAIN_MENU_DIALOG_WIDTH_PX);
    const float height = PADDING * 2.0F + title_height() + SPLIT_BLOCK + ROW_HEIGHT * 3.0F
        + BUTTON_HEIGHT;
    MenuLayout layout{};
    layout.panel = centered_panel(window_size, width, height);
    float cursor_y = layout.panel.y + PADDING;
    push_title(layout, "Map Pattern", cursor_y);
    push_split_line(layout, cursor_y);
    layout.labels.push_back(MenuLabel{
        state.notice_message,
        layout.panel.x + PADDING,
        cursor_y,
        constants::HUD_PIXEL_SCALE,
    });
    cursor_y += ROW_HEIGHT * 2.0F;
    push_split_line(layout, cursor_y);
    push_bottom_buttons(
        layout,
        {MenuButton{MainMenuAction::NoticeOk, "OK", MenuRect{}, false}});
    return layout;
}

[[nodiscard]] MenuLayout build_about_layout(const sf::Vector2u window_size)
{
    const float width = static_cast<float>(constants::MAIN_MENU_DIALOG_WIDTH_PX);
    const float height = PADDING * 2.0F + title_height() + SPLIT_BLOCK * 2.0F
        + label_height() * 8.0F
        + static_cast<float>(constants::ABOUT_DESCRIPTION_LINE_GAP_PX) * 6.0F
        + LABEL_GAP + BUTTON_HEIGHT;
    MenuLayout layout{};
    layout.panel = centered_panel(window_size, width, height);

    float cursor_y = layout.panel.y + PADDING;
    push_title(layout, "About", cursor_y);
    push_split_line(layout, cursor_y);
    layout.labels.push_back(MenuLabel{
        std::string(constants::WINDOW_TITLE),
        layout.panel.x + PADDING,
        cursor_y,
        constants::HUD_PIXEL_SCALE,
    });
    cursor_y += label_height() + LABEL_GAP * 0.5F;
    layout.labels.push_back(MenuLabel{
        "Version: " + std::string(constants::GAME_VERSION),
        layout.panel.x + PADDING,
        cursor_y,
        constants::HUD_PIXEL_SCALE,
    });
    cursor_y += label_height() + LABEL_GAP * 0.5F;
    push_wrapped_label(layout, std::string(constants::GAME_DESCRIPTION), cursor_y);
    push_split_line(layout, cursor_y);
    push_left_right_buttons(
        layout,
        MenuButton{
            MainMenuAction::AboutOpenWebsite,
            std::string(constants::ABOUT_WEBSITE_BUTTON_LABEL),
            MenuRect{},
            false},
        MenuButton{MainMenuAction::AboutClose, "Close", MenuRect{}, false});
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
        return build_lobby_layout(window_size, state, lobby, is_host, local_ready, can_start);
    case MainMenuScreen::Singleplayer:
        return build_singleplayer_layout(window_size, state);
    case MainMenuScreen::MapPatternSelect:
        return build_pattern_picker_layout(window_size, state);
    case MainMenuScreen::Notice:
        return build_notice_layout(window_size, state);
    case MainMenuScreen::About:
        return build_about_layout(window_size);
    case MainMenuScreen::BrowseGames:
        return build_browse_layout(window_size, state);
    case MainMenuScreen::Settings:
    case MainMenuScreen::Main:
        break;
    }

    return build_main_menu_layout(window_size);
}

MenuButtonHit hit_test_menu_buttons(
    const std::vector<MenuButton>& buttons,
    const float mouse_x,
    const float mouse_y)
{
    for (const MenuButton& button : buttons) {
        if (button.disabled || !button.rect.contains(mouse_x, mouse_y)) {
            continue;
        }

        return MenuButtonHit{button.action, button.slot, button.extra};
    }

    return MenuButtonHit{};
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
    if (settings.required_player_count > 0U) {
        return;
    }

    settings.player_count = settings.player_count == constants::MULTIPLAYER_MIN_PLAYER_COUNT
        ? constants::MULTIPLAYER_MAX_PLAYER_COUNT
        : constants::MULTIPLAYER_MIN_PLAYER_COUNT;
}

void cycle_map_size(net::LobbySettings& settings)
{
    if (settings.map_size_locked) {
        return;
    }

    const int current = static_cast<int>(settings.map_width);
    int next_size = constants::PATTERN_MAP_SIZE_PRESETS.front();
    for (std::size_t index = 0; index < constants::PATTERN_MAP_SIZE_PRESETS.size(); ++index) {
        if (constants::PATTERN_MAP_SIZE_PRESETS[index] != current) {
            continue;
        }

        const std::size_t next_index =
            (index + 1U) % constants::PATTERN_MAP_SIZE_PRESETS.size();
        next_size = constants::PATTERN_MAP_SIZE_PRESETS[next_index];
        break;
    }

    settings.map_width = static_cast<std::int16_t>(next_size);
    settings.map_height = static_cast<std::int16_t>(next_size);
}

bool player_name_is_acceptable(const std::string& player_name)
{
    return !player_name.empty();
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
