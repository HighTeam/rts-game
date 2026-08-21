#pragma once



#include "app/chat_state.hpp"
#include "app/command_panel.hpp"
#include "app/diplomacy.hpp"
#include "app/game_cursor.hpp"
#include "app/game_menu.hpp"

#include "audio/game_audio.hpp"

#include "app/selection_state.hpp"

#include "render/game_renderer.hpp"

#include "render/hud_overlay.hpp"

#include "render/sim_render_snapshot.hpp"

#include "net/net_constants.hpp"
#include "sim/player/player_commands.hpp"

#include <SFML/Window/Window.hpp>

#include <algorithm>
#include <array>



#include <chrono>

#include <cstdint>

#include <optional>

#include <string>



namespace aoa::sim {



class Simulation;



} // namespace aoa::sim



namespace aoa::net {



class LockstepSession;



} // namespace aoa::net



namespace aoa::app {



class GameInput {

public:

    void reset_frame_clock();

    void set_lockstep_session(net::LockstepSession* session) { lockstep_session_ = session; }

    void set_local_player_slot(const std::uint8_t player_slot) { local_player_slot_ = player_slot; }
    void set_local_is_spectator(const bool spectator) { local_is_spectator_ = spectator; }
    [[nodiscard]] bool local_is_spectator() const { return local_is_spectator_; }

    void set_chat_state(ChatState* chat_state) { chat_state_ = chat_state; }
    void set_match_roster(
        const bool multiplayer,
        const std::array<std::string, aoa::net::constants::LOCKSTEP_MAX_PLAYER_SLOTS>& player_names)
    {
        multiplayer_ = multiplayer;
        player_names_ = player_names;
    }

    void set_game_cursor(GameCursor* game_cursor) { game_cursor_ = game_cursor; }

    void set_game_audio(audio::GameAudio* game_audio)
    {
        game_audio_ = game_audio;
        if (game_audio_ == nullptr) {
            return;
        }

        game_menu_.master_volume = game_audio_->master_volume();
        game_menu_.music_volume = game_audio_->music_volume();
        game_menu_.sfx_volume = game_audio_->sfx_volume();
    }

    void set_menu_fullscreen(const bool fullscreen) { game_menu_.fullscreen = fullscreen; }

    void set_menu_video(const bool vsync, const int fps_limit)
    {
        game_menu_.vsync = vsync;
        game_menu_.fps_limit = fps_limit;
    }

    void set_menu_mouse_capture(const bool mouse_capture)
    {
        game_menu_.mouse_capture = mouse_capture;
    }

    [[nodiscard]] bool menu_mouse_capture() const { return game_menu_.mouse_capture; }

    [[nodiscard]] bool menu_vsync() const { return game_menu_.vsync; }

    [[nodiscard]] int menu_fps_limit() const { return game_menu_.fps_limit; }

    void set_scroll_speed(const float scroll_speed)
    {
        game_menu_.scroll_speed = std::clamp(
            scroll_speed,
            constants::CAMERA_SCROLL_SPEED_MIN,
            constants::CAMERA_SCROLL_SPEED_MAX);
    }

    [[nodiscard]] float scroll_speed() const { return game_menu_.scroll_speed; }

    void set_building_range_display(const constants::BuildingRangeDisplayMode mode)
    {
        game_menu_.building_range_display = mode;
    }

    [[nodiscard]] constants::BuildingRangeDisplayMode building_range_display() const
    {
        return game_menu_.building_range_display;
    }

    void set_hud_style(const constants::HudStyle style)
    {
        game_menu_.hud_style = style;
    }

    [[nodiscard]] constants::HudStyle hud_style() const
    {
        return game_menu_.hud_style;
    }

    [[nodiscard]] bool is_game_menu_open() const { return game_menu_.is_open(); }
    [[nodiscard]] bool is_simulation_paused() const
    {
        return game_menu_.is_open() || match_paused_;
    }

    [[nodiscard]] bool consume_exit_game_request()
    {
        const bool requested = exit_game_requested_;
        exit_game_requested_ = false;
        return requested;
    }

    [[nodiscard]] bool consume_exit_to_main_menu_request()
    {
        const bool requested = exit_to_main_menu_requested_;
        exit_to_main_menu_requested_ = false;
        return requested;
    }

    [[nodiscard]] bool consume_fullscreen_toggle_request()
    {
        const bool requested = fullscreen_toggle_requested_;
        fullscreen_toggle_requested_ = false;
        return requested;
    }

    [[nodiscard]] bool consume_video_apply_request()
    {
        const bool requested = video_apply_requested_;
        video_apply_requested_ = false;
        return requested;
    }

    void update_continuous(

        sf::Window& window,

        render::GameRenderer& renderer,

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot = nullptr);

    [[nodiscard]] bool handle_event(

        const sf::Event& event,

        const sf::Window& window,

        render::GameRenderer& renderer,

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot = nullptr);



    [[nodiscard]] const PlayerSelection& selection() const { return selection_; }

    void clear_selection()

    {

        selection_.clear();

        command_panel_mode_ = CommandPanelMode::Empty;

        placement_ghost_anchor_.reset();

        placement_ghost_valid_ = false;

        attack_targeting_mode_ = false;
        garrison_targeting_mode_ = false;

    }

