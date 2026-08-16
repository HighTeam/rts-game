#pragma once

#include "core/grid.hpp"
#include "sim/components/map_grid.hpp"

#include <SFML/System/Vector2.hpp>

#include <array>
#include <cstdint>
#include <entt/entt.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aoa::sim::player {

enum class SelectionModifyMode;

} // namespace aoa::sim::player
namespace aoa::render {

struct RenderEntityPose {
    entt::entity entity{entt::null};
    float prev_x{0.0F};
    float prev_y{0.0F};
    float cur_x{0.0F};
    float cur_y{0.0F};
    bool has_move_segment{false};
    float move_from_x{0.0F};
    float move_from_y{0.0F};
    float move_to_x{0.0F};
    float move_to_y{0.0F};
    int move_ticks_elapsed{0};
    int move_ticks_total{0};
    int grid_x{0};
    int grid_y{0};
    int health_current{0};
    int health_max{0};
    int carried_wood{0};
    int carried_food{0};
    int carried_money{0};
    int melee_attack{0};
    int melee_armor{0};
    int pierce_attack{0};
    int pierce_armor{0};
    bool is_enemy{false};
    bool is_worker{false};
    bool is_militia{false};
    bool is_mage{false};
    bool is_projectile{false};
    bool is_town_center{false};
    bool is_house{false};
    bool is_lumber_camp{false};
    bool is_mill{false};
    bool is_mining_camp{false};
    bool is_barracks{false};
    bool is_mage_academy{false};
    bool is_tower{false};
    bool is_market{false};
    bool is_extractor{false};
    bool is_garden{false};
    bool is_reservoir{false};
    bool is_farm{false};
    int farm_food_remaining{0};
    int farm_food_max{0};
    bool has_process{false};
    int process_percent{0};
    bool process_is_research{false};
    std::uint8_t house_variant{0U};
    int garrison_count{0};
    int garrison_capacity{0};
    bool is_mana_lake{false};
    bool lake_has_extractor{false};
    int mana_gen_ticks_remaining{0};
    bool under_construction{false};
    bool shrouded{false};
    bool is_nature{false};
    int footprint_width{1};
    int footprint_height{1};
    std::uint8_t player_slot{0U};
    std::string archetype_id{};
    std::vector<core::GridPos> debug_path_cells{};
    int debug_path_next_index{0};
};

struct RenderHudPlayerStats {
    int town_wood{0};
    int town_food{0};
    int town_money{0};
    int town_mana{0};
    int town_mana_max{0};
    int civil_cap_current{0};
    int civil_cap_max{0};
    int carried_wood{0};
    int militia_hp{0};
    int militia_max_hp{0};
};

struct SimRenderSnapshot {
    std::uint64_t tick_count{0U};
    int map_width{0};
    int map_height{0};
    std::vector<sim::components::TileType> tiles{};
    std::vector<sim::components::GroundType> ground{};
    std::vector<int> forest_wood{};
    std::vector<int> bush_food{};
    std::vector<int> mine_money{};
    std::vector<std::uint8_t> fog_explored{};
    std::vector<std::uint8_t> fog_visible{};
    std::vector<std::uint8_t> fog_memory_tiles{};
    std::vector<int> fog_memory_forest_wood{};
    std::vector<int> fog_memory_bush_food{};
    std::vector<int> fog_memory_mine_money{};
    std::vector<RenderEntityPose> buildings{};
    std::vector<RenderEntityPose> units{};
    std::array<RenderHudPlayerStats, 8> hud_by_player{};
    std::array<std::uint8_t, 8> player_color_indices{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
    std::array<std::uint8_t, 8> player_ages{};
    std::uint8_t vision_source_slots_mask{0U};
};

class BuildingSightMemory;

[[nodiscard]] SimRenderSnapshot capture_sim_render_snapshot(
    const entt::registry& registry,
    std::uint8_t local_player_slot,
    BuildingSightMemory* building_sight_memory = nullptr,
    std::uint64_t tick_count = 0U);

[[nodiscard]] std::pair<float, float> interpolate_render_pose(
    const RenderEntityPose& pose,
    float interpolation_alpha);

class GameRenderer;

[[nodiscard]] sf::Vector2f render_pose_screen_position(
    const GameRenderer& renderer,
    const RenderEntityPose& pose,
    float interpolation_alpha = 1.0F);

[[nodiscard]] entt::entity pick_hovered_unit_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px);

[[nodiscard]] entt::entity pick_player_unit_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px,
    std::uint8_t local_player_slot);

[[nodiscard]] entt::entity pick_enemy_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px,
    std::uint8_t local_player_slot);

[[nodiscard]] entt::entity pick_player_building_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px,
    std::uint8_t local_player_slot);

[[nodiscard]] entt::entity pick_enemy_building_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px,
    std::uint8_t local_player_slot);

// Lakes carrying an extractor are not selectable; the extractor owns the footprint.
[[nodiscard]] entt::entity pick_mana_lake_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    sf::Vector2f screen_position);

[[nodiscard]] bool snapshot_can_place_extractor_at(
    const SimRenderSnapshot& snapshot,
    core::GridPos anchor_cell);

[[nodiscard]] std::optional<core::GridPos> snapshot_extractor_snap_anchor(
    const SimRenderSnapshot& snapshot,
    core::GridPos hover_cell);

[[nodiscard]] bool snapshot_cell_covered_by_mana_lake(
    const SimRenderSnapshot& snapshot,
    core::GridPos cell);

[[nodiscard]] bool snapshot_cell_blocked_by_unit(
    const SimRenderSnapshot& snapshot,
    core::GridPos cell);

[[nodiscard]] std::vector<entt::entity> pick_player_units_in_screen_rect(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    sf::Vector2f rect_min,
    sf::Vector2f rect_max,
    std::uint8_t local_player_slot);

[[nodiscard]] std::optional<core::GridPos> pick_resource_forest_at_screen(
    const SimRenderSnapshot& snapshot,
    const GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px,
    std::uint8_t local_player_slot);

[[nodiscard]] bool snapshot_cell_is_unexplored(
    const SimRenderSnapshot& snapshot,
    core::GridPos cell);

[[nodiscard]] bool snapshot_cell_is_visible(
    const SimRenderSnapshot& snapshot,
    core::GridPos cell);

[[nodiscard]] core::GridPos snapshot_world_visibility_cell(float world_x, float world_z);

[[nodiscard]] bool snapshot_has_town_center(
    const SimRenderSnapshot& snapshot,
    entt::entity entity);

[[nodiscard]] bool snapshot_has_town_center_at_cell(
    const SimRenderSnapshot& snapshot,
    core::GridPos cell,
    std::uint8_t local_player_slot);

void prune_dead_selection(
    std::vector<entt::entity>& selected,
    const SimRenderSnapshot& snapshot,
    std::uint8_t local_player_slot);

void apply_selection_from_snapshot(
    std::vector<entt::entity>& selected,
    const SimRenderSnapshot& snapshot,
    const std::vector<entt::entity>& picked,
    sim::player::SelectionModifyMode mode,
    std::uint8_t local_player_slot);

} // namespace aoa::render
