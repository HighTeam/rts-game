#pragma once

#include "core/constants.hpp"
#include "core/grid.hpp"

#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace aoa::app {

enum class CommandPanelMode : std::uint8_t {
    Empty = 0,
    WorkerActions = 1,
    BuildMenu = 2,
    PlaceTownCenter = 3,
    TownCenterActions = 4,
    MilitiaActions = 5,
    PlaceHouse = 6,
    HouseActions = 7,
};

enum class CommandPanelAction : std::uint8_t {
    None = 0,
    Build = 1,
    Deselect = 2,
    Kill = 3,
    BuildTownCenter = 4,
    Back = 5,
    SpawnWorker = 6,
    SpawnMilitia = 7,
    Destroy = 8,
    Attack = 9,
    Stop = 10,
    BuildHouse = 11,
};

struct CommandPanelFrame {
    float x{0.0F};
    float y{0.0F};
    float width{0.0F};
    float height{0.0F};
};

struct CommandPanelButton {
    CommandPanelAction action{CommandPanelAction::None};
    std::string_view label{};
    float x{0.0F};
    float y{0.0F};
    float width{0.0F};
    float height{0.0F};
    bool disabled{false};
    int cost_wood{0};
    int cost_food{0};
    int slot{-1};
};

struct CommandPanelBuildOptions {
    int town_center_wood_cost{constants::TOWN_CENTER_BUILD_WOOD_COST};
    bool can_afford_town_center{true};
    int house_wood_cost{constants::HOUSE_BUILD_WOOD_COST};
    bool can_afford_house{true};
    int worker_food_cost{constants::WORKER_FOOD_COST};
    bool can_afford_worker{true};
    int militia_food_cost{constants::MILITIA_FOOD_COST};
    bool can_afford_militia{true};
};

[[nodiscard]] inline CommandPanelFrame bottom_panel_size(const sf::Vector2u window_size)
{
    const float width =
        static_cast<float>(window_size.x) * constants::HUD_BOTTOM_PANEL_WIDTH_FRACTION;
    const float height = width * (constants::HUD_BOTTOM_PANEL_ASPECT_HEIGHT
        / constants::HUD_BOTTOM_PANEL_ASPECT_WIDTH);
    return CommandPanelFrame{.x = 0.0F, .y = 0.0F, .width = width, .height = height};
}

[[nodiscard]] inline CommandPanelFrame command_panel_frame_rect(const sf::Vector2u window_size)
{
    const CommandPanelFrame size = bottom_panel_size(window_size);
    return CommandPanelFrame{
        .x = 0.0F,
        .y = static_cast<float>(window_size.y) - size.height,
        .width = size.width,
        .height = size.height,
    };
}

[[nodiscard]] inline CommandPanelFrame minimap_panel_frame_rect(const sf::Vector2u window_size)
{
    const CommandPanelFrame size = bottom_panel_size(window_size);
    return CommandPanelFrame{
        .x = static_cast<float>(window_size.x) - size.width,
        .y = static_cast<float>(window_size.y) - size.height,
        .width = size.width,
        .height = size.height,
    };
}

[[nodiscard]] inline CommandPanelFrame status_panel_frame_rect(const sf::Vector2u window_size)
{
    const CommandPanelFrame options = command_panel_frame_rect(window_size);
    const CommandPanelFrame minimap = minimap_panel_frame_rect(window_size);
    return CommandPanelFrame{
        .x = options.x + options.width,
        .y = options.y,
        .width = minimap.x > options.x + options.width
            ? minimap.x - (options.x + options.width)
            : 0.0F,
        .height = options.height,
    };
}

[[nodiscard]] inline std::string_view label_for_action(const CommandPanelAction action)
{
    switch (action) {
    case CommandPanelAction::Build:
        return "Bu";
    case CommandPanelAction::Deselect:
        return "De";
    case CommandPanelAction::Kill:
        return "Ki";
    case CommandPanelAction::BuildTownCenter:
        return "TC";
    case CommandPanelAction::BuildHouse:
        return "Ho";
    case CommandPanelAction::Back:
        return "Ba";
    case CommandPanelAction::SpawnWorker:
        return "Wo";
    case CommandPanelAction::SpawnMilitia:
        return "Mi";
    case CommandPanelAction::Destroy:
        return "Dr";
    case CommandPanelAction::Attack:
        return "At";
    case CommandPanelAction::Stop:
        return "St";
    case CommandPanelAction::None:
        return "";
    }

    return "";
}

