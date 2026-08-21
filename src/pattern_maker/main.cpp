#include "app/native_file_dialog.hpp"
#include "core/constants.hpp"
#include "core/grid.hpp"
#include "core/runtime_paths.hpp"
#include "sim/map/map_generator.hpp"
#include "sim/map/map_pattern.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace constants = aoa::constants;
using aoa::sim::map::MapGenerationConfig;
using aoa::sim::map::MapLinkKind;
using aoa::sim::map::MapPattern;
using aoa::sim::map::MapPiece;
using aoa::sim::map::MapPieceKind;
using aoa::sim::map::MapPieceLink;

enum class LinkTool : std::uint8_t {
    None = 0,
    Relative = 1,
    Road = 2,
};

struct MakerButton {
    sf::FloatRect rect{};
    std::string label{};
    std::string command{};
    bool enabled{true};
};

struct MapView {
    sf::Vector2f origin{};
    float half_cell{1.0F};
};

struct CreateMapDialog {
    bool open{false};
    bool limit_players{false};
    int player_limit{constants::PATTERN_FIXED_PLAYERS_MIN};
    int size_preset_index{constants::PATTERN_DEFAULT_MAP_SIZE_PRESET_INDEX};
};

struct PatternMaker {
    MapPattern pattern{};
    std::filesystem::path file_path{};
    std::vector<std::string> undo_stack{};
    std::vector<std::string> redo_stack{};
    std::optional<std::uint32_t> selected_id{};
    std::optional<std::uint32_t> link_from_id{};
    std::optional<MapPieceKind> place_kind{};
    LinkTool link_tool{LinkTool::None};
    bool preview_mode{false};
    bool dragging{false};
    sf::Vector2i drag_offset{};
    std::uint64_t preview_seed{1U};
    std::string status{"New pattern"};
    sf::Font font{};
    bool font_ready{false};
    CreateMapDialog create_dialog{};
    bool startup_open{true};
    bool confirm_close_open{false};
    bool session_started{false};
};

[[nodiscard]] MapPattern make_blank_pattern()
{
    MapPattern pattern{};
    pattern.name = std::string(constants::PATTERN_MAKER_DEFAULT_NAME);
    pattern.map_width = constants::MAP_TEST_WIDTH;
    pattern.map_height = constants::MAP_TEST_HEIGHT;
    pattern.min_players = static_cast<std::uint8_t>(constants::PATTERN_DEFAULT_MIN_PLAYERS);
    pattern.max_players = static_cast<std::uint8_t>(constants::PATTERN_DEFAULT_MAX_PLAYERS);
    return pattern;
}

[[nodiscard]] const char* piece_kind_label(const MapPieceKind kind)
{
    switch (kind) {
    case MapPieceKind::StartPosition:
        return "Start position";
    case MapPieceKind::MultiStartPosition:
        return "Multi start position";
    case MapPieceKind::Forest:
        return "Forest";
    case MapPieceKind::Gold:
        return "Gold";
    case MapPieceKind::Terrain:
        return "Terrain";
    case MapPieceKind::ManaLake:
        return "Mana lake";
    case MapPieceKind::Berries:
        return "Berries";
    case MapPieceKind::MiddlePoint:
        return "Middle point";
    }

    return "Forest";
}

[[nodiscard]] sf::Color piece_color(const MapPieceKind kind)
{
    switch (kind) {
    case MapPieceKind::StartPosition:
        return sf::Color{80, 220, 90};
    case MapPieceKind::MultiStartPosition:
        return sf::Color{40, 180, 70};
    case MapPieceKind::Forest:
        return sf::Color{30, 110, 40};
    case MapPieceKind::Gold:
        return sf::Color{220, 180, 40};
    case MapPieceKind::Terrain:
        return sf::Color{120, 100, 70};
    case MapPieceKind::ManaLake:
        return sf::Color{50, 110, 210};
    case MapPieceKind::Berries:
        return sf::Color{160, 40, 70};
    case MapPieceKind::MiddlePoint:
        return sf::Color{180, 180, 180};
    }

    return sf::Color::White;
}

void push_undo(PatternMaker& maker)
{
    maker.undo_stack.push_back(aoa::sim::map::serialize_raw_map_pattern(maker.pattern));
    if (static_cast<int>(maker.undo_stack.size()) > constants::PATTERN_MAKER_UNDO_LIMIT) {
        maker.undo_stack.erase(maker.undo_stack.begin());
    }

    maker.redo_stack.clear();
}

void restore_serialized(PatternMaker& maker, const std::string& text)
{
    if (const std::optional<MapPattern> parsed = aoa::sim::map::parse_raw_map_pattern(text)) {
        maker.pattern = *parsed;
    }
}

void undo(PatternMaker& maker)
{
    if (maker.undo_stack.empty()) {
        return;
    }

    maker.redo_stack.push_back(aoa::sim::map::serialize_raw_map_pattern(maker.pattern));
    restore_serialized(maker, maker.undo_stack.back());
    maker.undo_stack.pop_back();
    maker.status = "Undo";
}

void redo(PatternMaker& maker)
{
    if (maker.redo_stack.empty()) {
        return;
    }

    maker.undo_stack.push_back(aoa::sim::map::serialize_raw_map_pattern(maker.pattern));
    restore_serialized(maker, maker.redo_stack.back());
    maker.redo_stack.pop_back();
    maker.status = "Redo";
}

[[nodiscard]] int snap_coord(const int value)
{
    const int snap = std::max(1, constants::PATTERN_MAKER_SNAP_CELLS);
    return (value / snap) * snap;
}

[[nodiscard]] sf::FloatRect grid_rect(const sf::Vector2u window_size)
{
    const float left = static_cast<float>(constants::PATTERN_MAKER_GRID_PAD);
    const float top = static_cast<float>(constants::PATTERN_MAKER_GRID_PAD);
    const float right = static_cast<float>(
        window_size.x - constants::PATTERN_MAKER_PANEL_WIDTH - constants::PATTERN_MAKER_GRID_PAD);
    const float bottom = static_cast<float>(
        window_size.y - constants::PATTERN_MAKER_STATUS_HEIGHT - constants::PATTERN_MAKER_GRID_PAD);
    return sf::FloatRect{{left, top}, {std::max(1.0F, right - left), std::max(1.0F, bottom - top)}};
}

[[nodiscard]] MapView make_map_view(const PatternMaker& maker, const sf::FloatRect& grid)
{
    const int width = std::max(1, maker.pattern.map_width);
    const int height = std::max(1, maker.pattern.map_height);
    const float half = std::min(grid.size.x, grid.size.y) / static_cast<float>(width + height);
    const float diamond_h = static_cast<float>(width + height - 2) * half;
    return MapView{
        {grid.position.x + grid.size.x * 0.5F,
            grid.position.y + (grid.size.y - diamond_h) * 0.5F},
        half,
    };
}

[[nodiscard]] sf::Vector2f tile_to_screen(const MapView& view, const float x, const float y)
{
    return {
        view.origin.x + (x - y) * view.half_cell,
        view.origin.y + (x + y) * view.half_cell,
    };
}

[[nodiscard]] sf::Vector2i cell_at(
    const PatternMaker& maker,
    const sf::FloatRect& grid,
    const sf::Vector2f mouse)
{
    const MapView view = make_map_view(maker, grid);
    const float dx = (mouse.x - view.origin.x) / view.half_cell;
    const float dy = (mouse.y - view.origin.y) / view.half_cell;
    return {
        static_cast<int>(std::floor((dx + dy) * 0.5F)),
        static_cast<int>(std::floor((dy - dx) * 0.5F)),
    };
}

[[nodiscard]] MapPiece* piece_by_id(MapPattern& pattern, const std::uint32_t id)
{
    for (MapPiece& piece : pattern.pieces) {
        if (piece.id == id) {
            return &piece;
        }
    }

    return nullptr;
}

[[nodiscard]] const MapPiece* piece_by_id(const MapPattern& pattern, const std::uint32_t id)
{
    for (const MapPiece& piece : pattern.pieces) {
        if (piece.id == id) {
            return &piece;
        }
    }

    return nullptr;
}

[[nodiscard]] bool point_in_triangle(
    const sf::Vector2f point,
    const sf::Vector2f a,
    const sf::Vector2f b,
    const sf::Vector2f c)
{
    const auto sign = [](const sf::Vector2f p, const sf::Vector2f q, const sf::Vector2f r) {
        return (p.x - r.x) * (q.y - r.y) - (q.x - r.x) * (p.y - r.y);
    };
    const float d1 = sign(point, a, b);
    const float d2 = sign(point, b, c);
    const float d3 = sign(point, c, a);
    const bool has_neg = (d1 < 0.0F) || (d2 < 0.0F) || (d3 < 0.0F);
    const bool has_pos = (d1 > 0.0F) || (d2 > 0.0F) || (d3 > 0.0F);
    return !(has_neg && has_pos);
}

