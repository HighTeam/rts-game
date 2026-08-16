#pragma once

#include "core/grid.hpp"
#include "data/content_types.hpp"
#include "math/fixed.hpp"
#include "sim/components/resources.hpp"

#include <entt/entt.hpp>
#include <string>

namespace aoa::sim::spawn {

[[nodiscard]] entt::entity spawn_player_worker(
    entt::registry& registry,
    const data::ArchetypeDefinition& worker_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot);

[[nodiscard]] entt::entity spawn_player_militia(
    entt::registry& registry,
    const data::ArchetypeDefinition& militia_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot);

[[nodiscard]] entt::entity spawn_player_mage(
    entt::registry& registry,
    const data::ArchetypeDefinition& mage_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot);

[[nodiscard]] entt::entity spawn_player_town_center(
    entt::registry& registry,
    const data::ArchetypeDefinition& town_center_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    components::Stockpile starting_stockpile,
    bool under_construction = false);

[[nodiscard]] entt::entity spawn_player_house(
    entt::registry& registry,
    const data::ArchetypeDefinition& house_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    bool under_construction = false);

[[nodiscard]] entt::entity spawn_player_lumber_camp(
    entt::registry& registry,
    const data::ArchetypeDefinition& lumber_camp_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    bool under_construction = false);

// Nature entity: no owner tag, no health, blocks movement through its footprint.
[[nodiscard]] entt::entity spawn_mana_lake(
    entt::registry& registry,
    const data::ArchetypeDefinition& mana_lake_archetype,
    core::GridPos spawn_cell,
    std::uint8_t near_player_slot);

[[nodiscard]] entt::entity spawn_player_extractor(
    entt::registry& registry,
    const data::ArchetypeDefinition& extractor_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    bool under_construction = false);

[[nodiscard]] entt::entity spawn_player_mill(
    entt::registry& registry,
    const data::ArchetypeDefinition& mill_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    bool under_construction = false);

[[nodiscard]] entt::entity spawn_player_mining_camp(
    entt::registry& registry,
    const data::ArchetypeDefinition& mining_camp_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    bool under_construction = false);

[[nodiscard]] entt::entity spawn_player_barracks(
    entt::registry& registry,
    const data::ArchetypeDefinition& barracks_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    bool under_construction = false);

[[nodiscard]] entt::entity spawn_player_mage_academy(
    entt::registry& registry,
    const data::ArchetypeDefinition& mage_academy_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    bool under_construction = false);

[[nodiscard]] entt::entity spawn_player_tower(
    entt::registry& registry,
    const data::ArchetypeDefinition& tower_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    bool under_construction = false);

[[nodiscard]] entt::entity spawn_player_market(
    entt::registry& registry,
    const data::ArchetypeDefinition& market_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    bool under_construction = false);

[[nodiscard]] entt::entity spawn_player_garden(
    entt::registry& registry,
    const data::ArchetypeDefinition& garden_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    bool under_construction = false);

[[nodiscard]] entt::entity spawn_player_reservoir(
    entt::registry& registry,
    const data::ArchetypeDefinition& reservoir_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    bool under_construction = false);

[[nodiscard]] entt::entity spawn_player_farm(
    entt::registry& registry,
    const data::ArchetypeDefinition& farm_archetype,
    core::GridPos spawn_cell,
    std::uint8_t player_slot,
    bool under_construction = false);

[[nodiscard]] entt::entity find_farm_at_cell(entt::registry& registry, core::GridPos cell);

[[nodiscard]] entt::entity spawn_rock_projectile(
    entt::registry& registry,
    math::Fixed world_x,
    math::Fixed world_y,
    entt::entity target,
    std::uint8_t owner_slot,
    int pierce_damage);

[[nodiscard]] entt::entity find_mana_lake_at_anchor(entt::registry& registry, core::GridPos anchor);

[[nodiscard]] entt::entity find_extractor_on_mana_lake(
    const entt::registry& registry,
    entt::entity lake);

[[nodiscard]] bool mana_lake_footprint_contains_cell(
    entt::registry& registry,
    entt::entity lake,
    core::GridPos cell);

// Re-attaches ManaLakeRef by anchor; used after snapshot restores rebuild entities.
void relink_extractor_mana_lakes(entt::registry& registry);

} // namespace aoa::sim::spawn
