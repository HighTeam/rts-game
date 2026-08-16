#include "sim/map/map_pattern.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

namespace aoa::sim::map {

namespace {

[[nodiscard]] const char* kind_to_string(const MapPieceKind kind)
{
    switch (kind) {
    case MapPieceKind::StartPosition:
        return "start_position";
    case MapPieceKind::MultiStartPosition:
        return "multi_start_position";
    case MapPieceKind::Forest:
        return "forest";
    case MapPieceKind::Gold:
        return "gold";
    case MapPieceKind::Terrain:
        return "terrain";
    case MapPieceKind::ManaLake:
        return "mana_lake";
    case MapPieceKind::Berries:
        return "berries";
    case MapPieceKind::MiddlePoint:
        return "middle_point";
    }

    return "forest";
}

[[nodiscard]] std::optional<MapPieceKind> kind_from_string(const std::string& text)
{
    if (text == "start_position" || text == "single_start") {
        return MapPieceKind::StartPosition;
    }
    if (text == "multi_start_position" || text == "all_starts") {
        return MapPieceKind::MultiStartPosition;
    }
    if (text == "forest") {
        return MapPieceKind::Forest;
    }
    if (text == "gold" || text == "resources") {
        return MapPieceKind::Gold;
    }
    if (text == "terrain") {
        return MapPieceKind::Terrain;
    }
    if (text == "mana_lake") {
        return MapPieceKind::ManaLake;
    }
    if (text == "berries") {
        return MapPieceKind::Berries;
    }
    if (text == "middle_point") {
        return MapPieceKind::MiddlePoint;
    }

    return std::nullopt;
}

[[nodiscard]] const char* link_kind_to_string(const MapLinkKind kind)
{
    if (kind == MapLinkKind::Road) {
        return "road";
    }

    return "relative";
}

[[nodiscard]] MapLinkKind link_kind_from_string(const std::string& text)
{
    if (text == "road") {
        return MapLinkKind::Road;
    }

    return MapLinkKind::Relative;
}

[[nodiscard]] const char* builtin_to_string(const MapPatternBuiltin builtin)
{
    switch (builtin) {
    case MapPatternBuiltin::Default:
        return "default";
    case MapPatternBuiltin::Continental:
        return "continental";
    case MapPatternBuiltin::Archipelago:
        return "archipelago";
    case MapPatternBuiltin::Crossing:
        return "crossing";
    case MapPatternBuiltin::Commons:
        return "commons";
    case MapPatternBuiltin::Custom:
        break;
    }

    return "custom";
}

[[nodiscard]] MapPatternBuiltin builtin_from_string(const std::string& text)
{
    if (text == "default") {
        return MapPatternBuiltin::Default;
    }
    if (text == "continental") {
        return MapPatternBuiltin::Continental;
    }
    if (text == "archipelago") {
        return MapPatternBuiltin::Archipelago;
    }
    if (text == "crossing") {
        return MapPatternBuiltin::Crossing;
    }
    if (text == "commons") {
        return MapPatternBuiltin::Commons;
    }

    return MapPatternBuiltin::Custom;
}

[[nodiscard]] MapPiece make_piece(
    const std::uint32_t id,
    const MapPieceKind kind,
    const int x,
    const int y,
    const int width,
    const int height,
    const int range = constants::PATTERN_FOREST_DEFAULT_RANGE,
    const int gold_count = constants::PATTERN_RESOURCE_DEFAULT_GOLD,
    const int berry_count = constants::PATTERN_RESOURCE_DEFAULT_BERRY)
{
    MapPiece piece{};
    piece.id = id;
    piece.kind = kind;
    piece.x = x;
    piece.y = y;
    piece.width = width;
    piece.height = height;
    piece.range = range;
    piece.gold_count = gold_count;
    piece.berry_count = berry_count;
    return piece;
}

[[nodiscard]] MapPieceLink make_link(
    const std::uint32_t from_id,
    const std::uint32_t to_id,
    const MapLinkKind kind = MapLinkKind::Relative)
{
    return MapPieceLink{from_id, to_id, kind};
}

[[nodiscard]] MapPattern make_crossing_pattern()
{
    MapPattern pattern{};
    pattern.builtin = MapPatternBuiltin::Crossing;
    pattern.name = std::string(constants::MAP_PATTERN_NAMES[constants::MAP_PATTERN_CROSSING_INDEX]);
    pattern.map_width = constants::PATTERN_MAP_SIZE_PRESETS[1];
    pattern.map_height = constants::PATTERN_MAP_SIZE_PRESETS[1];
    pattern.fixed_map_size = true;
    pattern.min_players = 2U;
    pattern.max_players = 2U;
    pattern.fixed_player_count = 2U;
    pattern.border_trees = true;
    pattern.pieces = {
        make_piece(1U, MapPieceKind::StartPosition, 14, 14, 1, 1),
        make_piece(3U, MapPieceKind::Forest, 60, 24, 4, 4, constants::PATTERN_CROSSING_OUTER_FOREST_RANGE),
        make_piece(4U, MapPieceKind::Forest, 27, 60, 4, 4, constants::PATTERN_CROSSING_OUTER_FOREST_RANGE),
        make_piece(5U, MapPieceKind::Forest, 32, 0, 4, 4, constants::PATTERN_CROSSING_OUTER_FOREST_RANGE),
        make_piece(6U, MapPieceKind::Forest, 0, 33, 4, 4, constants::PATTERN_CROSSING_OUTER_FOREST_RANGE),
        make_piece(7U, MapPieceKind::Forest, 60, 0, 4, 4, constants::PATTERN_CROSSING_CORNER_FOREST_RANGE),
        make_piece(8U, MapPieceKind::Forest, 0, 60, 4, 4, constants::PATTERN_CROSSING_CORNER_FOREST_RANGE),
        make_piece(
            9U,
            MapPieceKind::Gold,
            7,
            53,
            4,
            4,
            constants::PATTERN_FOREST_DEFAULT_RANGE,
            constants::PATTERN_CROSSING_SIDE_GOLD_COUNT),
        make_piece(
            10U,
            MapPieceKind::Gold,
            52,
            8,
            4,
            4,
            constants::PATTERN_FOREST_DEFAULT_RANGE,
            constants::PATTERN_CROSSING_SIDE_GOLD_COUNT),
        make_piece(11U, MapPieceKind::MiddlePoint, 12, 48, 4, 4),
        make_piece(12U, MapPieceKind::MiddlePoint, 47, 13, 4, 4),
        make_piece(
            13U,
            MapPieceKind::Berries,
            34,
            33,
            4,
            4,
            constants::PATTERN_FOREST_DEFAULT_RANGE,
            constants::PATTERN_RESOURCE_DEFAULT_GOLD,
            constants::PATTERN_CROSSING_GAP_BERRY_COUNT),
        make_piece(
            14U,
            MapPieceKind::Berries,
            26,
            25,
            4,
            4,
            constants::PATTERN_FOREST_DEFAULT_RANGE,
            constants::PATTERN_RESOURCE_DEFAULT_GOLD,
            constants::PATTERN_CROSSING_GAP_BERRY_COUNT),
        make_piece(15U, MapPieceKind::StartPosition, 49, 49, 1, 1),
        make_piece(16U, MapPieceKind::Forest, 46, 0, 4, 4),
        make_piece(17U, MapPieceKind::Forest, 60, 11, 4, 4),
        make_piece(18U, MapPieceKind::Forest, 15, 60, 4, 4),
        make_piece(19U, MapPieceKind::Forest, 0, 46, 4, 4),
        make_piece(26U, MapPieceKind::Forest, 40, 25, 4, 4),
        make_piece(27U, MapPieceKind::Forest, 25, 40, 4, 4),
        make_piece(28U, MapPieceKind::Forest, 33, 19, 4, 4),
        make_piece(29U, MapPieceKind::Forest, 18, 34, 4, 4),
        make_piece(30U, MapPieceKind::Forest, 39, 20, 4, 4),
        make_piece(31U, MapPieceKind::Forest, 19, 39, 4, 4),
    };
    pattern.links = {
        make_link(15U, 4U),
        make_link(4U, 8U),
        make_link(8U, 6U),
        make_link(6U, 1U),
        make_link(1U, 5U),
        make_link(5U, 7U),
        make_link(7U, 3U),
        make_link(3U, 15U),
        make_link(7U, 10U),
        make_link(8U, 9U),
        make_link(9U, 11U),
        make_link(13U, 14U),
        make_link(11U, 13U),
        make_link(14U, 11U),
        make_link(14U, 12U),
        make_link(13U, 12U),
        make_link(3U, 13U),
        make_link(4U, 13U),
        make_link(14U, 6U),
        make_link(5U, 14U),
        make_link(1U, 14U),
        make_link(13U, 15U),
        make_link(15U, 13U, MapLinkKind::Road),
        make_link(13U, 14U, MapLinkKind::Road),
        make_link(14U, 1U, MapLinkKind::Road),
        make_link(15U, 11U, MapLinkKind::Road),
        make_link(11U, 1U, MapLinkKind::Road),
        make_link(1U, 12U, MapLinkKind::Road),
        make_link(12U, 15U, MapLinkKind::Road),
        make_link(10U, 12U),
        make_link(11U, 9U),
        make_link(26U, 14U),
        make_link(14U, 27U),
        make_link(27U, 13U),
        make_link(13U, 26U),
        make_link(26U, 12U),
        make_link(27U, 11U),
        make_link(28U, 26U),
        make_link(28U, 12U),
        make_link(28U, 14U),
        make_link(13U, 28U),
        make_link(13U, 29U),
        make_link(29U, 27U),
        make_link(29U, 11U),
        make_link(29U, 14U),
        make_link(29U, 31U),
        make_link(31U, 27U),
        make_link(26U, 30U),
        make_link(30U, 28U),
        make_link(30U, 12U),
        make_link(31U, 11U),
    };
    return pattern;
}

[[nodiscard]] MapPattern make_commons_pattern()
{
    MapPattern pattern{};
    pattern.builtin = MapPatternBuiltin::Commons;
    pattern.name = std::string(constants::MAP_PATTERN_NAMES[constants::MAP_PATTERN_COMMONS_INDEX]);
    pattern.map_width = constants::PATTERN_MAP_SIZE_PRESETS[2];
    pattern.map_height = constants::PATTERN_MAP_SIZE_PRESETS[2];
    pattern.fixed_map_size = false;
    pattern.min_players = static_cast<std::uint8_t>(constants::PATTERN_DEFAULT_MIN_PLAYERS);
    pattern.max_players = static_cast<std::uint8_t>(constants::PATTERN_DEFAULT_MAX_PLAYERS);
    pattern.border_trees = true;
    pattern.random_stray_trees = true;
    pattern.random_stray_tree_count = constants::PATTERN_RANDOM_STRAY_TREE_MAX;
    pattern.pieces = {
        make_piece(1U, MapPieceKind::MultiStartPosition, 64, 59, 4, 4),
        make_piece(4U, MapPieceKind::MiddlePoint, 18, 16, 4, 4),
        make_piece(5U, MapPieceKind::Forest, 2, 17, 4, 4, constants::PATTERN_COMMONS_FOREST_RANGE),
        make_piece(6U, MapPieceKind::Forest, 18, 0, 4, 4, constants::PATTERN_COMMONS_EDGE_FOREST_RANGE),
        make_piece(7U, MapPieceKind::Forest, 10, 39, 4, 4, constants::PATTERN_COMMONS_FOREST_RANGE),
        make_piece(8U, MapPieceKind::Forest, 40, 6, 4, 4, constants::PATTERN_COMMONS_FOREST_RANGE),
        make_piece(
            9U,
            MapPieceKind::Berries,
            72,
            67,
            4,
            4,
            constants::PATTERN_FOREST_DEFAULT_RANGE,
            constants::PATTERN_RESOURCE_DEFAULT_GOLD,
            constants::PATTERN_COMMONS_CENTER_BERRY_COUNT),
        make_piece(
            10U,
            MapPieceKind::Gold,
            68,
            63,
            4,
            4,
            constants::PATTERN_FOREST_DEFAULT_RANGE,
            constants::PATTERN_COMMONS_CENTER_GOLD_COUNT),
    };
    pattern.links = {
        make_link(1U, 4U, MapLinkKind::Road),
        make_link(1U, 5U),
        make_link(1U, 6U),
        make_link(6U, 5U),
        make_link(4U, 1U),
        make_link(4U, 1U, MapLinkKind::Road),
        make_link(7U, 1U),
        make_link(7U, 5U),
        make_link(1U, 8U),
        make_link(8U, 6U),
        make_link(7U, 8U),
        make_link(8U, 4U),
        make_link(4U, 7U),
        make_link(4U, 5U),
        make_link(4U, 6U),
        make_link(8U, 7U),
        make_link(1U, 9U),
        make_link(10U, 1U),
    };
    return pattern;
}

[[nodiscard]] MapPiece piece_from_json(const nlohmann::json& json)
{
    MapPiece piece{};
    piece.id = json.value("id", 0U);
    if (const auto kind = kind_from_string(json.value("kind", std::string{"forest"}))) {
        piece.kind = *kind;
    }
    piece.x = json.value("x", 0);
    piece.y = json.value("y", 0);
    piece.width = json.value("w", 8);
    piece.height = json.value("h", 8);
    piece.density = json.value("density", constants::PATTERN_FOREST_DEFAULT_DENSITY);
    piece.patch_size = json.value("patch_size", constants::PATTERN_FOREST_DEFAULT_PATCH_SIZE);
    piece.range = json.value("range", constants::PATTERN_FOREST_DEFAULT_RANGE);
    piece.gold_count = json.value("gold_count", constants::PATTERN_RESOURCE_DEFAULT_GOLD);
    piece.berry_count = json.value("berry_count", constants::PATTERN_RESOURCE_DEFAULT_BERRY);
    piece.ground_type = json.value("ground_type", static_cast<std::uint8_t>(0U));
    piece.start_slot = json.value("start_slot", static_cast<std::uint8_t>(0U));
    return piece;
}

[[nodiscard]] nlohmann::json piece_to_json(const MapPiece& piece)
{
    return {
        {"id", piece.id},
        {"kind", kind_to_string(piece.kind)},
        {"x", piece.x},
        {"y", piece.y},
        {"w", piece.width},
        {"h", piece.height},
        {"density", piece.density},
        {"patch_size", piece.patch_size},
        {"range", piece.range},
        {"gold_count", piece.gold_count},
        {"berry_count", piece.berry_count},
        {"ground_type", piece.ground_type},
        {"start_slot", piece.start_slot},
    };
}

void clamp_pattern_sizes(MapPattern& pattern)
{
    pattern.map_width = std::clamp(
        pattern.map_width,
        constants::PATTERN_MIN_MAP_SIZE,
        constants::PATTERN_MAX_MAP_SIZE);
    pattern.map_height = std::clamp(
        pattern.map_height,
        constants::PATTERN_MIN_MAP_SIZE,
        constants::PATTERN_MAX_MAP_SIZE);
    if (pattern.min_players == 0U) {
        pattern.min_players = 1U;
    }
    if (pattern.max_players < pattern.min_players) {
        pattern.max_players = pattern.min_players;
    }
    if (pattern.max_players > static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)) {
        pattern.max_players = static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS);
    }
}

} // namespace

