#pragma once

#include "core/constants.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aoa::sim::map {

enum class MapPieceKind : std::uint8_t {
    StartPosition = 0,
    MultiStartPosition = 1,
    Forest = 2,
    Gold = 3,
    Terrain = 4,
    ManaLake = 5,
    Berries = 6,
    MiddlePoint = 7,
};

enum class MapLinkKind : std::uint8_t {
    Relative = 0,
    Road = 1,
};

enum class MapPatternBuiltin : std::uint8_t {
    Custom = 0,
    Default = 1,
    Continental = 2,
    Archipelago = 3,
    Crossing = 4,
    Commons = 5,
};

struct MapPiece {
    std::uint32_t id{0U};
    MapPieceKind kind{MapPieceKind::Forest};
    int x{0};
    int y{0};
    int width{8};
    int height{8};
    int density{constants::PATTERN_FOREST_DEFAULT_DENSITY};
    int patch_size{constants::PATTERN_FOREST_DEFAULT_PATCH_SIZE};
    int range{constants::PATTERN_FOREST_DEFAULT_RANGE};
    int gold_count{constants::PATTERN_RESOURCE_DEFAULT_GOLD};
    int berry_count{constants::PATTERN_RESOURCE_DEFAULT_BERRY};
    std::uint8_t ground_type{0U};
    std::uint8_t start_slot{0U};
};

struct MapPieceLink {
    std::uint32_t from_id{0U};
    std::uint32_t to_id{0U};
    MapLinkKind kind{MapLinkKind::Relative};
};

struct MapPattern {
    int version{constants::PATTERN_FILE_VERSION};
    std::string name{};
    MapPatternBuiltin builtin{MapPatternBuiltin::Custom};
    std::uint8_t min_players{static_cast<std::uint8_t>(constants::PATTERN_DEFAULT_MIN_PLAYERS)};
    std::uint8_t max_players{static_cast<std::uint8_t>(constants::PATTERN_DEFAULT_MAX_PLAYERS)};
    std::uint8_t fixed_player_count{0U};
    int map_width{constants::MAP_TEST_WIDTH};
    int map_height{constants::MAP_TEST_HEIGHT};
    bool fixed_map_size{false};
    int min_start_separation{constants::PATTERN_DEFAULT_MIN_START_SEPARATION};
    bool random_gold{false};
    int random_gold_count{constants::PATTERN_RANDOM_GOLD_DEFAULT};
    bool random_berries{false};
    int random_berry_count{constants::PATTERN_RANDOM_BERRY_DEFAULT};
    bool border_trees{false};
    bool random_stray_trees{false};
    int random_stray_tree_count{constants::PATTERN_RANDOM_STRAY_TREE_DEFAULT};
    bool start_ring_mode{false};
    std::vector<MapPiece> pieces{};
    std::vector<MapPieceLink> links{};
};

[[nodiscard]] MapPattern make_builtin_pattern(std::uint8_t pattern_index);

[[nodiscard]] std::string serialize_map_pattern(const MapPattern& pattern);

[[nodiscard]] std::string serialize_raw_map_pattern(const MapPattern& pattern);

[[nodiscard]] std::optional<MapPattern> parse_map_pattern(const std::string& text);

[[nodiscard]] std::optional<MapPattern> parse_raw_map_pattern(const std::string& text);

[[nodiscard]] std::optional<MapPattern> load_map_pattern_file(const std::filesystem::path& path);

[[nodiscard]] std::optional<MapPattern> load_raw_map_pattern_file(const std::filesystem::path& path);

[[nodiscard]] bool save_map_pattern_file(const std::filesystem::path& path, const MapPattern& pattern);

[[nodiscard]] bool save_raw_map_pattern_file(const std::filesystem::path& path, const MapPattern& pattern);

[[nodiscard]] MapPattern resolve_map_pattern(
    std::uint8_t pattern_index,
    const std::string& pattern_payload);

[[nodiscard]] std::string map_pattern_display_name(
    std::uint8_t pattern_index,
    const std::string& pattern_name);

[[nodiscard]] std::optional<std::string> map_pattern_player_constraint_error(
    const MapPattern& pattern,
    std::uint8_t player_count);

[[nodiscard]] bool map_pattern_locks_player_count(const MapPattern& pattern);

[[nodiscard]] bool map_pattern_locks_map_size(const MapPattern& pattern);

[[nodiscard]] MapPattern apply_lobby_size_to_pattern(
    const MapPattern& pattern,
    int map_width,
    int map_height);

[[nodiscard]] std::uint8_t map_pattern_effective_player_count(
    const MapPattern& pattern,
    std::uint8_t requested_player_count);

[[nodiscard]] std::uint32_t next_map_piece_id(const MapPattern& pattern);

[[nodiscard]] bool piece_is_start(MapPieceKind kind);

[[nodiscard]] bool pattern_has_multi_start(const MapPattern& pattern);

[[nodiscard]] bool pattern_has_multi_start_center_road(const MapPattern& pattern);

[[nodiscard]] int pattern_start_position_count(const MapPattern& pattern);

[[nodiscard]] int pattern_start_position_limit(const MapPattern& pattern);

[[nodiscard]] bool pattern_can_place_start_position(const MapPattern& pattern);

[[nodiscard]] bool pattern_can_place_multi_start(const MapPattern& pattern);

void clamp_piece_to_map(MapPiece& piece, int map_width, int map_height);

} // namespace aoa::sim::map
