#include "render/menu_renderer.hpp"

#include "core/constants.hpp"
#include "net/lobby_wire.hpp"
#include "render/game_renderer.hpp"
#include "render/minimap_math.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/map/map_generator.hpp"
#include "sim/map/map_pattern.hpp"

#include <glad/glad.h>

#include <SFML/Window/Mouse.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace aoa::render {

namespace {

[[nodiscard]] float centered_text_y(const app::MenuRect& rect, const int pixel_scale)
{
    return rect.y + (rect.height - HudOverlay::text_height_px(pixel_scale)) * 0.5F;
}

[[nodiscard]] bool is_multiplayer_menu_screen(const app::MainMenuScreen screen)
{
    return screen == app::MainMenuScreen::Multiplayer
        || screen == app::MainMenuScreen::HostSetup
        || screen == app::MainMenuScreen::Connect
        || screen == app::MainMenuScreen::Lobby
        || screen == app::MainMenuScreen::BrowseGames;
}

void draw_multiplayer_version_label(
    HudOverlay& hud_overlay,
    const sf::Vector2u window_size,
    const app::MainMenuState& state)
{
    const bool show_on_notice = state.screen == app::MainMenuScreen::Notice
        && is_multiplayer_menu_screen(state.notice_return_screen);
    const bool show_on_pattern = state.screen == app::MainMenuScreen::MapPatternSelect
        && state.pattern_return_to_lobby;
    if (!is_multiplayer_menu_screen(state.screen) && !show_on_notice && !show_on_pattern) {
        return;
    }

    const std::string version_line =
        std::string(constants::GAME_VERSION_HUD_PREFIX) + std::string(constants::GAME_VERSION);
    const float text_width =
        HudOverlay::text_width_px(version_line.size(), constants::HUD_PIXEL_SCALE);
    const float text_height = HudOverlay::text_height_px(constants::HUD_PIXEL_SCALE);
    const float x = static_cast<float>(window_size.x) - constants::HUD_MARGIN_X - text_width;
    const float y = static_cast<float>(window_size.y) - constants::HUD_MARGIN_Y - text_height;
    hud_overlay.draw_text(
        window_size,
        x,
        y,
        version_line,
        constants::HUD_PIXEL_SCALE,
        constants::HUD_TEXT_R,
        constants::HUD_TEXT_G,
        constants::HUD_TEXT_B);
}

[[nodiscard]] std::string slot_status_text(const net::LobbySlotInfo& slot)
{
    if (!slot.occupied) {
        return "Open Slot";
    }

    return slot.name.empty() ? "Joining..." : slot.name;
}

[[nodiscard]] std::array<float, 3> preview_tile_color(
    const sim::components::GroundType ground,
    const sim::components::TileType tile)
{
    if (tile == sim::components::TileType::Forest) {
        return {
            constants::MENU_PATTERN_PREVIEW_FOREST_R,
            constants::MENU_PATTERN_PREVIEW_FOREST_G,
            constants::MENU_PATTERN_PREVIEW_FOREST_B,
        };
    }
    if (tile == sim::components::TileType::GoldMine) {
        return {
            constants::MENU_PATTERN_PREVIEW_GOLD_R,
            constants::MENU_PATTERN_PREVIEW_GOLD_G,
            constants::MENU_PATTERN_PREVIEW_GOLD_B,
        };
    }
    if (tile == sim::components::TileType::Berries
        || tile == sim::components::TileType::Blueberries) {
        return {
            constants::MENU_PATTERN_PREVIEW_BERRIES_R,
            constants::MENU_PATTERN_PREVIEW_BERRIES_G,
            constants::MENU_PATTERN_PREVIEW_BERRIES_B,
        };
    }
    if (ground == sim::components::GroundType::Snow) {
        return {
            constants::MENU_PATTERN_PREVIEW_SNOW_R,
            constants::MENU_PATTERN_PREVIEW_SNOW_G,
            constants::MENU_PATTERN_PREVIEW_SNOW_B,
        };
    }
    if (ground == sim::components::GroundType::Sand) {
        return {
            constants::MENU_PATTERN_PREVIEW_SAND_R,
            constants::MENU_PATTERN_PREVIEW_SAND_G,
            constants::MENU_PATTERN_PREVIEW_SAND_B,
        };
    }
    return {
        constants::MENU_PATTERN_PREVIEW_GRASS_R,
        constants::MENU_PATTERN_PREVIEW_GRASS_G,
        constants::MENU_PATTERN_PREVIEW_GRASS_B,
    };
}

} // namespace

MenuRenderer::MenuRenderer()
{
    if (!init_gl_loader()) {
        throw std::runtime_error("Failed to initialize OpenGL loader");
    }
}