MapPattern make_builtin_pattern(const std::uint8_t pattern_index)
{
    if (pattern_index == constants::MAP_PATTERN_CROSSING_INDEX) {
        return make_crossing_pattern();
    }

    return make_commons_pattern();
}

[[nodiscard]] nlohmann::json pattern_to_json(const MapPattern& pattern, const std::string_view kind)
{
    nlohmann::json json{
        {"kind", std::string(kind)},
        {"version", pattern.version},
        {"name", pattern.name},
        {"builtin", builtin_to_string(pattern.builtin)},
        {"min_players", pattern.min_players},
        {"max_players", pattern.max_players},
        {"fixed_player_count", pattern.fixed_player_count},
        {"map_width", pattern.map_width},
        {"map_height", pattern.map_height},
        {"fixed_map_size", pattern.fixed_map_size},
        {"min_start_separation", pattern.min_start_separation},
        {"random_gold", pattern.random_gold},
        {"random_gold_count", pattern.random_gold_count},
        {"random_berries", pattern.random_berries},
        {"random_berry_count", pattern.random_berry_count},
        {"border_trees", pattern.border_trees},
        {"random_stray_trees", pattern.random_stray_trees},
        {"random_stray_tree_count", pattern.random_stray_tree_count},
        {"start_ring_mode", pattern.start_ring_mode},
        {"pieces", nlohmann::json::array()},
        {"links", nlohmann::json::array()},
    };

    for (const MapPiece& piece : pattern.pieces) {
        json["pieces"].push_back(piece_to_json(piece));
    }
    for (const MapPieceLink& link : pattern.links) {
        json["links"].push_back({
            {"from", link.from_id},
            {"to", link.to_id},
            {"kind", link_kind_to_string(link.kind)},
        });
    }

    return json;
}