[[nodiscard]] bool point_in_iso_quad(
    const sf::Vector2f point,
    const sf::Vector2f a,
    const sf::Vector2f b,
    const sf::Vector2f c,
    const sf::Vector2f d)
{
    return point_in_triangle(point, a, b, c) || point_in_triangle(point, a, c, d);
}

[[nodiscard]] std::optional<std::uint32_t> hit_piece(
    const PatternMaker& maker,
    const sf::FloatRect& grid,
    const sf::Vector2f mouse)
{
    const MapView view = make_map_view(maker, grid);
    for (auto it = maker.pattern.pieces.rbegin(); it != maker.pattern.pieces.rend(); ++it) {
        const sf::Vector2f a = tile_to_screen(view, static_cast<float>(it->x), static_cast<float>(it->y));
        const sf::Vector2f b = tile_to_screen(
            view,
            static_cast<float>(it->x + it->width),
            static_cast<float>(it->y));
        const sf::Vector2f c = tile_to_screen(
            view,
            static_cast<float>(it->x + it->width),
            static_cast<float>(it->y + it->height));
        const sf::Vector2f d = tile_to_screen(
            view,
            static_cast<float>(it->x),
            static_cast<float>(it->y + it->height));
        if (point_in_iso_quad(mouse, a, b, c, d)) {
            return it->id;
        }
    }

    return std::nullopt;
}

[[nodiscard]] sf::Vector2i snap_start_cell(const PatternMaker& maker, const sf::Vector2i cell)
{
    if (!maker.pattern.start_ring_mode) {
        return cell;
    }

    const aoa::core::GridPos snapped = aoa::sim::map::snap_to_start_ring(
        {cell.x, cell.y},
        maker.pattern.map_width,
        maker.pattern.map_height);
    return {snapped.x, snapped.y};
}

[[nodiscard]] std::string validate_pattern(const MapPattern& pattern)
{
    const bool has_multi = aoa::sim::map::pattern_has_multi_start(pattern);
    const int start_rects = aoa::sim::map::pattern_start_position_count(pattern);
    if (has_multi && start_rects > 0) {
        return "Use either start positions or one multi start position.";
    }

    if (!has_multi && start_rects == 0) {
        return "Add a start piece before export.";
    }

    if (!has_multi && start_rects < static_cast<int>(pattern.min_players)) {
        return "Not enough start positions for the minimum player count.";
    }

    if (has_multi && !aoa::sim::map::pattern_has_multi_start_center_road(pattern)) {
        return "Multi start needs a middle point linked with a road.";
    }

    MapGenerationConfig config{};
    config.player_count = pattern.fixed_player_count > 0U ? pattern.fixed_player_count
                                                         : pattern.max_players;
    config.seed = 1U;
    config.pattern = pattern;
    const auto generated = aoa::sim::map::generate_map(config);
    if (generated.start_anchors.empty()) {
        return "Generator placed no starts. Check start pieces and map size.";
    }

    return {};
}

[[nodiscard]] bool has_raw_pattern_extension(const std::filesystem::path& path)
{
    return path.extension() == constants::PATTERN_FILE_EXTENSION
        && path.stem().extension() == ".raw";
}

void ensure_raw_pattern_extension(std::filesystem::path& path)
{
    if (has_raw_pattern_extension(path)) {
        return;
    }

    if (path.extension() == constants::PATTERN_FILE_EXTENSION) {
        path = path.parent_path()
            / (path.stem().string() + std::string(constants::PATTERN_RAW_FILE_EXTENSION));
        return;
    }

    path += constants::PATTERN_RAW_FILE_EXTENSION;
}

void ensure_runtime_pattern_extension(std::filesystem::path& path)
{
    if (has_raw_pattern_extension(path)) {
        path = path.parent_path()
            / (path.stem().stem().string() + std::string(constants::PATTERN_FILE_EXTENSION));
        return;
    }

    if (path.extension() != constants::PATTERN_FILE_EXTENSION) {
        path += constants::PATTERN_FILE_EXTENSION;
    }
}

void apply_new_pattern(PatternMaker& maker)
{
    MapPattern pattern = make_blank_pattern();
    const bool any_size =
        maker.create_dialog.size_preset_index == constants::PATTERN_MAP_SIZE_ANY_OPTION_INDEX;
    const int size = any_size
        ? constants::PATTERN_MAP_SIZE_PRESETS[static_cast<std::size_t>(
              constants::PATTERN_DEFAULT_MAP_SIZE_PRESET_INDEX)]
        : constants::PATTERN_MAP_SIZE_PRESETS[static_cast<std::size_t>(
              std::clamp(
                  maker.create_dialog.size_preset_index,
                  0,
                  static_cast<int>(constants::PATTERN_MAP_SIZE_PRESETS.size()) - 1))];
    pattern.map_width = size;
    pattern.map_height = size;
    pattern.fixed_map_size = !any_size;
    if (maker.create_dialog.limit_players) {
        const std::uint8_t count = static_cast<std::uint8_t>(std::clamp(
            maker.create_dialog.player_limit,
            constants::PATTERN_FIXED_PLAYERS_MIN,
            constants::PATTERN_FIXED_PLAYERS_MAX));
        pattern.fixed_player_count = count;
        pattern.min_players = count;
        pattern.max_players = count;
    }
    else {
        pattern.fixed_player_count = 0U;
        pattern.min_players = static_cast<std::uint8_t>(constants::PATTERN_UNLIMITED_MIN_PLAYERS);
        pattern.max_players = static_cast<std::uint8_t>(constants::PATTERN_DEFAULT_MAX_PLAYERS);
    }

    maker.pattern = pattern;
    maker.file_path.clear();
    maker.undo_stack.clear();
    maker.redo_stack.clear();
    maker.selected_id.reset();
    maker.link_from_id.reset();
    maker.preview_mode = false;
    maker.create_dialog.open = false;
    maker.startup_open = false;
    maker.session_started = true;
    maker.status = any_size
        ? ("New pattern Any (" + std::to_string(size) + "x" + std::to_string(size) + ")")
        : ("New pattern " + std::to_string(size) + "x" + std::to_string(size));
}

void open_create_dialog(PatternMaker& maker)
{
    maker.create_dialog.open = true;
    maker.create_dialog.limit_players = maker.pattern.fixed_player_count > 0U;
    maker.create_dialog.player_limit = maker.pattern.fixed_player_count > 0U
        ? static_cast<int>(maker.pattern.fixed_player_count)
        : constants::PATTERN_FIXED_PLAYERS_MIN;
    maker.create_dialog.size_preset_index = maker.pattern.fixed_map_size
        ? constants::PATTERN_DEFAULT_MAP_SIZE_PRESET_INDEX
        : constants::PATTERN_MAP_SIZE_ANY_OPTION_INDEX;
    if (maker.pattern.fixed_map_size) {
        for (std::size_t index = 0; index < constants::PATTERN_MAP_SIZE_PRESETS.size(); ++index) {
            if (constants::PATTERN_MAP_SIZE_PRESETS[index] == maker.pattern.map_width) {
                maker.create_dialog.size_preset_index = static_cast<int>(index);
                break;
            }
        }
    }
}

void open_pattern(PatternMaker& maker, sf::Window& window)
{
    const std::optional<std::filesystem::path> selected = aoa::app::pick_existing_file(
        window,
        aoa::core::default_patterns_directory(),
        std::string(constants::PATTERN_RAW_FILE_FILTER_DESCRIPTION),
        std::string(constants::PATTERN_RAW_FILE_FILTER_PATTERN));
    if (!selected.has_value()) {
        return;
    }

    const std::optional<MapPattern> loaded = aoa::sim::map::load_raw_map_pattern_file(*selected);
    if (!loaded.has_value()) {
        maker.status = std::string(constants::PATTERN_WRONG_KIND_OPEN_LABEL);
        return;
    }

    maker.pattern = *loaded;
    maker.file_path = *selected;
    maker.undo_stack.clear();
    maker.redo_stack.clear();
    maker.selected_id.reset();
    maker.startup_open = false;
    maker.session_started = true;
    maker.status = "Opened " + selected->filename().string();
}

