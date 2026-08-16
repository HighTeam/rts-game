#include "app/application.hpp"

#include "app/app_settings.hpp"
#include "app/fps_tracker.hpp"
#include "app/game_cursor.hpp"
#include "app/game_menu.hpp"
#include "app/main_menu.hpp"
#include "app/native_file_dialog.hpp"
#include "app/window_display.hpp"
#include "sim/map/map_generator.hpp"
#include "sim/map/map_pattern.hpp"
#include "sim/scenario/scenario_file.hpp"
#include "audio/game_audio.hpp"
#include "core/constants.hpp"
#include "core/runtime_paths.hpp"
#include "net/enet_transport.hpp"
#include "net/lan_discovery.hpp"
#include "net/lobby_session.hpp"
#include "net/net_constants.hpp"
#include "render/menu_renderer.hpp"
#include "sim/simulation.hpp"

#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Mouse.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/Window.hpp>

#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace aoa::app {

namespace {

enum class MenuOutcome : std::uint8_t {
    ExitApp = 0,
    StartSingleplayer,
    StartMatch,
};

struct MenuResult {
    MenuOutcome outcome{MenuOutcome::ExitApp};
    LockstepMatchSetup match_setup{};
    SingleplayerSetup singleplayer_setup{};
};

// Everything the menu event handlers need to mutate.
struct MenuContext {
    sf::Window& window;
    render::MenuRenderer& renderer;
    WindowDisplaySettings& display_settings;
    GameCursor& cursor;
    audio::GameAudio& audio;
    MainMenuState& state;
    AppShellSettings& shell;
    std::unique_ptr<net::LobbySession>& lobby;
    std::unique_ptr<net::LanDiscovery>& discovery;
    MenuResult& result;
    bool& running;
    std::uint8_t last_pattern_click_extra{255U};
    std::chrono::steady_clock::time_point last_pattern_click_time{};
    std::uint8_t last_browse_click_extra{255U};
    std::chrono::steady_clock::time_point last_browse_click_time{};
};

[[nodiscard]] std::uint16_t parse_port(const std::string& text)
{
    std::uint32_t value = 0U;
    for (const char character : text) {
        if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
            return net::constants::DEFAULT_PORT;
        }

        value = value * 10U + static_cast<std::uint32_t>(character - '0');
        if (value > net::constants::MAX_PORT_NUMBER) {
            return net::constants::DEFAULT_PORT;
        }
    }

    return value == 0U ? net::constants::DEFAULT_PORT : static_cast<std::uint16_t>(value);
}

void apply_menu_volumes(MenuContext& context)
{
    context.audio.set_master_volume(context.state.settings.master_volume);
    context.audio.set_music_volume(context.state.settings.music_volume);
    context.audio.set_sfx_volume(context.state.settings.sfx_volume);
    context.shell.master_volume = context.state.settings.master_volume;
    context.shell.music_volume = context.state.settings.music_volume;
    context.shell.sfx_volume = context.state.settings.sfx_volume;
}

void toggle_menu_fullscreen(MenuContext& context)
{
    const GraphicsContextResetFn reset_graphics_context =
        [&context](const sf::Vector2u size) { context.renderer.reset_graphics_context(size); };

    if (context.display_settings.fullscreen) {
        leave_fullscreen(context.window, reset_graphics_context, context.display_settings);
    }
    else {
        enter_fullscreen(context.window, reset_graphics_context, context.display_settings);
    }

    context.state.settings.fullscreen = context.display_settings.fullscreen;
    context.shell.fullscreen = context.display_settings.fullscreen;
    apply_mouse_capture(context.window, context.display_settings);
    save_app_settings(context.shell);
    context.cursor.force_reapply(context.window);
}

void stop_discovery(MenuContext& context)
{
    if (!context.discovery) {
        return;
    }

    context.discovery->stop();
    context.discovery.reset();
}

void leave_lobby(MenuContext& context)
{
    stop_discovery(context);
    if (!context.lobby) {
        return;
    }

    context.lobby->leave();
    context.lobby.reset();
}

void show_notice(
    MainMenuState& state,
    const std::string& message,
    const MainMenuScreen return_screen)
{
    state.notice_message = message;
    state.notice_return_screen = return_screen;
    state.screen = MainMenuScreen::Notice;
}

[[nodiscard]] sim::map::MapPattern current_map_pattern(const MainMenuState& state)
{
    return sim::map::resolve_map_pattern(state.map_pattern, state.selected_pattern_payload);
}

[[nodiscard]] std::uint8_t counted_singleplayer_players(const MainMenuState& state)
{
    std::uint8_t count = 0U;
    for (const SingleplayerSlotKind kind : state.slot_kinds) {
        if (kind != SingleplayerSlotKind::Disabled && kind != SingleplayerSlotKind::Spectator) {
            ++count;
        }
    }

    return count == 0U ? 1U : count;
}

void apply_builtin_pattern(MainMenuState& state, const std::uint8_t pattern_index)
{
    const sim::map::MapPattern pattern = sim::map::make_builtin_pattern(pattern_index);
    state.map_pattern = pattern_index;
    state.pattern_highlight = pattern_index;
    state.selected_pattern_path.clear();
    state.selected_pattern_name = pattern.name;
    state.selected_pattern_payload = sim::map::serialize_map_pattern(pattern);
    state.host_settings.pattern_min_players = pattern.min_players;
    state.host_settings.pattern_max_players = pattern.max_players;
    state.host_settings.map_size_locked = pattern.fixed_map_size;
    if (pattern.fixed_map_size) {
        state.host_settings.map_width = static_cast<std::int16_t>(pattern.map_width);
        state.host_settings.map_height = static_cast<std::int16_t>(pattern.map_height);
    }
    if (sim::map::map_pattern_locks_player_count(pattern)) {
        state.host_settings.player_count = pattern.fixed_player_count;
    }
}

[[nodiscard]] bool apply_pattern_file(MainMenuState& state, const std::filesystem::path& path)
{
    const std::optional<sim::map::MapPattern> pattern = sim::map::load_map_pattern_file(path);
    if (!pattern.has_value()) {
        return false;
    }

    state.map_pattern = constants::MAP_PATTERN_OTHER_INDEX;
    state.pattern_highlight = constants::MAP_PATTERN_OTHER_INDEX;
    state.selected_pattern_path = path;
    state.selected_pattern_name = path.stem().string();
    state.selected_pattern_payload = sim::map::serialize_map_pattern(*pattern);
    state.host_settings.pattern_min_players = pattern->min_players;
    state.host_settings.pattern_max_players = pattern->max_players;
    state.host_settings.map_size_locked = pattern->fixed_map_size;
    if (pattern->fixed_map_size) {
        state.host_settings.map_width = static_cast<std::int16_t>(pattern->map_width);
        state.host_settings.map_height = static_cast<std::int16_t>(pattern->map_height);
    }
    if (sim::map::map_pattern_locks_player_count(*pattern)) {
        state.host_settings.player_count = pattern->fixed_player_count;
    }

    return true;
}

