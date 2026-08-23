#pragma once

#include "core/grid.hpp"
#include "math/fixed.hpp"
#include "render/game_renderer.hpp"
#include "sim/components/building_process.hpp"

#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <entt/entt.hpp>

#include <optional>
#include <vector>

namespace aoa::sim::player {

enum class SelectionModifyMode {
    Replace,
    Add,
    Toggle,
};

[[nodiscard]] entt::entity pick_player_unit_at(entt::registry& registry, core::GridPos cell);

[[nodiscard]] entt::entity pick_player_unit_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px,
    std::uint8_t local_player_slot);

[[nodiscard]] std::vector<entt::entity> pick_player_units_in_screen_rect(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    sf::Vector2f rect_min,
    sf::Vector2f rect_max,
    std::uint8_t local_player_slot);

[[nodiscard]] entt::entity pick_enemy_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px,
    std::uint8_t local_player_slot);

[[nodiscard]] entt::entity pick_enemy_at(entt::registry& registry, core::GridPos cell);

void apply_selection(
    std::vector<entt::entity>& selected,
    entt::registry& registry,
    const std::vector<entt::entity>& picked,
    SelectionModifyMode mode);

bool issue_move_order(
    entt::registry& registry,
    entt::entity entity,
    core::GridPos goal,
    bool has_goal_world = false,
    math::Fixed goal_world_x = {},
    math::Fixed goal_world_y = {});

void issue_move_orders(
    entt::registry& registry,
    const std::vector<entt::entity>& entities,
    core::GridPos goal,
    bool has_goal_world = false,
    math::Fixed goal_world_x = {},
    math::Fixed goal_world_y = {});

bool issue_attack_order(entt::registry& registry, entt::entity entity, entt::entity target);

void issue_attack_orders(
    entt::registry& registry,
    const std::vector<entt::entity>& entities,
    entt::entity target);

bool issue_gather_order(entt::registry& registry, entt::entity entity, core::GridPos forest_cell);

void issue_gather_orders(
    entt::registry& registry,
    const std::vector<entt::entity>& entities,
    core::GridPos forest_cell);

void issue_deposit_orders(entt::registry& registry, const std::vector<entt::entity>& entities);

bool issue_spawn_worker_order(entt::registry& registry, entt::entity town_center);

bool issue_spawn_militia_order(entt::registry& registry, entt::entity barracks);

bool issue_spawn_mage_order(entt::registry& registry, entt::entity mage_academy);

void refund_training_process(
    entt::registry& registry,
    entt::entity building,
    components::BuildingProcessKind kind);

void issue_kill_orders(entt::registry& registry, const std::vector<entt::entity>& entities);

void issue_stop_orders(entt::registry& registry, const std::vector<entt::entity>& entities);

bool issue_build_town_center_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    core::GridPos anchor_cell);

bool issue_build_house_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    core::GridPos anchor_cell);

bool issue_build_lumber_camp_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    core::GridPos anchor_cell);

bool issue_build_extractor_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    core::GridPos anchor_cell);

bool issue_build_mill_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    core::GridPos anchor_cell);

bool issue_build_mining_camp_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    core::GridPos anchor_cell);

bool issue_build_barracks_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    core::GridPos anchor_cell);

bool issue_build_mage_academy_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    core::GridPos anchor_cell);

bool issue_build_tower_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    core::GridPos anchor_cell);

bool issue_build_market_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    core::GridPos anchor_cell);

bool issue_build_garden_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    core::GridPos anchor_cell);

bool issue_build_reservoir_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    core::GridPos anchor_cell);

bool issue_build_farm_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    core::GridPos anchor_cell);

bool issue_renew_farm_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    entt::entity farm);

void issue_garrison_orders(
    entt::registry& registry,
    const std::vector<entt::entity>& entities,
    entt::entity building);

bool issue_unload_garrison_order(entt::registry& registry, entt::entity town_center);

bool issue_advance_age_order(entt::registry& registry, entt::entity town_center);

bool issue_research_cartography_order(entt::registry& registry, entt::entity market);

bool issue_research_trades_order(entt::registry& registry, entt::entity market);

bool issue_research_spy_order(entt::registry& registry, entt::entity town_center);

bool issue_market_sell_wood_order(entt::registry& registry, entt::entity market);

bool issue_market_sell_food_order(entt::registry& registry, entt::entity market);

bool issue_market_buy_wood_order(entt::registry& registry, entt::entity market);

bool issue_market_buy_food_order(entt::registry& registry, entt::entity market);

bool issue_send_trade_order(
    entt::registry& registry,
    std::uint8_t player_slot,
    std::uint8_t target_slot,
    int wood,
    int food,
    int gold,
    int mana);

bool issue_set_diplomacy_order(
    entt::registry& registry,
    std::uint8_t player_slot,
    std::uint8_t ally_mask,
    bool ally_victory);

bool issue_resign_order(entt::registry& registry, std::uint8_t player_slot);

bool issue_map_ping_order(
    entt::registry& registry,
    std::uint8_t player_slot,
    core::GridPos cell);

void eject_garrisoned_units(entt::registry& registry, entt::entity building);

void issue_cheat_oknocraft_infinity(entt::registry& registry);

// Extractors may only be raised on a free mana lake with an exact footprint match.
[[nodiscard]] bool can_build_extractor_at(
    entt::registry& registry,
    core::GridPos anchor_cell,
    std::uint8_t player_slot);

// If hover_cell is on a free matching lake, returns that lake's anchor.
[[nodiscard]] std::optional<core::GridPos> extractor_snap_anchor(
    entt::registry& registry,
    core::GridPos hover_cell);

[[nodiscard]] entt::entity pick_mana_lake_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    sf::Vector2f screen_position,
    std::uint8_t local_player_slot);

bool issue_resume_build_order(
    entt::registry& registry,
    const std::vector<entt::entity>& workers,
    entt::entity building);

bool issue_destroy_building_order(entt::registry& registry, entt::entity building);

void prune_dead_selection(std::vector<entt::entity>& selected, entt::registry& registry);

[[nodiscard]] entt::entity pick_hovered_unit_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px,
    std::uint8_t local_player_slot);

[[nodiscard]] entt::entity pick_player_building_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px,
    std::uint8_t local_player_slot);

[[nodiscard]] entt::entity pick_enemy_building_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px,
    std::uint8_t local_player_slot);

[[nodiscard]] std::optional<core::GridPos> pick_resource_forest_at(
    entt::registry& registry,
    core::GridPos cell);

[[nodiscard]] std::optional<core::GridPos> pick_resource_forest_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px,
    std::uint8_t local_player_slot);

} // namespace aoa::sim::player