void save_pattern(PatternMaker& maker, sf::Window& window, const bool export_copy)
{
    const std::string error = validate_pattern(maker.pattern);
    if (export_copy && !error.empty()) {
        maker.status = error;
        return;
    }

    std::filesystem::path target = export_copy ? std::filesystem::path{} : maker.file_path;
    if (target.empty()) {
        const std::string extension = export_copy
            ? std::string(constants::PATTERN_FILE_EXTENSION)
            : std::string(constants::PATTERN_RAW_FILE_EXTENSION);
        const std::optional<std::filesystem::path> selected = aoa::app::pick_save_file(
            window,
            aoa::core::default_patterns_directory(),
            std::string(
                export_copy
                    ? constants::PATTERN_FILE_FILTER_DESCRIPTION
                    : constants::PATTERN_RAW_FILE_FILTER_DESCRIPTION),
            std::string(
                export_copy
                    ? constants::PATTERN_FILE_FILTER_PATTERN
                    : constants::PATTERN_RAW_FILE_FILTER_PATTERN),
            maker.pattern.name + extension);
        if (!selected.has_value()) {
            return;
        }

        target = *selected;
        if (export_copy) {
            ensure_runtime_pattern_extension(target);
        }
        else {
            ensure_raw_pattern_extension(target);
        }
    }

    if (export_copy) {
        maker.pattern.name = target.stem().string();
        if (!aoa::sim::map::save_map_pattern_file(target, maker.pattern)) {
            maker.status = "Could not export pattern.";
            return;
        }
        maker.status = "Exported " + target.filename().string();
        return;
    }

    maker.pattern.name = has_raw_pattern_extension(target)
        ? target.stem().stem().string()
        : target.stem().string();
    if (!aoa::sim::map::save_raw_map_pattern_file(target, maker.pattern)) {
        maker.status = "Could not save pattern.";
        return;
    }

    maker.file_path = target;
    maker.status = "Saved " + target.filename().string();
}

void place_piece(PatternMaker& maker, const sf::Vector2i cell)
{
    if (!maker.place_kind.has_value()) {
        return;
    }

    if (*maker.place_kind == MapPieceKind::StartPosition
        && !aoa::sim::map::pattern_can_place_start_position(maker.pattern)) {
        maker.status = "Start position limit reached";
        maker.place_kind.reset();
        return;
    }
    if (*maker.place_kind == MapPieceKind::MultiStartPosition
        && !aoa::sim::map::pattern_can_place_multi_start(maker.pattern)) {
        maker.status = "Multi start is already used or start positions exist";
        maker.place_kind.reset();
        return;
    }

    push_undo(maker);
    MapPiece piece{};
    piece.id = aoa::sim::map::next_map_piece_id(maker.pattern);
    piece.kind = *maker.place_kind;
    const sf::Vector2i placed =
        piece.kind == MapPieceKind::StartPosition ? snap_start_cell(maker, cell) : cell;
    piece.x = snap_coord(placed.x);
    piece.y = snap_coord(placed.y);
    piece.width = constants::PATTERN_MAKER_DEFAULT_PIECE_SIZE;
    piece.height = constants::PATTERN_MAKER_DEFAULT_PIECE_SIZE;
    if (piece.kind == MapPieceKind::StartPosition && maker.pattern.start_ring_mode) {
        piece.width = 1;
        piece.height = 1;
    }
    aoa::sim::map::clamp_piece_to_map(piece, maker.pattern.map_width, maker.pattern.map_height);
    maker.pattern.pieces.push_back(piece);
    maker.selected_id = piece.id;
    maker.status = std::string("Placed ") + piece_kind_label(piece.kind);
}

void delete_selected(PatternMaker& maker)
{
    if (!maker.selected_id.has_value()) {
        return;
    }

    push_undo(maker);
    const std::uint32_t id = *maker.selected_id;
    maker.pattern.pieces.erase(
        std::remove_if(
            maker.pattern.pieces.begin(),
            maker.pattern.pieces.end(),
            [id](const MapPiece& piece) { return piece.id == id; }),
        maker.pattern.pieces.end());
    maker.pattern.links.erase(
        std::remove_if(
            maker.pattern.links.begin(),
            maker.pattern.links.end(),
            [id](const MapPieceLink& link) { return link.from_id == id || link.to_id == id; }),
        maker.pattern.links.end());
    maker.selected_id.reset();
    maker.status = "Deleted piece";
}

void handle_grid_press(PatternMaker& maker, const sf::FloatRect& grid, const sf::Vector2f mouse)
{
    const std::optional<std::uint32_t> screen_hit = hit_piece(maker, grid, mouse);
    sf::Vector2i cell = cell_at(maker, grid, mouse);
    const bool on_map = cell.x >= 0 && cell.y >= 0 && cell.x < maker.pattern.map_width
        && cell.y < maker.pattern.map_height;
    if (!on_map && !screen_hit.has_value()) {
        return;
    }

    if (maker.place_kind.has_value() && maker.link_tool == LinkTool::None) {
        if (!on_map) {
            return;
        }
        if (*maker.place_kind == MapPieceKind::StartPosition) {
            cell = snap_start_cell(maker, cell);
        }
        place_piece(maker, cell);
        maker.place_kind.reset();
        return;
    }

    const std::optional<std::uint32_t> hit = screen_hit;
    if (maker.link_tool != LinkTool::None) {
        if (!hit.has_value()) {
            return;
        }

        if (!maker.link_from_id.has_value() || *maker.link_from_id == *hit) {
            maker.link_from_id = hit;
            maker.selected_id = hit;
            maker.status = "Select a piece to link to";
            return;
        }

        push_undo(maker);
        maker.pattern.links.push_back(MapPieceLink{
            *maker.link_from_id,
            *hit,
            maker.link_tool == LinkTool::Road ? MapLinkKind::Road : MapLinkKind::Relative,
        });
        maker.link_from_id.reset();
        maker.status = maker.link_tool == LinkTool::Road ? "Road linked" : "Relative linked";
        return;
    }

    maker.selected_id = hit;
    if (!hit.has_value()) {
        return;
    }

    if (MapPiece* piece = piece_by_id(maker.pattern, *hit)) {
        maker.dragging = true;
        maker.drag_offset = {cell.x - piece->x, cell.y - piece->y};
    }
}

void handle_drag(PatternMaker& maker, const sf::FloatRect& grid, const sf::Vector2f mouse)
{
    if (!maker.dragging || !maker.selected_id.has_value()) {
        return;
    }

    MapPiece* piece = piece_by_id(maker.pattern, *maker.selected_id);
    if (piece == nullptr) {
        return;
    }

    sf::Vector2i cell = cell_at(maker, grid, mouse);
    if (piece->kind == MapPieceKind::StartPosition && maker.pattern.start_ring_mode) {
        cell = snap_start_cell(maker, cell);
        piece->x = cell.x;
        piece->y = cell.y;
        piece->width = 1;
        piece->height = 1;
        aoa::sim::map::clamp_piece_to_map(*piece, maker.pattern.map_width, maker.pattern.map_height);
        return;
    }

    piece->x = snap_coord(cell.x - maker.drag_offset.x);
    piece->y = snap_coord(cell.y - maker.drag_offset.y);
    aoa::sim::map::clamp_piece_to_map(*piece, maker.pattern.map_width, maker.pattern.map_height);
}

void cycle_map_size(PatternMaker& maker, const int direction)
{
    push_undo(maker);
    int next = maker.pattern.map_width;
    for (std::size_t index = 0; index < constants::PATTERN_MAP_SIZE_PRESETS.size(); ++index) {
        if (constants::PATTERN_MAP_SIZE_PRESETS[index] != maker.pattern.map_width) {
            continue;
        }

        const int shifted = static_cast<int>(index) + direction;
        const int wrapped = (shifted + static_cast<int>(constants::PATTERN_MAP_SIZE_PRESETS.size()))
            % static_cast<int>(constants::PATTERN_MAP_SIZE_PRESETS.size());
        next = constants::PATTERN_MAP_SIZE_PRESETS[static_cast<std::size_t>(wrapped)];
        break;
    }

    maker.pattern.map_width = next;
    maker.pattern.map_height = next;
    maker.status = "Map size " + std::to_string(next);
}

void draw_text(
    sf::RenderWindow& window,
    const PatternMaker& maker,
    const std::string& text,
    const sf::Vector2f position,
    const sf::Color color = sf::Color::White)
{
    if (!maker.font_ready) {
        return;
    }

    sf::Text label(maker.font, text, static_cast<unsigned int>(constants::PATTERN_MAKER_FONT_SIZE));
    label.setPosition(position);
    label.setFillColor(color);
    window.draw(label);
}