void prepare_host_settings(MainMenuState& state)
{
    state.host_settings.fog_mode = static_cast<std::uint8_t>(state.fog_mode);
    state.host_settings.fog_of_war_enabled =
        state.fog_mode != sim::components::FogOfWarMode::Disabled;
    state.host_settings.cheats_enabled = state.cheats_enabled;
    state.host_settings.map_pattern = state.map_pattern;
    state.host_settings.game_style =
        state.game_style == SingleplayerGameStyle::Scenarios ? 1U : 0U;
    state.host_settings.victory_condition =
        static_cast<std::uint8_t>(sim::components::VictoryCondition::Normal);
    state.host_settings.pattern_name = state.selected_pattern_name;
    state.host_settings.pattern_payload = state.selected_pattern_payload;
    {
        const sim::map::MapPattern pattern = current_map_pattern(state);
        state.host_settings.map_size_locked = pattern.fixed_map_size;
        if (pattern.fixed_map_size) {
            state.host_settings.map_width = static_cast<std::int16_t>(pattern.map_width);
            state.host_settings.map_height = static_cast<std::int16_t>(pattern.map_height);
        }
    }
    if (state.selected_pattern_payload.empty()) {
        apply_builtin_pattern(state, state.map_pattern);
        state.host_settings.pattern_name = state.selected_pattern_name;
        state.host_settings.pattern_payload = state.selected_pattern_payload;
    }
}

[[nodiscard]] std::uint8_t counted_lobby_match_players(const net::LobbyStateMessage& lobby)
{
    std::uint8_t count = 0U;
    for (const net::LobbySlotInfo& info : lobby.slots) {
        if (info.kind == net::LobbySlotKind::Spectator) {
            continue;
        }

        if (info.kind == net::LobbySlotKind::Disabled && !info.occupied) {
            continue;
        }

        if (!info.occupied && info.kind != net::LobbySlotKind::Ai) {
            continue;
        }

        ++count;
    }

    return count;
}

[[nodiscard]] sim::map::MapPattern finalize_pattern_for_match(const MainMenuState& state)
{
    return sim::map::apply_lobby_size_to_pattern(
        current_map_pattern(state),
        state.host_settings.map_width,
        state.host_settings.map_height);
}

[[nodiscard]] std::optional<std::string> pattern_lobby_constraint_error(
    const MainMenuState& state,
    const std::uint8_t player_count)
{
    if (state.map_pattern == constants::MAP_PATTERN_OTHER_INDEX
        && state.selected_pattern_payload.empty()) {
        return std::string(constants::PATTERN_CONSTRAINT_MISSING_LABEL);
    }

    return sim::map::map_pattern_player_constraint_error(
        current_map_pattern(state),
        player_count);
}

void start_host_discovery(MenuContext& context)
{
    if (!context.lobby || !context.lobby->is_host()) {
        return;
    }

    net::DiscoveryAnnounce announce{};
    announce.game_port = context.state.active_port;
    announce.occupied = context.lobby->connected_human_count();
    announce.max_players = context.lobby->configured_slot_count();
    announce.lobby_open = true;
    announce.host_name = context.state.player_name;
    if (!context.discovery) {
        context.discovery = std::make_unique<net::LanDiscovery>();
        if (!context.discovery->start_host_announcer(announce)) {
            context.discovery.reset();
            return;
        }
    }

    context.discovery->set_announce(announce);
}

void sync_browse_games(MenuContext& context)
{
    context.state.browse_games.clear();
    if (!context.discovery) {
        return;
    }

    for (const net::DiscoveredGame& game : context.discovery->games()) {
        if (context.state.browse_filter == BrowseFilter::Lan
            && game.source != net::DiscoverySource::Lan) {
            continue;
        }

        BrowseGameEntry entry{};
        entry.address = game.address;
        entry.port = game.port;
        entry.label = game.host_name + "  "
            + std::to_string(game.occupied) + "/" + std::to_string(game.max_players)
            + "  " + game.address + ":" + std::to_string(game.port);
        context.state.browse_games.push_back(std::move(entry));
    }

    if (context.state.browse_highlight >= context.state.browse_games.size()
        && !context.state.browse_games.empty()) {
        context.state.browse_highlight = 0U;
    }
}

void sync_state_from_lobby(MainMenuState& state, const net::LobbyStateMessage& lobby)
{
    state.fog_mode = static_cast<sim::components::FogOfWarMode>(lobby.settings.fog_mode);
    state.cheats_enabled = lobby.settings.cheats_enabled;
    state.map_pattern = lobby.settings.map_pattern;
    state.game_style = lobby.settings.game_style == 0U
        ? SingleplayerGameStyle::RandomGame
        : SingleplayerGameStyle::Scenarios;
    state.host_settings = lobby.settings;
    state.selected_pattern_name = lobby.settings.pattern_name;
    state.selected_pattern_payload = lobby.settings.pattern_payload;
    if (!lobby.settings.scenario_name.empty()) {
        state.selected_scenario_name = lobby.settings.scenario_name;
    }
}

void start_hosting(MenuContext& context)
{
    if (!player_name_is_acceptable(context.state.player_name)) {
        context.state.status_message = std::string(constants::MULTIPLAYER_NAME_REQUIRED_LABEL);
        return;
    }

    context.state.active_port = parse_port(context.state.host_port);
    context.state.host_port = std::to_string(context.state.active_port);
    prepare_host_settings(context.state);
    auto lobby = std::make_unique<net::LobbySession>(net::LobbyRole::Host);
    if (!lobby->start_host(
            context.state.active_port,
            context.state.host_settings,
            context.state.player_name)) {
        context.state.status_message = "Failed to host on port "
            + std::to_string(context.state.active_port);
        return;
    }

    context.lobby = std::move(lobby);
    context.state.status_message.clear();
    context.state.focused_field = MenuTextField::None;
    context.state.screen = MainMenuScreen::Lobby;
    start_host_discovery(context);
}

