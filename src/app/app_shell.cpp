#include "app/application.hpp"

#include "app/fps_tracker.hpp"
#include "app/game_cursor.hpp"
#include "app/game_menu.hpp"
#include "app/main_menu.hpp"
#include "app/window_display.hpp"
#include "audio/game_audio.hpp"
#include "core/constants.hpp"
#include "core/runtime_paths.hpp"
#include "net/enet_transport.hpp"
#include "net/lobby_session.hpp"
#include "net/net_constants.hpp"
#include "render/menu_renderer.hpp"
#include "sim/simulation.hpp"

#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Mouse.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/Window.hpp>

#include <chrono>
#include <cctype>
#include <cstdint>
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
    MenuResult& result;
    bool& running;
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
    context.cursor.force_reapply(context.window);
}

void leave_lobby(MenuContext& context)
{
    if (!context.lobby) {
        return;
    }

    context.lobby->leave();
    context.lobby.reset();
}

void start_hosting(MenuContext& context)
{
    context.state.active_port = parse_port(context.state.host_port);
    context.state.host_port = std::to_string(context.state.active_port);
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
}

void start_connecting(MenuContext& context)
{
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
        context.state.screen = MainMenuScreen::Main;
        return;
    case MainMenuScreen::HostSetup:
    case MainMenuScreen::Connect:
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

void apply_menu_action(MenuContext& context, const MainMenuAction action)
{
    switch (action) {
    case MainMenuAction::Singleplayer:
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
    case MainMenuAction::ExitGame:
        context.result.outcome = MenuOutcome::ExitApp;
        context.running = false;
        return;
    case MainMenuAction::MultiplayerBack:
    case MainMenuAction::HostSetupBack:
    case MainMenuAction::ConnectBack:
        go_back_one_screen(context);
        return;
    case MainMenuAction::OpenHostSetup:
        context.state.screen = MainMenuScreen::HostSetup;
        context.state.focused_field = MenuTextField::None;
        return;
    case MainMenuAction::OpenConnect:
        context.state.screen = MainMenuScreen::Connect;
        context.state.focused_field = MenuTextField::None;
        return;
    case MainMenuAction::CyclePlayerCount:
        cycle_player_count(context.state.host_settings);
        return;
    case MainMenuAction::CycleCivilCap:
        cycle_civil_population_cap(context.state.host_settings);
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
        if (context.lobby) {
            context.lobby->request_match_start();
        }
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

    if (action == GameMenuAction::SettingsTabAudio) {
        context.state.settings.screen = GameMenuScreen::SettingsAudio;
        return;
    }

    if (action == GameMenuAction::ToggleFullscreen) {
        toggle_menu_fullscreen(context);
        return;
    }

    if (action == GameMenuAction::SettingsBack) {
        go_back_one_screen(context);
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

    const MainMenuAction action = hit_test_menu_buttons(layout.buttons, mouse_x, mouse_y);
    if (action == MainMenuAction::None) {
        context.state.focused_field = MenuTextField::None;
        return;
    }

    apply_menu_action(context, action);
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
    }
}

[[nodiscard]] LockstepMatchSetup make_match_setup(
    const net::LobbySession& lobby,
    const net::LobbySettings& settings,
    const MainMenuState& state)
{
    LockstepMatchSetup setup{};
    setup.is_host = lobby.is_host();
    setup.player_slot = lobby.local_slot();
    setup.player_count = settings.player_count;
    setup.civil_population_map_cap = static_cast<int>(settings.civil_population_map_cap);
    setup.fog_of_war_enabled = settings.fog_of_war_enabled;
    setup.port = state.active_port;
    setup.host_address = state.join_address;
    setup.delay_before_connect = !setup.is_host;
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
        context.state.status_message = "Could not reach the host";
        context.state.screen = MainMenuScreen::Connect;
        context.lobby.reset();
        return;
    }

    if (context.lobby->status() == net::LobbyStatus::Closed) {
        context.state.status_message = "The host closed the lobby";
        context.state.screen = MainMenuScreen::Multiplayer;
        context.lobby.reset();
        return;
    }

    if (context.lobby->status() == net::LobbyStatus::Joined) {
        context.state.status_message.clear();
    }

    const std::optional<net::LobbySettings> match_settings = context.lobby->consume_match_start();
    if (!match_settings.has_value()) {
        return;
    }

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
    state.settings.fullscreen = display_settings.fullscreen;
    shell.fullscreen = display_settings.fullscreen;
    state.screen = MainMenuScreen::Main;
    state.focused_field = MenuTextField::None;
    state.status_message.clear();

    std::unique_ptr<net::LobbySession> lobby{};
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
        renderer.update(frame_clock.restart().asSeconds());
        draw_menu_frame(context, fps_tracker);
    }

    if (lobby) {
        lobby->leave();
        lobby.reset();
    }

    shell.fullscreen = display_settings.fullscreen;
    shell.master_volume = menu_audio.master_volume();
    shell.music_volume = menu_audio.music_volume();
    shell.sfx_volume = menu_audio.sfx_volume();
    return result;
}

[[nodiscard]] bool create_shared_app_window(
    sf::Window& window,
    WindowDisplaySettings& display_settings)
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

    window.create(
        sf::VideoMode(display_settings.windowed_size),
        display_settings.title,
        sf::Style::Default,
        sf::State::Windowed,
        display_settings.context_settings);
    window.setVerticalSyncEnabled(false);
    window.setFramerateLimit(constants::TARGET_DISPLAY_FPS);
    (void)window.setActive(true);
    initialize_window_display_settings(window, display_settings);

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

    sf::Window window{};
    WindowDisplaySettings display_settings{};
    if (!create_shared_app_window(window, display_settings)) {
        std::cerr << "app: failed to create window\n";
        net::EnetTransport::global_deinitialize();
        return 1;
    }

    AppShellSettings shell_settings{};
    shell_settings.fullscreen = true;
    MainMenuState state = make_default_main_menu_state();

    while (window.isOpen()) {
        const MenuResult menu_result = run_main_menu(window, display_settings, state, shell_settings);

        if (!window.isOpen() || menu_result.outcome == MenuOutcome::ExitApp) {
            break;
        }

        if (menu_result.outcome == MenuOutcome::StartSingleplayer) {
            sim::Simulation simulation{constants::SINGLEPLAYER_PLAYER_COUNT};
            if (run_graphical(window, display_settings, simulation, shell_settings)
                == AppFlow::ExitApp) {
                break;
            }

            continue;
        }

        sim::Simulation simulation{menu_result.match_setup.player_count};
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

    if (window.isOpen()) {
        window.close();
    }

    net::EnetTransport::global_deinitialize();
    return 0;
}

} // namespace aoa::app
