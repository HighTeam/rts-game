#pragma once

#include "app/main_menu.hpp"
#include "net/lobby_wire.hpp"
#include "render/hud_overlay.hpp"
#include "render/menu_background.hpp"

#include <SFML/System/Vector2.hpp>

#include <filesystem>

namespace aoa::render {

struct MenuRenderContext {
    const app::MainMenuState* state{nullptr};
    const net::LobbyStateMessage* lobby{nullptr};
    bool is_host{false};
    bool local_ready{false};
    bool can_start{false};
    bool show_perf_hud{false};
    float fps{0.0F};
    sf::Vector2f mouse_position{};
};

// Renders the main menu screens over the slideshow background.
class MenuRenderer {
public:
    MenuRenderer();

    MenuRenderer(const MenuRenderer&) = delete;
    MenuRenderer& operator=(const MenuRenderer&) = delete;

    void load(const std::filesystem::path& assets_directory);
    void resize(sf::Vector2u window_size);
    void reset_graphics_context(sf::Vector2u window_size);
    void update(float delta_seconds);
    void draw(const MenuRenderContext& context);

private:
    void draw_background() const;
    void draw_panel(const app::MenuRect& panel) const;
    void draw_button(const app::MenuButton& button, sf::Vector2f mouse, bool highlighted) const;
    void draw_layout(const app::MenuLayout& layout, const MenuRenderContext& context) const;
    void draw_lobby_rows(const app::MenuLayout& layout, const MenuRenderContext& context) const;
    void draw_settings(const MenuRenderContext& context) const;
    void draw_status_message(const app::MenuLayout& layout, const MenuRenderContext& context) const;
    void draw_perf_hud(const MenuRenderContext& context) const;

    HudOverlay hud_overlay_{};
    MenuBackground background_{};
    sf::Vector2u window_size_{0U, 0U};
    std::filesystem::path assets_directory_{};
};

} // namespace aoa::render