void start_connecting(MenuContext& context)
{
    if (!player_name_is_acceptable(context.state.player_name)) {
        context.state.status_message = std::string(constants::MULTIPLAYER_NAME_REQUIRED_LABEL);
        return;
    }

    context.state.active_port = parse_port(context.state.join_port);
    auto lobby = std::make_unique<net::LobbySession>(net::LobbyRole::Client);
    if (!lobby->connect(
            context.state.join_address,
            context.state.active_port,
            context.state.player_name)) {
        context.state.status_message = "Failed to connect to " + context.state.join_address;
        return;
    }

    context.lobby = std::move(lobby);
    context.state.status_message = "Connecting...";
    context.state.focused_field = MenuTextField::None;
    context.state.screen = MainMenuScreen::Lobby;
}

void go_back_one_screen(MenuContext& context)
{
    context.state.focused_field = MenuTextField::None;
    context.state.status_message.clear();

    switch (context.state.screen) {
    case MainMenuScreen::Settings:
    case MainMenuScreen::Multiplayer:
    case MainMenuScreen::Singleplayer:
    case MainMenuScreen::About:
        context.state.screen = MainMenuScreen::Main;
        return;
    case MainMenuScreen::Notice:
        context.state.screen = context.state.notice_return_screen;
        return;
    case MainMenuScreen::MapPatternSelect:
        context.state.screen = context.state.pattern_return_to_lobby
            ? MainMenuScreen::Lobby
            : MainMenuScreen::Singleplayer;
        context.state.pattern_return_to_lobby = false;
        return;
    case MainMenuScreen::HostSetup:
    case MainMenuScreen::Connect:
    case MainMenuScreen::BrowseGames:
        stop_discovery(context);
        context.state.screen = MainMenuScreen::Multiplayer;
        return;
    case MainMenuScreen::Lobby:
        leave_lobby(context);
        context.state.screen = MainMenuScreen::Multiplayer;
        return;
    case MainMenuScreen::Main:
        break;
    }

    context.result.outcome = MenuOutcome::ExitApp;
    context.running = false;
}

[[nodiscard]] SingleplayerSetup make_singleplayer_setup(const MainMenuState& state)
{
    SingleplayerSetup setup{};
    setup.fog_mode = state.fog_mode;
    setup.civil_population_map_cap = static_cast<int>(state.host_settings.civil_population_map_cap);
    setup.cheats_enabled = state.cheats_enabled;
    setup.map_pattern = state.map_pattern;
    setup.map_seed = state.host_settings.map_seed;
    setup.pattern_payload = state.selected_pattern_payload;
    setup.victory_condition = static_cast<sim::components::VictoryCondition>(
        state.host_settings.victory_condition);
    setup.slot_is_ai.fill(false);

    std::uint8_t compact = 0U;
    for (int slot = 0; slot < constants::MAX_PLAYER_SLOTS; ++slot) {
        const SingleplayerSlotKind kind = state.slot_kinds[static_cast<std::size_t>(slot)];
        if (kind == SingleplayerSlotKind::Disabled) {
            continue;
        }

        setup.slot_colors[compact] = state.slot_colors[static_cast<std::size_t>(slot)];
        setup.slot_teams[compact] = state.slot_teams[static_cast<std::size_t>(slot)];
        setup.slot_is_ai[compact] = kind == SingleplayerSlotKind::Ai;
        if (kind == SingleplayerSlotKind::Host && !state.player_name.empty()) {
            setup.player_names[compact] = state.player_name;
        }
        else {
            setup.player_names[compact] = "Player " + std::to_string(compact + 1);
        }
        ++compact;
    }

    setup.player_count = compact == 0U ? 1U : compact;
    return setup;
}

void apply_pattern_player_lock(MenuContext& context, const sim::map::MapPattern& pattern)
{
    if (context.state.game_style == SingleplayerGameStyle::Scenarios) {
        return;
    }

    if (!sim::map::map_pattern_locks_player_count(pattern)) {
        context.state.required_player_count = 0U;
        if (context.lobby && context.lobby->is_host()) {
            context.lobby->clear_player_count_lock();
        }
        return;
    }

    if (context.lobby && context.lobby->is_host()) {
        if (!context.lobby->try_lock_player_count(pattern.fixed_player_count)) {
            show_notice(
                context.state,
                *sim::map::map_pattern_player_constraint_error(
                    pattern,
                    counted_lobby_match_players(context.lobby->view())),
                MainMenuScreen::Lobby);
        }
        return;
    }

    apply_required_player_count(context.state, pattern.fixed_player_count);
}

[[nodiscard]] bool confirm_highlighted_pattern(MenuContext& context)
{
    const std::uint8_t highlight = context.state.pattern_highlight;
    const MainMenuScreen return_screen = context.state.pattern_return_to_lobby
        ? MainMenuScreen::Lobby
        : MainMenuScreen::Singleplayer;

    if (highlight == constants::MAP_PATTERN_OTHER_INDEX) {
        const std::optional<std::filesystem::path> selected = pick_existing_file(
            context.window,
            core::default_patterns_directory(),
            std::string(constants::PATTERN_FILE_FILTER_DESCRIPTION),
            std::string(constants::PATTERN_FILE_FILTER_PATTERN));
        if (!selected.has_value()) {
            return false;
        }

        if (!apply_pattern_file(context.state, *selected)) {
            show_notice(
                context.state,
                std::string(constants::PATTERN_CONSTRAINT_LOAD_FAILED_LABEL),
                MainMenuScreen::MapPatternSelect);
            return false;
        }
    }
    else {
        apply_builtin_pattern(context.state, highlight);
    }

    apply_pattern_player_lock(context, current_map_pattern(context.state));
    if (context.state.screen == MainMenuScreen::Notice) {
        return false;
    }

    const std::uint8_t player_count = context.lobby
        ? counted_lobby_match_players(context.lobby->view())
        : counted_singleplayer_players(context.state);
    if (const std::optional<std::string> error =
            pattern_lobby_constraint_error(context.state, player_count)) {
        show_notice(context.state, *error, return_screen);
        context.state.pattern_return_to_lobby = false;
        if (context.lobby && context.lobby->is_host()) {
            prepare_host_settings(context.state);
            context.lobby->set_settings(context.state.host_settings);
        }
        return true;
    }

    context.state.screen = return_screen;
    if (context.state.pattern_return_to_lobby && context.lobby && context.lobby->is_host()) {
        prepare_host_settings(context.state);
        context.lobby->set_settings(context.state.host_settings);
    }

    context.state.pattern_return_to_lobby = false;
    return true;
}

