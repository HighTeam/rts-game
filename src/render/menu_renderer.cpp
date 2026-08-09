#include "render/menu_renderer.hpp"

#include "core/constants.hpp"
#include "render/game_renderer.hpp"

#include <glad/glad.h>

#include <SFML/Window/Mouse.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace aoa::render {

namespace {

[[nodiscard]] float centered_text_y(const app::MenuRect& rect, const int pixel_scale)
{
    return rect.y + (rect.height - HudOverlay::text_height_px(pixel_scale)) * 0.5F;
}

[[nodiscard]] std::string slot_status_text(const net::LobbySlotInfo& slot)
{
    if (!slot.occupied) {
        return "Open Slot";
    }

    return slot.name.empty() ? "Joining..." : slot.name;
}

} // namespace

MenuRenderer::MenuRenderer()
{
    if (!init_gl_loader()) {
        throw std::runtime_error("Failed to initialize OpenGL loader");
    }
}

void MenuRenderer::load(const std::filesystem::path& assets_directory)
{
    assets_directory_ = assets_directory;
    background_.load(assets_directory);
}

void MenuRenderer::resize(const sf::Vector2u window_size)
{
    window_size_ = window_size;
    glViewport(0, 0, static_cast<GLsizei>(window_size.x), static_cast<GLsizei>(window_size.y));
}

void MenuRenderer::reset_graphics_context(const sf::Vector2u window_size)
{
    hud_overlay_.invalidate_gl_cache();
    background_.destroy_gl_resources();

    if (!init_gl_loader()) {
        throw std::runtime_error("Failed to reinitialize OpenGL loader");
    }

    background_.load(assets_directory_);
    resize(window_size);
}

void MenuRenderer::update(const float delta_seconds)
{
    background_.update(delta_seconds);
}

void MenuRenderer::draw_background() const
{
    glClearColor(0.02F, 0.03F, 0.04F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!background_.ready()) {
        return;
    }

    hud_overlay_.draw_cover_texture(
        window_size_,
        background_.current_texture_id(),
        background_.current_aspect_ratio(),
        1.0F);

    const float incoming_alpha = background_.incoming_alpha();
    if (incoming_alpha <= 0.0F) {
        return;
    }

    hud_overlay_.draw_cover_texture(
        window_size_,
        background_.incoming_texture_id(),
        background_.incoming_aspect_ratio(),
        incoming_alpha);
}

void MenuRenderer::draw_panel(const app::MenuRect& panel) const
{
    hud_overlay_.draw_rect(
        window_size_,
        panel.x,
        panel.y,
        panel.width,
        panel.height,
        constants::HUD_OPTIONS_FRAME_BORDER_R,
        constants::HUD_OPTIONS_FRAME_BORDER_G,
        constants::HUD_OPTIONS_FRAME_BORDER_B,
        constants::MAIN_MENU_PANEL_ALPHA);
    hud_overlay_.draw_rect(
        window_size_,
        panel.x + 1.0F,
        panel.y + 1.0F,
        panel.width - 2.0F,
        panel.height - 2.0F,
        constants::HUD_OPTIONS_FRAME_R,
        constants::HUD_OPTIONS_FRAME_G,
        constants::HUD_OPTIONS_FRAME_B,
        constants::MAIN_MENU_PANEL_ALPHA);
}

