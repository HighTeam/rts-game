#include "data/content_loader.hpp"

#include <fstream>
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
    archetype.spawn_worker_wood_cost = json.value("spawn_worker_wood_cost", 0);
    archetype.wood_capacity = json.value("wood_capacity", 0);
    return archetype;
}

CivManifest parse_civ_manifest(const nlohmann::json& json)
{
    CivManifest civ{};
    civ.civ_id = json.at("civ_id").get<std::string>();
    civ.display_name = json.at("display_name").get<std::string>();
    civ.starting_stockpile_wood = json.value("starting_stockpile_wood", 0);

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
    ContentDatabase content{};

    const std::filesystem::path archetypes_directory = data_directory / "archetypes";
    if (!std::filesystem::is_directory(archetypes_directory)) {
        throw std::runtime_error("Archetypes directory not found: " + archetypes_directory.string());
    }

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(archetypes_directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        std::ifstream input(entry.path());
        if (!input) {
            throw std::runtime_error("Failed to open archetype: " + entry.path().string());
        }

        const ArchetypeDefinition archetype = parse_archetype_definition(nlohmann::json::parse(input));
        content.archetypes.insert_or_assign(archetype.id, archetype);
    }

    const std::filesystem::path civ_path = data_directory / "civs" / "earth.json";
    std::ifstream civ_input(civ_path);
    if (!civ_input) {
        throw std::runtime_error("Failed to open civ manifest: " + civ_path.string());
    }

    content.civ = parse_civ_manifest(nlohmann::json::parse(civ_input));
    validate_civ_archetypes(content.civ, content);

    return content;
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