std::string serialize_map_pattern(const MapPattern& pattern)
{
    return pattern_to_json(pattern, constants::PATTERN_RUNTIME_KIND).dump();
}

std::string serialize_raw_map_pattern(const MapPattern& pattern)
{
    return pattern_to_json(pattern, constants::PATTERN_EDITOR_KIND).dump();
}

[[nodiscard]] std::optional<MapPattern> parse_pattern_json(
    const std::string& text,
    const std::string_view required_kind)
{
    if (text.empty()) {
        return std::nullopt;
    }

    nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return std::nullopt;
    }

    const std::string kind = json.value("kind", std::string{});
    if (kind != required_kind) {
        return std::nullopt;
    }

    MapPattern pattern{};
    pattern.version = json.value("version", constants::PATTERN_FILE_VERSION);
    pattern.name = json.value("name", std::string{});
    pattern.builtin = builtin_from_string(json.value("builtin", std::string{"custom"}));
    pattern.min_players = json.value(
        "min_players",
        static_cast<std::uint8_t>(constants::PATTERN_DEFAULT_MIN_PLAYERS));
    pattern.max_players = json.value(
        "max_players",
        static_cast<std::uint8_t>(constants::PATTERN_DEFAULT_MAX_PLAYERS));
    pattern.fixed_player_count = json.value("fixed_player_count", static_cast<std::uint8_t>(0U));
    pattern.map_width = json.value("map_width", constants::MAP_TEST_WIDTH);
    pattern.map_height = json.value("map_height", constants::MAP_TEST_HEIGHT);
    pattern.fixed_map_size = json.value("fixed_map_size", true);
    pattern.min_start_separation =
        json.value("min_start_separation", constants::PATTERN_DEFAULT_MIN_START_SEPARATION);
    pattern.random_gold = json.value("random_gold", false);
    pattern.random_gold_count = json.value("random_gold_count", constants::PATTERN_RANDOM_GOLD_DEFAULT);
    pattern.random_berries = json.value("random_berries", false);
    pattern.random_berry_count =
        json.value("random_berry_count", constants::PATTERN_RANDOM_BERRY_DEFAULT);
    pattern.border_trees = json.value("border_trees", false);
    pattern.random_stray_trees = json.value("random_stray_trees", false);
    pattern.random_stray_tree_count =
        json.value("random_stray_tree_count", constants::PATTERN_RANDOM_STRAY_TREE_DEFAULT);
    pattern.start_ring_mode = json.value("start_ring_mode", false);

    if (json.contains("pieces") && json["pieces"].is_array()) {
        for (const nlohmann::json& piece_json : json["pieces"]) {
            if (!piece_json.is_object()) {
                continue;
            }
            pattern.pieces.push_back(piece_from_json(piece_json));
        }
    }

    if (json.contains("links") && json["links"].is_array()) {
        for (const nlohmann::json& link_json : json["links"]) {
            if (!link_json.is_object()) {
                continue;
            }
            pattern.links.push_back(MapPieceLink{
                .from_id = link_json.value("from", 0U),
                .to_id = link_json.value("to", 0U),
                .kind = link_kind_from_string(link_json.value("kind", std::string{"relative"})),
            });
        }
    }

    clamp_pattern_sizes(pattern);
    return pattern;
}

