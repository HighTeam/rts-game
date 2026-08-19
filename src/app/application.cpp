#include "app/application.hpp"

#include "app/fps_tracker.hpp"
#include "app/game_input.hpp"
#include "app/window_display.hpp"
#include "core/constants.hpp"
#include "core/fixed_timestep_loop.hpp"
#include "harness/regression_harness.hpp"
#include "net/enet_transport.hpp"
#include "net/lockstep_debug_log.hpp"
#include "net/lockstep_runner.hpp"
#include "net/lockstep_session.hpp"
#include "net/net_constants.hpp"
#include "render/game_renderer.hpp"

#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/Window.hpp>

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace aoa::app {

namespace {

std::uint64_t parse_hash_argument(const std::string& hash_text)
{
    std::string normalized = hash_text;
    if (normalized.starts_with("0x") || normalized.starts_with("0X")) {
        normalized = normalized.substr(2U);
    }

    return std::stoull(normalized, nullptr, 16);
}

} // namespace

LaunchOptions parse_launch_options(const int argc, char** argv)
{
    LaunchOptions options{};

    for (int arg_index = 1; arg_index < argc; ++arg_index) {
        const std::string arg = argv[arg_index];

        if (arg == "--headless") {
            options.headless = true;
            continue;
        }

        if (arg == "--harness") {
            options.run_harness = true;
            continue;
        }

        if (arg == "--net-smoke") {
            options.run_net_smoke = true;
            continue;
        }

        if (arg == "--lockstep-smoke") {
            options.run_lockstep_smoke = true;
            continue;
        }

        if (arg == "--lockstep-disconnect-smoke") {
            options.run_lockstep_disconnect_smoke = true;
            continue;
        }

        if (arg == "--lockstep-reconnect-smoke") {
            options.run_lockstep_reconnect_smoke = true;
            continue;
        }

        if (arg == "--lockstep-4-smoke") {
            options.run_lockstep_4_smoke = true;
            continue;
        }

        if (arg == "--lockstep-peer-silence-smoke") {
            options.run_lockstep_peer_silence_smoke = true;
            continue;
        }

        if (arg == "--snapshot-smoke") {
            options.run_snapshot_smoke = true;
            continue;
        }

        if (arg == "--snapshot-double-spawn-smoke") {
            options.run_snapshot_double_spawn_smoke = true;
            continue;
        }

        if (arg == "--snapshot-reconnect-smoke") {
            options.run_snapshot_reconnect_smoke = true;
            continue;
        }

        if (arg == "--snapshot-heavy-smoke") {
            options.run_snapshot_heavy_smoke = true;
            continue;
        }

        if (arg == "--lockstep-host") {
            options.lockstep_host = true;
            continue;
        }

        if (arg == "--lockstep-join" && arg_index + 1 < argc) {
            options.lockstep_join = true;
            options.lockstep_join_address = argv[++arg_index];
            continue;
        }

        if (arg == "--port" && arg_index + 1 < argc) {
            const int port_value = std::stoi(argv[++arg_index]);
            if (port_value <= 0 || port_value > 65535) {
                throw std::invalid_argument("Invalid port: " + std::to_string(port_value));
            }

            options.lockstep_port = static_cast<std::uint16_t>(port_value);
            continue;
        }

        if (arg == "--print-hash") {
            options.print_state_hash = true;
            continue;
        }

        if (arg == "--expect-hash" && arg_index + 1 < argc) {
            options.expect_state_hash = parse_hash_argument(argv[++arg_index]);
            continue;
        }

        if (arg == "--ticks" && arg_index + 1 < argc) {
            const std::uint64_t tick_count = static_cast<std::uint64_t>(std::stoull(argv[++arg_index]));
            options.headless_ticks = tick_count;
            options.lockstep_ticks = tick_count;
            continue;
        }

        if (arg == "--lockstep-debug") {
            options.lockstep_debug = true;
            continue;
        }

        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: aoa [--headless] [--ticks N] [--expect-hash HEX] [--print-hash]\n"
                         "       aoa --harness\n"
                         "       aoa --net-smoke\n"
                         "       aoa --lockstep-smoke\n"
                         "       aoa --lockstep-disconnect-smoke\n"
                         "       aoa --lockstep-reconnect-smoke\n"
                         "       aoa --lockstep-4-smoke\n"
                         "       aoa --lockstep-peer-silence-smoke\n"
                         "       aoa --snapshot-smoke\n"
                         "       aoa --lockstep-host [--port PORT] [--headless] [--ticks N] [--lockstep-debug]\n"
                         "       aoa --lockstep-join HOST:PORT [--headless] [--ticks N] [--lockstep-debug]\n";
            std::exit(0);
        }

        throw std::invalid_argument("Unknown argument: " + arg);
    }

    if (options.headless && options.headless_ticks == 0U) {
        options.headless_ticks = constants::HEADLESS_DEFAULT_TICK_COUNT;
    }

    if ((options.lockstep_host || options.lockstep_join) && options.headless
        && options.lockstep_ticks == 0U) {
        options.lockstep_ticks = aoa::net::constants::LOCKSTEP_DEFAULT_TICK_COUNT;
    }

    if (!options.headless) {
        options.lockstep_ticks = 0U;
    }

    return options;
}