[[nodiscard]] std::vector<MakerButton> make_buttons(const PatternMaker& maker, const sf::Vector2u window_size)
{
    std::vector<MakerButton> buttons{};
    const float panel_x = static_cast<float>(
        window_size.x - constants::PATTERN_MAKER_PANEL_WIDTH + constants::PATTERN_MAKER_GRID_PAD);
    float y = static_cast<float>(constants::PATTERN_MAKER_GRID_PAD);
    const float width = static_cast<float>(
        constants::PATTERN_MAKER_PANEL_WIDTH - constants::PATTERN_MAKER_GRID_PAD * 2);
    const float height = static_cast<float>(constants::PATTERN_MAKER_BUTTON_HEIGHT);
    const float gap = static_cast<float>(constants::PATTERN_MAKER_BUTTON_GAP);

    const std::array<std::pair<const char*, const char*>, 14> commands{{
        {"New", "new"},
        {"Open", "open"},
        {"Save", "save"},
        {"Export", "export"},
        {"Undo", "undo"},
        {"Redo", "redo"},
        {"Linker", "linker"},
        {"Road linker", "road_linker"},
        {"Preview", "preview"},
        {"Validate", "validate"},
        {"Fixed size", "fixed_size"},
        {"Ring mode", "ring_mode"},
        {"Border trees", "border_trees"},
        {"Stray trees", "stray_trees"},
    }};
    for (const auto& [label, command] : commands) {
        buttons.push_back(MakerButton{sf::FloatRect{{panel_x, y}, {width, height}}, label, command});
        y += height + gap;
    }

    y += gap;
    const std::array<MapPieceKind, 8> kinds{{
        MapPieceKind::StartPosition,
        MapPieceKind::MultiStartPosition,
        MapPieceKind::Forest,
        MapPieceKind::Gold,
        MapPieceKind::Berries,
        MapPieceKind::Terrain,
        MapPieceKind::ManaLake,
        MapPieceKind::MiddlePoint,
    }};
    for (const MapPieceKind kind : kinds) {
        bool enabled = true;
        if (kind == MapPieceKind::StartPosition) {
            enabled = aoa::sim::map::pattern_can_place_start_position(maker.pattern);
        }
        else if (kind == MapPieceKind::MultiStartPosition) {
            enabled = aoa::sim::map::pattern_can_place_multi_start(maker.pattern);
        }
        buttons.push_back(MakerButton{
            sf::FloatRect{{panel_x, y}, {width, height}},
            piece_kind_label(kind),
            std::string("place:") + std::to_string(static_cast<int>(kind)),
            enabled,
        });
        y += height + gap;
    }

    y += gap;
    buttons.push_back(MakerButton{
        sf::FloatRect{{panel_x, y}, {width, height}},
        maker.pattern.random_gold
            ? ("Random gold: " + std::to_string(maker.pattern.random_gold_count))
            : "Random gold: off",
        "toggle_random_gold",
        true,
    });
    y += height + gap;
    buttons.push_back(MakerButton{
        sf::FloatRect{{panel_x, y}, {width, height}},
        maker.pattern.random_berries
            ? ("Random berries: " + std::to_string(maker.pattern.random_berry_count))
            : "Random berries: off",
        "toggle_random_berries",
        true,
    });
    y += height + gap;
    if (maker.pattern.random_stray_trees) {
        buttons.push_back(MakerButton{
            sf::FloatRect{{panel_x, y}, {width, height}},
            "Stray count " + std::to_string(maker.pattern.random_stray_tree_count),
            "stray_count_label",
            false,
        });
        y += height + gap;
        buttons.push_back(MakerButton{
            sf::FloatRect{{panel_x, y}, {width * 0.5F - 4.0F, height}},
            "-",
            "stray_count:-",
            true,
        });
        buttons.push_back(MakerButton{
            sf::FloatRect{{panel_x + width * 0.5F + 4.0F, y}, {width * 0.5F - 4.0F, height}},
            "+",
            "stray_count:+",
            true,
        });
        y += height + gap;
    }
    if (maker.pattern.random_gold || maker.pattern.random_berries) {
        buttons.push_back(MakerButton{
            sf::FloatRect{{panel_x, y}, {width * 0.5F - 4.0F, height}},
            "Count -",
            "random_count:-",
            true,
        });
        buttons.push_back(MakerButton{
            sf::FloatRect{{panel_x + width * 0.5F + 4.0F, y}, {width * 0.5F - 4.0F, height}},
            "Count +",
            "random_count:+",
            true,
        });
        y += height + gap;
    }

    if (maker.selected_id.has_value()) {
        if (const MapPiece* selected = piece_by_id(maker.pattern, *maker.selected_id)) {
            if (selected->kind == MapPieceKind::Forest
                || selected->kind == MapPieceKind::Gold
                || selected->kind == MapPieceKind::Berries) {
                std::string label = "Value ";
                if (selected->kind == MapPieceKind::Forest) {
                    label = "Range " + std::to_string(selected->range);
                }
                else if (selected->kind == MapPieceKind::Gold) {
                    label = "Mines " + std::to_string(selected->gold_count);
                }
                else {
                    label = "Bushes " + std::to_string(selected->berry_count);
                }
                buttons.push_back(MakerButton{
                    sf::FloatRect{{panel_x, y}, {width, height}},
                    label,
                    "prop_label",
                    false,
                });
                y += height + gap;
                buttons.push_back(MakerButton{
                    sf::FloatRect{{panel_x, y}, {width * 0.5F - 4.0F, height}},
                    "-",
                    "prop:-",
                    true,
                });
                buttons.push_back(MakerButton{
                    sf::FloatRect{{panel_x + width * 0.5F + 4.0F, y}, {width * 0.5F - 4.0F, height}},
                    "+",
                    "prop:+",
                    true,
                });
            }
        }
    }

    return buttons;
}

void run_command(PatternMaker& maker, sf::Window& window, const std::string& command)
{
    if (command == "new") {
        open_create_dialog(maker);
        return;
    }
    if (command == "open") {
        open_pattern(maker, window);
        return;
    }
    if (command == "save") {
        save_pattern(maker, window, false);
        return;
    }
    if (command == "export") {
        save_pattern(maker, window, true);
        return;
    }
    if (command == "undo") {
        undo(maker);
        return;
    }
    if (command == "redo") {
        redo(maker);
        return;
    }
    if (command == "linker") {
        maker.link_tool = maker.link_tool == LinkTool::Relative ? LinkTool::None : LinkTool::Relative;
        maker.place_kind.reset();
        maker.link_from_id.reset();
        maker.status = maker.link_tool == LinkTool::Relative ? "Linker on" : "Linker off";
        return;
    }
    if (command == "road_linker") {
        maker.link_tool = maker.link_tool == LinkTool::Road ? LinkTool::None : LinkTool::Road;
        maker.place_kind.reset();
        maker.link_from_id.reset();
        maker.status = maker.link_tool == LinkTool::Road ? "Road linker on" : "Road linker off";
        return;
    }
    if (command == "toggle_random_gold") {
        push_undo(maker);
        maker.pattern.random_gold = !maker.pattern.random_gold;
        maker.status = maker.pattern.random_gold ? "Random gold on" : "Random gold off";
        return;
    }
    if (command == "toggle_random_berries") {
        push_undo(maker);
        maker.pattern.random_berries = !maker.pattern.random_berries;
        maker.status = maker.pattern.random_berries ? "Random berries on" : "Random berries off";
        return;
    }
    if (command == "stray_count:-" || command == "stray_count:+") {
        push_undo(maker);
        const int delta = command.back() == '+' ? 1 : -1;
        maker.pattern.random_stray_tree_count = std::clamp(
            maker.pattern.random_stray_tree_count + delta,
            constants::PATTERN_RESOURCE_MIN_COUNT,
            constants::PATTERN_RANDOM_STRAY_TREE_MAX);
        return;
    }
    if (command == "random_count:-" || command == "random_count:+") {
        push_undo(maker);
        const int delta = command.back() == '+' ? 1 : -1;
        if (maker.pattern.random_gold) {
            maker.pattern.random_gold_count = std::clamp(
                maker.pattern.random_gold_count + delta,
                constants::PATTERN_RESOURCE_MIN_COUNT,
                constants::PATTERN_RESOURCE_MAX_COUNT);
        }
        if (maker.pattern.random_berries) {
            maker.pattern.random_berry_count = std::clamp(
                maker.pattern.random_berry_count + delta,
                constants::PATTERN_RESOURCE_MIN_COUNT,
                constants::PATTERN_RESOURCE_MAX_COUNT);
        }
        return;
    }
    if (command == "prop:-" || command == "prop:+") {
        if (!maker.selected_id.has_value()) {
            return;
        }
        MapPiece* selected = piece_by_id(maker.pattern, *maker.selected_id);
        if (selected == nullptr) {
            return;
        }
        push_undo(maker);
        const int delta = command.back() == '+' ? 1 : -1;
        if (selected->kind == MapPieceKind::Forest) {
            selected->range = std::clamp(
                selected->range + delta,
                constants::PATTERN_FOREST_MIN_RANGE,
                constants::PATTERN_FOREST_MAX_RANGE);
        }
        else if (selected->kind == MapPieceKind::Gold) {
            selected->gold_count = std::clamp(
                selected->gold_count + delta,
                constants::PATTERN_RESOURCE_MIN_COUNT,
                constants::PATTERN_RESOURCE_MAX_COUNT);
        }
        else if (selected->kind == MapPieceKind::Berries) {
            selected->berry_count = std::clamp(
                selected->berry_count + delta,
                constants::PATTERN_RESOURCE_MIN_COUNT,
                constants::PATTERN_RESOURCE_MAX_COUNT);
        }
        return;
    }
    if (command == "preview") {
        if (!maker.preview_mode) {
            const std::string error = validate_pattern(maker.pattern);
            if (!error.empty()) {
                maker.status = error;
                return;
            }
        }
        maker.preview_mode = !maker.preview_mode;
        maker.preview_seed = aoa::sim::map::generate_map_seed();
        maker.status = maker.preview_mode ? "Preview on" : "Preview off";
        return;
    }
    if (command == "validate") {
        const std::string error = validate_pattern(maker.pattern);
        maker.status = error.empty() ? "Pattern is valid" : error;
        return;
    }
    if (command == "fixed_size") {
        push_undo(maker);
        maker.pattern.fixed_map_size = !maker.pattern.fixed_map_size;
        maker.status = maker.pattern.fixed_map_size ? "Map size locked" : "Map size unlocked";
        return;
    }
    if (command == "ring_mode") {
        push_undo(maker);
        maker.pattern.start_ring_mode = !maker.pattern.start_ring_mode;
        if (maker.pattern.start_ring_mode) {
            for (MapPiece& piece : maker.pattern.pieces) {
                if (piece.kind != MapPieceKind::StartPosition) {
                    continue;
                }
                const aoa::core::GridPos snapped = aoa::sim::map::snap_to_start_ring(
                    {piece.x, piece.y},
                    maker.pattern.map_width,
                    maker.pattern.map_height);
                piece.x = snapped.x;
                piece.y = snapped.y;
                piece.width = 1;
                piece.height = 1;
            }
        }
        maker.status = maker.pattern.start_ring_mode ? "Ring mode on" : "Ring mode off";
        return;
    }
    if (command == "border_trees") {
        push_undo(maker);
        maker.pattern.border_trees = !maker.pattern.border_trees;
        maker.status = maker.pattern.border_trees ? "Border trees on" : "Border trees off";
        return;
    }
    if (command == "stray_trees") {
        push_undo(maker);
        maker.pattern.random_stray_trees = !maker.pattern.random_stray_trees;
        maker.status = maker.pattern.random_stray_trees ? "Stray trees on" : "Stray trees off";
        return;
    }
    if (command.rfind("place:", 0) == 0) {
        const auto kind = static_cast<MapPieceKind>(std::stoi(command.substr(6)));
        if (kind == MapPieceKind::StartPosition
            && !aoa::sim::map::pattern_can_place_start_position(maker.pattern)) {
            maker.status = "Start position limit reached";
            return;
        }
        if (kind == MapPieceKind::MultiStartPosition
            && !aoa::sim::map::pattern_can_place_multi_start(maker.pattern)) {
            maker.status = "Cannot mix start types or place another multi start";
            return;
        }
        maker.link_tool = LinkTool::None;
        maker.place_kind = kind;
        maker.status = std::string("Place ") + piece_kind_label(*maker.place_kind);
    }
}

