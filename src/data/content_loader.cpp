#include "data/content_loader.hpp"

#include "core/asset_pack_format.hpp"
#include "core/asset_store.hpp"
#include "core/constants.hpp"
#include "core/runtime_paths.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace aoa::data {

namespace {

ArchetypeDefinition parse_archetype_definition(const nlohmann::json& json)
{
    ArchetypeDefinition archetype{};
    archetype.id = json.at("id").get<std::string>();
    archetype.kind = parse_archetype_kind(json.at("kind").get<std::string>());
    archetype.display_name = json.value("display_name", archetype.id);
    archetype.max_hp = math::Fixed::from_int(json.value("max_hp", 1));
    archetype.move_ticks_per_tile = json.value("move_ticks_per_tile", 1);
    archetype.gather_per_tick = json.value("gather_per_tick", 0);
    archetype.carry_capacity = json.value("carry_capacity", 0);
    archetype.melee_attack = json.value("melee_attack", json.value("attack_damage", 0));
    archetype.melee_armor = json.value("melee_armor", 0);
    archetype.pierce_attack = json.value("pierce_attack", 0);
    archetype.pierce_armor = json.value("pierce_armor", 0);
    archetype.attack_cooldown_ticks = json.value("attack_cooldown_ticks", 1);
    archetype.vision_range = json.value("vision_range", 0);
    archetype.spawn_worker_food_cost = json.value(
        "spawn_worker_food_cost",
        json.value("spawn_worker_wood_cost", 0));
    archetype.spawn_militia_food_cost = json.value(
        "spawn_militia_food_cost",
        json.value("spawn_militia_wood_cost", 0));
    archetype.build_wood_cost = json.value("build_wood_cost", 0);
    archetype.wood_capacity = json.value("wood_capacity", 0);
    archetype.food_capacity = json.value("food_capacity", 0);
    archetype.money_capacity = json.value("money_capacity", 0);
    archetype.footprint_width = json.value("footprint_width", 1);
    archetype.footprint_height = json.value("footprint_height", 1);
    archetype.gather_interval_ticks = json.value("gather_interval_ticks", 1);
    return archetype;
}

CivManifest parse_civ_manifest(const nlohmann::json& json)
{
    CivManifest civ{};
    civ.civ_id = json.at("civ_id").get<std::string>();
    civ.display_name = json.at("display_name").get<std::string>();
    civ.starting_stockpile_wood =
        json.value("starting_stockpile_wood", aoa::constants::DEFAULT_STARTING_STOCKPILE_WOOD);
    civ.starting_stockpile_food =
        json.value("starting_stockpile_food", aoa::constants::DEFAULT_STARTING_STOCKPILE_FOOD);
    civ.starting_stockpile_money =
        json.value("starting_stockpile_money", aoa::constants::DEFAULT_STARTING_STOCKPILE_MONEY);
    civ.starting_stockpile_mana =
        json.value("starting_stockpile_mana", aoa::constants::DEFAULT_STARTING_STOCKPILE_MANA);

    if (json.contains("unit_archetypes")) {
        civ.unit_archetypes = json.at("unit_archetypes").get<std::vector<std::string>>();
    }

    if (json.contains("structure_archetypes")) {
        civ.structure_archetypes = json.at("structure_archetypes").get<std::vector<std::string>>();
    }

    if (json.contains("resource_node_archetypes")) {
        civ.resource_node_archetypes = json.at("resource_node_archetypes").get<std::vector<std::string>>();
    }

    return civ;
}

void validate_civ_archetypes(const CivManifest& civ, const ContentDatabase& content)
{
    const auto require_ids = [&content](const std::vector<std::string>& ids, const ArchetypeKind kind) {
        for (const std::string& id : ids) {
            const ArchetypeDefinition* archetype = find_archetype(content, id);
            if (archetype == nullptr) {
                throw std::runtime_error("Missing archetype: " + id);
            }

            if (archetype->kind != kind) {
                throw std::runtime_error("Archetype kind mismatch for: " + id);
            }
        }
    };

    require_ids(civ.unit_archetypes, ArchetypeKind::Unit);
    require_ids(civ.structure_archetypes, ArchetypeKind::Structure);
    require_ids(civ.resource_node_archetypes, ArchetypeKind::ResourceNode);
}

[[nodiscard]] bool ends_with_json(const std::string& path)
{
    return path.size() >= 5U && path.compare(path.size() - 5U, 5U, ".json") == 0;
}

} // namespace