void MenuRenderer::draw_button(
    const app::MenuButton& button,
    const sf::Vector2f mouse,
    const bool highlighted) const
{
    const bool hovered = button.rect.contains(mouse.x, mouse.y);
    const bool pressed =
        !button.disabled && hovered && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);

    float button_r = constants::HUD_OPTIONS_BUTTON_R;
    float button_g = constants::HUD_OPTIONS_BUTTON_G;
    float button_b = constants::HUD_OPTIONS_BUTTON_B;
    if (button.disabled) {
        button_r *= constants::HUD_MENU_DISABLED_DIM;
        button_g *= constants::HUD_MENU_DISABLED_DIM;
        button_b *= constants::HUD_MENU_DISABLED_DIM;
    }
    if (highlighted) {
        button_r = std::min(1.0F, button_r * constants::HUD_SETTINGS_ACTIVE_TAB_BRIGHTEN);
        button_g = std::min(1.0F, button_g * constants::HUD_SETTINGS_ACTIVE_TAB_BRIGHTEN);
        button_b = std::min(1.0F, button_b * constants::HUD_SETTINGS_ACTIVE_TAB_BRIGHTEN);
    }
    if (!button.disabled && hovered && !pressed) {
        button_r = std::min(1.0F, button_r * constants::HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN);
        button_g = std::min(1.0F, button_g * constants::HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN);
        button_b = std::min(1.0F, button_b * constants::HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN);
    }
    if (pressed) {
        button_r *= constants::HUD_OPTIONS_BUTTON_PRESS_DARKEN;
        button_g *= constants::HUD_OPTIONS_BUTTON_PRESS_DARKEN;
        button_b *= constants::HUD_OPTIONS_BUTTON_PRESS_DARKEN;
    }

    const float press_offset = pressed
        ? static_cast<float>(constants::HUD_OPTIONS_BUTTON_PRESS_OFFSET_PX)
        : 0.0F;
    const app::MenuRect draw_rect{
        button.rect.x + press_offset,
        button.rect.y + press_offset,
        button.rect.width - press_offset,
        button.rect.height - press_offset,
    };

    hud_overlay_.draw_rect(
        window_size_,
        draw_rect.x,
        draw_rect.y,
        draw_rect.width,
        draw_rect.height,
        button_r * 0.6F,
        button_g * 0.6F,
        button_b * 0.6F);
    hud_overlay_.draw_rect(
        window_size_,
        draw_rect.x + 1.0F,
        draw_rect.y + 1.0F,
        draw_rect.width - 2.0F,
        draw_rect.height - 2.0F,
        button_r,
        button_g,
        button_b);

    const float text_width =
        HudOverlay::text_width_px(button.label.size(), constants::HUD_PIXEL_SCALE);
    const float dim = button.disabled ? constants::HUD_MENU_DISABLED_DIM : 1.0F;
    hud_overlay_.draw_text(
        window_size_,
        draw_rect.x + (draw_rect.width - text_width) * 0.5F,
        centered_text_y(draw_rect, constants::HUD_PIXEL_SCALE),
        button.label,
        constants::HUD_PIXEL_SCALE,
        constants::HUD_TEXT_R * dim,
        constants::HUD_TEXT_G * dim,
        constants::HUD_TEXT_B * dim);
}

void MenuRenderer::draw_lobby_rows(
    const app::MenuLayout& layout,
    const MenuRenderContext& context) const
{
    const net::LobbyStateMessage& lobby = *context.lobby;
    const float char_step = HudOverlay::text_width_px(2U, constants::HUD_PIXEL_SCALE)
        - HudOverlay::text_width_px(1U, constants::HUD_PIXEL_SCALE);

    for (std::uint8_t slot = 0U; slot < lobby.settings.player_count; ++slot) {
        const app::MenuRect row = app::lobby_row_rect(layout.panel, static_cast<int>(slot));
        const net::LobbySlotInfo& info = lobby.slots[slot];
        const auto& rgb = constants::PLAYER_SLOT_COLOR_RGB[slot];
        const float text_y = centered_text_y(row, constants::HUD_PIXEL_SCALE);
        const std::string name_text =
            "P" + std::to_string(slot + 1U) + " " + slot_status_text(info);

        hud_overlay_.draw_text(
            window_size_,
            row.x,
            text_y,
            name_text,
            constants::HUD_PIXEL_SCALE,
            rgb[0],
            rgb[1],
            rgb[2]);

        if (!info.occupied) {
            continue;
        }

        if (slot != lobby.host_slot) {
            const std::string ping_text = std::to_string(info.ping_ms) + "ms";
            hud_overlay_.draw_text(
                window_size_,
                row.x + row.width * 0.6F,
                text_y,
                ping_text,
                constants::HUD_PIXEL_SCALE,
                constants::HUD_TEXT_R,
                constants::HUD_TEXT_G,
                constants::HUD_TEXT_B);
        }

        const std::string ready_text = info.ready ? "READY" : "NOT READY";
        const float ready_width =
            HudOverlay::text_width_px(ready_text.size(), constants::HUD_PIXEL_SCALE);
        hud_overlay_.draw_text(
            window_size_,
            row.x + row.width - ready_width - char_step,
            text_y,
            ready_text,
            constants::HUD_PIXEL_SCALE,
            info.ready ? constants::MAIN_MENU_READY_R : constants::HUD_UNAFFORDABLE_R,
            info.ready ? constants::MAIN_MENU_READY_G : constants::HUD_UNAFFORDABLE_G,
            info.ready ? constants::MAIN_MENU_READY_B : constants::HUD_UNAFFORDABLE_B);
    }
}

