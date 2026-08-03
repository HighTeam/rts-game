#pragma once

#include "app/selection_state.hpp"
#include "render/game_renderer.hpp"
#include "sim/player/player_commands.hpp"
#include "sim/simulation.hpp"

#include <SFML/Window/Window.hpp>

#include <chrono>
#include <optional>

namespace aoa::app {

class GameInput {
public:
    void reset_frame_clock();
    void update_continuous(const sf::Window& window, render::GameRenderer& renderer, sim::Simulation& simulation);
    void handle_event(
        const sf::Event& event,
        const sf::Window& window,
        render::GameRenderer& renderer,
        sim::Simulation& simulation);

    [[nodiscard]] const PlayerSelection& selection() const { return selection_; }
    [[nodiscard]] HoverHighlight hover() const { return hover_; }
    [[nodiscard]] render::SelectionBoxOverlay selection_box() const { return selection_box_; }

private:
    [[nodiscard]] sim::player::SelectionModifyMode current_modify_mode() const;
    void update_hover(const sf::Window& window, render::GameRenderer& renderer, sim::Simulation& simulation);
    void finalize_left_release(
        const sf::Window& window,
        render::GameRenderer& renderer,
        sim::Simulation& simulation,
        sf::Vector2i mouse_position);

    PlayerSelection selection_{};
    HoverHighlight hover_{};
    render::SelectionBoxOverlay selection_box_{};
    std::optional<sf::Vector2i> left_press_position_{};
    bool left_button_down_{false};
    std::chrono::steady_clock::time_point previous_frame_time_{};
    bool frame_clock_initialized_{false};
};

} // namespace aoa::app