std::optional<MapPattern> parse_map_pattern(const std::string& text)
{
    return parse_pattern_json(text, constants::PATTERN_RUNTIME_KIND);
}

std::optional<MapPattern> parse_raw_map_pattern(const std::string& text)
{
    return parse_pattern_json(text, constants::PATTERN_EDITOR_KIND);
}

std::optional<MapPattern> load_map_pattern_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parse_map_pattern(buffer.str());
}

bool save_map_pattern_file(const std::filesystem::path& path, const MapPattern& pattern)
{
    std::error_code error{};
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }

    output << serialize_map_pattern(pattern);
    return static_cast<bool>(output);
}

std::optional<MapPattern> load_raw_map_pattern_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parse_raw_map_pattern(buffer.str());
}

bool save_raw_map_pattern_file(const std::filesystem::path& path, const MapPattern& pattern)
{
    std::error_code error{};
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }

    output << serialize_raw_map_pattern(pattern);
    return static_cast<bool>(output);
}

MapPattern resolve_map_pattern(const std::uint8_t pattern_index, const std::string& pattern_payload)
{
    if (!pattern_payload.empty()) {
        if (const std::optional<MapPattern> parsed = parse_map_pattern(pattern_payload)) {
            return *parsed;
        }
    }

    if (pattern_index == constants::MAP_PATTERN_OTHER_INDEX) {
        return make_builtin_pattern(constants::MAP_PATTERN_COMMONS_INDEX);
    }

    return make_builtin_pattern(pattern_index);
}