int run_headless(sim::Simulation& simulation, const LaunchOptions& options)
{
    core::FixedTimestepLoop loop{};
    loop.run_headless([&simulation]() { simulation.tick(); }, options.headless_ticks);

    const std::uint64_t actual_hash = simulation.state_hash();
    if (options.print_state_hash) {
        std::cout << "state_hash=0x" << std::hex << actual_hash << std::dec << '\n';
    }

    if (options.expect_state_hash.has_value() && actual_hash != *options.expect_state_hash) {
        std::cerr << "Hash mismatch: expected 0x" << std::hex << *options.expect_state_hash
                  << " got 0x" << actual_hash << std::dec << '\n';
        return 1;
    }

    return 0;
}

int run_graphical(sim::Simulation& simulation)
{
    sf::ContextSettings context_settings{
        .depthBits = 24U,
        .majorVersion = static_cast<unsigned int>(constants::OPENGL_MAJOR_VERSION),
        .minorVersion = static_cast<unsigned int>(constants::OPENGL_MINOR_VERSION),
    };

    sf::Window window(
        sf::VideoMode({constants::DEFAULT_WINDOW_WIDTH, constants::DEFAULT_WINDOW_HEIGHT}),
        std::string(constants::WINDOW_TITLE),
        sf::State::Windowed,
        context_settings);

    window.setVerticalSyncEnabled(false);
    window.setFramerateLimit(constants::TARGET_DISPLAY_FPS);
    (void)window.setActive(true);

    WindowDisplaySettings display_settings{};
    display_settings.title = std::string(constants::WINDOW_TITLE);
    display_settings.context_settings = context_settings;
    initialize_window_display_settings(window, display_settings);

    render::GameRenderer renderer{};
    renderer.resize(window.getSize());
    renderer.set_local_player_slot(0U);

    GameInput game_input{};
    game_input.set_local_player_slot(0U);
    game_input.set_local_player_slot(0U);
    game_input.reset_frame_clock();
    FpsTracker fps_tracker{};

    core::FixedTimestepLoop loop{};
    loop.run_realtime(
        [&simulation]() { simulation.tick(); },
        [&renderer, &simulation, &window, &game_input, &fps_tracker](const float interpolation_alpha) {
            fps_tracker.record_frame();
            game_input.update_continuous(window, renderer, simulation);
            (void)window.setActive(true);
            const app::PlayerSelection& selection = game_input.selection();
            const app::HoverHighlight& hover = game_input.hover();
            renderer.draw(
                simulation,
                selection.units,
                interpolation_alpha,
                game_input.selection_box(),
                hover.unit,
                hover.unit_is_enemy,
                hover.building,
                hover.resource_cell,
                selection.resource_cell,
                selection.building,
                fps_tracker.fps());
            window.display();
        },
        [&window, &renderer, &simulation, &game_input, &display_settings]() {
            while (const std::optional event = window.pollEvent()) {
                if (event->is<sf::Event::Closed>()) {
                    window.close();
                }

                game_input.handle_event(*event, window, renderer, simulation);

                if (const auto* key_pressed = event->getIf<sf::Event::KeyPressed>()) {
                    if (key_pressed->code == sf::Keyboard::Key::Escape) {
                        window.close();
                    }
                    else {
                        handle_display_key(window, renderer, display_settings, key_pressed->code);
                    }
                }

                if (const auto* resized = event->getIf<sf::Event::Resized>()) {
                    if (!display_settings.fullscreen) {
                        renderer.resize(resized->size);
                    }
                }
            }

            return window.isOpen();
        },
        [&simulation]() { simulation.snapshot_world_positions_for_render(); });

    return 0;
}

