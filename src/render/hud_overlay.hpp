#pragma once

#include "app/chat_state.hpp"
#include "app/command_panel.hpp"
#include "app/game_menu.hpp"
#include "core/grid.hpp"
#include "net/lockstep_network_hud.hpp"
#include "render/hud_icons.hpp"
#include "render/sim_render_snapshot.hpp"
#include "sim/simulation.hpp"

#include <SFML/System/Vector2.hpp>

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace aoa::render {

struct HudUnitContext {
    entt::entity selected_single_unit{entt::null};
    entt::entity hover_unit{entt::null};
    bool hover_unit_is_enemy{false};
    std::optional<core::GridPos> selected_resource_cell{};
    app::CommandPanelMode command_panel_mode{app::CommandPanelMode::Empty};
    app::CommandPanelBuildOptions build_options{};
    sf::Vector2f mouse_screen_position{};
    bool has_selected_building_health{false};
    int selected_building_health_current{0};
    int selected_building_health_max{0};
    bool selected_building_is_house{false};
    bool has_selected_building_owner{false};
    std::uint8_t selected_building_player_slot{0U};
    int command_panel_pressed_slot{-1};
    bool chat_composing{false};
    std::string chat_draft{};
    std::deque<app::ChatLine> chat_lines{};
    bool has_camera_view{false};
    float camera_world_min_x{0.0F};
    float camera_world_min_z{0.0F};
    float camera_world_max_x{0.0F};
    float camera_world_max_z{0.0F};
    app::GameMenuState game_menu{};
};

struct HudInfoPanel {
    bool active{false};
    std::string title{};
    float title_r{1.0F};
    float title_g{1.0F};
    float title_b{1.0F};
    bool has_health{false};
    int health_current{0};
    int health_max{0};
    bool has_combat_stats{false};
    int melee_attack{0};
    int melee_armor{0};
    int pierce_attack{0};
    int pierce_armor{0};
    int carry_amount{0};
    HudIcon carry_icon{HudIcon::Wood};
    bool carry_is_remaining{false};
};

class HudOverlay {
public:
    void draw(
        const sim::Simulation& simulation,
        sf::Vector2u window_size,
        float fps,
        float tps,
        std::uint8_t local_player_slot,
        float camera_zoom,
        const HudUnitContext& unit_context = {},
        const net::LockstepNetworkHudStats& network_stats = {},
        bool show_perf_hud = false) const;

    void draw_snapshot(
        const SimRenderSnapshot& snapshot,
        sf::Vector2u window_size,
        float fps,
        float tps,
        std::uint8_t local_player_slot,
        float camera_zoom,
        const HudUnitContext& unit_context = {},
        const net::LockstepNetworkHudStats& network_stats = {},
        bool show_perf_hud = false) const;

    void draw_waiting_overlay(
        sf::Vector2u window_size,
        const std::string& title,
        const std::string& subtitle) const;

    void invalidate_gl_cache();

    // Screen-space primitives shared with the main menu renderer.
    void draw_text(
        sf::Vector2u window_size,
        float x,
        float y,
        const std::string& text,
        int pixel_scale,
        float r,
        float g,
        float b) const;

    void draw_rect(
        sf::Vector2u window_size,
        float x,
        float y,
        float width,
        float height,
        float r,
        float g,
        float b,
        float a = 1.0F) const;

    // Draws a texture scaled to cover the whole window, cropping the overflowing axis.
    void draw_cover_texture(
        sf::Vector2u window_size,
        unsigned int texture_id,
        float texture_aspect_ratio,
        float alpha) const;

    [[nodiscard]] static float text_width_px(std::size_t character_count, int pixel_scale);
    [[nodiscard]] static float text_height_px(int pixel_scale);

private:
    [[nodiscard]] unsigned int hud_shader_program() const;
    [[nodiscard]] unsigned int hud_textured_shader_program() const;
    [[nodiscard]] unsigned int hud_tinted_texture_shader_program() const;
    [[nodiscard]] HudIconAtlas& icon_atlas() const;

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

    void draw_icon(
        sf::Vector2u window_size,
        float x,
        float y,
        float size,
        HudIcon icon) const;

    void draw_resource_bar(
        sf::Vector2u window_size,
        float x,
        float y,
        int wood,
        int food,
        int money,
        int mana,
        int mana_max,
        int cap_current,
        int cap_max) const;

    void draw_command_panel(
        sf::Vector2u window_size,
        app::CommandPanelMode mode,
        const app::CommandPanelBuildOptions& build_options,
        sf::Vector2f mouse_screen_position,
        int pressed_slot = -1) const;

    void draw_minimap(
        sf::Vector2u window_size,
        const SimRenderSnapshot& snapshot,
        std::uint8_t local_player_slot,
        const HudUnitContext& unit_context) const;

    void draw_info_panel(sf::Vector2u window_size, const HudInfoPanel& panel) const;

    void draw_chat(
        sf::Vector2u window_size,
        float top_y,
        const HudUnitContext& unit_context) const;

    void draw_game_menu(sf::Vector2u window_size, const HudUnitContext& unit_context) const;

    mutable unsigned int hud_shader_program_{0U};
    mutable unsigned int hud_textured_shader_program_{0U};
    mutable unsigned int hud_tinted_texture_shader_program_{0U};
    mutable unsigned int minimap_texture_{0U};
    mutable int minimap_texture_width_{0};
    mutable int minimap_texture_height_{0};
    mutable HudIconAtlas icon_atlas_{};
    mutable bool icon_atlas_load_attempted_{false};
};

} // namespace aoa::render