std::string map_pattern_display_name(
    const std::uint8_t pattern_index,
    const std::string& pattern_name)
{
    if (pattern_index == constants::MAP_PATTERN_OTHER_INDEX) {
        if (!pattern_name.empty()) {
            return pattern_name;
        }
        return std::string(constants::MAP_PATTERN_NAMES[constants::MAP_PATTERN_OTHER_INDEX]);
    }

    if (pattern_index < constants::MAP_PATTERN_NAMES.size()) {
        return std::string(constants::MAP_PATTERN_NAMES[pattern_index]);
    }

    return std::string(constants::MAP_PATTERN_NAMES[constants::MAP_PATTERN_COMMONS_INDEX]);
}

std::optional<std::string> map_pattern_player_constraint_error(
    const MapPattern& pattern,
    const std::uint8_t player_count)
{
    const auto make_message = [player_count](const std::string_view prefix, const int required) {
        return std::string(prefix) + std::to_string(required)
            + std::string(constants::PATTERN_CONSTRAINT_PLAYERS_MID)
            + std::to_string(player_count)
            + std::string(constants::PATTERN_CONSTRAINT_PLAYERS_SUFFIX);
    };

    if (pattern.fixed_player_count > 0U && player_count != pattern.fixed_player_count) {
        return make_message(
            constants::PATTERN_CONSTRAINT_FIXED_PLAYERS_PREFIX,
            pattern.fixed_player_count);
    }

    if (player_count < pattern.min_players) {
        return make_message(constants::PATTERN_CONSTRAINT_MIN_PLAYERS_PREFIX, pattern.min_players);
    }

    if (player_count > pattern.max_players) {
        return make_message(constants::PATTERN_CONSTRAINT_MAX_PLAYERS_PREFIX, pattern.max_players);
    }

    return std::nullopt;
}