[[nodiscard]] std::uint8_t preview_player_count(const MapPattern& pattern)
{
    if (pattern.fixed_player_count > 0U) {
        return pattern.fixed_player_count;
    }

    return static_cast<std::uint8_t>(std::max(
        static_cast<int>(pattern.min_players),
        constants::PATTERN_DEFAULT_MIN_PLAYERS));
}

void draw_iso_cell(
    sf::RenderWindow& window,
    const MapView& view,
    const float x,
    const float y,
    const sf::Color color)
{
    sf::ConvexShape cell;
    cell.setPointCount(4);
    cell.setPoint(0, tile_to_screen(view, x, y));
    cell.setPoint(1, tile_to_screen(view, x + 1.0F, y));
    cell.setPoint(2, tile_to_screen(view, x + 1.0F, y + 1.0F));
    cell.setPoint(3, tile_to_screen(view, x, y + 1.0F));
    cell.setFillColor(color);
    window.draw(cell);
}

void draw_grid_circle(
    sf::RenderWindow& window,
    const MapView& view,
    const float center_x,
    const float center_y,
    const float radius,
    const sf::Color color)
{
    sf::VertexArray ring{sf::PrimitiveType::LineStrip, constants::PATTERN_CIRCLE_SEGMENTS + 1U};
    for (int index = 0; index <= constants::PATTERN_CIRCLE_SEGMENTS; ++index) {
        const float angle = constants::PATTERN_TAU * static_cast<float>(index)
            / static_cast<float>(constants::PATTERN_CIRCLE_SEGMENTS);
        ring[static_cast<std::size_t>(index)] = sf::Vertex{
            tile_to_screen(
                view,
                center_x + std::cos(angle) * radius,
                center_y + std::sin(angle) * radius),
            color,
        };
    }
    window.draw(ring);
}

void draw_start_ranges(
    sf::RenderWindow& window,
    const MapView& view,
    const std::vector<aoa::core::GridPos>& starts,
    const int map_width,
    const int map_height)
{
    const float ring_r = aoa::sim::map::start_ring_radius(map_width, map_height);
    draw_grid_circle(
        window,
        view,
        static_cast<float>(map_width - 1) * 0.5F,
        static_cast<float>(map_height - 1) * 0.5F,
        ring_r,
        sf::Color{255, 220, 80, 180});
    for (const aoa::core::GridPos& start : starts) {
        draw_grid_circle(
            window,
            view,
            static_cast<float>(start.x),
            static_cast<float>(start.y),
            static_cast<float>(constants::PATTERN_START_PLAZA_RADIUS),
            sf::Color{80, 220, 90, 160});
        draw_grid_circle(
            window,
            view,
            static_cast<float>(start.x),
            static_cast<float>(start.y),
            static_cast<float>(constants::PATTERN_START_ECONOMY_RADIUS),
            sf::Color{220, 180, 40, 140});
    }
}

void draw_border_overlay(
    sf::RenderWindow& window,
    const MapView& view,
    const int map_width,
    const int map_height)
{
    const int depth = constants::PATTERN_BORDER_TREE_DEPTH;
    for (int y = 0; y < map_height; ++y) {
        for (int x = 0; x < map_width; ++x) {
            const int edge = std::min(std::min(x, y), std::min(map_width - 1 - x, map_height - 1 - y));
            if (edge >= depth) {
                continue;
            }
            draw_iso_cell(window, view, static_cast<float>(x), static_cast<float>(y), sf::Color{18, 70, 28, 120});
        }
    }
}

void draw_preview(
    sf::RenderWindow& window,
    const PatternMaker& maker,
    const sf::FloatRect& grid)
{
    MapGenerationConfig config{};
    config.player_count = preview_player_count(maker.pattern);
    config.seed = maker.preview_seed;
    config.pattern = maker.pattern;
    const auto generated = aoa::sim::map::generate_map(config);
    const MapView view = make_map_view(maker, grid);
    for (int y = 0; y < generated.grid.height; ++y) {
        for (int x = 0; x < generated.grid.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * generated.grid.width + x);
            const auto ground = generated.grid.ground[index];
            const auto tile = generated.grid.tiles[index];
            sf::Color color{70, 110, 50};
            if (ground == aoa::sim::components::GroundType::Snow) {
                color = sf::Color{200, 210, 220};
            }
            else if (ground == aoa::sim::components::GroundType::Sand) {
                color = sf::Color{194, 178, 128};
            }

            if (tile == aoa::sim::components::TileType::Forest) {
                color = sf::Color{20, 80, 30};
            }
            else if (tile == aoa::sim::components::TileType::GoldMine) {
                color = sf::Color{220, 180, 40};
            }
            else if (tile == aoa::sim::components::TileType::Berries
                || tile == aoa::sim::components::TileType::Blueberries) {
                color = sf::Color{160, 40, 70};
            }

            draw_iso_cell(window, view, static_cast<float>(x), static_cast<float>(y), color);
        }
    }

    draw_start_ranges(
        window,
        view,
        generated.start_anchors,
        generated.grid.width,
        generated.grid.height);

    for (std::size_t index = 0; index < generated.start_anchors.size(); ++index) {
        const aoa::core::GridPos start = generated.start_anchors[index];
        const sf::Vector2f pos = tile_to_screen(
            view,
            static_cast<float>(start.x) + 0.5F,
            static_cast<float>(start.y) + 0.5F);
        draw_iso_cell(
            window,
            view,
            static_cast<float>(start.x),
            static_cast<float>(start.y),
            sf::Color{255, 230, 80, 220});
        draw_text(window, maker, "P" + std::to_string(index + 1U), pos, sf::Color::Black);
    }

    for (const aoa::core::GridPos& lake : generated.mana_lake_anchors) {
        draw_iso_cell(
            window,
            view,
            static_cast<float>(lake.x),
            static_cast<float>(lake.y),
            sf::Color{50, 110, 210, 200});
    }

    draw_text(
        window,
        maker,
        std::string(constants::PATTERN_MAKER_PREVIEW_LEGEND)
            + "   yellow ring=starts   green=plaza 8   gold=economy 14",
        {grid.position.x, grid.position.y + grid.size.y - 18.0F},
        sf::Color{210, 210, 180});
}

