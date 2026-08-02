#include "data/content_loader.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace aoa::data {

namespace {

UnitDefinition parse_unit_definition(const nlohmann::json& json)
{
    UnitDefinition definition{};
    definition.max_hp = math::Fixed::from_int(json.at("max_hp").get<int>());
    definition.move_ticks_per_tile = json.at("move_ticks_per_tile").get<int>();
    definition.gather_per_tick = json.value("gather_per_tick", 0);
    definition.carry_capacity = json.value("carry_capacity", 0);
    definition.attack_damage = json.value("attack_damage", 0);
    definition.attack_cooldown_ticks = json.value("attack_cooldown_ticks", 1);
    return definition;
}

BuildingDefinition parse_building_definition(const nlohmann::json& json)
{
    BuildingDefinition definition{};
    definition.max_hp = math::Fixed::from_int(json.at("max_hp").get<int>());
    definition.spawn_worker_wood_cost = json.value("spawn_worker_wood_cost", 0);
    return definition;
}

} // namespace

CivDefinition load_civ_definition(const std::filesystem::path& civ_json_path)
{
    std::ifstream input(civ_json_path);
    if (!input) {
        throw std::runtime_error("Failed to open civ definition: " + civ_json_path.string());
    }

    const nlohmann::json json = nlohmann::json::parse(input);

    CivDefinition civ{};
    civ.civ_id = json.at("civ_id").get<std::string>();
    civ.display_name = json.at("display_name").get<std::string>();
    civ.forest_patch_wood = json.at("resources").at("forest_patch_wood").get<int>();

    for (const auto& [unit_id, unit_json] : json.at("units").items()) {
        civ.units.emplace(unit_id, parse_unit_definition(unit_json));
    }

    for (const auto& [building_id, building_json] : json.at("buildings").items()) {
        civ.buildings.emplace(building_id, parse_building_definition(building_json));
    }

    return civ;
}

std::filesystem::path default_data_directory()
{
#ifdef AOA_DATA_DIR
    return std::filesystem::path(AOA_DATA_DIR);
#else
    return std::filesystem::path("data");
#endif
}

} // namespace aoa::data