namespace {

void update_lockstep_window_title(
    sf::Window& window,
    const net::LockstepSession& session,
    const std::uint8_t player_slot)
{
    std::string title = std::string(constants::WINDOW_TITLE);

    if (session.is_desynced()) {
        title += " - DESYNC tick " + std::to_string(session.desync_tick());
    }
    else if (session.is_host_gone()) {
        title += " - Host left the game";
    }
    else if (session.is_reconnecting()) {
        title += " - Reconnecting to host...";
    }
    else if (!session.is_session_ready() && session.is_connected()) {
        title += " - Joining match...";
    }
    else if (session.is_ai_fallback() && session.is_waiting_for_opponent_reconnect()) {
        title += " - Player " + std::to_string(session.ai_controlled_slot() + 1U)
            + " left — AI playing (waiting for reconnect)";
    }
    else if (session.is_ai_fallback()) {
        title += " - Player " + std::to_string(session.ai_controlled_slot() + 1U)
            + " disconnected, AI playing";
    }
    else if (session.is_waiting_for_opponent_reconnect()) {
        title += " - Opponent disconnected, waiting for reconnect...";
    }
    else if (!session.is_connected()) {
        title += " - Waiting for opponent...";
    }
    else {
        title += " - Player " + std::to_string(player_slot + 1U);
    }

    static std::string last_title{};
    if (title == last_title) {
        return;
    }

    last_title = std::move(title);
    window.setTitle(last_title);
}

bool lockstep_should_show_waiting_overlay(
    const net::LockstepSession& session,
    const net::LockstepRole role)
{
    if (session.is_awaiting_reconnect_handshake()) {
        return true;
    }

    if (role == net::LockstepRole::Host && !session.is_connected()) {
        return true;
    }

    if (role == net::LockstepRole::Client) {
        if (!session.is_connected()) {
            return true;
        }

        if (!session.is_session_ready()) {
            return true;
        }
    }

    return false;
}

std::pair<std::string, std::string> lockstep_waiting_overlay_text(
    const net::LockstepSession& session,
    const net::LockstepRole role)
{
    if (session.is_awaiting_reconnect_handshake()) {
        return {"LOADING", "SYNCHRONIZING MATCH STATE"};
    }

    if (role == net::LockstepRole::Host && !session.is_connected()) {
        return {"LOADING", "WAITING FOR PLAYER CONNECTION"};
    }

    if (role == net::LockstepRole::Client && !session.is_connected()) {
        return {"LOADING", "CONNECTING TO HOST"};
    }

    if (role == net::LockstepRole::Client && !session.is_session_ready()) {
        return {"LOADING", "WAITING FOR HOST"};
    }

    return {"", ""};
}

} // namespace

