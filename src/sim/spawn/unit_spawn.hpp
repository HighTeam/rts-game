#pragma once

#include "core/grid.hpp"
#include "data/content_types.hpp"
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

[[nodiscard]] entt::entity spawn_player_lumberjack(
    entt::registry& registry,
    const data::ArchetypeDefinition& lumberjack_archetype,
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