// 5x3 option grid hotkeys:
// Q W E R T
// A S D F G
// Z X C V B
[[nodiscard]] inline std::optional<int> command_panel_slot_for_key(const sf::Keyboard::Key key)
{
    switch (key) {
    case sf::Keyboard::Key::Q:
        return 0;
    case sf::Keyboard::Key::W:
        return 1;
    case sf::Keyboard::Key::E:
        return 2;
    case sf::Keyboard::Key::R:
        return 3;
    case sf::Keyboard::Key::T:
        return 4;
    case sf::Keyboard::Key::A:
        return 5;
    case sf::Keyboard::Key::S:
        return 6;
    case sf::Keyboard::Key::D:
        return 7;
    case sf::Keyboard::Key::F:
        return 8;
    case sf::Keyboard::Key::G:
        return 9;
    case sf::Keyboard::Key::Z:
        return 10;
    case sf::Keyboard::Key::X:
        return 11;
    case sf::Keyboard::Key::C:
        return 12;
    case sf::Keyboard::Key::V:
        return 13;
    case sf::Keyboard::Key::B:
        return 14;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] inline std::vector<std::pair<int, CommandPanelAction>> command_panel_slot_actions(
    const CommandPanelMode mode)
{
    switch (mode) {
    case CommandPanelMode::WorkerActions:
        return {
            {0, CommandPanelAction::Build},
            {2, CommandPanelAction::Attack},
            {3, CommandPanelAction::Stop},
            {4, CommandPanelAction::Deselect},
            {14, CommandPanelAction::Kill},
        };
    case CommandPanelMode::MilitiaActions:
        return {
            {0, CommandPanelAction::Attack},
            {3, CommandPanelAction::Stop},
            {4, CommandPanelAction::Deselect},
            {14, CommandPanelAction::Kill},
        };
    case CommandPanelMode::BuildMenu:
        return {
            {0, CommandPanelAction::BuildHouse},
            {4, CommandPanelAction::BuildTownCenter},
            {13, CommandPanelAction::Back},
        };
    case CommandPanelMode::TownCenterActions:
        return {
            {1, CommandPanelAction::SpawnWorker},
            {6, CommandPanelAction::SpawnMilitia},
            {4, CommandPanelAction::Deselect},
            {14, CommandPanelAction::Destroy},
        };
    case CommandPanelMode::HouseActions:
        return {
            {4, CommandPanelAction::Deselect},
            {14, CommandPanelAction::Destroy},
        };
    case CommandPanelMode::Empty:
    case CommandPanelMode::PlaceTownCenter:
    case CommandPanelMode::PlaceHouse:
        break;
    }

    return {};
}

[[nodiscard]] inline std::vector<CommandPanelButton> build_command_panel_buttons(
    const CommandPanelMode mode,
    const sf::Vector2u window_size,
    const CommandPanelBuildOptions& build_options = {})
{
    std::vector<CommandPanelButton> buttons{};
    const auto slot_actions = command_panel_slot_actions(mode);
    if (slot_actions.empty()) {
        return buttons;
    }

    const CommandPanelFrame frame = command_panel_frame_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    const int columns = constants::HUD_BOTTOM_PANEL_COLUMNS;
    const int rows = constants::HUD_BOTTOM_PANEL_ROWS;
    const float content_width = frame.width - padding * 2.0F;
    const float content_height = frame.height - padding * 2.0F;
    const float button_width =
        (content_width - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    const float button_height =
        (content_height - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows);
    const int max_slots = columns * rows;

    for (const auto& [slot, action] : slot_actions) {
        if (slot < 0 || slot >= max_slots) {
            continue;
        }

        const int column = slot % columns;
        const int row = slot / columns;
        CommandPanelButton button{
            .action = action,
            .label = label_for_action(action),
            .x = frame.x + padding + static_cast<float>(column) * (button_width + gap),
            .y = frame.y + padding + static_cast<float>(row) * (button_height + gap),
            .width = button_width,
            .height = button_height,
            .slot = slot,
        };
        if (button.action == CommandPanelAction::BuildTownCenter) {
            button.cost_wood = build_options.town_center_wood_cost;
            button.disabled = !build_options.can_afford_town_center;
        }
        if (button.action == CommandPanelAction::BuildHouse) {
            button.cost_wood = build_options.house_wood_cost;
            button.disabled = !build_options.can_afford_house;
        }
        if (button.action == CommandPanelAction::SpawnWorker) {
            button.cost_food = build_options.worker_food_cost;
            button.disabled = !build_options.can_afford_worker;
        }
        if (button.action == CommandPanelAction::SpawnMilitia) {
            button.cost_food = build_options.militia_food_cost;
            button.disabled = !build_options.can_afford_militia;
        }
        buttons.push_back(button);
    }

    return buttons;
}

[[nodiscard]] inline CommandPanelAction action_for_command_panel_slot(
    const CommandPanelMode mode,
    const int slot,
    const CommandPanelBuildOptions& build_options = {})
{
    for (const auto& [action_slot, action] : command_panel_slot_actions(mode)) {
        if (action_slot != slot) {
            continue;
        }
        if (action == CommandPanelAction::BuildTownCenter
            && !build_options.can_afford_town_center) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::BuildHouse && !build_options.can_afford_house) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::SpawnWorker && !build_options.can_afford_worker) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::SpawnMilitia && !build_options.can_afford_militia) {
            return CommandPanelAction::None;
        }
        return action;
    }

    return CommandPanelAction::None;
}