int run_graphical_lockstep(sim::Simulation& simulation, const LaunchOptions& options)
{
    if (!net::EnetTransport::global_initialize()) {
        std::cerr << "lockstep: enet_initialize failed\n";
        return 1;
    }

    const net::LockstepRole role =
        options.lockstep_host ? net::LockstepRole::Host : net::LockstepRole::Client;
    const std::uint8_t player_slot = options.lockstep_host
        ? net::constants::LOCKSTEP_HOST_PLAYER_SLOT
        : net::constants::LOCKSTEP_CLIENT_PLAYER_SLOT;

    net::LockstepSession session{role, player_slot, simulation};

    if (options.lockstep_debug) {
        net::LockstepDebugLog::enable(player_slot, role);
    }

    if (options.lockstep_host) {
        const std::uint16_t port =
            options.lockstep_port == 0U ? net::constants::DEFAULT_PORT : options.lockstep_port;
        if (!session.start_host(port)) {
            std::cerr << "lockstep: failed to start host on port " << port << '\n';
            net::EnetTransport::global_deinitialize();
            return 1;
        }

        std::cout << "lockstep-host: listening on port " << port << " (graphical)\n";
    }
    else {
        if (!options.lockstep_join_address.has_value()) {
            std::cerr << "lockstep-join: missing host address\n";
            net::EnetTransport::global_deinitialize();
            return 1;
        }

        const auto parsed_address =
            net::parse_lockstep_join_address(*options.lockstep_join_address);
        if (!parsed_address.has_value()) {
            std::cerr << "lockstep-join: invalid address (expected HOST:PORT)\n";
            net::EnetTransport::global_deinitialize();
            return 1;
        }

        if (!session.connect(parsed_address->first.c_str(), parsed_address->second)) {
            std::cerr << "lockstep-join: failed to connect to " << *options.lockstep_join_address
                      << '\n';
            net::EnetTransport::global_deinitialize();
            return 1;
        }

        std::cout << "lockstep-join: connecting to " << *options.lockstep_join_address
                  << " (graphical)\n";
    }

    sf::ContextSettings context_settings{
        .depthBits = 24U,
        .majorVersion = static_cast<unsigned int>(constants::OPENGL_MAJOR_VERSION),
        .minorVersion = static_cast<unsigned int>(constants::OPENGL_MINOR_VERSION),
    };

    sf::Window window(
        sf::VideoMode({constants::DEFAULT_WINDOW_WIDTH, constants::DEFAULT_WINDOW_HEIGHT}),
        std::string(constants::WINDOW_TITLE),
        sf::State::Windowed,
        context_settings);

    window.setVerticalSyncEnabled(false);
    window.setFramerateLimit(constants::TARGET_DISPLAY_FPS);
    (void)window.setActive(true);
    update_lockstep_window_title(window, session, player_slot);

    WindowDisplaySettings display_settings{};
    display_settings.title = std::string(constants::WINDOW_TITLE);
    display_settings.context_settings = context_settings;
    initialize_window_display_settings(window, display_settings);

    render::GameRenderer renderer{};
    renderer.resize(window.getSize());
    renderer.set_local_player_slot(player_slot);

    GameInput game_input{};
    game_input.set_lockstep_session(&session);
    game_input.set_local_player_slot(player_slot);
    game_input.reset_frame_clock();
    FpsTracker fps_tracker{};

    const std::uint64_t tick_limit = options.lockstep_ticks;

    session.ensure_initial_render_snapshot();
    session.start_background_tick_loop();

    while (window.isOpen()) {
        update_lockstep_window_title(window, session, player_slot);

        const std::shared_ptr<const render::SimRenderSnapshot> input_frame = session.render_snapshot();
        const render::SimRenderSnapshot* input_snapshot = input_frame.get();

        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            }

            game_input.handle_event(*event, window, renderer, simulation, input_snapshot);

            if (const auto* key_pressed = event->getIf<sf::Event::KeyPressed>()) {
                if (key_pressed->code == sf::Keyboard::Key::Escape) {
                    window.close();
                }
                else {
                    handle_display_key(window, renderer, display_settings, key_pressed->code);
                }
            }

            if (const auto* resized = event->getIf<sf::Event::Resized>()) {
                if (!display_settings.fullscreen) {
                    renderer.resize(resized->size, true);
                }
            }
        }

        fps_tracker.record_frame();

        session.service_network_latency();

        if (session.consume_snapshot_restored()) {
            game_input.clear_selection();
            renderer.reset_camera_frame();
        }

        game_input.update_continuous(window, renderer, simulation, input_snapshot);

        app::PlayerSelection selection = game_input.selection();
        app::HoverHighlight hover = game_input.hover();

        const std::shared_ptr<const render::SimRenderSnapshot> frame = session.render_snapshot();
        const bool show_waiting_overlay = lockstep_should_show_waiting_overlay(session, role);
        const auto waiting_text = lockstep_waiting_overlay_text(session, role);

        (void)window.setActive(true);

        if (frame) {
            renderer.draw_snapshot(
                *frame,
                selection.units,
                session.render_interpolation_alpha(),
                game_input.selection_box(),
                hover.unit,
                hover.unit_is_enemy,
                hover.building,
                hover.resource_cell,
                selection.resource_cell,
                selection.building,
                fps_tracker.fps(),
                session.network_hud_stats());

            if (tick_limit > 0U && frame->tick_count >= tick_limit) {
                window.close();
            }
        }
        else {
            renderer.clear_frame();
        }

        if (show_waiting_overlay) {
            renderer.draw_waiting_overlay(waiting_text.first, waiting_text.second);
        }

        window.display();
    }

    session.stop_background_tick_loop();

    if (options.lockstep_debug) {
        net::LockstepDebugLog::disable();
    }

    net::EnetTransport::global_deinitialize();

    if (session.is_desynced()) {
        std::cerr << "lockstep: desync at tick " << session.desync_tick()
                  << " (window closed manually)\n";
    }

    if (session.is_host_gone()) {
        std::cout << "lockstep: host left the game at tick " << simulation.tick_count() << '\n';
    }

    return 0;
}

} // namespace aoa::app