ArchetypeKind parse_archetype_kind(const std::string& kind_text)
{
    if (kind_text == "unit") {
        return ArchetypeKind::Unit;
    }

    if (kind_text == "structure") {
        return ArchetypeKind::Structure;
    }

    if (kind_text == "resource_node") {
        return ArchetypeKind::ResourceNode;
    }

    if (kind_text == "prop") {
        return ArchetypeKind::Prop;
    }

    throw std::invalid_argument("Unknown archetype kind: " + kind_text);
}

const ArchetypeDefinition* find_archetype(const ContentDatabase& content, const std::string& archetype_id)
{
    const auto iterator = content.archetypes.find(archetype_id);
    if (iterator == content.archetypes.end()) {
        return nullptr;
    }

    return &iterator->second;
}

const ArchetypeDefinition* find_unit_archetype(
    const ContentDatabase& content,
    const std::string& archetype_id)
{
    const ArchetypeDefinition* archetype = find_archetype(content, archetype_id);
    if (archetype == nullptr || archetype->kind != ArchetypeKind::Unit) {
        return nullptr;
    }

    return archetype;
}

const ArchetypeDefinition* find_structure_archetype(
    const ContentDatabase& content,
    const std::string& archetype_id)
{
    const ArchetypeDefinition* archetype = find_archetype(content, archetype_id);
    if (archetype == nullptr || archetype->kind != ArchetypeKind::Structure) {
        return nullptr;
    }

    return archetype;
}

const ArchetypeDefinition* find_resource_node_archetype(
    const ContentDatabase& content,
    const std::string& archetype_id)
{
    const ArchetypeDefinition* archetype = find_archetype(content, archetype_id);
    if (archetype == nullptr || archetype->kind != ArchetypeKind::ResourceNode) {
        return nullptr;
    }

    return archetype;
}

ContentDatabase load_content_database(const std::filesystem::path& data_directory)
{
    (void)data_directory;

    core::AssetStore& store = core::AssetStore::instance();
    if (!store.ready()) {
        throw std::runtime_error("Asset store is not initialized; cannot load content database");
    }

    ContentDatabase content{};
    const std::string archetypes_prefix = std::string{core::DATA_PACK_PREFIX} + "archetypes/";
    const std::vector<std::string> archetype_keys = store.list_prefix(archetypes_prefix);
    if (archetype_keys.empty()) {
        throw std::runtime_error("No archetype files found under " + archetypes_prefix);
    }

    for (const std::string& key : archetype_keys) {
        if (!ends_with_json(key)) {
            continue;
        }

        const auto text = store.read_text(key);
        if (!text.has_value()) {
            throw std::runtime_error("Failed to open archetype: " + key);
        }

        const ArchetypeDefinition archetype =
            parse_archetype_definition(nlohmann::json::parse(*text));
        content.archetypes.insert_or_assign(archetype.id, archetype);
    }

    const std::string civ_key = std::string{core::DATA_PACK_PREFIX} + "civs/earth.json";
    const auto civ_text = store.read_text(civ_key);
    if (!civ_text.has_value()) {
        throw std::runtime_error("Failed to open civ manifest: " + civ_key);
    }

    content.civ = parse_civ_manifest(nlohmann::json::parse(*civ_text));
    validate_civ_archetypes(content.civ, content);

    return content;
}

std::filesystem::path default_data_directory()
{
    return core::default_data_directory();
}

} // namespace aoa::data
