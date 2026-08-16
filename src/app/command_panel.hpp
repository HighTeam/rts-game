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
    PlaceLumberCamp = 8,
    LumberCampActions = 9,
    PlaceExtractor = 10,
    ExtractorActions = 11,
    ManaLakeInfo = 12,
    PlaceMill = 13,
    MillActions = 14,
    PlaceMiningCamp = 15,
    MiningCampActions = 16,
    PlaceBarracks = 17,
    BarracksActions = 18,
    PlaceMageAcademy = 19,
    MageAcademyActions = 20,
    PlaceTower = 21,
    TowerActions = 22,
    PlaceMarket = 23,
    MarketActions = 24,
    MageActions = 25,
    BuildMilitaryMenu = 26,
    PlaceGarden = 27,
    GardenActions = 28,
    PlaceReservoir = 29,
    ReservoirActions = 30,
    PlaceFarm = 31,
    FarmActions = 32,
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
    BuildLumberCamp = 12,
    BuildExtractor = 13,
    BuildMill = 14,
    BuildMiningCamp = 15,
    BuildBarracks = 16,
    BuildMageAcademy = 17,
    BuildTower = 18,
    BuildMarket = 19,
    SpawnMage = 20,
    Garrison = 21,
    UnloadGarrison = 22,
    AdvanceAge = 23,
    OpenMilitaryBuild = 24,
    BuildGarden = 25,
    BuildReservoir = 26,
    BuildFarm = 27,
    ResearchCartography = 28,
    MarketSellWood = 29,
    MarketSellFood = 30,
    MarketBuyWood = 31,
    MarketBuyFood = 32,
    ResearchSpy = 33,
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
    bool locked{false};
    int cost_wood{0};
    int cost_food{0};
    int cost_money{0};
    int cost_mana{0};
    int slot{-1};
};