void MenuRenderer::draw_status_message(
    const app::MenuLayout& layout,
    const MenuRenderContext& context) const
{
    const std::string& message = context.state->status_message;
    if (message.empty()) {
        return;
    }

    const float text_width = HudOverlay::text_width_px(message.size(), constants::HUD_PIXEL_SCALE);
    hud_overlay_.draw_text(
        window_size_,
        layout.panel.x + (layout.panel.width - text_width) * 0.5F,
        layout.panel.y + layout.panel.height
            + static_cast<float>(constants::MAIN_MENU_PANEL_PADDING_PX),
        message,
        constants::HUD_PIXEL_SCALE,
        constants::MAIN_MENU_STATUS_R,
        constants::MAIN_MENU_STATUS_G,
        constants::MAIN_MENU_STATUS_B);
}

void MenuRenderer::draw_layout(
    const app::MenuLayout& layout,
    const MenuRenderContext& context) const
{
    draw_panel(layout.panel);

    for (const app::MenuLabel& label : layout.labels) {
        hud_overlay_.draw_text(
            window_size_,
            label.x,
            label.y,
            label.text,
            label.pixel_scale,
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);
    }

    for (const app::MenuRect& split_line : layout.split_lines) {
        hud_overlay_.draw_rect(
            window_size_,
            split_line.x,
            split_line.y,
            split_line.width,
            split_line.height,
            constants::MAIN_MENU_SPLIT_LINE_R,
            constants::MAIN_MENU_SPLIT_LINE_G,
            constants::MAIN_MENU_SPLIT_LINE_B);
    }

    for (const app::MenuTextFieldEntry& field : layout.text_fields) {
        const bool focused = context.state->focused_field == field.field;
        hud_overlay_.draw_rect(
            window_size_,
            field.rect.x,
            field.rect.y,
            field.rect.width,
            field.rect.height,
            focused ? constants::MAIN_MENU_FIELD_FOCUS_R : constants::HUD_OPTIONS_FRAME_BORDER_R,
            focused ? constants::MAIN_MENU_FIELD_FOCUS_G : constants::HUD_OPTIONS_FRAME_BORDER_G,
            focused ? constants::MAIN_MENU_FIELD_FOCUS_B : constants::HUD_OPTIONS_FRAME_BORDER_B);
        hud_overlay_.draw_rect(
            window_size_,
            field.rect.x + 1.0F,
            field.rect.y + 1.0F,
            field.rect.width - 2.0F,
            field.rect.height - 2.0F,
            constants::MAIN_MENU_FIELD_BG_R,
            constants::MAIN_MENU_FIELD_BG_G,
            constants::MAIN_MENU_FIELD_BG_B);

        const std::string value = focused ? field.value + "_" : field.value;
        hud_overlay_.draw_text(
            window_size_,
            field.rect.x + static_cast<float>(constants::MAIN_MENU_PANEL_PADDING_PX),
            centered_text_y(field.rect, constants::HUD_PIXEL_SCALE),
            value,
            constants::HUD_PIXEL_SCALE,
            constants::HUD_TEXT_R,
            constants::HUD_TEXT_G,
            constants::HUD_TEXT_B);
    }

    if (context.state->screen == app::MainMenuScreen::Lobby && context.lobby != nullptr) {
        draw_lobby_rows(layout, context);
    }

    for (const app::MenuButton& button : layout.buttons) {
        draw_button(button, context.mouse_position, false);
    }

    draw_status_message(layout, context);
}