MenuRenderer::~MenuRenderer()
{
    destroy_present_target();
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
    destroy_present_target();
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

void MenuRenderer::destroy_present_target() const
{
    if (present_color_ != 0U) {
        glDeleteTextures(1, &present_color_);
        present_color_ = 0U;
    }
    if (present_fbo_ != 0U) {
        glDeleteFramebuffers(1, &present_fbo_);
        present_fbo_ = 0U;
    }
    present_width_ = 0;
    present_height_ = 0;
}

bool MenuRenderer::ensure_present_target() const
{
    const int width = static_cast<int>(window_size_.x);
    const int height = static_cast<int>(window_size_.y);
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (present_fbo_ != 0U && present_color_ != 0U && present_width_ == width
        && present_height_ == height) {
        return true;
    }

    destroy_present_target();
    glGenTextures(1, &present_color_);
    glBindTexture(GL_TEXTURE_2D, present_color_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);
    glBindTexture(GL_TEXTURE_2D, 0U);

    glGenFramebuffers(1, &present_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, present_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, present_color_, 0);
    const bool complete =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0U);
    if (!complete) {
        destroy_present_target();
        return false;
    }

    present_width_ = width;
    present_height_ = height;
    return true;
}

void MenuRenderer::present_target_to_window() const
{
    if (present_fbo_ == 0U || present_width_ <= 0 || present_height_ <= 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0U);
        return;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, present_fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0U);
    glBlitFramebuffer(
        0,
        0,
        present_width_,
        present_height_,
        0,
        0,
        present_width_,
        present_height_,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0U);
}

void MenuRenderer::draw_background() const
{
    glDisable(GL_SCISSOR_TEST);
    clear_opaque_framebuffer(
        constants::MENU_CLEAR_R,
        constants::MENU_CLEAR_G,
        constants::MENU_CLEAR_B,
        constants::RENDER_CLEAR_A);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

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
        constants::HUD_OPTIONS_FRAME_BORDER_B);
    hud_overlay_.draw_rect(
        window_size_,
        panel.x + 1.0F,
        panel.y + 1.0F,
        panel.width - 2.0F,
        panel.height - 2.0F,
        constants::HUD_OPTIONS_FRAME_R,
        constants::HUD_OPTIONS_FRAME_G,
        constants::HUD_OPTIONS_FRAME_B);
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
        HudOverlay::text_width_px(button.label, constants::HUD_PIXEL_SCALE);
    const float dim = button.disabled ? constants::HUD_MENU_DISABLED_DIM : 1.0F;
    float text_r = constants::HUD_TEXT_R;
    float text_g = constants::HUD_TEXT_G;
    float text_b = constants::HUD_TEXT_B;
    if (button.action == app::MainMenuAction::CycleSlotColor
        && button.extra < constants::PLAYER_SLOT_COLOR_RGB.size()) {
        const auto& rgb = constants::PLAYER_SLOT_COLOR_RGB[button.extra];
        text_r = rgb[0];
        text_g = rgb[1];
        text_b = rgb[2];
    }
    hud_overlay_.draw_text(
        window_size_,
        draw_rect.x + (draw_rect.width - text_width) * 0.5F,
        centered_text_y(draw_rect, constants::HUD_PIXEL_SCALE),
        button.label,
        constants::HUD_PIXEL_SCALE,
        text_r * dim,
        text_g * dim,
        text_b * dim);
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
            label.r,
            label.g,
            label.b);
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

    for (const app::MenuButton& button : layout.buttons) {
        draw_button(button, context.mouse_position, button.selected);
    }

    draw_status_message(layout, context);
    draw_pattern_preview(layout, context);
}

