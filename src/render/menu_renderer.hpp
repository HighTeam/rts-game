#pragma once

#include "app/main_menu.hpp"
#include "net/lobby_wire.hpp"
#include "render/hud_overlay.hpp"
#include "render/menu_background.hpp"
#include "sim/map/map_generator.hpp"

#include <SFML/System/Vector2.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

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
    ~MenuRenderer();

    MenuRenderer(const MenuRenderer&) = delete;
    MenuRenderer& operator=(const MenuRenderer&) = delete;

    void load(const std::filesystem::path& assets_directory);
    void resize(sf::Vector2u window_size);
    void reset_graphics_context(sf::Vector2u window_size);
    void update(float delta_seconds);
    void draw(const MenuRenderContext& context);

private:
    void destroy_present_target() const;
    [[nodiscard]] bool ensure_present_target() const;
    void present_target_to_window() const;
    void draw_background() const;
    void draw_panel(const app::MenuRect& panel) const;
    void draw_button(const app::MenuButton& button, sf::Vector2f mouse, bool highlighted) const;
    void draw_layout(const app::MenuLayout& layout, const MenuRenderContext& context) const;
    void refresh_pattern_preview_cache(const MenuRenderContext& context) const;
    void draw_pattern_preview(const app::MenuLayout& layout, const MenuRenderContext& context) const;
    void draw_lobby_rows(const app::MenuLayout& layout, const MenuRenderContext& context) const;
    void draw_settings(const MenuRenderContext& context) const;
    void draw_status_message(const app::MenuLayout& layout, const MenuRenderContext& context) const;
    void draw_perf_hud(const MenuRenderContext& context) const;

    HudOverlay hud_overlay_{};
    MenuBackground background_{};
    sf::Vector2u window_size_{0U, 0U};
    std::filesystem::path assets_directory_{};
    mutable sim::map::GeneratedMap preview_map_{};
    mutable std::uint8_t preview_pattern_index_{255U};
    mutable std::string preview_payload_{};
    mutable int preview_map_width_{0};
    mutable int preview_map_height_{0};
    mutable std::uint8_t preview_player_count_{0U};
    mutable unsigned int present_fbo_{0U};
    mutable unsigned int present_color_{0U};
    mutable int present_width_{0};
    mutable int present_height_{0};
};

} // namespace aoa::render
