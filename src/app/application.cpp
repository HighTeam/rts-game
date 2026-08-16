#include "app/application.hpp"

#include "app/app_settings.hpp"
#include "app/chat_state.hpp"
#include "app/fps_tracker.hpp"
#include "app/game_cursor.hpp"
#include "app/tps_tracker.hpp"
#include "app/game_input.hpp"
#include "app/window_display.hpp"
#include "audio/game_audio.hpp"
#include "core/runtime_paths.hpp"
#include "core/constants.hpp"
#include "core/fixed_timestep_loop.hpp"
#include "harness/regression_harness.hpp"
#include "net/enet_transport.hpp"
#include "net/lockstep_debug_log.hpp"
#include "net/lockstep_runner.hpp"
#include "net/lockstep_session.hpp"
#include "net/net_constants.hpp"
#include "render/game_renderer.hpp"
#include "sim/components/match_announcements.hpp"
#include "sim/components/match_session.hpp"
#include "sim/components/tags.hpp"
#include "sim/persistence/save_game.hpp"
#include "sim/systems/disconnected_player_ai.hpp"
#include "sim/systems/match_outcome.hpp"
#include "sim/systems/visibility_system.hpp"

#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/Window.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

void validate_lockstep_player_count(const std::uint8_t player_count)
{
    if (player_count != 2U && player_count != 4U && player_count != 8U) {
        throw std::invalid_argument(
            "Invalid player count: " + std::to_string(player_count) + " (expected 2, 4, or 8)");
    }
}

[[nodiscard]] std::string match_chat_player_name(
    const std::array<std::string, aoa::net::constants::LOCKSTEP_MAX_PLAYER_SLOTS>& names,
    const std::uint8_t player_slot)
{
    if (player_slot < names.size() && !names[player_slot].empty()) {
        return names[player_slot];
    }

    return "Player " + std::to_string(static_cast<int>(player_slot) + 1);
}

void drain_match_announcements_to_chat(
    entt::registry& registry,
    ChatState& chat_state,
    const std::uint8_t local_player_slot,
    const std::array<std::string, aoa::net::constants::LOCKSTEP_MAX_PLAYER_SLOTS>& names)
{
    for (const sim::components::MatchAnnouncement& event :
         sim::components::drain_match_announcements(registry)) {
        if (event.kind == sim::components::MatchAnnouncementKind::AgeAdvanced) {
            const std::string name = match_chat_player_name(names, event.player_slot);
            const std::string_view age_name = sim::components::player_age_name(
                static_cast<constants::PlayerAge>(event.age));
            chat_state.push_system_spans({
                ChatTextSpan{name, true, event.player_slot},
                ChatTextSpan{std::string(constants::CHAT_AGE_ADVANCED_MID), false, 0U},
                ChatTextSpan{std::string(age_name), false, 0U},
            });
            continue;
        }

        if (event.kind != sim::components::MatchAnnouncementKind::AttackedBy
            || event.player_slot != local_player_slot) {
            continue;
        }

        const std::string attacker = match_chat_player_name(names, event.other_slot);
        chat_state.push_system_spans({
            ChatTextSpan{std::string(constants::CHAT_ATTACKED_YOU), true, local_player_slot},
            ChatTextSpan{std::string(constants::CHAT_ATTACKED_MID), false, 0U},
            ChatTextSpan{attacker, true, event.other_slot},
        });
    }
}

void apply_lockstep_player_count(LaunchOptions& options, const int player_count)
{
    if (player_count != 2 && player_count != 4 && player_count != 8) {
        throw std::invalid_argument(
            "Invalid player count: " + std::to_string(player_count) + " (expected 2, 4, or 8)");
    }

    options.lockstep_player_count = static_cast<std::uint8_t>(player_count);
}

[[nodiscard]] std::uint8_t resolve_lockstep_host_player_count(const LaunchOptions& options)
{
    validate_lockstep_player_count(options.lockstep_player_count);
    return options.lockstep_player_count;
}

[[nodiscard]] std::uint8_t resolve_lockstep_join_player_slot_impl(const LaunchOptions& options)
{
    validate_lockstep_player_count(options.lockstep_player_count);

    const std::uint8_t player_number = options.lockstep_player_number.value_or(2U);
    if (player_number < 2U || player_number > options.lockstep_player_count) {
        throw std::invalid_argument(
            "Invalid --player-slot: " + std::to_string(player_number)
            + " (expected 2-" + std::to_string(options.lockstep_player_count) + ")");
    }

    return static_cast<std::uint8_t>(player_number - 1U);
}

} // namespace