void MenuRenderer::refresh_pattern_preview_cache(const MenuRenderContext& context) const
{
    if (context.state == nullptr) {
        return;
    }

    const app::MainMenuState& state = *context.state;
    const bool preview_needed = state.screen == app::MainMenuScreen::MapPatternSelect
        || state.screen == app::MainMenuScreen::Lobby
        || (state.screen == app::MainMenuScreen::Singleplayer
            && state.game_style == app::SingleplayerGameStyle::RandomGame);
    if (!preview_needed) {
        return;
    }

    const std::uint8_t pattern_index = state.screen == app::MainMenuScreen::MapPatternSelect
        ? state.pattern_highlight
        : state.map_pattern;
    sim::map::MapPattern pattern{};
    if (state.screen == app::MainMenuScreen::MapPatternSelect
        && pattern_index != constants::MAP_PATTERN_OTHER_INDEX
        && pattern_index != state.map_pattern) {
        pattern = sim::map::make_builtin_pattern(pattern_index);
    }
    else {
        pattern = sim::map::resolve_map_pattern(pattern_index, state.selected_pattern_payload);
    }

    const int settings_width = context.lobby != nullptr
        ? context.lobby->settings.map_width
        : state.host_settings.map_width;
    const int settings_height = context.lobby != nullptr
        ? context.lobby->settings.map_height
        : state.host_settings.map_height;
    pattern = sim::map::apply_lobby_size_to_pattern(pattern, settings_width, settings_height);

    std::uint8_t requested_players = state.host_settings.player_count;
    if (state.screen == app::MainMenuScreen::Singleplayer) {
        requested_players = app::occupied_singleplayer_slots(state);
    }
    else if (context.lobby != nullptr) {
        requested_players = net::lobby_playing_slot_count(*context.lobby);
    }
    const std::uint8_t player_count =
        sim::map::map_pattern_effective_player_count(pattern, requested_players);

    if (preview_pattern_index_ == pattern_index && preview_payload_ == state.selected_pattern_payload
        && preview_map_width_ == pattern.map_width && preview_map_height_ == pattern.map_height
        && preview_player_count_ == player_count && preview_map_.grid.width > 0) {
        return;
    }

    sim::map::MapGenerationConfig config{};
    config.player_count = player_count;
    config.seed = constants::MENU_PATTERN_PREVIEW_SEED;
    config.pattern = pattern;
    preview_map_ = sim::map::generate_map(config);
    preview_pattern_index_ = pattern_index;
    preview_payload_ = state.selected_pattern_payload;
    preview_map_width_ = pattern.map_width;
    preview_map_height_ = pattern.map_height;
    preview_player_count_ = player_count;
}

void MenuRenderer::draw_pattern_preview(
    const app::MenuLayout& layout,
    const MenuRenderContext& context) const
{
    if (!layout.show_pattern_preview || context.state == nullptr) {
        return;
    }

    draw_panel(layout.pattern_preview);

    const int map_width = preview_map_.grid.width;
    const int map_height = preview_map_.grid.height;
    const float pad = static_cast<float>(constants::MENU_PATTERN_PREVIEW_PAD_PX);
    const app::CommandPanelFrame content{
        layout.pattern_preview.x + pad,
        layout.pattern_preview.y + pad,
        layout.pattern_preview.width - pad * 2.0F,
        layout.pattern_preview.height - pad * 2.0F,
    };
    const MinimapIsoLayout iso = make_minimap_iso_layout(content, map_width, map_height);
    if (iso.scale <= 0.0F || map_width <= 0 || map_height <= 0) {
        return;
    }

    const auto tile_point = [&](const float x, const float y) {
        return minimap_world_to_screen(iso, x, y);
    };
    const auto draw_cell = [&](
                               const float x0,
                               const float y0,
                               const float x1,
                               const float y1,
                               const float r,
                               const float g,
                               const float b) {
        hud_overlay_.draw_quad(
            window_size_,
            tile_point(x0, y0),
            tile_point(x1, y0),
            tile_point(x1, y1),
            tile_point(x0, y1),
            r,
            g,
            b);
    };

    const int sample_edge = constants::MENU_PATTERN_PREVIEW_SAMPLE_EDGE;
    const int step = std::max(
        1,
        (std::max(map_width, map_height) + sample_edge - 1) / sample_edge);

    hud_overlay_.begin_batch();
    for (int y = 0; y < map_height; y += step) {
        const int y1 = std::min(y + step, map_height);
        for (int x = 0; x < map_width; x += step) {
            const int x1 = std::min(x + step, map_width);
            const std::size_t index = static_cast<std::size_t>(y * map_width + x);
            const auto color =
                preview_tile_color(preview_map_.grid.ground[index], preview_map_.grid.tiles[index]);
            draw_cell(
                static_cast<float>(x),
                static_cast<float>(y),
                static_cast<float>(x1),
                static_cast<float>(y1),
                color[0],
                color[1],
                color[2]);
        }
    }
    for (const aoa::core::GridPos& lake : preview_map_.mana_lake_anchors) {
        draw_cell(
            static_cast<float>(lake.x),
            static_cast<float>(lake.y),
            static_cast<float>(lake.x + 1),
            static_cast<float>(lake.y + 1),
            constants::MENU_PATTERN_PREVIEW_LAKE_R,
            constants::MENU_PATTERN_PREVIEW_LAKE_G,
            constants::MENU_PATTERN_PREVIEW_LAKE_B);
    }
    for (const aoa::core::GridPos& start : preview_map_.start_anchors) {
        draw_cell(
            static_cast<float>(start.x),
            static_cast<float>(start.y),
            static_cast<float>(start.x + 1),
            static_cast<float>(start.y + 1),
            constants::MENU_PATTERN_PREVIEW_START_R,
            constants::MENU_PATTERN_PREVIEW_START_G,
            constants::MENU_PATTERN_PREVIEW_START_B);
    }

    const float map_w = static_cast<float>(map_width);
    const float map_h = static_cast<float>(map_height);
    const sf::Vector2f north = tile_point(0.0F, 0.0F);
    const sf::Vector2f east = tile_point(map_w, 0.0F);
    const sf::Vector2f south = tile_point(map_w, map_h);
    const sf::Vector2f west = tile_point(0.0F, map_h);
    hud_overlay_.draw_line(
        window_size_,
        north.x,
        north.y,
        east.x,
        east.y,
        constants::MENU_PATTERN_PREVIEW_DIAMOND_EDGE_R,
        constants::MENU_PATTERN_PREVIEW_DIAMOND_EDGE_G,
        constants::MENU_PATTERN_PREVIEW_DIAMOND_EDGE_B);
    hud_overlay_.draw_line(
        window_size_,
        east.x,
        east.y,
        south.x,
        south.y,
        constants::MENU_PATTERN_PREVIEW_DIAMOND_EDGE_R,
        constants::MENU_PATTERN_PREVIEW_DIAMOND_EDGE_G,
        constants::MENU_PATTERN_PREVIEW_DIAMOND_EDGE_B);
    hud_overlay_.draw_line(
        window_size_,
        south.x,
        south.y,
        west.x,
        west.y,
        constants::MENU_PATTERN_PREVIEW_DIAMOND_EDGE_R,
        constants::MENU_PATTERN_PREVIEW_DIAMOND_EDGE_G,
        constants::MENU_PATTERN_PREVIEW_DIAMOND_EDGE_B);
    hud_overlay_.draw_line(
        window_size_,
        west.x,
        west.y,
        north.x,
        north.y,
        constants::MENU_PATTERN_PREVIEW_DIAMOND_EDGE_R,
        constants::MENU_PATTERN_PREVIEW_DIAMOND_EDGE_G,
        constants::MENU_PATTERN_PREVIEW_DIAMOND_EDGE_B);
    hud_overlay_.end_batch();
}

