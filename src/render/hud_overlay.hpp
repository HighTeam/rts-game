#pragma once

#include "net/lockstep_network_hud.hpp"
#include "render/sim_render_snapshot.hpp"
#include "sim/simulation.hpp"

#include <SFML/System/Vector2.hpp>

#include <cstdint>
#include <string>

namespace aoa::render {

class HudOverlay {
public:
    void draw(
        const sim::Simulation& simulation,
        sf::Vector2u window_size,
        float fps,
        std::uint8_t local_player_slot,
        float camera_zoom,
        const net::LockstepNetworkHudStats& network_stats = {}) const;

    void draw_snapshot(
        const SimRenderSnapshot& snapshot,
        sf::Vector2u window_size,
        float fps,
        std::uint8_t local_player_slot,
        float camera_zoom,
        const net::LockstepNetworkHudStats& network_stats = {}) const;

    void draw_waiting_overlay(
        sf::Vector2u window_size,
        const std::string& title,
        const std::string& subtitle) const;

    void invalidate_gl_cache();

private:
    [[nodiscard]] unsigned int hud_shader_program() const;

    void draw_string(
        sf::Vector2u window_size,
        float x,
        float y,
        const std::string& text,
        float r,
        float g,
        float b) const;

    void draw_string_scaled(
        sf::Vector2u window_size,
        float x,
        float y,
        const std::string& text,
        int pixel_scale,
        float r,
        float g,
        float b) const;

    mutable unsigned int hud_shader_program_{0U};
};

} // namespace aoa::render
