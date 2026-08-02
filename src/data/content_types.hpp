#pragma once

#include "math/fixed.hpp"

#include <string>
#include <unordered_map>

namespace aoa::data {

struct UnitDefinition {
    math::Fixed max_hp{};
    int move_ticks_per_tile{1};
    int gather_per_tick{0};
    int carry_capacity{0};
    int attack_damage{0};
    int attack_cooldown_ticks{1};
};

struct BuildingDefinition {
    math::Fixed max_hp{};
    int spawn_worker_wood_cost{0};
};

struct CivDefinition {
    std::string civ_id;
    std::string display_name;
    std::unordered_map<std::string, UnitDefinition> units;
    std::unordered_map<std::string, BuildingDefinition> buildings;
    int forest_patch_wood{0};
};

} // namespace aoa::data