void MenuRenderer::draw_settings(const MenuRenderContext& context) const
{
    const app::GameMenuState& settings = context.state->settings;
    const app::GameMenuRect panel =
        app::settings_panel_rect(window_size_, settings.center_settings_panel);
    draw_panel(panel);

    for (const app::GameMenuButton& button : app::build_settings_buttons(settings, window_size_)) {
        const bool active_tab =
            (button.action == app::GameMenuAction::SettingsTabGame
                && settings.screen == app::GameMenuScreen::SettingsGame)
            || (button.action == app::GameMenuAction::SettingsTabVideo
                && settings.screen == app::GameMenuScreen::SettingsVideo)
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

    if (settings.screen == app::GameMenuScreen::SettingsGame) {
        const app::GameMenuRect slider =
            app::scroll_speed_slider_rect(window_size_, settings.center_settings_panel);
        const int percent = static_cast<int>(
            (settings.scroll_speed / constants::CAMERA_SCROLL_SPEED_DEFAULT) * 100.0F + 0.5F);
        hud_overlay_.draw_text(
            window_size_,
            slider.x,
            slider.y - constants::HUD_SETTINGS_LABEL_GAP_PX,
            "Scroll Speed: " + std::to_string(percent) + "%",
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
            (settings.scroll_speed - constants::CAMERA_SCROLL_SPEED_MIN)
                / (constants::CAMERA_SCROLL_SPEED_MAX - constants::CAMERA_SCROLL_SPEED_MIN),
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
        return;
    }

    if (settings.screen != app::GameMenuScreen::SettingsAudio) {
        return;
    }

    const auto draw_volume_slider =
        [&](const int row, const std::string& label, const float value) {
            const app::GameMenuRect slider =
                app::volume_slider_rect(window_size_, row, settings.center_settings_panel);
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

    refresh_pattern_preview_cache(context);

    const bool offscreen = ensure_present_target();
    if (offscreen) {
        glBindFramebuffer(GL_FRAMEBUFFER, present_fbo_);
    }

    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, static_cast<GLsizei>(window_size_.x), static_cast<GLsizei>(window_size_.y));
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

    draw_multiplayer_version_label(hud_overlay_, window_size_, *context.state);
    draw_perf_hud(context);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (offscreen) {
        present_target_to_window();
    }
    else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0U);
    }
}

} // namespace aoa::render