void apply_menu_action(MenuContext& context, const MenuButtonHit& hit)
{
    switch (hit.action) {
    case MainMenuAction::Singleplayer:
        context.state.screen = MainMenuScreen::Singleplayer;
        context.state.status_message.clear();
        return;
    case MainMenuAction::CycleGameStyle:
        if (context.lobby && context.lobby->is_host()) {
            if (context.state.game_style == SingleplayerGameStyle::RandomGame) {
                context.state.game_style = SingleplayerGameStyle::Scenarios;
            }
            else {
                context.state.game_style = SingleplayerGameStyle::RandomGame;
                context.lobby->clear_scenario_requirement();
                apply_pattern_player_lock(context, current_map_pattern(context.state));
            }

            prepare_host_settings(context.state);
            context.lobby->set_settings(context.state.host_settings);
            return;
        }

        cycle_singleplayer_game_style(context.state);
        if (context.state.game_style == SingleplayerGameStyle::RandomGame) {
            apply_pattern_player_lock(context, current_map_pattern(context.state));
        }
        return;
    case MainMenuAction::SelectScenario: {
        const std::optional<std::filesystem::path> selected = pick_existing_file(
            context.window,
            core::default_playable_scenarios_directory(),
            std::string(constants::SINGLEPLAYER_SCENARIO_FILTER_DESCRIPTION),
            std::string(constants::SCENARIO_FILE_FILTER_PATTERN));
        if (!selected.has_value()) {
            return;
        }

        if (context.lobby && context.lobby->is_host()) {
            const std::optional<sim::scenario::ScenarioFile> scenario =
                sim::scenario::load_scenario_file(*selected);
            if (!scenario.has_value() || scenario->required_player_count == 0U) {
                context.state.status_message = "Could not load scenario";
                return;
            }

            const std::uint8_t humans = context.lobby->connected_human_count();
            if (humans > scenario->required_player_count) {
                context.state.status_message = "Scenario requires "
                    + std::to_string(scenario->required_player_count)
                    + " players, but " + std::to_string(humans) + " are in the lobby";
                return;
            }

            apply_selected_scenario_path(context.state, *selected);
            if (!context.lobby->try_apply_scenario(
                    scenario->required_player_count,
                    context.state.selected_scenario_name)) {
                context.state.status_message = "Scenario requires "
                    + std::to_string(scenario->required_player_count)
                    + " players, but " + std::to_string(humans) + " are in the lobby";
                return;
            }

            context.state.status_message.clear();
            return;
        }

        apply_selected_scenario_path(context.state, *selected);
        context.state.status_message.clear();
        return;
    }
    case MainMenuAction::SelectMapPattern:
        context.state.pattern_highlight = context.state.map_pattern;
        context.state.pattern_return_to_lobby =
            context.state.screen == MainMenuScreen::Lobby;
        context.state.screen = MainMenuScreen::MapPatternSelect;
        return;
    case MainMenuAction::HighlightMapPattern: {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - context.last_pattern_click_time);
        if (hit.extra == context.last_pattern_click_extra
            && elapsed_ms.count() <= constants::PATTERN_DOUBLE_CLICK_MS) {
            context.state.pattern_highlight = hit.extra;
            context.last_pattern_click_extra = 255U;
            (void)confirm_highlighted_pattern(context);
            return;
        }

        context.state.pattern_highlight = hit.extra;
        context.last_pattern_click_extra = hit.extra;
        context.last_pattern_click_time = now;
        return;
    }
    case MainMenuAction::ConfirmMapPattern:
        (void)confirm_highlighted_pattern(context);
        return;
    case MainMenuAction::MapPatternBack:
        context.state.screen = context.state.pattern_return_to_lobby
            ? MainMenuScreen::Lobby
            : MainMenuScreen::Singleplayer;
        context.state.pattern_return_to_lobby = false;
        return;
    case MainMenuAction::CycleFogMode:
        cycle_fog_mode(context.state);
        if (context.lobby && context.lobby->is_host()) {
            prepare_host_settings(context.state);
            context.lobby->set_settings(context.state.host_settings);
        }
        return;
    case MainMenuAction::CycleCheats:
        context.state.cheats_enabled = !context.state.cheats_enabled;
        if (context.lobby && context.lobby->is_host()) {
            prepare_host_settings(context.state);
            context.lobby->set_settings(context.state.host_settings);
        }
        return;
    case MainMenuAction::CycleVictoryCondition:
        return;
    case MainMenuAction::CycleSlotKind:
        if (context.lobby && context.lobby->is_host()) {
            context.lobby->cycle_slot_kind(hit.slot);
            start_host_discovery(context);
            return;
        }

        cycle_singleplayer_slot_kind(context.state, hit.slot);
        return;
    case MainMenuAction::CycleSlotColor:
        if (context.lobby) {
            if (context.lobby->is_host()) {
                context.lobby->cycle_slot_color(hit.slot);
            }
            else if (hit.slot == context.lobby->local_slot()) {
                context.lobby->request_local_color_cycle();
            }
            return;
        }

        cycle_singleplayer_slot_color(context.state, hit.slot);
        return;
    case MainMenuAction::CycleSlotTeam:
        if (context.lobby) {
            if (context.lobby->is_host()) {
                context.lobby->cycle_slot_team(hit.slot);
            }
            else if (hit.slot == context.lobby->local_slot()) {
                context.lobby->request_local_team_cycle();
            }
            return;
        }

        cycle_singleplayer_slot_team(context.state, hit.slot);
        return;
    case MainMenuAction::SingleplayerStart:
        if (context.state.game_style == SingleplayerGameStyle::Scenarios
            && context.state.selected_scenario_path.empty()) {
            context.state.status_message = "Select a scenario first";
            return;
        }

        if (context.state.game_style == SingleplayerGameStyle::RandomGame) {
            const std::uint8_t player_count = counted_singleplayer_players(context.state);
            if (const std::optional<std::string> error =
                    pattern_lobby_constraint_error(context.state, player_count)) {
                show_notice(context.state, *error, MainMenuScreen::Singleplayer);
                return;
            }

            const sim::map::MapPattern finalized = finalize_pattern_for_match(context.state);
            context.state.selected_pattern_payload = sim::map::serialize_map_pattern(finalized);
            context.state.host_settings.map_seed = sim::map::generate_map_seed();
            context.state.host_settings.pattern_payload = context.state.selected_pattern_payload;
        }

        context.result.singleplayer_setup = make_singleplayer_setup(context.state);
        context.result.outcome = MenuOutcome::StartSingleplayer;
        context.running = false;
        return;
    case MainMenuAction::OpenMultiplayer:
        context.state.screen = MainMenuScreen::Multiplayer;
        context.state.status_message.clear();
        return;
    case MainMenuAction::OpenSettings:
        context.state.screen = MainMenuScreen::Settings;
        context.state.settings.screen = GameMenuScreen::SettingsGame;
        return;
    case MainMenuAction::OpenAbout:
        context.state.screen = MainMenuScreen::About;
        context.state.status_message.clear();
        return;
    case MainMenuAction::AboutOpenWebsite:
        open_url_in_browser(std::string(constants::GAME_WEBSITE_URL));
        return;
    case MainMenuAction::AboutClose:
        context.state.screen = MainMenuScreen::Main;
        return;
    case MainMenuAction::ExitGame:
        context.result.outcome = MenuOutcome::ExitApp;
        context.running = false;
        return;
    case MainMenuAction::MultiplayerBack:
    case MainMenuAction::HostSetupBack:
    case MainMenuAction::ConnectBack:
    case MainMenuAction::SingleplayerBack:
        go_back_one_screen(context);
        return;
    case MainMenuAction::OpenHostSetup:
        start_hosting(context);
        return;
    case MainMenuAction::OpenBrowseGames:
        if (!player_name_is_acceptable(context.state.player_name)) {
            context.state.status_message = std::string(constants::MULTIPLAYER_NAME_REQUIRED_LABEL);
            return;
        }

        stop_discovery(context);
        context.discovery = std::make_unique<net::LanDiscovery>();
        if (!context.discovery->start_browser()) {
            context.state.status_message = "Could not start LAN browse";
            context.discovery.reset();
            return;
        }

        context.state.browse_highlight = 0U;
        context.state.status_message.clear();
        context.state.screen = MainMenuScreen::BrowseGames;
        return;
    case MainMenuAction::BrowseBack:
        go_back_one_screen(context);
        return;
    case MainMenuAction::BrowseRefresh:
        if (context.discovery) {
            context.discovery->refresh();
        }
        return;
    case MainMenuAction::CycleBrowseFilter:
        context.state.browse_filter = context.state.browse_filter == BrowseFilter::All
            ? BrowseFilter::Lan
            : BrowseFilter::All;
        return;
    case MainMenuAction::HighlightBrowseGame: {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - context.last_browse_click_time);
        if (hit.extra == context.last_browse_click_extra
            && elapsed_ms.count() <= constants::PATTERN_DOUBLE_CLICK_MS) {
            context.state.browse_highlight = hit.extra;
            if (hit.extra < context.state.browse_games.size()) {
                const BrowseGameEntry& game =
                    context.state.browse_games[hit.extra];
                context.state.join_address = game.address;
                context.state.join_port = std::to_string(game.port);
                start_connecting(context);
            }
            return;
        }

        context.state.browse_highlight = hit.extra;
        context.last_browse_click_extra = hit.extra;
        context.last_browse_click_time = now;
        return;
    }
    case MainMenuAction::BrowseHost:
        start_hosting(context);
        return;
    case MainMenuAction::BrowseConnect:
        if (context.state.browse_highlight >= context.state.browse_games.size()) {
            context.state.status_message = std::string(constants::MULTIPLAYER_SELECT_GAME_LABEL);
            return;
        }

        {
            const BrowseGameEntry& game =
                context.state.browse_games[context.state.browse_highlight];
            context.state.join_address = game.address;
            context.state.join_port = std::to_string(game.port);
            start_connecting(context);
        }
        return;
    case MainMenuAction::OpenConnect:
        context.state.screen = MainMenuScreen::Connect;
        context.state.focused_field = MenuTextField::None;
        return;
    case MainMenuAction::CyclePlayerCount:
        if (context.state.host_settings.required_player_count > 0U) {
            show_notice(
                context.state,
                std::string(constants::PATTERN_PLAYER_COUNT_LOCKED_LABEL),
                context.state.screen);
            return;
        }

        cycle_player_count(context.state.host_settings);
        return;
    case MainMenuAction::CycleMapSize:
        if (context.state.host_settings.map_size_locked) {
            show_notice(
                context.state,
                std::string(constants::PATTERN_MAP_SIZE_LOCKED_LABEL),
                context.state.screen);
            return;
        }

        cycle_map_size(context.state.host_settings);
        if (context.lobby && context.lobby->is_host()) {
            prepare_host_settings(context.state);
            context.lobby->set_settings(context.state.host_settings);
        }
        return;
    case MainMenuAction::CycleCivilCap:
        cycle_civil_population_cap(context.state.host_settings);
        if (context.lobby && context.lobby->is_host()) {
            context.lobby->set_settings(context.state.host_settings);
        }
        return;
    case MainMenuAction::ToggleFog:
        context.state.host_settings.fog_of_war_enabled =
            !context.state.host_settings.fog_of_war_enabled;
        return;
    case MainMenuAction::CreateLobby:
        start_hosting(context);
        return;
    case MainMenuAction::ConnectStart:
        start_connecting(context);
        return;
    case MainMenuAction::LobbyLeave:
        leave_lobby(context);
        context.state.screen = MainMenuScreen::Multiplayer;
        context.state.status_message.clear();
        return;
    case MainMenuAction::LobbyReady:
        if (context.lobby) {
            context.lobby->toggle_local_ready();
        }
        return;
    case MainMenuAction::LobbyStart:
        if (!context.lobby || !context.lobby->is_host()) {
            return;
        }

        if (context.state.game_style == SingleplayerGameStyle::RandomGame) {
            const std::uint8_t player_count =
                counted_lobby_match_players(context.lobby->view());
            if (const std::optional<std::string> error =
                    pattern_lobby_constraint_error(context.state, player_count)) {
                show_notice(context.state, *error, MainMenuScreen::Lobby);
                return;
            }

            const sim::map::MapPattern finalized = finalize_pattern_for_match(context.state);
            context.state.selected_pattern_payload = sim::map::serialize_map_pattern(finalized);
            context.state.host_settings.map_seed = sim::map::generate_map_seed();
            prepare_host_settings(context.state);
            context.lobby->set_settings(context.state.host_settings);
        }

        context.lobby->request_match_start();
        return;
    case MainMenuAction::NoticeOk:
        context.state.screen = context.state.notice_return_screen;
        return;
    case MainMenuAction::None:
        break;
    }
}