void draw_editor(
    sf::RenderWindow& window,
    const PatternMaker& maker,
    const sf::FloatRect& grid)
{
    sf::RectangleShape background{grid.size};
    background.setPosition(grid.position);
    background.setFillColor(sf::Color{28, 32, 28});
    window.draw(background);

    if (maker.preview_mode) {
        draw_preview(window, maker, grid);
        return;
    }

    const MapView view = make_map_view(maker, grid);
    const float map_w = static_cast<float>(maker.pattern.map_width);
    const float map_h = static_cast<float>(maker.pattern.map_height);
    sf::ConvexShape diamond;
    diamond.setPointCount(4);
    diamond.setPoint(0, tile_to_screen(view, 0.0F, 0.0F));
    diamond.setPoint(1, tile_to_screen(view, map_w, 0.0F));
    diamond.setPoint(2, tile_to_screen(view, map_w, map_h));
    diamond.setPoint(3, tile_to_screen(view, 0.0F, map_h));
    diamond.setFillColor(sf::Color{36, 42, 36});
    diamond.setOutlineThickness(1.0F);
    diamond.setOutlineColor(sf::Color{80, 90, 70});
    window.draw(diamond);

    if (maker.pattern.border_trees) {
        draw_border_overlay(window, view, maker.pattern.map_width, maker.pattern.map_height);
    }

    std::vector<aoa::core::GridPos> editor_starts{};
    if (aoa::sim::map::pattern_has_multi_start(maker.pattern) || maker.pattern.start_ring_mode) {
        editor_starts = aoa::sim::map::start_ring_positions(
            preview_player_count(maker.pattern),
            maker.pattern.map_width,
            maker.pattern.map_height);
    }
    else {
        for (const MapPiece& piece : maker.pattern.pieces) {
            if (piece.kind == MapPieceKind::StartPosition) {
                editor_starts.push_back({piece.x + piece.width / 2, piece.y + piece.height / 2});
            }
        }
    }
    draw_start_ranges(
        window,
        view,
        editor_starts,
        maker.pattern.map_width,
        maker.pattern.map_height);

    for (const MapPiece& piece : maker.pattern.pieces) {
        sf::ConvexShape rect;
        rect.setPointCount(4);
        const float x0 = static_cast<float>(piece.x);
        const float y0 = static_cast<float>(piece.y);
        const float x1 = static_cast<float>(piece.x + piece.width);
        const float y1 = static_cast<float>(piece.y + piece.height);
        rect.setPoint(0, tile_to_screen(view, x0, y0));
        rect.setPoint(1, tile_to_screen(view, x1, y0));
        rect.setPoint(2, tile_to_screen(view, x1, y1));
        rect.setPoint(3, tile_to_screen(view, x0, y1));
        sf::Color color = piece_color(piece.kind);
        color.a = 180;
        rect.setFillColor(color);
        if (maker.selected_id.has_value() && *maker.selected_id == piece.id) {
            rect.setOutlineThickness(2.0F);
            rect.setOutlineColor(sf::Color::White);
        }
        window.draw(rect);
    }

    for (const MapPieceLink& link : maker.pattern.links) {
        const MapPiece* from = piece_by_id(maker.pattern, link.from_id);
        const MapPiece* to = piece_by_id(maker.pattern, link.to_id);
        if (from == nullptr || to == nullptr) {
            continue;
        }

        const sf::Color color = link.kind == MapLinkKind::Road ? sf::Color::Yellow : sf::Color{80, 200, 220};
        const sf::Vertex line[]{
            sf::Vertex{
                tile_to_screen(
                    view,
                    static_cast<float>(from->x) + static_cast<float>(from->width) * 0.5F,
                    static_cast<float>(from->y) + static_cast<float>(from->height) * 0.5F),
                color,
            },
            sf::Vertex{
                tile_to_screen(
                    view,
                    static_cast<float>(to->x) + static_cast<float>(to->width) * 0.5F,
                    static_cast<float>(to->y) + static_cast<float>(to->height) * 0.5F),
                color,
            },
        };
        window.draw(line, 2, sf::PrimitiveType::Lines);
    }
}

[[nodiscard]] sf::FloatRect create_dialog_rect(const sf::Vector2u window_size)
{
    const float width = static_cast<float>(constants::PATTERN_MAKER_DIALOG_WIDTH);
    const float height = static_cast<float>(constants::PATTERN_MAKER_DIALOG_HEIGHT);
    return sf::FloatRect{
        {(static_cast<float>(window_size.x) - width) * 0.5F,
            (static_cast<float>(window_size.y) - height) * 0.5F},
        {width, height},
    };
}

struct CreateDialogHit {
    sf::FloatRect limit_toggle{};
    sf::FloatRect player_minus{};
    sf::FloatRect player_plus{};
    std::array<sf::FloatRect, constants::PATTERN_MAP_SIZE_OPTION_COUNT> sizes{};
    sf::FloatRect create{};
    sf::FloatRect cancel{};
};

[[nodiscard]] CreateDialogHit create_dialog_hits(const sf::FloatRect& dialog)
{
    CreateDialogHit hits{};
    const float pad = static_cast<float>(constants::PATTERN_MAKER_DIALOG_PAD);
    const float button_h = static_cast<float>(constants::PATTERN_MAKER_BUTTON_HEIGHT);
    const float x = dialog.position.x + pad;
    float y = dialog.position.y + pad + 36.0F;
    hits.limit_toggle = sf::FloatRect{{x, y}, {160.0F, button_h}};
    hits.player_minus = sf::FloatRect{{x + 170.0F, y}, {32.0F, button_h}};
    hits.player_plus = sf::FloatRect{{x + 250.0F, y}, {32.0F, button_h}};
    y += button_h + 28.0F;
    const float size_w = static_cast<float>(constants::PATTERN_MAKER_SIZE_BUTTON_WIDTH);
    const float size_gap = static_cast<float>(constants::PATTERN_MAKER_SIZE_BUTTON_GAP);
    for (std::size_t index = 0; index < hits.sizes.size(); ++index) {
        hits.sizes[index] = sf::FloatRect{
            {x + static_cast<float>(index) * (size_w + size_gap), y},
            {size_w, button_h},
        };
    }
    y = dialog.position.y + dialog.size.y - pad - button_h;
    hits.create = sf::FloatRect{{x, y}, {120.0F, button_h}};
    hits.cancel = sf::FloatRect{{x + 136.0F, y}, {120.0F, button_h}};
    return hits;
}

void handle_create_dialog_click(PatternMaker& maker, const sf::Vector2u window_size, const sf::Vector2f mouse)
{
    const CreateDialogHit hits = create_dialog_hits(create_dialog_rect(window_size));
    if (hits.limit_toggle.contains(mouse)) {
        maker.create_dialog.limit_players = !maker.create_dialog.limit_players;
        return;
    }
    if (maker.create_dialog.limit_players && hits.player_minus.contains(mouse)) {
        maker.create_dialog.player_limit = std::max(
            constants::PATTERN_FIXED_PLAYERS_MIN,
            maker.create_dialog.player_limit - 1);
        return;
    }
    if (maker.create_dialog.limit_players && hits.player_plus.contains(mouse)) {
        maker.create_dialog.player_limit = std::min(
            constants::PATTERN_FIXED_PLAYERS_MAX,
            maker.create_dialog.player_limit + 1);
        return;
    }
    for (std::size_t index = 0; index < hits.sizes.size(); ++index) {
        if (!hits.sizes[index].contains(mouse)) {
            continue;
        }
        maker.create_dialog.size_preset_index = static_cast<int>(index);
        return;
    }
    if (hits.create.contains(mouse)) {
        apply_new_pattern(maker);
        return;
    }
    if (hits.cancel.contains(mouse)) {
        maker.create_dialog.open = false;
        if (!maker.session_started) {
            maker.startup_open = true;
        }
    }
}

