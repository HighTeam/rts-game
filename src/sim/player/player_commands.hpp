#pragma once

#include "core/grid.hpp"
#include "render/game_renderer.hpp"

#include <SFML/System/Vector2.hpp>
#include <entt/entt.hpp>

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
    float pick_radius_px);

[[nodiscard]] std::vector<entt::entity> pick_player_units_in_screen_rect(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    sf::Vector2f rect_min,
    sf::Vector2f rect_max);

[[nodiscard]] entt::entity pick_enemy_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px);

[[nodiscard]] entt::entity pick_enemy_at(entt::registry& registry, core::GridPos cell);

void apply_selection(
    std::vector<entt::entity>& selected,
    entt::registry& registry,
    const std::vector<entt::entity>& picked,
    SelectionModifyMode mode);

bool issue_move_order(entt::registry& registry, entt::entity entity, core::GridPos goal);

void issue_move_orders(
    entt::registry& registry,
    const std::vector<entt::entity>& entities,
    core::GridPos goal);

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

void prune_dead_selection(std::vector<entt::entity>& selected, entt::registry& registry);

[[nodiscard]] entt::entity pick_hovered_unit_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px);

[[nodiscard]] entt::entity pick_player_building_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px);

[[nodiscard]] std::optional<core::GridPos> pick_resource_forest_at(
    entt::registry& registry,
    core::GridPos cell);

[[nodiscard]] std::optional<core::GridPos> pick_resource_forest_at_screen(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    sf::Vector2f screen_position,
    float pick_radius_px);

} // namespace aoa::sim::player