bool map_pattern_locks_player_count(const MapPattern& pattern)
{
    return pattern.fixed_player_count > 0U;
}

bool map_pattern_locks_map_size(const MapPattern& pattern)
{
    return pattern.fixed_map_size;
}

MapPattern apply_lobby_size_to_pattern(
    const MapPattern& pattern,
    const int map_width,
    const int map_height)
{
    if (pattern.fixed_map_size) {
        return pattern;
    }

    MapPattern adjusted = pattern;
    adjusted.map_width = map_width;
    adjusted.map_height = map_height;
    clamp_pattern_sizes(adjusted);
    return adjusted;
}

std::uint8_t map_pattern_effective_player_count(
    const MapPattern& pattern,
    const std::uint8_t requested_player_count)
{
    if (pattern.fixed_player_count > 0U) {
        return pattern.fixed_player_count;
    }

    return std::clamp(requested_player_count, pattern.min_players, pattern.max_players);
}

std::uint32_t next_map_piece_id(const MapPattern& pattern)
{
    std::uint32_t next_id = 1U;
    for (const MapPiece& piece : pattern.pieces) {
        if (piece.id >= next_id) {
            next_id = piece.id + 1U;
        }
    }

    return next_id;
}

bool piece_is_start(const MapPieceKind kind)
{
    return kind == MapPieceKind::StartPosition || kind == MapPieceKind::MultiStartPosition;
}