[[nodiscard]] inline CommandPanelAction hit_test_command_panel(
    const CommandPanelMode mode,
    const sf::Vector2u window_size,
    const float mouse_x,
    const float mouse_y,
    const CommandPanelBuildOptions& build_options = {})
{
    for (const CommandPanelButton& button :
         build_command_panel_buttons(mode, window_size, build_options)) {
        if (mouse_x >= button.x && mouse_x <= button.x + button.width && mouse_y >= button.y
            && mouse_y <= button.y + button.height) {
            if (button.disabled) {
                return CommandPanelAction::None;
            }
            return button.action;
        }
    }

    return CommandPanelAction::None;
}

[[nodiscard]] inline bool hit_test_command_panel_frame(
    const sf::Vector2u window_size,
    const float mouse_x,
    const float mouse_y)
{
    const CommandPanelFrame frame = command_panel_frame_rect(window_size);
    return mouse_x >= frame.x && mouse_x <= frame.x + frame.width && mouse_y >= frame.y
        && mouse_y <= frame.y + frame.height;
}

[[nodiscard]] inline CommandPanelFrame minimap_content_rect(const sf::Vector2u window_size)
{
    const CommandPanelFrame frame = minimap_panel_frame_rect(window_size);
    const float pad = static_cast<float>(constants::MINIMAP_CONTENT_PADDING_PX);
    const float width = std::max(0.0F, frame.width - pad * 2.0F);
    const float height = std::max(0.0F, frame.height - pad * 2.0F);
    return CommandPanelFrame{
        .x = frame.x + pad,
        .y = frame.y + pad,
        .width = width,
        .height = height,
    };
}

[[nodiscard]] inline bool hit_test_minimap_panel_frame(
    const sf::Vector2u window_size,
    const float mouse_x,
    const float mouse_y)
{
    const CommandPanelFrame frame = minimap_panel_frame_rect(window_size);
    return mouse_x >= frame.x && mouse_x <= frame.x + frame.width && mouse_y >= frame.y
        && mouse_y <= frame.y + frame.height;
}

[[nodiscard]] inline std::optional<std::pair<float, float>> minimap_screen_to_world(
    const sf::Vector2u window_size,
    const float mouse_x,
    const float mouse_y,
    const int map_width,
    const int map_height)
{
    if (map_width <= 0 || map_height <= 0) {
        return std::nullopt;
    }

    if (!hit_test_minimap_panel_frame(window_size, mouse_x, mouse_y)) {
        return std::nullopt;
    }

    // Keep mapping identical to HudOverlay draw (includes texture max-edge clamp).
    const CommandPanelFrame content = minimap_content_rect(window_size);
    const float iso_span = static_cast<float>(map_width + map_height);
    if (content.width <= 0.0F || content.height <= 0.0F || iso_span <= 0.0F) {
        return std::nullopt;
    }

    const float min_iso_u = -static_cast<float>(map_height);
    const float min_iso_v = 0.0F;
    float scale = std::min(content.width / iso_span, content.height / iso_span);
    float used = iso_span * scale;
    if (used > static_cast<float>(constants::MINIMAP_TEXTURE_MAX_EDGE_PX)) {
        scale = static_cast<float>(constants::MINIMAP_TEXTURE_MAX_EDGE_PX) / iso_span;
        used = iso_span * scale;
    }

    const float offset_x = content.x + (content.width - used) * 0.5F;
    const float offset_y = content.y + (content.height - used) * 0.5F;
    const float iso_u = (mouse_x - offset_x) / scale + min_iso_u;
    const float iso_v = (mouse_y - offset_y) / scale + min_iso_v;
    const float world_x = (iso_u + iso_v) * 0.5F;
    const float world_z = (iso_v - iso_u) * 0.5F;
    if (world_x < 0.0F || world_z < 0.0F || world_x >= static_cast<float>(map_width)
        || world_z >= static_cast<float>(map_height)) {
        return std::nullopt;
    }

    return std::pair<float, float>{world_x, world_z};
}

[[nodiscard]] inline core::GridPos town_center_anchor_from_center_cell(const core::GridPos center_cell)
{
    const int half = constants::TOWN_CENTER_FOOTPRINT_TILES / 2;
    return core::GridPos{center_cell.x - half, center_cell.y - half};
}

[[nodiscard]] inline core::GridPos house_anchor_from_center_cell(const core::GridPos center_cell)
{
    const int half = constants::HOUSE_FOOTPRINT_TILES / 2;
    return core::GridPos{center_cell.x - half, center_cell.y - half};
}

} // namespace aoa::app