void draw_create_dialog(
    sf::RenderWindow& window,
    const PatternMaker& maker,
    const sf::Vector2u window_size)
{
    const sf::FloatRect dialog = create_dialog_rect(window_size);
    const CreateDialogHit hits = create_dialog_hits(dialog);
    sf::RectangleShape scrim{sf::Vector2f{
        static_cast<float>(window_size.x),
        static_cast<float>(window_size.y)}};
    scrim.setFillColor(sf::Color{0, 0, 0, 140});
    window.draw(scrim);

    sf::RectangleShape panel{dialog.size};
    panel.setPosition(dialog.position);
    panel.setFillColor(sf::Color{36, 40, 36});
    panel.setOutlineThickness(1.0F);
    panel.setOutlineColor(sf::Color{90, 100, 80});
    window.draw(panel);

    const float pad = static_cast<float>(constants::PATTERN_MAKER_DIALOG_PAD);
    draw_text(
        window,
        maker,
        std::string(constants::PATTERN_MAKER_CREATE_TITLE),
        {dialog.position.x + pad, dialog.position.y + pad});

    const auto draw_button = [&](const sf::FloatRect& rect, const std::string& label, const bool active) {
        sf::RectangleShape box{rect.size};
        box.setPosition(rect.position);
        box.setFillColor(active ? sf::Color{70, 90, 60} : sf::Color{50, 54, 50});
        window.draw(box);
        draw_text(window, maker, label, {rect.position.x + 8.0F, rect.position.y + 4.0F});
    };

    draw_button(
        hits.limit_toggle,
        std::string(constants::PATTERN_MAKER_LIMIT_PLAYERS_LABEL) + ": "
            + (maker.create_dialog.limit_players
                ? std::string(constants::PATTERN_MAKER_LIMIT_YES_LABEL)
                : std::string(constants::PATTERN_MAKER_LIMIT_NO_LABEL)),
        maker.create_dialog.limit_players);
    if (maker.create_dialog.limit_players) {
        draw_button(hits.player_minus, "-", false);
        draw_text(
            window,
            maker,
            std::to_string(maker.create_dialog.player_limit),
            {hits.player_minus.position.x + 42.0F, hits.player_minus.position.y + 4.0F});
        draw_button(hits.player_plus, "+", false);
    }

    draw_text(
        window,
        maker,
        std::string(constants::PATTERN_MAKER_MAP_SIZE_LABEL),
        {dialog.position.x + pad, hits.sizes[0].position.y - 22.0F});
    for (std::size_t index = 0; index < hits.sizes.size(); ++index) {
        const std::string label = static_cast<int>(index) == constants::PATTERN_MAP_SIZE_ANY_OPTION_INDEX
            ? std::string(constants::PATTERN_MAKER_MAP_SIZE_ANY_LABEL)
            : (std::to_string(constants::PATTERN_MAP_SIZE_PRESETS[index]) + "x"
                + std::to_string(constants::PATTERN_MAP_SIZE_PRESETS[index]));
        draw_button(
            hits.sizes[index],
            label,
            maker.create_dialog.size_preset_index == static_cast<int>(index));
    }

    draw_button(hits.create, std::string(constants::PATTERN_MAKER_CREATE_BUTTON_LABEL), true);
    draw_button(hits.cancel, std::string(constants::PATTERN_MAKER_CANCEL_BUTTON_LABEL), false);
}

[[nodiscard]] sf::FloatRect centered_dialog_rect(
    const sf::Vector2u window_size,
    const float width,
    const float height)
{
    return sf::FloatRect{
        {(static_cast<float>(window_size.x) - width) * 0.5F,
            (static_cast<float>(window_size.y) - height) * 0.5F},
        {width, height},
    };
}

struct StartupDialogHit {
    sf::FloatRect new_file{};
    sf::FloatRect open_file{};
    sf::FloatRect close{};
};

[[nodiscard]] sf::FloatRect startup_dialog_rect(const sf::Vector2u window_size)
{
    return centered_dialog_rect(
        window_size,
        static_cast<float>(constants::PATTERN_MAKER_STARTUP_WIDTH),
        static_cast<float>(constants::PATTERN_MAKER_STARTUP_HEIGHT));
}

[[nodiscard]] StartupDialogHit startup_dialog_hits(const sf::FloatRect& dialog)
{
    StartupDialogHit hits{};
    const float pad = static_cast<float>(constants::PATTERN_MAKER_DIALOG_PAD);
    const float button_h = static_cast<float>(constants::PATTERN_MAKER_BUTTON_HEIGHT);
    const float button_w = static_cast<float>(constants::PATTERN_MAKER_STARTUP_BUTTON_WIDTH);
    const float x = dialog.position.x + (dialog.size.x - button_w) * 0.5F;
    float y = dialog.position.y + pad
        + static_cast<float>(constants::PATTERN_MAKER_DIALOG_TITLE_BODY_GAP);
    const float gap = static_cast<float>(constants::PATTERN_MAKER_BUTTON_GAP)
        + static_cast<float>(constants::PATTERN_MAKER_STARTUP_ROW_EXTRA_GAP);
    hits.new_file = sf::FloatRect{{x, y}, {button_w, button_h}};
    y += button_h + gap;
    hits.open_file = sf::FloatRect{{x, y}, {button_w, button_h}};
    y += button_h + gap;
    hits.close = sf::FloatRect{{x, y}, {button_w, button_h}};
    return hits;
}

struct ConfirmCloseHit {
    sf::FloatRect yes{};
    sf::FloatRect no{};
};

[[nodiscard]] sf::FloatRect confirm_close_rect(const sf::Vector2u window_size)
{
    return centered_dialog_rect(
        window_size,
        static_cast<float>(constants::PATTERN_MAKER_CONFIRM_WIDTH),
        static_cast<float>(constants::PATTERN_MAKER_CONFIRM_HEIGHT));
}

[[nodiscard]] ConfirmCloseHit confirm_close_hits(const sf::FloatRect& dialog)
{
    ConfirmCloseHit hits{};
    const float pad = static_cast<float>(constants::PATTERN_MAKER_DIALOG_PAD);
    const float button_h = static_cast<float>(constants::PATTERN_MAKER_BUTTON_HEIGHT);
    const float button_w = static_cast<float>(constants::PATTERN_MAKER_DIALOG_ACTION_WIDTH);
    const float y = dialog.position.y + dialog.size.y - pad - button_h;
    hits.yes = sf::FloatRect{{dialog.position.x + pad, y}, {button_w, button_h}};
    hits.no = sf::FloatRect{
        {dialog.position.x + pad + button_w
                + static_cast<float>(constants::PATTERN_MAKER_DIALOG_ACTION_GAP),
            y},
        {button_w, button_h}};
    return hits;
}

void request_close(PatternMaker& maker)
{
    maker.confirm_close_open = true;
}

void draw_dialog_panel(
    sf::RenderWindow& window,
    const PatternMaker& maker,
    const sf::Vector2u window_size,
    const sf::FloatRect& dialog,
    const std::string& title)
{
    sf::RectangleShape scrim{sf::Vector2f{
        static_cast<float>(window_size.x),
        static_cast<float>(window_size.y)}};
    scrim.setFillColor(sf::Color{0, 0, 0, 140});
    window.draw(scrim);

    sf::RectangleShape panel{dialog.size};
    panel.setPosition(dialog.position);
    panel.setFillColor(sf::Color{36, 40, 36});
    panel.setOutlineThickness(1.0F);
    panel.setOutlineColor(sf::Color{90, 100, 80});
    window.draw(panel);

    const float pad = static_cast<float>(constants::PATTERN_MAKER_DIALOG_PAD);
    draw_text(window, maker, title, {dialog.position.x + pad, dialog.position.y + pad});
}

void draw_dialog_button(
    sf::RenderWindow& window,
    const PatternMaker& maker,
    const sf::FloatRect& rect,
    const std::string& label,
    const bool active)
{
    sf::RectangleShape box{rect.size};
    box.setPosition(rect.position);
    box.setFillColor(active ? sf::Color{70, 90, 60} : sf::Color{50, 54, 50});
    window.draw(box);
    draw_text(window, maker, label, {rect.position.x + 8.0F, rect.position.y + 4.0F});
}

void draw_startup_dialog(
    sf::RenderWindow& window,
    const PatternMaker& maker,
    const sf::Vector2u window_size)
{
    const sf::FloatRect dialog = startup_dialog_rect(window_size);
    const StartupDialogHit hits = startup_dialog_hits(dialog);
    draw_dialog_panel(
        window,
        maker,
        window_size,
        dialog,
        std::string(constants::PATTERN_MAKER_STARTUP_TITLE));
    draw_dialog_button(
        window,
        maker,
        hits.new_file,
        std::string(constants::PATTERN_MAKER_STARTUP_NEW_LABEL),
        true);
    draw_dialog_button(
        window,
        maker,
        hits.open_file,
        std::string(constants::PATTERN_MAKER_STARTUP_OPEN_LABEL),
        false);
    draw_dialog_button(
        window,
        maker,
        hits.close,
        std::string(constants::PATTERN_MAKER_STARTUP_CLOSE_LABEL),
        false);
}

void draw_confirm_close_dialog(
    sf::RenderWindow& window,
    const PatternMaker& maker,
    const sf::Vector2u window_size)
{
    const sf::FloatRect dialog = confirm_close_rect(window_size);
    const ConfirmCloseHit hits = confirm_close_hits(dialog);
    draw_dialog_panel(
        window,
        maker,
        window_size,
        dialog,
        std::string(constants::PATTERN_MAKER_CONFIRM_CLOSE_TITLE));
    const float pad = static_cast<float>(constants::PATTERN_MAKER_DIALOG_PAD);
    draw_text(
        window,
        maker,
        std::string(constants::PATTERN_MAKER_CONFIRM_CLOSE_MESSAGE),
        {dialog.position.x + pad,
            dialog.position.y + pad
                + static_cast<float>(constants::PATTERN_MAKER_DIALOG_TITLE_BODY_GAP)});
    draw_dialog_button(
        window,
        maker,
        hits.yes,
        std::string(constants::PATTERN_MAKER_CONFIRM_YES_LABEL),
        true);
    draw_dialog_button(
        window,
        maker,
        hits.no,
        std::string(constants::PATTERN_MAKER_CONFIRM_NO_LABEL),
        false);
}