void handle_settings_click(MenuContext& context, const float mouse_x, const float mouse_y)
{
    const sf::Vector2u window_size = context.window.getSize();
    const GameMenuAction action = hit_test_menu_button(
        build_settings_buttons(context.state.settings, window_size),
        mouse_x,
        mouse_y);

    if (action == GameMenuAction::SettingsTabGame) {
        context.state.settings.screen = GameMenuScreen::SettingsGame;
        return;
    }

    if (action == GameMenuAction::SettingsTabVideo) {
        context.state.settings.screen = GameMenuScreen::SettingsVideo;
        return;
    }

    if (action == GameMenuAction::SettingsTabAudio) {
        context.state.settings.screen = GameMenuScreen::SettingsAudio;
        return;
    }

    if (action == GameMenuAction::ToggleFullscreen) {
        toggle_menu_fullscreen(context);
        return;
    }

    if (action == GameMenuAction::ToggleMouseCapture) {
        if (context.display_settings.fullscreen) {
            return;
        }
        context.state.settings.mouse_capture = !context.state.settings.mouse_capture;
        context.display_settings.mouse_capture = context.state.settings.mouse_capture;
        context.shell.mouse_capture = context.display_settings.mouse_capture;
        apply_mouse_capture(context.window, context.display_settings);
        save_app_settings(context.shell);
        return;
    }

    if (action == GameMenuAction::ToggleVsync) {
        context.state.settings.vsync = !context.state.settings.vsync;
        context.display_settings.vsync = context.state.settings.vsync;
        context.display_settings.fps_limit = context.state.settings.fps_limit;
        apply_window_frame_limits(context.window, context.display_settings);
        context.shell.vsync = context.display_settings.vsync;
        context.shell.fps_limit = context.display_settings.fps_limit;
        save_app_settings(context.shell);
        return;
    }

    if (action == GameMenuAction::CycleFps) {
        if (context.state.settings.vsync) {
            return;
        }
        context.state.settings.fps_limit = next_video_fps_limit(context.state.settings.fps_limit);
        context.display_settings.vsync = context.state.settings.vsync;
        context.display_settings.fps_limit = context.state.settings.fps_limit;
        apply_window_frame_limits(context.window, context.display_settings);
        context.shell.vsync = context.display_settings.vsync;
        context.shell.fps_limit = context.display_settings.fps_limit;
        save_app_settings(context.shell);
        return;
    }

    if (action == GameMenuAction::SettingsBack) {
        go_back_one_screen(context);
        return;
    }

    if (context.state.settings.screen == GameMenuScreen::SettingsGame
        && scroll_speed_slider_rect(window_size).contains(mouse_x, mouse_y)) {
        context.state.settings.dragging_slider = GameMenuSlider::ScrollSpeed;
        apply_slider_drag(context.state.settings, window_size, mouse_x);
        context.shell.scroll_speed = context.state.settings.scroll_speed;
        return;
    }

    if (context.state.settings.screen != GameMenuScreen::SettingsAudio) {
        return;
    }

    const GameMenuSlider slider = hit_test_volume_slider(window_size, mouse_x, mouse_y);
    if (slider == GameMenuSlider::None) {
        return;
    }

    context.state.settings.dragging_slider = slider;
    apply_slider_drag(context.state.settings, window_size, mouse_x);
    apply_menu_volumes(context);
}