struct CommandPanelBuildOptions {
    int town_center_wood_cost{constants::TOWN_CENTER_BUILD_WOOD_COST};
    bool can_afford_town_center{true};
    int house_wood_cost{constants::HOUSE_BUILD_WOOD_COST};
    bool can_afford_house{true};
    int lumber_camp_wood_cost{constants::LUMBER_CAMP_BUILD_WOOD_COST};
    bool can_afford_lumber_camp{true};
    int extractor_wood_cost{constants::EXTRACTOR_BUILD_WOOD_COST};
    int extractor_money_cost{constants::EXTRACTOR_BUILD_MONEY_COST};
    bool can_afford_extractor{true};
    int worker_food_cost{constants::WORKER_FOOD_COST};
    bool can_afford_worker{true};
    int militia_food_cost{constants::MILITIA_FOOD_COST};
    int militia_money_cost{constants::MILITIA_MONEY_COST};
    bool can_afford_militia{true};
    int mill_wood_cost{constants::MILL_BUILD_WOOD_COST};
    bool can_afford_mill{true};
    int mining_camp_wood_cost{constants::MINING_CAMP_BUILD_WOOD_COST};
    bool can_afford_mining_camp{true};
    int barracks_wood_cost{constants::BARRACKS_BUILD_WOOD_COST};
    bool can_afford_barracks{true};
    int mage_academy_wood_cost{constants::MAGE_ACADEMY_BUILD_WOOD_COST};
    int mage_academy_money_cost{constants::MAGE_ACADEMY_BUILD_MONEY_COST};
    int mage_academy_mana_cost{constants::MAGE_ACADEMY_BUILD_MANA_COST};
    bool can_afford_mage_academy{true};
    int tower_wood_cost{constants::TOWER_BUILD_WOOD_COST};
    int tower_money_cost{constants::TOWER_BUILD_MONEY_COST};
    bool can_afford_tower{true};
    int market_wood_cost{constants::MARKET_BUILD_WOOD_COST};
    bool can_afford_market{true};
    int garden_wood_cost{constants::GARDEN_BUILD_WOOD_COST};
    int garden_money_cost{constants::GARDEN_BUILD_MONEY_COST};
    int garden_mana_cost{constants::GARDEN_BUILD_MANA_COST};
    bool can_afford_garden{true};
    int reservoir_wood_cost{constants::RESERVOIR_BUILD_WOOD_COST};
    int reservoir_money_cost{constants::RESERVOIR_BUILD_MONEY_COST};
    bool can_afford_reservoir{true};
    int farm_wood_cost{constants::FARM_BUILD_WOOD_COST};
    bool can_afford_farm{true};
    int town_center_money_cost{constants::TOWN_CENTER_BUILD_MONEY_COST};
    int town_center_mana_cost{constants::TOWN_CENTER_BUILD_MANA_COST};
    int mage_money_cost{constants::MAGE_MONEY_COST};
    int mage_mana_cost{constants::MAGE_MANA_COST};
    bool can_afford_mage{true};
    int age_food_cost{0};
    int age_money_cost{0};
    int age_mana_cost{0};
    bool can_afford_age{false};
    bool can_advance_age{false};
    std::string_view next_age_name{};
    bool building_busy{false};
    bool has_cartography{false};
    bool can_afford_cartography{false};
    bool has_spy{false};
    bool can_afford_spy{false};
    bool unlocked_elemental_buildings{false};
    bool unlocked_garden{false};
    bool unlocked_farm{false};
    bool unlocked_spy{false};
    int spy_money_cost{0};
    bool can_sell_wood{false};
    bool can_sell_food{false};
    bool can_buy_wood{false};
    bool can_buy_food{false};
    int cartography_money_cost{constants::CARTOGRAPHY_GOLD_COST};
    int market_trade_amount{constants::MARKET_TRADE_RESOURCE_AMOUNT};
    int market_sell_gold{constants::MARKET_SELL_GOLD_AMOUNT};
    int market_buy_gold{constants::MARKET_BUY_GOLD_AMOUNT};
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
        return "Ec";
    case CommandPanelAction::OpenMilitaryBuild:
        return "Mi";
    case CommandPanelAction::BuildGarden:
        return "Gd";
    case CommandPanelAction::BuildReservoir:
        return "Rv";
    case CommandPanelAction::BuildFarm:
        return "Fa";
    case CommandPanelAction::Deselect:
        return "De";
    case CommandPanelAction::Kill:
        return "Ki";
    case CommandPanelAction::BuildTownCenter:
        return "TC";
    case CommandPanelAction::BuildHouse:
        return "Ho";
    case CommandPanelAction::BuildLumberCamp:
        return "Lu";
    case CommandPanelAction::BuildExtractor:
        return "Ex";
    case CommandPanelAction::BuildMill:
        return "Ml";
    case CommandPanelAction::BuildMiningCamp:
        return "Mn";
    case CommandPanelAction::BuildBarracks:
        return "Br";
    case CommandPanelAction::BuildMageAcademy:
        return "Ma";
    case CommandPanelAction::BuildTower:
        return "Tw";
    case CommandPanelAction::BuildMarket:
        return "Mk";
    case CommandPanelAction::Back:
        return "Ba";
    case CommandPanelAction::SpawnWorker:
        return "Wo";
    case CommandPanelAction::SpawnMilitia:
        return "Mi";
    case CommandPanelAction::SpawnMage:
        return "Mg";
    case CommandPanelAction::Garrison:
        return "In";
    case CommandPanelAction::UnloadGarrison:
        return "Ou";
    case CommandPanelAction::AdvanceAge:
        return "Ag";
    case CommandPanelAction::Destroy:
        return "Dr";
    case CommandPanelAction::Attack:
        return "At";
    case CommandPanelAction::Stop:
        return "St";
    case CommandPanelAction::ResearchCartography:
        return "Ca";
    case CommandPanelAction::ResearchSpy:
        return "Sp";
    case CommandPanelAction::MarketSellWood:
        return "SW";
    case CommandPanelAction::MarketSellFood:
        return "SF";
    case CommandPanelAction::MarketBuyWood:
        return "BW";
    case CommandPanelAction::MarketBuyFood:
        return "BF";
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
            {1, CommandPanelAction::OpenMilitaryBuild},
            {2, CommandPanelAction::Attack},
            {3, CommandPanelAction::Stop},
            {4, CommandPanelAction::Deselect},
            {10, CommandPanelAction::Garrison},
            {14, CommandPanelAction::Kill},
        };
    case CommandPanelMode::MilitiaActions:
    case CommandPanelMode::MageActions:
        return {
            {0, CommandPanelAction::Attack},
            {3, CommandPanelAction::Stop},
            {4, CommandPanelAction::Deselect},
            {14, CommandPanelAction::Kill},
        };
    case CommandPanelMode::BuildMenu:
        return {
            {0, CommandPanelAction::BuildHouse},
            {1, CommandPanelAction::BuildLumberCamp},
            {2, CommandPanelAction::BuildMill},
            {3, CommandPanelAction::BuildMiningCamp},
            {4, CommandPanelAction::BuildExtractor},
            {5, CommandPanelAction::BuildMarket},
            {6, CommandPanelAction::BuildGarden},
            {7, CommandPanelAction::BuildFarm},
            {9, CommandPanelAction::BuildReservoir},
            {10, CommandPanelAction::BuildTownCenter},
            {13, CommandPanelAction::OpenMilitaryBuild},
            {14, CommandPanelAction::Back},
        };
    case CommandPanelMode::BuildMilitaryMenu:
        return {
            {0, CommandPanelAction::BuildBarracks},
            {1, CommandPanelAction::BuildMageAcademy},
            {10, CommandPanelAction::BuildTower},
            {13, CommandPanelAction::Build},
            {14, CommandPanelAction::Back},
        };
    case CommandPanelMode::TownCenterActions:
        return {
            {0, CommandPanelAction::AdvanceAge},
            {1, CommandPanelAction::SpawnWorker},
            {4, CommandPanelAction::Deselect},
            {6, CommandPanelAction::ResearchSpy},
            {11, CommandPanelAction::UnloadGarrison},
            {14, CommandPanelAction::Destroy},
        };
    case CommandPanelMode::BarracksActions:
        return {
            {0, CommandPanelAction::SpawnMilitia},
            {4, CommandPanelAction::Deselect},
            {14, CommandPanelAction::Destroy},
        };
    case CommandPanelMode::MageAcademyActions:
        return {
            {0, CommandPanelAction::SpawnMage},
            {4, CommandPanelAction::Deselect},
            {14, CommandPanelAction::Destroy},
        };
    case CommandPanelMode::HouseActions:
    case CommandPanelMode::LumberCampActions:
    case CommandPanelMode::ExtractorActions:
    case CommandPanelMode::MillActions:
    case CommandPanelMode::MiningCampActions:
    case CommandPanelMode::TowerActions:
        return {
            {4, CommandPanelAction::Deselect},
            {14, CommandPanelAction::Destroy},
        };
    case CommandPanelMode::MarketActions:
        return {
            {0, CommandPanelAction::ResearchCartography},
            {5, CommandPanelAction::MarketSellWood},
            {6, CommandPanelAction::MarketSellFood},
            {10, CommandPanelAction::MarketBuyWood},
            {11, CommandPanelAction::MarketBuyFood},
            {4, CommandPanelAction::Deselect},
            {14, CommandPanelAction::Destroy},
        };
    case CommandPanelMode::GardenActions:
    case CommandPanelMode::ReservoirActions:
    case CommandPanelMode::FarmActions:
        return {
            {4, CommandPanelAction::Deselect},
            {14, CommandPanelAction::Destroy},
        };
    case CommandPanelMode::ManaLakeInfo:
        return {
            {4, CommandPanelAction::Deselect},
        };
    case CommandPanelMode::PlaceTownCenter:
    case CommandPanelMode::PlaceHouse:
    case CommandPanelMode::PlaceLumberCamp:
    case CommandPanelMode::PlaceExtractor:
    case CommandPanelMode::PlaceMill:
    case CommandPanelMode::PlaceMiningCamp:
    case CommandPanelMode::PlaceBarracks:
    case CommandPanelMode::PlaceMageAcademy:
    case CommandPanelMode::PlaceTower:
    case CommandPanelMode::PlaceMarket:
    case CommandPanelMode::PlaceGarden:
    case CommandPanelMode::PlaceReservoir:
    case CommandPanelMode::PlaceFarm:
        return {
            {4, CommandPanelAction::Deselect},
        };
    case CommandPanelMode::Empty:
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

        if (action == CommandPanelAction::AdvanceAge && !build_options.can_advance_age) {
            continue;
        }

        if (action == CommandPanelAction::ResearchCartography && build_options.has_cartography) {
            continue;
        }

        if (action == CommandPanelAction::ResearchSpy && build_options.has_spy) {
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
            button.cost_money = build_options.town_center_money_cost;
            button.cost_mana = build_options.town_center_mana_cost;
            button.disabled = !build_options.can_afford_town_center;
        }
        if (button.action == CommandPanelAction::BuildHouse) {
            button.cost_wood = build_options.house_wood_cost;
            button.disabled = !build_options.can_afford_house;
        }
        if (button.action == CommandPanelAction::BuildLumberCamp) {
            button.cost_wood = build_options.lumber_camp_wood_cost;
            button.disabled = !build_options.can_afford_lumber_camp;
        }
        if (button.action == CommandPanelAction::BuildExtractor) {
            button.cost_wood = build_options.extractor_wood_cost;
            button.cost_money = build_options.extractor_money_cost;
            button.locked = !build_options.unlocked_elemental_buildings;
            button.disabled = button.locked || !build_options.can_afford_extractor;
        }
        if (button.action == CommandPanelAction::BuildMill) {
            button.cost_wood = build_options.mill_wood_cost;
            button.disabled = !build_options.can_afford_mill;
        }
        if (button.action == CommandPanelAction::BuildMiningCamp) {
            button.cost_wood = build_options.mining_camp_wood_cost;
            button.disabled = !build_options.can_afford_mining_camp;
        }
        if (button.action == CommandPanelAction::BuildBarracks) {
            button.cost_wood = build_options.barracks_wood_cost;
            button.disabled = !build_options.can_afford_barracks;
        }
        if (button.action == CommandPanelAction::BuildMageAcademy) {
            button.cost_wood = build_options.mage_academy_wood_cost;
            button.cost_money = build_options.mage_academy_money_cost;
            button.cost_mana = build_options.mage_academy_mana_cost;
            button.locked = !build_options.unlocked_elemental_buildings;
            button.disabled = button.locked || !build_options.can_afford_mage_academy;
        }
        if (button.action == CommandPanelAction::BuildTower) {
            button.cost_wood = build_options.tower_wood_cost;
            button.cost_money = build_options.tower_money_cost;
            button.locked = !build_options.unlocked_elemental_buildings;
            button.disabled = button.locked || !build_options.can_afford_tower;
        }
        if (button.action == CommandPanelAction::BuildMarket) {
            button.cost_wood = build_options.market_wood_cost;
            button.locked = !build_options.unlocked_elemental_buildings;
            button.disabled = button.locked || !build_options.can_afford_market;
        }
        if (button.action == CommandPanelAction::BuildGarden) {
            button.cost_wood = build_options.garden_wood_cost;
            button.cost_money = build_options.garden_money_cost;
            button.cost_mana = build_options.garden_mana_cost;
            button.locked = !build_options.unlocked_garden;
            button.disabled = button.locked || !build_options.can_afford_garden;
        }
        if (button.action == CommandPanelAction::BuildReservoir) {
            button.cost_wood = build_options.reservoir_wood_cost;
            button.cost_money = build_options.reservoir_money_cost;
            button.locked = !build_options.unlocked_elemental_buildings;
            button.disabled = button.locked || !build_options.can_afford_reservoir;
        }
        if (button.action == CommandPanelAction::BuildFarm) {
            button.cost_wood = build_options.farm_wood_cost;
            button.locked = !build_options.unlocked_farm;
            button.disabled = button.locked || !build_options.can_afford_farm;
        }
        if (button.action == CommandPanelAction::SpawnWorker) {
            button.cost_food = build_options.worker_food_cost;
            button.disabled = !build_options.can_afford_worker || build_options.building_busy;
        }
        if (button.action == CommandPanelAction::SpawnMilitia) {
            button.cost_food = build_options.militia_food_cost;
            button.cost_money = build_options.militia_money_cost;
            button.disabled = !build_options.can_afford_militia || build_options.building_busy;
        }
        if (button.action == CommandPanelAction::SpawnMage) {
            button.cost_money = build_options.mage_money_cost;
            button.cost_mana = build_options.mage_mana_cost;
            button.disabled = !build_options.can_afford_mage || build_options.building_busy;
        }
        if (button.action == CommandPanelAction::AdvanceAge) {
            button.cost_food = build_options.age_food_cost;
            button.cost_money = build_options.age_money_cost;
            button.cost_mana = build_options.age_mana_cost;
            button.disabled = !build_options.can_afford_age || build_options.building_busy;
        }
        if (button.action == CommandPanelAction::ResearchCartography) {
            button.cost_money = build_options.cartography_money_cost;
            button.disabled = build_options.building_busy || !build_options.can_afford_cartography;
        }
        if (button.action == CommandPanelAction::ResearchSpy) {
            button.cost_money = build_options.spy_money_cost;
            button.locked = !build_options.unlocked_spy;
            button.disabled = button.locked || build_options.building_busy
                || !build_options.can_afford_spy;
        }
        if (button.action == CommandPanelAction::MarketSellWood) {
            button.cost_wood = build_options.market_trade_amount;
            button.disabled = !build_options.can_sell_wood;
        }
        if (button.action == CommandPanelAction::MarketSellFood) {
            button.cost_food = build_options.market_trade_amount;
            button.disabled = !build_options.can_sell_food;
        }
        if (button.action == CommandPanelAction::MarketBuyWood) {
            button.cost_money = build_options.market_buy_gold;
            button.disabled = !build_options.can_buy_wood;
        }
        if (button.action == CommandPanelAction::MarketBuyFood) {
            button.cost_money = build_options.market_buy_gold;
            button.disabled = !build_options.can_buy_food;
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
        if (action == CommandPanelAction::BuildLumberCamp && !build_options.can_afford_lumber_camp) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::BuildExtractor
            && (!build_options.unlocked_elemental_buildings
                || !build_options.can_afford_extractor)) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::BuildMill && !build_options.can_afford_mill) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::BuildMiningCamp && !build_options.can_afford_mining_camp) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::BuildBarracks && !build_options.can_afford_barracks) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::BuildMageAcademy
            && (!build_options.unlocked_elemental_buildings
                || !build_options.can_afford_mage_academy)) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::BuildTower
            && (!build_options.unlocked_elemental_buildings || !build_options.can_afford_tower)) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::BuildMarket
            && (!build_options.unlocked_elemental_buildings || !build_options.can_afford_market)) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::BuildGarden
            && (!build_options.unlocked_garden || !build_options.can_afford_garden)) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::BuildReservoir
            && (!build_options.unlocked_elemental_buildings
                || !build_options.can_afford_reservoir)) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::BuildFarm
            && (!build_options.unlocked_farm || !build_options.can_afford_farm)) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::SpawnWorker
            && (!build_options.can_afford_worker || build_options.building_busy)) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::SpawnMilitia
            && (!build_options.can_afford_militia || build_options.building_busy)) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::SpawnMage
            && (!build_options.can_afford_mage || build_options.building_busy)) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::AdvanceAge
            && (!build_options.can_advance_age || !build_options.can_afford_age
                || build_options.building_busy)) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::ResearchCartography
            && (build_options.has_cartography || build_options.building_busy
                || !build_options.can_afford_cartography)) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::ResearchSpy
            && (!build_options.unlocked_spy || build_options.has_spy
                || build_options.building_busy || !build_options.can_afford_spy)) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::MarketSellWood && !build_options.can_sell_wood) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::MarketSellFood && !build_options.can_sell_food) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::MarketBuyWood && !build_options.can_buy_wood) {
            return CommandPanelAction::None;
        }
        if (action == CommandPanelAction::MarketBuyFood && !build_options.can_buy_food) {
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

[[nodiscard]] inline bool hit_test_status_panel_frame(
    const sf::Vector2u window_size,
    const float mouse_x,
    const float mouse_y)
{
    const CommandPanelFrame frame = status_panel_frame_rect(window_size);
    return mouse_x >= frame.x && mouse_x <= frame.x + frame.width && mouse_y >= frame.y
        && mouse_y <= frame.y + frame.height;
}

[[nodiscard]] inline CommandPanelFrame resource_bar_frame_rect(const sf::Vector2u)
{
    const float icon_size = static_cast<float>(constants::HUD_ICON_DRAW_SIZE_PX);
    const float gap = static_cast<float>(constants::HUD_ICON_TEXT_GAP_PX);
    const float char_step = static_cast<float>(
        (constants::HUD_GLYPH_WIDTH + constants::HUD_CHAR_SPACING) * constants::HUD_PIXEL_SCALE);
    const float group_width = icon_size + gap
        + static_cast<float>(constants::HUD_RESOURCE_BAR_VALUE_MAX_CHARS) * char_step
        + icon_size;
    return CommandPanelFrame{
        .x = 0.0F,
        .y = 0.0F,
        .width = constants::HUD_MARGIN_X
            + group_width * static_cast<float>(constants::HUD_RESOURCE_BAR_GROUP_COUNT),
        .height = constants::HUD_MARGIN_Y + icon_size + constants::HUD_MARGIN_Y,
    };
}

[[nodiscard]] inline bool hit_test_resource_bar_frame(
    const sf::Vector2u window_size,
    const float mouse_x,
    const float mouse_y)
{
    const CommandPanelFrame frame = resource_bar_frame_rect(window_size);
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

[[nodiscard]] inline core::GridPos lumber_camp_anchor_from_center_cell(const core::GridPos center_cell)
{
    const int half = constants::LUMBER_CAMP_FOOTPRINT_TILES / 2;
    return core::GridPos{center_cell.x - half, center_cell.y - half};
}

[[nodiscard]] inline core::GridPos extractor_anchor_from_center_cell(const core::GridPos center_cell)
{
    const int half = constants::EXTRACTOR_FOOTPRINT_TILES / 2;
    return core::GridPos{center_cell.x - half, center_cell.y - half};
}

[[nodiscard]] inline core::GridPos mill_anchor_from_center_cell(const core::GridPos center_cell)
{
    const int half = constants::MILL_FOOTPRINT_TILES / 2;
    return core::GridPos{center_cell.x - half, center_cell.y - half};
}

[[nodiscard]] inline core::GridPos mining_camp_anchor_from_center_cell(const core::GridPos center_cell)
{
    const int half = constants::MINING_CAMP_FOOTPRINT_TILES / 2;
    return core::GridPos{center_cell.x - half, center_cell.y - half};
}

[[nodiscard]] inline core::GridPos barracks_anchor_from_center_cell(const core::GridPos center_cell)
{
    const int half = constants::BARRACKS_FOOTPRINT_TILES / 2;
    return core::GridPos{center_cell.x - half, center_cell.y - half};
}

[[nodiscard]] inline core::GridPos mage_academy_anchor_from_center_cell(const core::GridPos center_cell)
{
    const int half = constants::MAGE_ACADEMY_FOOTPRINT_TILES / 2;
    return core::GridPos{center_cell.x - half, center_cell.y - half};
}

[[nodiscard]] inline core::GridPos tower_anchor_from_center_cell(const core::GridPos center_cell)
{
    return center_cell;
}

[[nodiscard]] inline core::GridPos market_anchor_from_center_cell(const core::GridPos center_cell)
{
    const int half = constants::MARKET_FOOTPRINT_TILES / 2;
    return core::GridPos{center_cell.x - half, center_cell.y - half};
}

[[nodiscard]] inline core::GridPos garden_anchor_from_center_cell(const core::GridPos center_cell)
{
    const int half = constants::GARDEN_FOOTPRINT_TILES / 2;
    return core::GridPos{center_cell.x - half, center_cell.y - half};
}

[[nodiscard]] inline core::GridPos reservoir_anchor_from_center_cell(const core::GridPos center_cell)
{
    const int half = constants::RESERVOIR_FOOTPRINT_TILES / 2;
    return core::GridPos{center_cell.x - half, center_cell.y - half};
}

[[nodiscard]] inline core::GridPos farm_anchor_from_center_cell(const core::GridPos center_cell)
{
    const int half = constants::FARM_FOOTPRINT_TILES / 2;
    return core::GridPos{center_cell.x - half, center_cell.y - half};
}

[[nodiscard]] inline int placement_footprint_tiles(const CommandPanelMode mode)
{
    switch (mode) {
    case CommandPanelMode::PlaceHouse:
        return constants::HOUSE_FOOTPRINT_TILES;
    case CommandPanelMode::PlaceLumberCamp:
        return constants::LUMBER_CAMP_FOOTPRINT_TILES;
    case CommandPanelMode::PlaceExtractor:
        return constants::EXTRACTOR_FOOTPRINT_TILES;
    case CommandPanelMode::PlaceMill:
        return constants::MILL_FOOTPRINT_TILES;
    case CommandPanelMode::PlaceMiningCamp:
        return constants::MINING_CAMP_FOOTPRINT_TILES;
    case CommandPanelMode::PlaceBarracks:
        return constants::BARRACKS_FOOTPRINT_TILES;
    case CommandPanelMode::PlaceMageAcademy:
        return constants::MAGE_ACADEMY_FOOTPRINT_TILES;
    case CommandPanelMode::PlaceTower:
        return constants::TOWER_FOOTPRINT_TILES;
    case CommandPanelMode::PlaceMarket:
        return constants::MARKET_FOOTPRINT_TILES;
    case CommandPanelMode::PlaceGarden:
        return constants::GARDEN_FOOTPRINT_TILES;
    case CommandPanelMode::PlaceReservoir:
        return constants::RESERVOIR_FOOTPRINT_TILES;
    case CommandPanelMode::PlaceFarm:
        return constants::FARM_FOOTPRINT_TILES;
    case CommandPanelMode::PlaceTownCenter:
    default:
        return constants::TOWN_CENTER_FOOTPRINT_TILES;
    }
}

[[nodiscard]] inline core::GridPos placement_anchor_from_center_cell(
    const CommandPanelMode mode,
    const core::GridPos center_cell)
{
    switch (mode) {
    case CommandPanelMode::PlaceHouse:
        return house_anchor_from_center_cell(center_cell);
    case CommandPanelMode::PlaceLumberCamp:
        return lumber_camp_anchor_from_center_cell(center_cell);
    case CommandPanelMode::PlaceExtractor:
        return extractor_anchor_from_center_cell(center_cell);
    case CommandPanelMode::PlaceMill:
        return mill_anchor_from_center_cell(center_cell);
    case CommandPanelMode::PlaceMiningCamp:
        return mining_camp_anchor_from_center_cell(center_cell);
    case CommandPanelMode::PlaceBarracks:
        return barracks_anchor_from_center_cell(center_cell);
    case CommandPanelMode::PlaceMageAcademy:
        return mage_academy_anchor_from_center_cell(center_cell);
    case CommandPanelMode::PlaceTower:
        return tower_anchor_from_center_cell(center_cell);
    case CommandPanelMode::PlaceMarket:
        return market_anchor_from_center_cell(center_cell);
    case CommandPanelMode::PlaceGarden:
        return garden_anchor_from_center_cell(center_cell);
    case CommandPanelMode::PlaceReservoir:
        return reservoir_anchor_from_center_cell(center_cell);
    case CommandPanelMode::PlaceFarm:
        return farm_anchor_from_center_cell(center_cell);
    case CommandPanelMode::PlaceTownCenter:
    default:
        return town_center_anchor_from_center_cell(center_cell);
    }
}

[[nodiscard]] inline bool is_placement_command_mode(const CommandPanelMode mode)
{
    return mode == CommandPanelMode::PlaceTownCenter
        || mode == CommandPanelMode::PlaceHouse
        || mode == CommandPanelMode::PlaceLumberCamp
        || mode == CommandPanelMode::PlaceExtractor
        || mode == CommandPanelMode::PlaceMill
        || mode == CommandPanelMode::PlaceMiningCamp
        || mode == CommandPanelMode::PlaceBarracks
        || mode == CommandPanelMode::PlaceMageAcademy
        || mode == CommandPanelMode::PlaceTower
        || mode == CommandPanelMode::PlaceMarket
        || mode == CommandPanelMode::PlaceGarden
        || mode == CommandPanelMode::PlaceReservoir
        || mode == CommandPanelMode::PlaceFarm;
}

[[nodiscard]] inline CommandPanelMode build_tree_for_placement(const CommandPanelMode mode)
{
    switch (mode) {
    case CommandPanelMode::PlaceBarracks:
    case CommandPanelMode::PlaceMageAcademy:
    case CommandPanelMode::PlaceTower:
        return CommandPanelMode::BuildMilitaryMenu;
    default:
        return CommandPanelMode::BuildMenu;
    }
}

[[nodiscard]] inline bool is_build_tree_command_mode(const CommandPanelMode mode)
{
    return mode == CommandPanelMode::BuildMenu
        || mode == CommandPanelMode::BuildMilitaryMenu;
}

} // namespace aoa::app