void MenuRenderer::draw_settings(const MenuRenderContext& context) const
{
    const app::GameMenuState& settings = context.state->settings;
    const app::GameMenuRect panel = app::settings_panel_rect(window_size_);
    draw_panel(panel);

    for (const app::GameMenuButton& button : app::build_settings_buttons(settings, window_size_)) {
        const bool active_tab =
            (button.action == app::GameMenuAction::SettingsTabGame
                && settings.screen == app::GameMenuScreen::SettingsGame)
            || (button.action == app::GameMenuAction::SettingsTabAudio
                && settings.screen == app::GameMenuScreen::SettingsAudio);
        draw_button(
            app::MenuButton{
                app::MainMenuAction::None,
                std::string(button.label),
                button.rect,
                button.disabled,
            },
            context.mouse_position,
            active_tab);
    }

    if (settings.screen != app::GameMenuScreen::SettingsAudio) {
        return;
    }

    const auto draw_volume_slider =
        [&](const int row, const std::string& label, const float value) {
            const app::GameMenuRect slider = app::volume_slider_rect(window_size_, row);
            hud_overlay_.draw_text(
                window_size_,
                slider.x,
                slider.y - constants::HUD_SETTINGS_LABEL_GAP_PX,
                label,
                constants::HUD_PIXEL_SCALE,
                constants::HUD_TEXT_R,
                constants::HUD_TEXT_G,
                constants::HUD_TEXT_B);
            hud_overlay_.draw_rect(
                window_size_,
                slider.x,
                slider.y,
                slider.width,
                slider.height,
                constants::HUD_VOLUME_SLIDER_TRACK_R,
                constants::HUD_VOLUME_SLIDER_TRACK_G,
                constants::HUD_VOLUME_SLIDER_TRACK_B);

            const float fill = std::clamp(
                (value - constants::AUDIO_VOLUME_MIN)
                    / (constants::AUDIO_VOLUME_MAX - constants::AUDIO_VOLUME_MIN),
                0.0F,
                1.0F);
            hud_overlay_.draw_rect(
                window_size_,
                slider.x,
                slider.y,
                slider.width * fill,
                slider.height,
                constants::HUD_VOLUME_SLIDER_FILL_R,
                constants::HUD_VOLUME_SLIDER_FILL_G,
                constants::HUD_VOLUME_SLIDER_FILL_B);
        };

    draw_volume_slider(0, "Master", settings.master_volume);
    draw_volume_slider(1, "Music", settings.music_volume);
    draw_volume_slider(2, "SFX", settings.sfx_volume);
}

void MenuRenderer::draw_perf_hud(const MenuRenderContext& context) const
{
    if (!context.show_perf_hud) {
        return;
    }

    const std::string fps_line = "FPS: " + std::to_string(static_cast<int>(context.fps + 0.5F));
    const float text_width =
        HudOverlay::text_width_px(fps_line.size(), constants::HUD_PIXEL_SCALE);
    const float x = static_cast<float>(window_size_.x) - constants::HUD_MARGIN_X - text_width;
    const float y = constants::HUD_MARGIN_Y;
    hud_overlay_.draw_text(
        window_size_,
        x,
        y,
        fps_line,
        constants::HUD_PIXEL_SCALE,
        constants::HUD_TEXT_R * 0.85F,
        constants::HUD_TEXT_G * 0.85F,
        constants::HUD_TEXT_B * 0.85F);
}

void MenuRenderer::draw(const MenuRenderContext& context)
{
    if (context.state == nullptr || window_size_.x == 0U || window_size_.y == 0U) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    draw_background();

    if (context.state->screen != app::MainMenuScreen::Main) {
        hud_overlay_.draw_rect(
            window_size_,
            0.0F,
            0.0F,
            static_cast<float>(window_size_.x),
            static_cast<float>(window_size_.y),
            constants::HUD_MENU_SCRIM_R,
            constants::HUD_MENU_SCRIM_G,
            constants::HUD_MENU_SCRIM_B,
            constants::HUD_MENU_SCRIM_A);
    }

    if (context.state->screen == app::MainMenuScreen::Settings) {
        draw_settings(context);
    }
    else {
        const net::LobbyStateMessage fallback_lobby{};
        const net::LobbyStateMessage& lobby =
            context.lobby != nullptr ? *context.lobby : fallback_lobby;
        const app::MenuLayout layout = app::build_menu_layout(
            window_size_,
            *context.state,
            lobby,
            context.is_host,
            context.local_ready,
            context.can_start);
        draw_layout(layout, context);
    }

    draw_perf_hud(context);
}

} // namespace aoa::render