void handle_layout_click(MenuContext& context, const float mouse_x, const float mouse_y)
{
    const net::LobbyStateMessage fallback_lobby{};
    const bool has_lobby = static_cast<bool>(context.lobby);
    const MenuLayout layout = build_menu_layout(
        context.window.getSize(),
        context.state,
        has_lobby ? context.lobby->view() : fallback_lobby,
        has_lobby && context.lobby->is_host(),
        has_lobby && context.lobby->local_ready(),
        has_lobby && context.lobby->can_start_match());

    const MenuTextField field = hit_test_menu_text_fields(layout.text_fields, mouse_x, mouse_y);
    if (field != MenuTextField::None) {
        context.state.focused_field = field;
        return;
    }

    const MenuButtonHit hit = hit_test_menu_buttons(layout.buttons, mouse_x, mouse_y);
    if (hit.action == MainMenuAction::None) {
        context.state.focused_field = MenuTextField::None;
        return;
    }

    apply_menu_action(context, hit);
}

void handle_key_pressed(MenuContext& context, const sf::Keyboard::Key key)
{
    if (key == sf::Keyboard::Key::F9) {
        context.shell.show_perf_hud = !context.shell.show_perf_hud;
        return;
    }

    if (key == sf::Keyboard::Key::F12) {
        toggle_menu_fullscreen(context);
        return;
    }

    if (key == sf::Keyboard::Key::Escape) {
        go_back_one_screen(context);
        return;
    }

    if (key == sf::Keyboard::Key::Backspace) {
        backspace_focused_text(context.state);
        return;
    }

    if (key != sf::Keyboard::Key::Enter) {
        return;
    }

    if (context.state.focused_field != MenuTextField::None) {
        context.state.focused_field = MenuTextField::None;
        return;
    }

    if (context.state.screen == MainMenuScreen::Connect) {
        start_connecting(context);
    }
}

void handle_menu_event(MenuContext& context, const sf::Event& event)
{
    if (event.is<sf::Event::Closed>()) {
        context.result.outcome = MenuOutcome::ExitApp;
        context.running = false;
        return;
    }

    if (const auto* resized = event.getIf<sf::Event::Resized>()) {
        if (!context.display_settings.fullscreen) {
            context.renderer.resize(resized->size);
        }
        return;
    }

    if (const auto* key_pressed = event.getIf<sf::Event::KeyPressed>()) {
        handle_key_pressed(context, key_pressed->code);
        return;
    }

    if (const auto* text_entered = event.getIf<sf::Event::TextEntered>()) {
        const char32_t unicode = text_entered->unicode;
        if (unicode >= constants::MAIN_MENU_MIN_PRINTABLE_CHAR
            && unicode <= constants::MAIN_MENU_MAX_PRINTABLE_CHAR) {
            append_focused_text(context.state, static_cast<char>(unicode));
        }
        return;
    }

    if (const auto* mouse_pressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (mouse_pressed->button != sf::Mouse::Button::Left) {
            return;
        }

        const float mouse_x = static_cast<float>(mouse_pressed->position.x);
        const float mouse_y = static_cast<float>(mouse_pressed->position.y);
        if (context.state.screen == MainMenuScreen::Settings) {
            handle_settings_click(context, mouse_x, mouse_y);
            return;
        }

        handle_layout_click(context, mouse_x, mouse_y);
        return;
    }

    if (event.is<sf::Event::MouseButtonReleased>()) {
        context.state.settings.dragging_slider = GameMenuSlider::None;
        return;
    }

    if (const auto* mouse_moved = event.getIf<sf::Event::MouseMoved>()) {
        if (context.state.settings.dragging_slider == GameMenuSlider::None) {
            return;
        }

        apply_slider_drag(
            context.state.settings,
            context.window.getSize(),
            static_cast<float>(mouse_moved->position.x));
        apply_menu_volumes(context);
        context.shell.scroll_speed = context.state.settings.scroll_speed;
    }
}