std::uint8_t resolve_lockstep_join_player_slot(const LaunchOptions& options)
{
    return resolve_lockstep_join_player_slot_impl(options);
}

LaunchOptions parse_launch_options(const int argc, char** argv)
{
    LaunchOptions options{};

    for (int arg_index = 1; arg_index < argc; ++arg_index) {
        const std::string arg = argv[arg_index];

        if (arg == "--headless") {
            options.headless = true;
            continue;
        }

        if (arg == constants::CONSOLE_DEBUG_ARG) {
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

        if (arg == "--lockstep-2h2ai-smoke") {
            options.run_lockstep_2h2ai_smoke = true;
            continue;
        }

        if (arg == "--sim-8ai-bench") {
            options.run_sim_8ai_bench = true;
            continue;
        }

        if (arg == "--lockstep-4-stress-smoke") {
            options.run_lockstep_4_stress_smoke = true;
            continue;
        }

        if (arg == "--lockstep-4-reconnect-smoke") {
            options.run_lockstep_4_reconnect_smoke = true;
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

        if (arg == "--lockstep-auto-input") {
            options.lockstep_auto_input = true;
            continue;
        }

        if (arg == "--loose-assets") {
            options.prefer_loose_assets = true;
            continue;
        }

        if (arg == "--lockstep-players" && arg_index + 1 < argc) {
            apply_lockstep_player_count(options, std::stoi(argv[++arg_index]));
            continue;
        }

        if (arg == "--players" && arg_index + 1 < argc) {
            apply_lockstep_player_count(options, std::stoi(argv[++arg_index]));
            continue;
        }

        if (arg == "--player-slot" && arg_index + 1 < argc) {
            const int player_slot = std::stoi(argv[++arg_index]);
            if (player_slot < 2 || player_slot > net::constants::LOCKSTEP_MAX_PLAYER_SLOTS) {
                throw std::invalid_argument(
                    "Invalid --player-slot: " + std::to_string(player_slot));
            }

            options.lockstep_player_number = static_cast<std::uint8_t>(player_slot);
            continue;
        }

        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: aoa [--headless] [--ticks N] [--expect-hash HEX] [--print-hash]\n"
                         "       aoa [--loose-assets] [--console-debug]\n"
                         "       aoa --harness\n"
                         "       aoa --net-smoke\n"
                         "       aoa --lockstep-smoke\n"
                         "       aoa --lockstep-disconnect-smoke\n"
                         "       aoa --lockstep-reconnect-smoke\n"
                         "       aoa --lockstep-4-smoke\n"
                         "       aoa --lockstep-4-stress-smoke\n"
                         "       aoa --snapshot-smoke\n"
                         "       aoa --lockstep-host [--port PORT] [--players N | --lockstep-players N]\n"
                         "                           [--headless] [--ticks N] [--lockstep-debug]\n"
                         "       aoa --lockstep-join HOST:PORT [--player-slot N]\n"
                         "                            [--players N | --lockstep-players N]\n"
                         "                            [--headless] [--ticks N] [--lockstep-debug]\n";
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

AppFlow run_graphical(
    sf::Window& window,
    WindowDisplaySettings& display_settings,
    sim::Simulation& simulation,
    AppShellSettings& shell_settings,
    const SingleplayerSetup& setup)
{
    render::GameRenderer renderer{};
    renderer.reset_graphics_context(window.getSize());
    renderer.set_local_player_slot(0U);

    GameInput game_input{};
    ChatState chat_state{};
    GameCursor game_cursor{};
    audio::GameAudio game_audio{};
    (void)game_cursor.load(core::default_assets_directory());
    (void)game_audio.load(core::default_assets_directory());
    game_audio.set_master_volume(shell_settings.master_volume);
    game_audio.set_music_volume(shell_settings.music_volume);
    game_audio.set_sfx_volume(shell_settings.sfx_volume);
    game_input.set_local_player_slot(0U);
    game_input.set_match_roster(false, setup.player_names);
    game_input.set_chat_state(&chat_state);
    game_input.set_game_cursor(&game_cursor);
    game_input.set_game_audio(&game_audio);
    chat_state.set_message_hook(
        [&game_audio](const std::uint8_t /*player_slot*/, const std::string& text) {
            game_audio.play_chat_reaction(text);
        });
    game_input.reset_frame_clock();
    FpsTracker fps_tracker{};
    TpsTracker tps_tracker{};

    if (shell_settings.fullscreen != display_settings.fullscreen) {
        if (shell_settings.fullscreen) {
            enter_fullscreen(window, renderer, display_settings);
        }
        else {
            leave_fullscreen(window, renderer, display_settings);
        }
    }

    if (shell_settings.vsync != display_settings.vsync
        || shell_settings.fps_limit != display_settings.fps_limit) {
        display_settings.vsync = shell_settings.vsync;
        display_settings.fps_limit = shell_settings.fps_limit;
        apply_window_frame_limits(window, display_settings);
    }

    game_input.set_menu_fullscreen(display_settings.fullscreen);
    game_input.set_menu_video(display_settings.vsync, display_settings.fps_limit);
    display_settings.mouse_capture = shell_settings.mouse_capture;
    game_input.set_menu_mouse_capture(display_settings.mouse_capture);
    apply_mouse_capture(window, display_settings);
    game_input.set_scroll_speed(shell_settings.scroll_speed);
    renderer.set_show_perf_hud(shell_settings.show_perf_hud);
    game_cursor.force_reapply(window);

    {
        const auto world_view =
            simulation.registry().view<sim::components::WorldTag, sim::components::MatchSession>();
        if (world_view.begin() != world_view.end()) {
            auto& session = world_view.get<sim::components::MatchSession>(*world_view.begin());
            session.civil_population_map_cap = setup.civil_population_map_cap;
            session.fog_of_war_mode = setup.fog_mode;
            session.fog_of_war_enabled =
                setup.fog_mode != sim::components::FogOfWarMode::Disabled;
            session.cheats_enabled = setup.cheats_enabled;
            session.player_color_indices = setup.slot_colors;
            sim::components::apply_lobby_teams_to_session(
                session,
                setup.slot_teams,
                setup.player_count,
                setup.map_seed);
            session.victory_condition = setup.victory_condition;
            if (setup.player_count > 0U
                && setup.player_count <= static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)) {
                session.playing_slots_mask =
                    static_cast<std::uint8_t>((1U << setup.player_count) - 1U);
            }
        }
    }
    renderer.set_fog_of_war_enabled(
        setup.fog_mode != sim::components::FogOfWarMode::Disabled);
    if (setup.fog_mode == sim::components::FogOfWarMode::Explored) {
        sim::systems::reveal_all_explored(simulation.registry());
    }
    game_cursor.set_player_color(
        static_cast<CursorPlayerColor>(setup.slot_colors[0]));
    for (std::uint8_t slot = 0U; slot < static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS);
         ++slot) {
        if (setup.slot_is_ai[slot]) {
            simulation.set_player_ai_controlled(slot, true);
        }
    }
    simulation.set_compute_state_hash(false);
    std::uint64_t ai_command_sequence = 1U;
    AppFlow flow = AppFlow::ExitApp;
    auto last_autosave_time = std::chrono::steady_clock::now();

    core::FixedTimestepLoop loop{};
    loop.run_realtime(
        [&simulation, &tps_tracker, &game_input, &ai_command_sequence, &last_autosave_time]() {
            if (game_input.is_game_menu_open()) {
                return;
            }

            if (sim::systems::match_is_finished(simulation.registry())) {
                return;
            }

            const std::uint64_t execute_tick = simulation.next_command_execute_tick();
            for (std::uint8_t slot = 0U;
                 slot < static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS);
                 ++slot) {
                if (!simulation.is_player_ai_controlled(slot)) {
                    continue;
                }

                if ((simulation.tick_count() + static_cast<std::uint64_t>(slot))
                        % static_cast<std::uint64_t>(constants::AI_THINK_INTERVAL_TICKS)
                    != 0U) {
                    continue;
                }

                std::vector<sim::player::PlayerCommand> ai_commands =
                    sim::systems::generate_ai_commands_for_slot(
                        simulation.registry(),
                        slot,
                        execute_tick,
                        ai_command_sequence);
                for (sim::player::PlayerCommand& command : ai_commands) {
                    simulation.enqueue_player_command(std::move(command));
                }
            }

            simulation.tick();
            tps_tracker.record_tick();

            const auto now = std::chrono::steady_clock::now();
            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last_autosave_time);
            if (elapsed_ms.count() >= constants::AUTOSAVE_INTERVAL_MS) {
                if (sim::persistence::save_simulation_to_file(
                        simulation,
                        sim::persistence::default_autosave_path())) {
                    last_autosave_time = now;
                }
            }
        },
        [&renderer,
         &simulation,
         &window,
         &game_input,
         &fps_tracker,
         &tps_tracker,
         &game_audio,
         &chat_state,
         &setup](
            const float interpolation_alpha) {
            fps_tracker.record_frame();
            game_audio.update();
            game_audio.drain_sim_sfx(
                simulation.registry(),
                renderer,
                renderer.local_player_slot());
            drain_match_announcements_to_chat(
                simulation.registry(),
                chat_state,
                0U,
                setup.player_names);
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
                fps_tracker.fps(),
                tps_tracker.tps(),
                {},
                game_input.make_hud_context(window, simulation, nullptr),
                game_input.placement_ghost_anchor(),
                game_input.placement_ghost_valid());
            window.display();
        },
        [&window,
         &renderer,
         &simulation,
         &game_input,
         &display_settings,
         &game_cursor,
         &flow,
         &shell_settings,
         &setup]() {
            game_input.set_menu_fullscreen(display_settings.fullscreen);

            while (const std::optional event = window.pollEvent()) {
                if (event->is<sf::Event::Closed>()) {
                    window.close();
                }

                (void)game_input.handle_event(*event, window, renderer, simulation);

                if (game_input.consume_exit_game_request()) {
                    window.close();
                }

                if (game_input.consume_fullscreen_toggle_request()) {
                    if (display_settings.fullscreen) {
                        leave_fullscreen(window, renderer, display_settings);
                    }
                    else {
                        enter_fullscreen(window, renderer, display_settings);
                    }
                    game_input.set_menu_fullscreen(display_settings.fullscreen);
                    shell_settings.fullscreen = display_settings.fullscreen;
                    save_app_settings(shell_settings);
                    game_cursor.force_reapply(window);
                }

                if (game_input.consume_video_apply_request()) {
                    display_settings.vsync = game_input.menu_vsync();
                    display_settings.fps_limit = game_input.menu_fps_limit();
                    display_settings.mouse_capture = game_input.menu_mouse_capture();
                    apply_window_frame_limits(window, display_settings);
                    apply_mouse_capture(window, display_settings);
                    shell_settings.vsync = display_settings.vsync;
                    shell_settings.fps_limit = display_settings.fps_limit;
                    shell_settings.mouse_capture = display_settings.mouse_capture;
                    save_app_settings(shell_settings);
                }

                if (const auto* key_pressed = event->getIf<sf::Event::KeyPressed>()) {
                    if (key_pressed->code != sf::Keyboard::Key::Escape) {
                        if (handle_display_key(
                                window,
                                renderer,
                                display_settings,
                                key_pressed->code,
                                setup.cheats_enabled,
                                setup.cheats_enabled)) {
                            game_input.set_menu_fullscreen(display_settings.fullscreen);
                            game_cursor.force_reapply(window);
                        }
                        shell_settings.show_perf_hud = renderer.show_perf_hud();
                    }
                }

                if (game_input.consume_exit_to_main_menu_request()) {
                    flow = AppFlow::ReturnToMainMenu;
                    return false;
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

    shell_settings.fullscreen = display_settings.fullscreen;
    shell_settings.vsync = display_settings.vsync;
    shell_settings.fps_limit = display_settings.fps_limit;
    shell_settings.show_perf_hud = renderer.show_perf_hud();
    shell_settings.master_volume = game_audio.master_volume();
    shell_settings.music_volume = game_audio.music_volume();
    shell_settings.sfx_volume = game_audio.sfx_volume();
    shell_settings.scroll_speed = game_input.scroll_speed();
    save_app_settings(shell_settings);
    return flow;
}

namespace {

void update_lockstep_window_title(
    sf::Window& window,
    const net::LockstepSession& session,
    const net::LockstepRole role,
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
    else if (session.is_awaiting_reconnect_handshake()) {
        title += " - Synchronizing match data...";
    }
    else if (role == net::LockstepRole::Client && !session.is_session_ready() && session.is_connected()) {
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
    else if (session.has_disconnected_human_slots()) {
        title += " - Player "
            + std::to_string(session.first_disconnected_human_slot() + 1U)
            + " left — AI playing (waiting for reconnect)";
    }
    else if (session.is_waiting_for_opponent_reconnect()) {
        title += " - Opponent disconnected, waiting for reconnect...";
    }
    else if (role == net::LockstepRole::Host && !session.is_lobby_full()) {
        const int connected_players =
            static_cast<int>(session.lobby_registered_client_count()) + 1;
        title += " - Waiting for players (" + std::to_string(connected_players) + "/"
            + std::to_string(session.expected_human_player_count()) + ")...";
    }
    else if (!session.is_connected() && session.expected_human_player_count() > 1U) {
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
    if (session.is_desynced() || session.is_host_gone()) {
        return false;
    }

    if (role == net::LockstepRole::Client
        && (session.is_awaiting_reconnect_handshake() || !session.is_session_ready())) {
        return true;
    }

    if (role == net::LockstepRole::Host && session.is_awaiting_reconnect_handshake()
        && session.session_player_count() <= 2U) {
        return true;
    }

    // Centered LOADING is pre-match only. Mid-game disconnect/AI uses chat.
    if (session.is_match_started() || session.is_reconnecting() || session.is_ai_fallback()
        || session.has_disconnected_human_slots()) {
        return false;
    }

    if (role == net::LockstepRole::Host && !session.is_lobby_full()) {
        return true;
    }

    if (role == net::LockstepRole::Host && !session.is_connected()
        && session.expected_human_player_count() > 1U) {
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
    if (session.is_awaiting_reconnect_handshake()
        || (role == net::LockstepRole::Client && session.is_connected()
            && !session.is_session_ready())) {
        return {"LOADING", "SYNCHRONIZING MATCH DATA"};
    }
    if (role == net::LockstepRole::Host && !session.is_lobby_full()) {
        const int connected_players =
            static_cast<int>(session.lobby_registered_client_count()) + 1;
        return {
            "LOADING",
            "WAITING FOR PLAYERS (" + std::to_string(connected_players) + "/"
                + std::to_string(session.expected_human_player_count()) + ")"};
    }

    if (role == net::LockstepRole::Host && !session.is_connected()
        && !session.is_ai_fallback() && session.expected_human_player_count() > 1U) {
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

AppFlow run_lockstep_match(
    sf::Window& window,
    WindowDisplaySettings& display_settings,
    sim::Simulation& simulation,
    const LockstepMatchSetup& setup,
    AppShellSettings& shell_settings)
{
    const net::LockstepRole role =
        setup.is_host ? net::LockstepRole::Host : net::LockstepRole::Client;
    const std::uint8_t session_player_count = setup.player_count;
    const std::uint8_t player_slot = setup.player_slot;

    {
        const auto world_view =
            simulation.registry().view<sim::components::WorldTag, sim::components::MatchSession>();
        if (world_view.begin() != world_view.end()) {
            auto& match_session = world_view.get<sim::components::MatchSession>(*world_view.begin());
            match_session.civil_population_map_cap = setup.civil_population_map_cap;
            match_session.fog_of_war_mode = setup.fog_mode;
            match_session.fog_of_war_enabled =
                setup.fog_mode != sim::components::FogOfWarMode::Disabled;
            match_session.cheats_enabled = setup.cheats_enabled;
            match_session.player_color_indices = setup.slot_colors;
            sim::components::apply_lobby_teams_to_session(
                match_session,
                setup.slot_teams,
                setup.player_count,
                setup.map_seed);
            match_session.victory_condition = setup.victory_condition;
            if (setup.player_count > 0U
                && setup.player_count <= static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)) {
                match_session.playing_slots_mask =
                    static_cast<std::uint8_t>((1U << setup.player_count) - 1U);
            }
        }
    }

    if (setup.fog_mode == sim::components::FogOfWarMode::Explored) {
        sim::systems::reveal_all_explored(simulation.registry());
    }

    for (std::uint8_t slot = 0U; slot < static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS);
         ++slot) {
        if (setup.slot_is_ai[slot]) {
            simulation.set_player_ai_controlled(slot, true);
        }
    }

    net::LockstepSession session{role, player_slot, simulation, session_player_count};
    session.configure_match_identity(setup.player_names, setup.reconnect_tokens);
    session.configure_ai_slots(setup.slot_is_ai);
    {
        net::LobbySettings lobby_settings = setup.lobby_settings;
        lobby_settings.player_count = setup.player_count;
        lobby_settings.civil_population_map_cap =
            static_cast<std::uint8_t>(std::clamp(setup.civil_population_map_cap, 0, 255));
        lobby_settings.fog_of_war_enabled = setup.fog_of_war_enabled;
        lobby_settings.fog_mode = static_cast<std::uint8_t>(setup.fog_mode);
        lobby_settings.cheats_enabled = setup.cheats_enabled;
        lobby_settings.victory_condition = static_cast<std::uint8_t>(setup.victory_condition);
        if (lobby_settings.map_seed == 0U) {
            lobby_settings.map_seed = setup.map_seed;
        }
        if (lobby_settings.pattern_payload.empty()) {
            lobby_settings.pattern_payload = setup.pattern_payload;
        }
        session.configure_lobby_settings(lobby_settings, setup.slot_teams, setup.slot_colors);
    }

    if (setup.lockstep_debug) {
        net::LockstepDebugLog::enable(player_slot, role);
    }

    session.set_auto_input_enabled(setup.auto_input);

    if (setup.is_host) {
        if (!session.start_host(setup.port)) {
            std::cerr << "lockstep: failed to start host on port " << setup.port << '\n';
            return AppFlow::ReturnToMainMenu;
        }

        std::cout << "lockstep-host: listening on port " << setup.port << " for "
                  << static_cast<int>(session_player_count) << " players (graphical)\n";
    }
    else {
        if (setup.delay_before_connect) {
            const int slot_stagger_ms = static_cast<int>(player_slot - 1U)
                * constants::MULTIPLAYER_MATCH_CONNECT_SLOT_STAGGER_MS;
            std::this_thread::sleep_for(std::chrono::milliseconds(
                constants::MULTIPLAYER_MATCH_CONNECT_DELAY_MS + slot_stagger_ms));
        }

        if (!session.connect(setup.host_address.c_str(), setup.port)) {
            std::cerr << "lockstep-join: failed to connect to " << setup.host_address << ':'
                      << setup.port << '\n';
            return AppFlow::ReturnToMainMenu;
        }

        std::cout << "lockstep-join: connecting to " << setup.host_address << ':' << setup.port
                  << " as player " << static_cast<int>(player_slot + 1U) << " (graphical)\n";
    }

    update_lockstep_window_title(window, session, role, player_slot);

    render::GameRenderer renderer{};
    renderer.reset_graphics_context(window.getSize());
    renderer.set_local_player_slot(player_slot);
    renderer.set_fog_of_war_enabled(
        setup.fog_mode != sim::components::FogOfWarMode::Disabled);

    GameInput game_input{};
    ChatState chat_state{};
    GameCursor game_cursor{};
    audio::GameAudio game_audio{};
    (void)game_cursor.load(core::default_assets_directory());
    if (setup.player_slot < setup.slot_colors.size()) {
        game_cursor.set_player_color(
            static_cast<CursorPlayerColor>(setup.slot_colors[setup.player_slot]));
    }

    (void)game_audio.load(core::default_assets_directory());
    game_audio.set_master_volume(shell_settings.master_volume);
    game_audio.set_music_volume(shell_settings.music_volume);
    game_audio.set_sfx_volume(shell_settings.sfx_volume);
    game_input.set_lockstep_session(&session);
    game_input.set_local_player_slot(player_slot);
    game_input.set_match_roster(true, setup.player_names);
    game_input.set_chat_state(&chat_state);
    game_input.set_game_cursor(&game_cursor);
    game_input.set_game_audio(&game_audio);
    chat_state.set_message_hook(
        [&game_audio](const std::uint8_t /*player_slot*/, const std::string& text) {
            game_audio.play_chat_reaction(text);
        });
    session.set_chat_state(&chat_state);
    game_input.reset_frame_clock();
    FpsTracker fps_tracker{};
    TpsTracker tps_tracker{};

    if (shell_settings.fullscreen != display_settings.fullscreen) {
        if (shell_settings.fullscreen) {
            enter_fullscreen(window, renderer, display_settings);
        }
        else {
            leave_fullscreen(window, renderer, display_settings);
        }
    }

    if (shell_settings.vsync != display_settings.vsync
        || shell_settings.fps_limit != display_settings.fps_limit) {
        display_settings.vsync = shell_settings.vsync;
        display_settings.fps_limit = shell_settings.fps_limit;
        apply_window_frame_limits(window, display_settings);
    }

    game_input.set_menu_fullscreen(display_settings.fullscreen);
    game_input.set_menu_video(display_settings.vsync, display_settings.fps_limit);
    display_settings.mouse_capture = shell_settings.mouse_capture;
    game_input.set_menu_mouse_capture(display_settings.mouse_capture);
    apply_mouse_capture(window, display_settings);
    game_input.set_scroll_speed(shell_settings.scroll_speed);
    renderer.set_show_perf_hud(shell_settings.show_perf_hud);
    game_cursor.force_reapply(window);

    const std::uint64_t tick_limit = setup.tick_limit;
    AppFlow flow = AppFlow::ExitApp;
    bool leave_match = false;
    auto last_autosave_time = std::chrono::steady_clock::now();

    session.ensure_initial_render_snapshot();
    session.start_background_tick_loop();

    while (window.isOpen() && !leave_match) {
        update_lockstep_window_title(window, session, role, player_slot);

        const std::shared_ptr<const render::SimRenderSnapshot> input_frame = session.render_snapshot();
        const render::SimRenderSnapshot* input_snapshot = input_frame.get();

        game_input.set_menu_fullscreen(display_settings.fullscreen);

        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            }

            (void)game_input.handle_event(*event, window, renderer, simulation, input_snapshot);

            if (game_input.consume_exit_game_request()) {
                window.close();
            }

            if (game_input.consume_fullscreen_toggle_request()) {
                if (display_settings.fullscreen) {
                    leave_fullscreen(window, renderer, display_settings);
                }
                else {
                    enter_fullscreen(window, renderer, display_settings);
                }
                game_input.set_menu_fullscreen(display_settings.fullscreen);
                shell_settings.fullscreen = display_settings.fullscreen;
                save_app_settings(shell_settings);
                game_cursor.force_reapply(window);
            }

            if (game_input.consume_video_apply_request()) {
                display_settings.vsync = game_input.menu_vsync();
                display_settings.fps_limit = game_input.menu_fps_limit();
                display_settings.mouse_capture = game_input.menu_mouse_capture();
                apply_window_frame_limits(window, display_settings);
                apply_mouse_capture(window, display_settings);
                shell_settings.vsync = display_settings.vsync;
                shell_settings.fps_limit = display_settings.fps_limit;
                shell_settings.mouse_capture = display_settings.mouse_capture;
                save_app_settings(shell_settings);
            }

            if (const auto* key_pressed = event->getIf<sf::Event::KeyPressed>()) {
                if (key_pressed->code != sf::Keyboard::Key::Escape) {
                    if (handle_display_key(
                            window,
                            renderer,
                            display_settings,
                            key_pressed->code,
                            setup.cheats_enabled,
                            setup.cheats_enabled)) {
                        game_input.set_menu_fullscreen(display_settings.fullscreen);
                        game_cursor.force_reapply(window);
                    }
                    shell_settings.show_perf_hud = renderer.show_perf_hud();
                }
            }

            if (game_input.consume_exit_to_main_menu_request()) {
                flow = AppFlow::ReturnToMainMenu;
                leave_match = true;
                break;
            }

            if (const auto* resized = event->getIf<sf::Event::Resized>()) {
                if (!display_settings.fullscreen) {
                    renderer.resize(resized->size, true);
                }
            }
        }

        if (leave_match) {
            break;
        }

        fps_tracker.record_frame();
        game_audio.update();
        {
            std::lock_guard lock(session.simulation_access_mutex());
            game_audio.drain_sim_sfx(
                simulation.registry(),
                renderer,
                renderer.local_player_slot());
            drain_match_announcements_to_chat(
                simulation.registry(),
                chat_state,
                player_slot,
                setup.player_names);
        }

        session.service_network_latency();

        if (session.consume_snapshot_restored()) {
            game_input.clear_selection();
            renderer.reset_camera_frame();
        }

        if (setup.is_host && !game_input.is_game_menu_open()) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last_autosave_time);
            if (elapsed_ms.count() >= constants::AUTOSAVE_INTERVAL_MS) {
                std::lock_guard lock(session.simulation_access_mutex());
                if (sim::persistence::save_simulation_to_file(
                        simulation,
                        sim::persistence::default_autosave_mp_path())) {
                    last_autosave_time = now;
                }
            }
        }

        game_input.update_continuous(window, renderer, simulation, input_snapshot);

        app::PlayerSelection selection = game_input.selection();
        app::HoverHighlight hover = game_input.hover();

        const std::shared_ptr<const render::SimRenderSnapshot> frame = session.render_snapshot();
        const bool show_waiting_overlay = lockstep_should_show_waiting_overlay(session, role);
        const auto waiting_text = lockstep_waiting_overlay_text(session, role);

        (void)window.setActive(true);

        if (frame) {
            tps_tracker.observe_tick_count(frame->tick_count);
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
                tps_tracker.tps(),
                session.network_hud_stats(),
                game_input.make_hud_context(window, simulation, frame.get()),
                game_input.placement_ghost_anchor(),
                game_input.placement_ghost_valid());

            if (tick_limit > 0U && frame->tick_count >= tick_limit) {
                leave_match = true;
                flow = AppFlow::ExitApp;
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
    session.disconnect_transport();

    if (setup.lockstep_debug) {
        net::LockstepDebugLog::disable();
    }

    if (session.is_desynced()) {
        std::cerr << "lockstep: desync at tick " << session.desync_tick()
                  << " (window closed manually)\n";
    }

    if (session.is_host_gone()) {
        std::cout << "lockstep: host left the game at tick " << simulation.tick_count() << '\n';
    }

    shell_settings.fullscreen = display_settings.fullscreen;
    shell_settings.vsync = display_settings.vsync;
    shell_settings.fps_limit = display_settings.fps_limit;
    shell_settings.show_perf_hud = renderer.show_perf_hud();
    shell_settings.master_volume = game_audio.master_volume();
    shell_settings.music_volume = game_audio.music_volume();
    shell_settings.sfx_volume = game_audio.sfx_volume();
    shell_settings.scroll_speed = game_input.scroll_speed();
    save_app_settings(shell_settings);
    return flow;
}

int run_graphical_lockstep(sim::Simulation& simulation, const LaunchOptions& options)
{
    if (!net::EnetTransport::global_initialize()) {
        std::cerr << "lockstep: enet_initialize failed\n";
        return 1;
    }

    LockstepMatchSetup setup{};
    setup.is_host = options.lockstep_host;
    setup.player_count = resolve_lockstep_host_player_count(options);
    setup.player_slot = options.lockstep_host
        ? net::constants::LOCKSTEP_HOST_PLAYER_SLOT
        : resolve_lockstep_join_player_slot(options);
    setup.port =
        options.lockstep_port == 0U ? net::constants::DEFAULT_PORT : options.lockstep_port;
    setup.lockstep_debug = options.lockstep_debug;
    setup.auto_input = options.lockstep_auto_input;
    setup.tick_limit = options.lockstep_ticks;

    if (!options.lockstep_host) {
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

        setup.host_address = parsed_address->first;
        setup.port = parsed_address->second;
    }

    sf::ContextSettings context_settings{
        .depthBits = 24U,
        .majorVersion = static_cast<unsigned int>(constants::OPENGL_MAJOR_VERSION),
        .minorVersion = static_cast<unsigned int>(constants::OPENGL_MINOR_VERSION),
    };

    WindowDisplaySettings display_settings{};
    display_settings.title = std::string(constants::WINDOW_TITLE);
    display_settings.context_settings = context_settings;

    sf::Window window(
        sf::VideoMode({constants::DEFAULT_WINDOW_WIDTH, constants::DEFAULT_WINDOW_HEIGHT}),
        std::string(constants::WINDOW_TITLE),
        sf::State::Windowed,
        context_settings);
    apply_window_frame_limits(window, display_settings);
    (void)window.setActive(true);
    initialize_window_display_settings(window, display_settings);

    AppShellSettings shell_settings = load_app_settings();
    display_settings.vsync = shell_settings.vsync;
    display_settings.fps_limit = shell_settings.fps_limit;
    display_settings.mouse_capture = shell_settings.mouse_capture;
    apply_window_frame_limits(window, display_settings);
    apply_mouse_capture(window, display_settings);
    if (shell_settings.fullscreen) {
        enter_fullscreen(
            window,
            [](const sf::Vector2u /*size*/) {},
            display_settings);
    }

    (void)run_lockstep_match(window, display_settings, simulation, setup, shell_settings);

    if (window.isOpen()) {
        window.close();
    }

    net::EnetTransport::global_deinitialize();
    return 0;
}

} // namespace aoa::app
