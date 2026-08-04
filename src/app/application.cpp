#include "app/application.hpp"

#include "app/fps_tracker.hpp"
#include "app/game_input.hpp"
#include "core/constants.hpp"
#include "core/fixed_timestep_loop.hpp"
#include "harness/regression_harness.hpp"
#include "net/lockstep_runner.hpp"
#include "net/net_constants.hpp"
#include "render/game_renderer.hpp"

#include <SFML/Window/Event.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/Window.hpp>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

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

        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: aoa [--headless] [--ticks N] [--expect-hash HEX] [--print-hash]\n"
                         "       aoa --harness\n"
                         "       aoa --net-smoke\n"
                         "       aoa --lockstep-smoke\n"
                         "       aoa --lockstep-host [--port PORT] [--ticks N]\n"
                         "       aoa --lockstep-join HOST:PORT [--ticks N]\n";
            std::exit(0);
        }

        throw std::invalid_argument("Unknown argument: " + arg);
    }

    if (options.headless && options.headless_ticks == 0U) {
        options.headless_ticks = constants::HEADLESS_DEFAULT_TICK_COUNT;
    }

    if ((options.lockstep_host || options.lockstep_join) && options.lockstep_ticks == 0U) {
        options.lockstep_ticks = aoa::net::constants::LOCKSTEP_DEFAULT_TICK_COUNT;
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

    render::GameRenderer renderer{};
    renderer.resize(window.getSize());

    GameInput game_input{};
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
        [&window, &renderer, &simulation, &game_input]() {
            while (const std::optional event = window.pollEvent()) {
                if (event->is<sf::Event::Closed>()) {
                    window.close();
                }

                game_input.handle_event(*event, window, renderer, simulation);

                if (const auto* key_pressed = event->getIf<sf::Event::KeyPressed>()) {
                    if (key_pressed->code == sf::Keyboard::Key::Escape) {
                        window.close();
                    }
                }

                if (const auto* resized = event->getIf<sf::Event::Resized>()) {
                    renderer.resize(resized->size);
                }
            }

            return window.isOpen();
        },
        [&simulation]() { simulation.snapshot_world_positions_for_render(); });

    return 0;
}

} // namespace aoa::app