[[nodiscard]] std::uint64_t reconnect_token_for_slot(
    const std::uint8_t player_slot,
    const std::string& player_name)
{
    // Deterministic across host/peers from the shared lobby roster (never zero).
    std::uint64_t hash = 14695981039346656037ULL;
    hash ^= static_cast<std::uint64_t>(player_slot) + 0x9E3779B97F4A7C15ULL;
    hash *= 1099511628211ULL;
    for (const unsigned char character : player_name) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }

    return hash | 1ULL;
}

[[nodiscard]] LockstepMatchSetup make_match_setup(
    const net::LobbySession& lobby,
    const net::LobbySettings& settings,
    const MainMenuState& state)
{
    LockstepMatchSetup setup{};
    setup.is_host = lobby.is_host();
    setup.civil_population_map_cap = static_cast<int>(settings.civil_population_map_cap);
    setup.fog_mode = static_cast<sim::components::FogOfWarMode>(settings.fog_mode);
    setup.fog_of_war_enabled =
        setup.fog_mode != sim::components::FogOfWarMode::Disabled;
    setup.cheats_enabled = settings.cheats_enabled;
    setup.victory_condition =
        static_cast<sim::components::VictoryCondition>(settings.victory_condition);
    setup.port = state.active_port;
    setup.host_address = state.join_address;
    setup.delay_before_connect = !setup.is_host;
    setup.lockstep_debug = false;
    setup.slot_is_ai.fill(false);

    const net::LobbyStateMessage& lobby_view = lobby.view();
    std::array<std::uint8_t, aoa::net::constants::LOCKSTEP_MAX_PLAYER_SLOTS> lobby_to_match{};
    lobby_to_match.fill(0xFFU);
    std::uint8_t compact = 0U;
    for (std::uint8_t slot = 0U; slot < lobby_view.slots.size(); ++slot) {
        const net::LobbySlotInfo& info = lobby_view.slots[slot];
        if (info.kind == net::LobbySlotKind::Spectator) {
            continue;
        }

        if (info.kind == net::LobbySlotKind::Disabled && !info.occupied) {
            continue;
        }

        if (!info.occupied && info.kind != net::LobbySlotKind::Ai) {
            continue;
        }

        lobby_to_match[slot] = compact;
        setup.slot_colors[compact] = info.color;
        setup.slot_teams[compact] = info.team;
        setup.slot_is_ai[compact] = info.kind == net::LobbySlotKind::Ai;
        setup.player_names[compact] = info.name.empty()
            ? ("Player " + std::to_string(static_cast<int>(compact) + 1))
            : info.name;
        setup.reconnect_tokens[compact] =
            reconnect_token_for_slot(compact, setup.player_names[compact]);
        ++compact;
    }

    setup.player_count = compact < 2U ? 2U : compact;
    const std::uint8_t local_match_slot = lobby.local_slot() < lobby_to_match.size()
        ? lobby_to_match[lobby.local_slot()]
        : 0U;
    setup.player_slot = local_match_slot == 0xFFU ? 0U : local_match_slot;
    setup.map_seed = settings.map_seed;
    setup.pattern_payload = settings.pattern_payload.empty()
        ? state.selected_pattern_payload
        : settings.pattern_payload;
    setup.lobby_settings = settings;
    setup.lobby_settings.map_seed = setup.map_seed;
    setup.lobby_settings.pattern_payload = setup.pattern_payload;
    return setup;
}

// Gives the host transport time to deliver LobbyMatchStart before the port is reused.
void finish_lobby_for_match(net::LobbySession& lobby)
{
    if (lobby.is_host()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(constants::MULTIPLAYER_MATCH_START_FLUSH_MS));
        lobby.poll();
    }

    lobby.shutdown();
}

void update_lobby(MenuContext& context)
{
    if (!context.lobby) {
        return;
    }

    context.lobby->poll();

    if (context.lobby->status() == net::LobbyStatus::ConnectFailed) {
        context.state.status_message =
            "Could not join lobby (host unreachable or match rejoin failed)";
        context.state.screen = MainMenuScreen::Connect;
        context.lobby.reset();
        return;
    }

    if (context.lobby->status() == net::LobbyStatus::Rejected) {
        context.state.status_message = context.lobby->reject_reason().empty()
            ? std::string(constants::LOBBY_VERSION_MISMATCH_MESSAGE)
            : context.lobby->reject_reason();
        context.state.screen = MainMenuScreen::Connect;
        context.lobby.reset();
        return;
    }

    if (context.lobby->status() == net::LobbyStatus::Closed) {
        context.state.status_message =
            "Disconnected from host (lobby closed or match rejoin rejected)";
        context.state.screen = MainMenuScreen::Multiplayer;
        context.lobby.reset();
        return;
    }

    if (context.lobby->status() == net::LobbyStatus::Joined) {
        sync_state_from_lobby(context.state, context.lobby->view());
        if (context.state.status_message == "Connecting...") {
            context.state.status_message.clear();
        }

        start_host_discovery(context);
    }

    const std::optional<net::LobbySettings> match_settings = context.lobby->consume_match_start();
    if (!match_settings.has_value()) {
        return;
    }

    stop_discovery(context);
    context.result.outcome = MenuOutcome::StartMatch;
    context.result.match_setup = make_match_setup(*context.lobby, *match_settings, context.state);
    finish_lobby_for_match(*context.lobby);
    context.lobby.reset();
    context.running = false;
}

void draw_menu_frame(MenuContext& context, FpsTracker& fps_tracker)
{
    const sf::Vector2i mouse_position = sf::Mouse::getPosition(context.window);
    const bool has_lobby = static_cast<bool>(context.lobby);

    fps_tracker.record_frame();

    render::MenuRenderContext render_context{};
    render_context.state = &context.state;
    render_context.lobby = has_lobby ? &context.lobby->view() : nullptr;
    render_context.is_host = has_lobby && context.lobby->is_host();
    render_context.local_ready = has_lobby && context.lobby->local_ready();
    render_context.can_start = has_lobby && context.lobby->can_start_match();
    render_context.show_perf_hud = context.shell.show_perf_hud;
    render_context.fps = fps_tracker.fps();
    render_context.mouse_position = {
        static_cast<float>(mouse_position.x),
        static_cast<float>(mouse_position.y),
    };

    (void)context.window.setActive(true);
    context.renderer.draw(render_context);
    context.window.display();
}

