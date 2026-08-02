#include "app/application.hpp"

#include "core/constants.hpp"
#include "core/fixed_timestep_loop.hpp"
#include "harness/regression_harness.hpp"
#include "render/game_renderer.hpp"

#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>
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

        if (arg == "--print-hash") {
            options.print_state_hash = true;
            continue;
        }

        if (arg == "--expect-hash" && arg_index + 1 < argc) {
            options.expect_state_hash = parse_hash_argument(argv[++arg_index]);
            continue;
        }

        if (arg == "--ticks" && arg_index + 1 < argc) {
            options.headless_ticks = static_cast<std::uint64_t>(
                std::stoull(argv[++arg_index]));
            continue;
        }

        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: aoa [--headless] [--ticks N] [--expect-hash HEX] [--print-hash]\n"
                         "       aoa --harness\n";
            std::exit(0);
        }

        throw std::invalid_argument("Unknown argument: " + arg);
    }

    if (options.headless && options.headless_ticks == 0U) {
        options.headless_ticks = constants::HEADLESS_DEFAULT_TICK_COUNT;
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

    window.setVerticalSyncEnabled(true);
    (void)window.setActive(true);

    render::GameRenderer renderer{};
    renderer.resize(window.getSize());

    core::FixedTimestepLoop loop{};
    loop.run_realtime(
        [&simulation]() { simulation.tick(); },
        [&renderer, &simulation, &window](const float /*interpolation_alpha*/) {
            (void)window.setActive(true);
            renderer.draw(simulation);
            window.display();
        },
        [&window, &renderer]() {
            while (const std::optional event = window.pollEvent()) {
                if (event->is<sf::Event::Closed>()) {
                    window.close();
                }

                if (const auto* key_pressed = event->getIf<sf::Event::KeyPressed>()) {
                    if (key_pressed->code == sf::Keyboard::Key::Escape) {
                        window.close();
                    }

                    if (key_pressed->code == sf::Keyboard::Key::Up) {
                        renderer.pan_camera(0.0F, constants::CAMERA_CLASSIC_PAN_STEP);
                    }
                    if (key_pressed->code == sf::Keyboard::Key::Down) {
                        renderer.pan_camera(0.0F, -constants::CAMERA_CLASSIC_PAN_STEP);
                    }
                    if (key_pressed->code == sf::Keyboard::Key::Left) {
                        renderer.pan_camera(constants::CAMERA_CLASSIC_PAN_STEP, 0.0F);
                    }
                    if (key_pressed->code == sf::Keyboard::Key::Right) {
                        renderer.pan_camera(-constants::CAMERA_CLASSIC_PAN_STEP, 0.0F);
                    }
                }

                if (const auto* scroll = event->getIf<sf::Event::MouseWheelScrolled>()) {
                    renderer.zoom_camera(scroll->delta * constants::CAMERA_CLASSIC_ZOOM_STEP);
                }

                if (const auto* resized = event->getIf<sf::Event::Resized>()) {
                    renderer.resize(resized->size);
                }
            }

            return window.isOpen();
        });

    return 0;
}

} // namespace aoa::app
