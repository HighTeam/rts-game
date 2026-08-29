#pragma once

#include "app/window_display.hpp"
#include "core/constants.hpp"
#include "net/lobby_wire.hpp"
#include "net/net_constants.hpp"
#include "sim/components/match_session.hpp"
#include "sim/simulation.hpp"

#include <SFML/Window/Window.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace aoa::app {

enum class AppFlow : std::uint8_t {
    ExitApp = 0,
    ReturnToMainMenu = 1,
};

// Settings that survive across menu and match sessions inside one app run.
struct AppShellSettings {
    bool fullscreen{true};
    bool mouse_capture{constants::RENDER_MOUSE_CAPTURE_DEFAULT};
    bool vsync{constants::RENDER_VERTICAL_SYNC};
    int fps_limit{constants::TARGET_DISPLAY_FPS};
    bool show_perf_hud{false};
    float master_volume{constants::AUDIO_MASTER_VOLUME};
    float music_volume{constants::AUDIO_MUSIC_VOLUME};
    float sfx_volume{constants::AUDIO_SFX_VOLUME};
    float scroll_speed{constants::CAMERA_SCROLL_SPEED_DEFAULT};
    constants::BuildingRangeDisplayMode building_range_display{
        constants::BuildingRangeDisplayMode::Never};
    constants::HudStyle hud_style{constants::HudStyle::Default};
    std::string player_name{std::string(constants::MULTIPLAYER_DEFAULT_PLAYER_NAME)};
};

struct SingleplayerSetup {
    std::uint8_t player_count{constants::SINGLEPLAYER_PLAYER_COUNT};
    std::array<bool, constants::MAX_PLAYER_SLOTS> slot_is_ai{false, true};
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> slot_colors{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> slot_teams{};
    sim::components::FogOfWarMode fog_mode{sim::components::FogOfWarMode::Enabled};
    int civil_population_map_cap{constants::CIVIL_POPULATION_MAP_CAP_DEFAULT};
    bool cheats_enabled{false};
    std::uint8_t map_pattern{constants::MAP_PATTERN_COMMONS_INDEX};
    std::uint64_t map_seed{0U};
    std::string pattern_payload{};
    sim::components::VictoryCondition victory_condition{
        sim::components::VictoryCondition::Normal};
    std::array<std::string, aoa::net::constants::LOCKSTEP_MAX_PLAYER_SLOTS> player_names{};
    std::uint8_t biome_preset{constants::MAP_BIOME_PRESET_MIXED};
};

// Everything a graphical lockstep match needs, independent of the CLI options.
struct LockstepMatchSetup {
    bool is_host{true};
    std::uint8_t player_slot{0U};
    std::uint8_t player_count{2U};
    std::string host_address{};
    std::uint16_t port{aoa::net::constants::DEFAULT_PORT};
    int civil_population_map_cap{constants::CIVIL_POPULATION_MAP_CAP_DEFAULT};
    bool fog_of_war_enabled{true};
    sim::components::FogOfWarMode fog_mode{sim::components::FogOfWarMode::Enabled};
    bool cheats_enabled{false};
    std::array<bool, constants::MAX_PLAYER_SLOTS> slot_is_ai{};
    std::array<bool, constants::MAX_PLAYER_SLOTS> slot_is_spectator{};
    bool local_is_spectator{false};
    std::uint8_t playing_player_count{2U};
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> slot_colors{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> slot_teams{};
    bool lockstep_debug{false};
    bool auto_input{false};
    std::uint64_t tick_limit{0U};
    // Peers coming from a lobby must wait for the host to rebind the match port.
    bool delay_before_connect{false};
    std::array<std::string, aoa::net::constants::LOCKSTEP_MAX_PLAYER_SLOTS> player_names{};
    std::array<std::uint64_t, aoa::net::constants::LOCKSTEP_MAX_PLAYER_SLOTS> reconnect_tokens{};
    sim::components::VictoryCondition victory_condition{
        sim::components::VictoryCondition::Normal};
    std::uint64_t map_seed{0U};
    std::string pattern_payload{};
    net::LobbySettings lobby_settings{};
};

struct LaunchOptions {
    bool headless{false};
    bool run_harness{false};
    bool run_net_smoke{false};
    bool run_lockstep_smoke{false};
    bool run_lockstep_disconnect_smoke{false};
    bool run_lockstep_reconnect_smoke{false};
    bool run_lockstep_4_smoke{false};
    bool run_lockstep_4_disconnect_smoke{false};
    bool run_lockstep_peer_silence_smoke{false};
    bool run_lockstep_2h2ai_smoke{false};
    bool run_sim_8ai_bench{false};
    bool run_lockstep_4_stress_smoke{false};
    bool run_lockstep_4_reconnect_smoke{false};
    bool run_snapshot_smoke{false};
    bool run_snapshot_double_spawn_smoke{false};
    bool run_snapshot_reconnect_smoke{false};
    bool run_snapshot_resign_smoke{false};
    bool run_snapshot_heavy_smoke{false};
    bool lockstep_host{false};
    bool lockstep_join{false};
    bool print_state_hash{false};
    std::uint64_t headless_ticks{0U};
    std::uint64_t lockstep_ticks{0U};
    std::uint16_t lockstep_port{0U};
    bool lockstep_debug{false};
    bool lockstep_auto_input{false};
    bool prefer_loose_assets{false};
    std::uint8_t lockstep_player_count{
        static_cast<std::uint8_t>(aoa::net::constants::LOCKSTEP_PLAYER_COUNT)};
    std::optional<std::uint8_t> lockstep_player_number{};
    std::optional<std::uint64_t> expect_state_hash{};
    std::optional<std::string> lockstep_join_address{};
};

LaunchOptions parse_launch_options(int argc, char** argv);

[[nodiscard]] std::uint8_t resolve_lockstep_join_player_slot(const LaunchOptions& options);

int run_headless(sim::Simulation& simulation, const LaunchOptions& options);

AppFlow run_graphical(
    sf::Window& window,
    WindowDisplaySettings& display_settings,
    sim::Simulation& simulation,
    AppShellSettings& shell_settings,
    const SingleplayerSetup& setup = {});

struct MainMenuState;

AppFlow run_lockstep_match(
    sf::Window& window,
    WindowDisplaySettings& display_settings,
    sim::Simulation& simulation,
    const LockstepMatchSetup& setup,
    AppShellSettings& shell_settings,
    MainMenuState* menu_state = nullptr);

int run_graphical_lockstep(sim::Simulation& simulation, const LaunchOptions& options);

// Main menu driven entry point used when the app starts without lockstep CLI flags.
int run_app_shell();

} // namespace aoa::app