    [[nodiscard]] HoverHighlight hover() const { return hover_; }

    [[nodiscard]] render::SelectionBoxOverlay selection_box() const { return selection_box_; }

    [[nodiscard]] CommandPanelMode command_panel_mode() const { return command_panel_mode_; }

    [[nodiscard]] bool is_chat_composing() const { return chat_composing_; }

    [[nodiscard]] std::optional<core::GridPos> placement_ghost_anchor() const

    {

        return placement_ghost_anchor_;

    }

    [[nodiscard]] bool placement_ghost_valid() const { return placement_ghost_valid_; }

    [[nodiscard]] render::HudUnitContext make_hud_context(

        const sf::Window& window,

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot) const;



private:

    [[nodiscard]] sim::player::SelectionModifyMode current_modify_mode() const;

    void update_hover(

        const sf::Window& window,

        render::GameRenderer& renderer,

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot);

    void finalize_left_release(

        const sf::Window& window,

        render::GameRenderer& renderer,

        sim::Simulation& simulation,

        sf::Vector2i mouse_position,

        const render::SimRenderSnapshot* render_snapshot);



    bool submit_player_command(sim::Simulation& simulation, sim::player::PlayerCommand command);

    void play_order_ack_sfx(
        sim::Simulation& simulation,
        const render::SimRenderSnapshot* render_snapshot,
        const sim::player::PlayerCommand& command) const;

    void play_select_ack_if_own_units(
        sim::Simulation& simulation,
        const render::SimRenderSnapshot* render_snapshot) const;

    void sync_command_panel_mode(

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot);

    [[nodiscard]] bool selection_has_worker(

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot) const;

    [[nodiscard]] bool selection_has_militia(

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot) const;

    [[nodiscard]] bool selection_has_mage(

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot) const;

    bool try_issue_garrison_on_building(

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot,

        entt::entity building);

    [[nodiscard]] CommandPanelBuildOptions current_build_options(

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot) const;

    bool handle_command_panel_click(

        const sf::Window& window,

        render::GameRenderer& renderer,

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot,

        sf::Vector2f screen_position);

    bool handle_minimap_navigation(

        const sf::Window& window,

        render::GameRenderer& renderer,

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot,

        sf::Vector2f screen_position);

    bool apply_command_panel_action(

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot,

        CommandPanelAction action);

    bool try_issue_attack_at_screen(

        const sf::Window& window,

        render::GameRenderer& renderer,

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot,

        sf::Vector2f screen_position);

    void submit_chat_message(
        std::string text,
        sim::Simulation& simulation,
        const render::SimRenderSnapshot* render_snapshot);

    bool handle_chat_event(
        const sf::Event& event,
        sim::Simulation& simulation,
        const render::SimRenderSnapshot* render_snapshot);

    bool handle_game_menu_event(
        const sf::Event& event,
        const sf::Window& window,
        sim::Simulation& simulation);

    void apply_game_menu_action(GameMenuAction action, sim::Simulation& simulation);

    void sync_audio_volumes_from_menu();

    void sync_diplomacy_draft(sim::Simulation& simulation);

    void sync_diplomacy_chat_focus();

    bool handle_diplomacy_click(
        const sf::Window& window,
        sim::Simulation& simulation,
        const render::SimRenderSnapshot* render_snapshot,
        float mouse_x,
        float mouse_y,
        bool subtract = false);

    [[nodiscard]] CursorShape resolve_cursor_shape(

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot) const;

    void update_game_cursor(

        sf::Window& window,

        sim::Simulation& simulation,

        const render::SimRenderSnapshot* render_snapshot);



    PlayerSelection selection_{};

    HoverHighlight hover_{};

    render::SelectionBoxOverlay selection_box_{};

    CommandPanelMode command_panel_mode_{CommandPanelMode::Empty};

    std::optional<core::GridPos> placement_ghost_anchor_{};

    bool placement_ghost_valid_{false};

    int command_panel_pressed_slot_{-1};

    std::chrono::steady_clock::time_point command_panel_press_until_{};

    bool attack_targeting_mode_{false};
    bool garrison_targeting_mode_{false};
    bool local_is_spectator_{false};
    bool minimap_show_units_{true};

    bool chat_composing_{false};

    std::string chat_draft_{};

    ChatState* chat_state_{nullptr};
    bool multiplayer_{false};
    std::array<std::string, aoa::net::constants::LOCKSTEP_MAX_PLAYER_SLOTS> player_names_{};
    DiplomacyState diplomacy_{};
    bool tab_scoreboard_{false};

    GameCursor* game_cursor_{nullptr};

    audio::GameAudio* game_audio_{nullptr};

    GameMenuState game_menu_{};
    bool match_paused_{false};

    bool exit_game_requested_{false};
    bool exit_to_main_menu_requested_{false};

    bool fullscreen_toggle_requested_{false};
    bool video_apply_requested_{false};

    std::optional<sf::Vector2i> left_press_position_{};

    bool left_button_down_{false};

    bool minimap_navigation_active_{false};

    std::chrono::steady_clock::time_point previous_frame_time_{};

    bool frame_clock_initialized_{false};

    net::LockstepSession* lockstep_session_{nullptr};

    std::uint8_t local_player_slot_{0U};

};



} // namespace aoa::app


