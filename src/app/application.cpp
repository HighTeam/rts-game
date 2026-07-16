#include "app/application.hpp"

#include "core/constants.hpp"
#include "core/fixed_timestep_loop.hpp"
#include "render/gl_renderer.hpp"

#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/Window.hpp>

#include <iostream>
#include <optional>
#include <cstdlib>
#include <string>

namespace aoa::app {

LaunchOptions parse_launch_options(const int argc, char** argv)
{
    LaunchOptions options{};

    for (int arg_index = 1; arg_index < argc; ++arg_index) {
        const std::string arg = argv[arg_index];

        if (arg == "--headless") {
            options.headless = true;
            continue;
        }

        if (arg == "--ticks" && arg_index + 1 < argc) {
            options.headless_ticks = static_cast<std::uint64_t>(
                std::stoull(argv[++arg_index]));
            continue;
        }

        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: aoa [--headless] [--ticks N]\n";
            std::exit(0);
        }

        throw std::invalid_argument("Unknown argument: " + arg);
    }

    if (options.headless && options.headless_ticks == 0U) {
        options.headless_ticks = constants::HEADLESS_DEFAULT_TICK_COUNT;
    }

    return options;
}

int run_headless(sim::Simulation& simulation, const std::uint64_t tick_count)
{
    core::FixedTimestepLoop loop{};
    loop.run_headless([&simulation]() { simulation.tick(); }, tick_count);
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

    render::GlRenderer renderer{};
    renderer.resize(window.getSize());

    core::FixedTimestepLoop loop{};
    loop.run_realtime(
        [&simulation]() { simulation.tick(); },
        [&renderer, &window](const float interpolation_alpha) {
            renderer.draw_triangle(interpolation_alpha);
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
