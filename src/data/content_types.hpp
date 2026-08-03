#pragma once

#include "math/fixed.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace aoa::data {

enum class ArchetypeKind {
    Unit,
    Structure,
    ResourceNode,
    Prop,
};

struct ArchetypeDefinition {
    std::string id{};
    ArchetypeKind kind{ArchetypeKind::Unit};
    std::string display_name{};
    math::Fixed max_hp{};
    int move_ticks_per_tile{1};
    int gather_per_tick{0};
    int carry_capacity{0};
    int melee_attack{0};
    int melee_armor{0};
    int pierce_attack{0};
    int pierce_armor{0};
    int attack_cooldown_ticks{1};
    int spawn_worker_wood_cost{0};
    int wood_capacity{0};
};

struct CivManifest {
    std::string civ_id{};
    std::string display_name{};
    std::vector<std::string> unit_archetypes{};
    std::vector<std::string> structure_archetypes{};
    std::vector<std::string> resource_node_archetypes{};
    int starting_stockpile_wood{0};
};

struct ContentDatabase {
    std::unordered_map<std::string, ArchetypeDefinition> archetypes{};
    CivManifest civ{};
};

[[nodiscard]] ArchetypeKind parse_archetype_kind(const std::string& kind_text);

[[nodiscard]] const ArchetypeDefinition* find_archetype(
    const ContentDatabase& content,
    const std::string& archetype_id);

[[nodiscard]] const ArchetypeDefinition* find_unit_archetype(
    const ContentDatabase& content,
    const std::string& archetype_id);

[[nodiscard]] const ArchetypeDefinition* find_structure_archetype(
    const ContentDatabase& content,
    const std::string& archetype_id);

[[nodiscard]] const ArchetypeDefinition* find_resource_node_archetype(
    const ContentDatabase& content,
    const std::string& archetype_id);

} // namespace aoa::data