void handle_startup_dialog_click(
    PatternMaker& maker,
    sf::Window& window,
    const sf::Vector2u window_size,
    const sf::Vector2f mouse)
{
    const StartupDialogHit hits = startup_dialog_hits(startup_dialog_rect(window_size));
    if (hits.new_file.contains(mouse)) {
        maker.startup_open = false;
        open_create_dialog(maker);
        return;
    }
    if (hits.open_file.contains(mouse)) {
        open_pattern(maker, window);
        return;
    }
    if (hits.close.contains(mouse)) {
        request_close(maker);
    }
}

[[nodiscard]] bool handle_confirm_close_click(
    PatternMaker& maker,
    const sf::Vector2u window_size,
    const sf::Vector2f mouse)
{
    const ConfirmCloseHit hits = confirm_close_hits(confirm_close_rect(window_size));
    if (hits.yes.contains(mouse)) {
        return true;
    }
    if (hits.no.contains(mouse)) {
        maker.confirm_close_open = false;
    }
    return false;
}

} // namespace

int main()
{
    PatternMaker maker{};
    maker.pattern = make_blank_pattern();
    maker.font_ready = maker.font.openFromFile(std::filesystem::path(constants::PATTERN_MAKER_FONT_PATH))
        || maker.font.openFromFile(std::filesystem::path(constants::PATTERN_MAKER_FONT_FALLBACK_PATH));

    sf::RenderWindow window{
        sf::VideoMode{
            {static_cast<unsigned int>(constants::PATTERN_MAKER_WINDOW_WIDTH),
             static_cast<unsigned int>(constants::PATTERN_MAKER_WINDOW_HEIGHT)}},
        std::string(constants::PATTERN_MAKER_TITLE),
        sf::Style::Default,
        sf::State::Windowed,
    };
    window.setVerticalSyncEnabled(true);

    while (window.isOpen()) {
        const sf::Vector2u window_size = window.getSize();
        const sf::FloatRect grid = grid_rect(window_size);
        const std::vector<MakerButton> buttons = make_buttons(maker, window_size);

        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                request_close(maker);
                continue;
            }

            if (const auto* resized = event->getIf<sf::Event::Resized>()) {
                window.setView(sf::View{
                    sf::FloatRect{{0.0F, 0.0F},
                        {static_cast<float>(resized->size.x), static_cast<float>(resized->size.y)}}});
                continue;
            }

            if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                if (key->code == sf::Keyboard::Key::Escape) {
                    if (maker.confirm_close_open) {
                        maker.confirm_close_open = false;
                    }
                    else if (maker.create_dialog.open) {
                        maker.create_dialog.open = false;
                        if (!maker.session_started) {
                            maker.startup_open = true;
                        }
                    }
                    else {
                        request_close(maker);
                    }
                    continue;
                }
                if (maker.startup_open || maker.confirm_close_open) {
                    continue;
                }
                if (key->code == sf::Keyboard::Key::Delete) {
                    delete_selected(maker);
                }
                else if (key->code == sf::Keyboard::Key::Z
                    && (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl)
                        || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl))) {
                    undo(maker);
                }
                else if (key->code == sf::Keyboard::Key::Y
                    && (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl)
                        || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl))) {
                    redo(maker);
                }
                else if (key->code == sf::Keyboard::Key::S
                    && (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl)
                        || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl))) {
                    save_pattern(maker, window, false);
                }
                else if (key->code == sf::Keyboard::Key::O
                    && (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl)
                        || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl))) {
                    open_pattern(maker, window);
                }
                else if (key->code == sf::Keyboard::Key::N
                    && (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl)
                        || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl))) {
                    open_create_dialog(maker);
                }
                else if (key->code == sf::Keyboard::Key::L) {
                    run_command(maker, window, "linker");
                }
                else if (key->code == sf::Keyboard::Key::P) {
                    run_command(maker, window, "preview");
                }
                else if (key->code == sf::Keyboard::Key::Left) {
                    cycle_map_size(maker, -1);
                }
                else if (key->code == sf::Keyboard::Key::Right) {
                    cycle_map_size(maker, 1);
                }
                else if (key->code == sf::Keyboard::Key::Up) {
                    push_undo(maker);
                    maker.pattern.max_players = static_cast<std::uint8_t>(std::min(
                        static_cast<int>(maker.pattern.max_players) + 1,
                        constants::MAX_PLAYER_SLOTS));
                    maker.status = "Max players " + std::to_string(maker.pattern.max_players);
                }
                else if (key->code == sf::Keyboard::Key::Down) {
                    push_undo(maker);
                    maker.pattern.max_players = static_cast<std::uint8_t>(std::max(
                        static_cast<int>(maker.pattern.min_players),
                        static_cast<int>(maker.pattern.max_players) - 1));
                    maker.status = "Max players " + std::to_string(maker.pattern.max_players);
                }
                else if (key->code == sf::Keyboard::Key::F) {
                    push_undo(maker);
                    maker.pattern.fixed_player_count = maker.pattern.fixed_player_count == 0U
                        ? maker.pattern.max_players
                        : 0U;
                    maker.status = maker.pattern.fixed_player_count == 0U
                        ? "Player count unlocked"
                        : "Player count locked";
                }
                continue;
            }

            if (const auto* press = event->getIf<sf::Event::MouseButtonPressed>()) {
                if (press->button != sf::Mouse::Button::Left) {
                    continue;
                }

                const sf::Vector2f mouse{static_cast<float>(press->position.x),
                    static_cast<float>(press->position.y)};
                if (maker.confirm_close_open) {
                    if (handle_confirm_close_click(maker, window_size, mouse)) {
                        window.close();
                    }
                    continue;
                }
                if (maker.startup_open) {
                    handle_startup_dialog_click(maker, window, window_size, mouse);
                    continue;
                }
                if (maker.create_dialog.open) {
                    handle_create_dialog_click(maker, window_size, mouse);
                    continue;
                }

                bool clicked_button = false;
                for (const MakerButton& button : buttons) {
                    if (!button.enabled || !button.rect.contains(mouse)) {
                        continue;
                    }

                    run_command(maker, window, button.command);
                    clicked_button = true;
                    break;
                }

                if (!clicked_button) {
                    handle_grid_press(maker, grid, mouse);
                }
                continue;
            }

            if (event->is<sf::Event::MouseButtonReleased>()) {
                maker.dragging = false;
                continue;
            }

            if (event->is<sf::Event::MouseMoved>() && maker.dragging) {
                const sf::Vector2i pixel = sf::Mouse::getPosition(window);
                handle_drag(
                    maker,
                    grid,
                    {static_cast<float>(pixel.x), static_cast<float>(pixel.y)});
            }
        }

        window.clear(sf::Color{18, 20, 18});
        draw_editor(window, maker, grid);

        for (const MakerButton& button : buttons) {
            sf::RectangleShape rect{button.rect.size};
            rect.setPosition(button.rect.position);
            rect.setFillColor(button.enabled ? sf::Color{46, 50, 46} : sf::Color{28, 30, 28});
            if ((button.command == "linker" && maker.link_tool == LinkTool::Relative)
                || (button.command == "road_linker" && maker.link_tool == LinkTool::Road)
                || (button.command == "preview" && maker.preview_mode)
                || (button.command == "fixed_size" && maker.pattern.fixed_map_size)
                || (button.command == "toggle_random_gold" && maker.pattern.random_gold)
                || (button.command == "toggle_random_berries" && maker.pattern.random_berries)
                || (button.command == "ring_mode" && maker.pattern.start_ring_mode)
                || (button.command == "border_trees" && maker.pattern.border_trees)
                || (button.command == "stray_trees" && maker.pattern.random_stray_trees)) {
                rect.setFillColor(sf::Color{70, 90, 60});
            }
            window.draw(rect);
            draw_text(
                window,
                maker,
                button.label,
                {button.rect.position.x + 8.0F, button.rect.position.y + 4.0F},
                button.enabled ? sf::Color::White : sf::Color{90, 90, 90});
        }

        const std::string footer = maker.status + "  |  "
            + std::to_string(maker.pattern.map_width) + "x"
            + std::to_string(maker.pattern.map_height)
            + (maker.pattern.fixed_map_size ? " locked" : "")
            + "  players " + std::to_string(maker.pattern.min_players) + "-"
            + std::to_string(maker.pattern.max_players)
            + (maker.pattern.fixed_player_count > 0U
                ? (" fixed " + std::to_string(maker.pattern.fixed_player_count))
                : "")
            + "  arrows=size/max  F=lock players";
        draw_text(
            window,
            maker,
            footer,
            {static_cast<float>(constants::PATTERN_MAKER_GRID_PAD),
                static_cast<float>(window_size.y - constants::PATTERN_MAKER_STATUS_HEIGHT)});
        if (maker.create_dialog.open) {
            draw_create_dialog(window, maker, window_size);
        }
        if (maker.startup_open) {
            draw_startup_dialog(window, maker, window_size);
        }
        if (maker.confirm_close_open) {
            draw_confirm_close_dialog(window, maker, window_size);
        }
        window.display();
    }

    return 0;
}