MenuResult run_main_menu(
    sf::Window& window,
    WindowDisplaySettings& display_settings,
    MainMenuState& state,
    AppShellSettings& shell)
{
    render::MenuRenderer renderer{};
    renderer.load(core::default_assets_directory());
    renderer.reset_graphics_context(window.getSize());

    GameCursor cursor{};
    (void)cursor.load(core::default_assets_directory());
    cursor.apply(window);

    audio::GameAudio menu_audio{};
    (void)menu_audio.load(core::default_assets_directory(), audio::MusicMode::MainMenuTheme);
    menu_audio.set_master_volume(shell.master_volume);
    menu_audio.set_music_volume(shell.music_volume);
    menu_audio.set_sfx_volume(shell.sfx_volume);

    state.settings.master_volume = shell.master_volume;
    state.settings.music_volume = shell.music_volume;
    state.settings.sfx_volume = shell.sfx_volume;
    state.settings.scroll_speed = shell.scroll_speed;
    state.settings.fullscreen = display_settings.fullscreen;
    state.settings.mouse_capture = display_settings.mouse_capture;
    state.settings.vsync = display_settings.vsync;
    state.settings.fps_limit = display_settings.fps_limit;
    shell.fullscreen = display_settings.fullscreen;
    shell.vsync = display_settings.vsync;
    shell.fps_limit = display_settings.fps_limit;
    state.screen = MainMenuScreen::Main;
    state.focused_field = MenuTextField::None;
    state.status_message.clear();

    std::unique_ptr<net::LobbySession> lobby{};
    std::unique_ptr<net::LanDiscovery> discovery{};
    MenuResult result{};
    bool running = true;

    MenuContext context{
        window,
        renderer,
        display_settings,
        cursor,
        menu_audio,
        state,
        shell,
        lobby,
        discovery,
        result,
        running,
    };

    sf::Clock frame_clock{};
    FpsTracker fps_tracker{};

    while (running && window.isOpen()) {
        while (const std::optional event = window.pollEvent()) {
            handle_menu_event(context, *event);
            if (!running) {
                break;
            }
        }

        if (!running || !window.isOpen()) {
            break;
        }

        menu_audio.update();
        update_lobby(context);
        if (discovery) {
            discovery->poll();
            if (state.screen == MainMenuScreen::BrowseGames) {
                sync_browse_games(context);
            }
        }

        renderer.update(frame_clock.restart().asSeconds());
        draw_menu_frame(context, fps_tracker);
    }

    if (discovery) {
        discovery->stop();
        discovery.reset();
    }

    if (lobby) {
        lobby->leave();
        lobby.reset();
    }

    shell.fullscreen = display_settings.fullscreen;
    shell.mouse_capture = display_settings.mouse_capture;
    shell.vsync = display_settings.vsync;
    shell.fps_limit = display_settings.fps_limit;
    shell.master_volume = menu_audio.master_volume();
    shell.music_volume = menu_audio.music_volume();
    shell.sfx_volume = menu_audio.sfx_volume();
    shell.scroll_speed = state.settings.scroll_speed;
    return result;
}

[[nodiscard]] bool create_shared_app_window(
    sf::Window& window,
    WindowDisplaySettings& display_settings,
    const AppShellSettings& shell)
{
    display_settings.title = std::string(constants::WINDOW_TITLE);
    display_settings.context_settings = sf::ContextSettings{
        .depthBits = 24U,
        .majorVersion = static_cast<unsigned int>(constants::OPENGL_MAJOR_VERSION),
        .minorVersion = static_cast<unsigned int>(constants::OPENGL_MINOR_VERSION),
    };
    display_settings.windowed_size = {
        constants::DEFAULT_WINDOW_WIDTH,
        constants::DEFAULT_WINDOW_HEIGHT,
    };
    display_settings.windowed_position = {0, 0};
    display_settings.fullscreen = false;
    display_settings.mouse_capture = shell.mouse_capture;
    display_settings.vsync = shell.vsync;
    display_settings.fps_limit = shell.fps_limit;

    window.create(
        sf::VideoMode(display_settings.windowed_size),
        display_settings.title,
        sf::Style::Default,
        sf::State::Windowed,
        display_settings.context_settings);
    apply_window_frame_limits(window, display_settings);
    (void)window.setActive(true);
    initialize_window_display_settings(window, display_settings);
    apply_mouse_capture(window, display_settings);

    if (!shell.fullscreen) {
        return window.isOpen();
    }

    enter_fullscreen(
        window,
        [](const sf::Vector2u /*size*/) {},
        display_settings);
    return window.isOpen();
}

} // namespace

int run_app_shell()
{
    if (!net::EnetTransport::global_initialize()) {
        std::cerr << "app: enet_initialize failed\n";
        return 1;
    }

    AppShellSettings shell_settings = load_app_settings();
    sf::Window window{};
    WindowDisplaySettings display_settings{};
    if (!create_shared_app_window(window, display_settings, shell_settings)) {
        std::cerr << "app: failed to create window\n";
        net::EnetTransport::global_deinitialize();
        return 1;
    }

    MainMenuState state = make_default_main_menu_state();

    while (window.isOpen()) {
        const MenuResult menu_result = run_main_menu(window, display_settings, state, shell_settings);

        save_app_settings(shell_settings);
        if (!window.isOpen() || menu_result.outcome == MenuOutcome::ExitApp) {
            break;
        }

        if (menu_result.outcome == MenuOutcome::StartSingleplayer) {
            const auto& setup = menu_result.singleplayer_setup;
            sim::Simulation simulation{sim::map::make_generation_config(
                setup.player_count,
                setup.map_seed,
                setup.map_pattern,
                setup.pattern_payload)};
            if (run_graphical(
                    window,
                    display_settings,
                    simulation,
                    shell_settings,
                    menu_result.singleplayer_setup)
                == AppFlow::ExitApp) {
                break;
            }

            continue;
        }

        const auto& match = menu_result.match_setup;
        sim::Simulation simulation{sim::map::make_generation_config(
            match.player_count,
            match.map_seed,
            0U,
            match.pattern_payload)};
        if (run_lockstep_match(
                window,
                display_settings,
                simulation,
                menu_result.match_setup,
                shell_settings)
            == AppFlow::ExitApp) {
            break;
        }
    }

    save_app_settings(shell_settings);
    if (window.isOpen()) {
        window.close();
    }

    net::EnetTransport::global_deinitialize();
    return 0;
}

} // namespace aoa::app