bool pattern_has_multi_start(const MapPattern& pattern)
{
    for (const MapPiece& piece : pattern.pieces) {
        if (piece.kind == MapPieceKind::MultiStartPosition) {
            return true;
        }
    }

    return false;
}

bool pattern_has_multi_start_center_road(const MapPattern& pattern)
{
    std::uint32_t multi_id = 0U;
    bool has_multi = false;
    bool has_middle = false;
    for (const MapPiece& piece : pattern.pieces) {
        if (piece.kind == MapPieceKind::MultiStartPosition) {
            multi_id = piece.id;
            has_multi = true;
        }
        if (piece.kind == MapPieceKind::MiddlePoint) {
            has_middle = true;
        }
    }
    if (!has_multi || !has_middle) {
        return false;
    }

    for (const MapPieceLink& link : pattern.links) {
        if (link.kind != MapLinkKind::Road) {
            continue;
        }

        const MapPiece* from = nullptr;
        const MapPiece* to = nullptr;
        for (const MapPiece& piece : pattern.pieces) {
            if (piece.id == link.from_id) {
                from = &piece;
            }
            if (piece.id == link.to_id) {
                to = &piece;
            }
        }
        if (from == nullptr || to == nullptr) {
            continue;
        }

        const bool touches_multi =
            from->id == multi_id || to->id == multi_id;
        const bool touches_middle =
            from->kind == MapPieceKind::MiddlePoint || to->kind == MapPieceKind::MiddlePoint;
        if (touches_multi && touches_middle) {
            return true;
        }
    }

    return false;
}

int pattern_start_position_count(const MapPattern& pattern)
{
    int count = 0;
    for (const MapPiece& piece : pattern.pieces) {
        if (piece.kind == MapPieceKind::StartPosition) {
            ++count;
        }
    }

    return count;
}

int pattern_start_position_limit(const MapPattern& pattern)
{
    if (pattern.fixed_player_count > 0U) {
        return static_cast<int>(pattern.fixed_player_count);
    }

    return static_cast<int>(pattern.max_players);
}

bool pattern_can_place_start_position(const MapPattern& pattern)
{
    if (pattern_has_multi_start(pattern)) {
        return false;
    }

    return pattern_start_position_count(pattern) < pattern_start_position_limit(pattern);
}

bool pattern_can_place_multi_start(const MapPattern& pattern)
{
    return !pattern_has_multi_start(pattern) && pattern_start_position_count(pattern) == 0;
}

void clamp_piece_to_map(MapPiece& piece, const int map_width, const int map_height)
{
    piece.width = std::clamp(piece.width, 1, std::max(1, map_width));
    piece.height = std::clamp(piece.height, 1, std::max(1, map_height));
    piece.x = std::clamp(piece.x, 0, std::max(0, map_width - piece.width));
    piece.y = std::clamp(piece.y, 0, std::max(0, map_height - piece.height));
}

} // namespace aoa::sim::map
