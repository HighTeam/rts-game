#include "app/game_input.hpp"

#include "app/command_panel.hpp"
#include "app/text_field_edit.hpp"
#include "core/constants.hpp"
#include "core/grid.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/building_process.hpp"
#include "sim/components/content_pack.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/fog_of_war.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/match_session.hpp"
#include "sim/components/match_session.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"
#include "sim/persistence/save_game.hpp"
#include "sim/player/player_command.hpp"
#include "sim/player/player_commands.hpp"
#include "sim/player/player_economy.hpp"
#include "sim/simulation.hpp"
#include "data/content_types.hpp"
#include "net/lockstep_session.hpp"
#include "net/net_constants.hpp"
#include "render/game_renderer.hpp"
#include "render/sim_render_snapshot.hpp"
#include "sim/systems/match_outcome.hpp"
#include "sim/systems/pathfinding.hpp"
#include "sim/systems/visibility_system.hpp"

#include <cctype>

#include "math/fixed.hpp"

#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace aoa::app {

namespace {

sim::player::PlayerCommand make_command(
    sim::Simulation& simulation,
    const sim::player::PlayerCommandType type,
    const std::vector<entt::entity>& units,
    const render::SimRenderSnapshot* render_snapshot)
{
    sim::player::PlayerCommand command{};
    if (render_snapshot != nullptr) {
        command.execute_tick = render_snapshot->tick_count
            + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
    }
    else {
        command.execute_tick = simulation.next_command_execute_tick();
    }
    command.type = type;
    command.unit_ids = units;
    if (render_snapshot != nullptr && !units.empty()) {
        command.unit_keys.reserve(units.size());
        for (const entt::entity unit : units) {
            bool found = false;
            for (const render::RenderEntityPose& pose : render_snapshot->units) {
                if (pose.entity != unit || !pose.snapshot_key.has_value()) {
                    continue;
                }

                command.unit_keys.push_back(*pose.snapshot_key);
                found = true;
                break;
            }

            if (!found) {
                command.unit_keys.clear();
                break;
            }
        }
    }
    return command;
}

void bind_building_target(
    sim::player::PlayerCommand& command,
    const entt::entity building,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot)
{
    command.target_entity = building;
    if (render_snapshot != nullptr) {
        for (const render::RenderEntityPose& pose : render_snapshot->buildings) {
            if (pose.entity != building) {
                continue;
            }

            command.cell = core::GridPos{pose.grid_x, pose.grid_y};
            command.target_entity_key = pose.snapshot_key;
            return;
        }
    }

    auto& registry = simulation.registry();
    if (!registry.valid(building) || !registry.any_of<sim::components::GridPosition>(building)) {
        return;
    }

    command.cell = registry.get<sim::components::GridPosition>(building).cell;
}

void fill_move_command_from_screen(
    sim::player::PlayerCommand& command,
    const render::GameRenderer& renderer,
    const core::GridPos goal_cell,
    const sf::Vector2f screen_position)
{
    command.cell = goal_cell;
    const auto [world_x, world_z] = renderer.screen_to_world_xz(screen_position.x, screen_position.y);
    command.has_goal_world = true;
    command.goal_world_x = math::Fixed::from_float(world_x);
    command.goal_world_y = math::Fixed::from_float(world_z);
}

} // namespace

void GameInput::play_order_ack_sfx(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    const sim::player::PlayerCommand& command) const
{
    if (game_audio_ == nullptr) {
        return;
    }

    using sim::player::PlayerCommandType;
    if (command.type == PlayerCommandType::Move) {
        game_audio_->play_random_move_ack();
        return;
    }

    if (command.type == PlayerCommandType::BuildTownCenter
        || command.type == PlayerCommandType::BuildHouse
        || command.type == PlayerCommandType::BuildLumberCamp
        || command.type == PlayerCommandType::BuildGarden
        || command.type == PlayerCommandType::BuildReservoir
        || command.type == PlayerCommandType::BuildFarm
        || command.type == PlayerCommandType::ResumeBuild
        || command.type == PlayerCommandType::RenewFarm) {
        game_audio_->play_sfx(audio::SfxId::Building);
        return;
    }

    if (command.type != PlayerCommandType::Gather) {
        return;
    }

    if (!selection_has_worker(simulation, render_snapshot)) {
        return;
    }

    sim::components::TileType tile = sim::components::TileType::Grass;
    if (render_snapshot != nullptr
        && core::is_inside_grid(
            command.cell,
            render_snapshot->map_width,
            render_snapshot->map_height)) {
        const int index = core::grid_index(command.cell, render_snapshot->map_width);
        if (static_cast<std::size_t>(index) < render_snapshot->tiles.size()) {
            tile = render_snapshot->tiles[static_cast<std::size_t>(index)];
        }
    }
    else {
        auto& registry = simulation.registry();
        const auto world_view =
            registry.view<sim::components::WorldTag, sim::components::MapGrid>();
        if (world_view.begin() != world_view.end()) {
            const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
            if (core::is_inside_grid(command.cell, map.width, map.height)) {
                tile = map.tiles[static_cast<std::size_t>(
                    core::grid_index(command.cell, map.width))];
            }
        }
    }

    if (tile == sim::components::TileType::Forest) {
        game_audio_->play_sfx(audio::SfxId::Chopping);
        return;
    }

    if (tile == sim::components::TileType::GoldMine) {
        game_audio_->play_sfx(audio::SfxId::Mining);
        return;
    }

    if (tile == sim::components::TileType::Berries
        || tile == sim::components::TileType::Blueberries) {
        game_audio_->play_sfx(audio::SfxId::Gathering);
    }
}

void GameInput::play_select_ack_if_own_units(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot) const
{
    if (game_audio_ == nullptr || selection_.units.empty()) {
        return;
    }

    if (!selection_has_worker(simulation, render_snapshot)
        && !selection_has_militia(simulation, render_snapshot)
        && !selection_has_mage(simulation, render_snapshot)) {
        return;
    }

    game_audio_->play_random_select_ack();
}

bool GameInput::submit_player_command(
    sim::Simulation& simulation,
    sim::player::PlayerCommand command)
{
    if (local_is_spectator_) {
        return false;
    }

    command.player_slot = local_player_slot_;
    play_order_ack_sfx(simulation, nullptr, command);

    if (lockstep_session_ != nullptr) {
        return lockstep_session_->submit_local_command(std::move(command));
    }

    simulation.enqueue_player_command(std::move(command));
    return true;
}

bool GameInput::submit_map_ping(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    const core::GridPos cell)
{
    int map_width = 0;
    int map_height = 0;
    if (render_snapshot != nullptr) {
        map_width = render_snapshot->map_width;
        map_height = render_snapshot->map_height;
    }
    else {
        const auto world_view = simulation.registry()
            .view<sim::components::WorldTag, sim::components::MapGrid>();
        if (world_view.begin() == world_view.end()) {
            return false;
        }

        const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
        map_width = map.width;
        map_height = map.height;
    }

    if (!core::is_inside_grid(cell, map_width, map_height)) {
        return false;
    }

    sim::player::PlayerCommand command = make_command(
        simulation,
        sim::player::PlayerCommandType::MapPing,
        {},
        render_snapshot);
    command.cell = cell;
    return submit_player_command(simulation, std::move(command));
}

bool GameInput::selection_has_worker(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot) const
{
    if (selection_.units.empty()) {
        return false;
    }

    if (render_snapshot != nullptr) {
        for (const entt::entity entity : selection_.units) {
            for (const render::RenderEntityPose& pose : render_snapshot->units) {
                if (pose.entity == entity && pose.is_worker && pose.health_current > 0
                    && pose.player_slot == local_player_slot_) {
                    return true;
                }
            }
        }

        return false;
    }

    auto& registry = simulation.registry();
    for (const entt::entity entity : selection_.units) {
        if (!registry.valid(entity)) {
            continue;
        }

        if (!registry.any_of<sim::components::WorkerUnitTag>(entity)) {
            continue;
        }

        if (sim::components::entity_player_slot(registry, entity) != local_player_slot_) {
            continue;
        }

        return true;
    }

    return false;
}

bool GameInput::selection_has_militia(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot) const
{
    if (selection_.units.empty()) {
        return false;
    }

    if (render_snapshot != nullptr) {
        for (const entt::entity entity : selection_.units) {
            for (const render::RenderEntityPose& pose : render_snapshot->units) {
                if (pose.entity == entity && pose.is_militia && pose.health_current > 0
                    && pose.player_slot == local_player_slot_) {
                    return true;
                }
            }
        }

        return false;
    }

    auto& registry = simulation.registry();
    for (const entt::entity entity : selection_.units) {
        if (!registry.valid(entity)) {
            continue;
        }

        if (!registry.any_of<sim::components::MilitiaUnitTag>(entity)) {
            continue;
        }

        if (sim::components::entity_player_slot(registry, entity) != local_player_slot_) {
            continue;
        }

        return true;
    }

    return false;
}

bool GameInput::selection_has_mage(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot) const
{
    if (selection_.units.empty()) {
        return false;
    }

    if (render_snapshot != nullptr) {
        for (const entt::entity entity : selection_.units) {
            for (const render::RenderEntityPose& pose : render_snapshot->units) {
                if (pose.entity == entity && pose.is_mage && pose.health_current > 0
                    && pose.player_slot == local_player_slot_) {
                    return true;
                }
            }
        }

        return false;
    }

    auto& registry = simulation.registry();
    for (const entt::entity entity : selection_.units) {
        if (!registry.valid(entity)) {
            continue;
        }

        if (!registry.any_of<sim::components::MageUnitTag>(entity)) {
            continue;
        }

        if (sim::components::entity_player_slot(registry, entity) != local_player_slot_) {
            continue;
        }

        return true;
    }

    return false;
}

bool GameInput::try_issue_garrison_on_building(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    const entt::entity building)
{
    if (building == entt::null || selection_.units.empty()) {
        return false;
    }

    bool is_town_center = false;
    bool under_construction = false;
    if (render_snapshot != nullptr) {
        for (const render::RenderEntityPose& pose : render_snapshot->buildings) {
            if (pose.entity != building) {
                continue;
            }

            is_town_center = pose.is_town_center;
            under_construction = pose.under_construction;
            break;
        }
    }
    else {
        auto& registry = simulation.registry();
        if (!registry.valid(building)) {
            return false;
        }

        is_town_center = registry.any_of<sim::components::TownCenterTag>(building);
        under_construction = registry.any_of<sim::components::UnderConstructionTag>(building);
    }

    if (!is_town_center || under_construction) {
        return false;
    }

    sim::player::PlayerCommand command = make_command(
        simulation,
        sim::player::PlayerCommandType::Garrison,
        selection_.units,
        render_snapshot);
    bind_building_target(command, building, simulation, render_snapshot);
    submit_player_command(simulation, std::move(command));
    garrison_targeting_mode_ = false;
    return true;
}

CommandPanelBuildOptions GameInput::current_build_options(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot) const
{
    CommandPanelBuildOptions options{};
    options.town_center_wood_cost = constants::TOWN_CENTER_BUILD_WOOD_COST;
    options.house_wood_cost = constants::HOUSE_BUILD_WOOD_COST;
    options.lumber_camp_wood_cost = constants::LUMBER_CAMP_BUILD_WOOD_COST;
    options.worker_food_cost = constants::WORKER_FOOD_COST;
    options.militia_food_cost = constants::MILITIA_FOOD_COST;
    options.militia_money_cost = constants::MILITIA_MONEY_COST;

    auto& registry = simulation.registry();
    entt::entity world = entt::null;
    {
        const auto world_view = registry.view<sim::components::WorldTag>();
        if (world_view.begin() != world_view.end()) {
            world = *world_view.begin();
        }
    }

    if (world != entt::null && registry.any_of<sim::components::MatchSession>(world)) {
        const auto& session = registry.get<sim::components::MatchSession>(world);
        const sim::components::AgeAdvanceCost age_cost = sim::components::age_advance_cost(
            sim::components::player_age(session, local_player_slot_));
        options.can_advance_age = age_cost.can_advance;
        options.next_age_name = age_cost.next_name;
        options.age_food_cost = age_cost.food;
        options.age_money_cost = age_cost.money;
        options.age_mana_cost = age_cost.mana;
        options.unlocked_elemental_buildings = sim::components::slot_age_at_least(
            session, local_player_slot_, constants::PlayerAge::Magic);
        options.unlocked_garden = sim::components::slot_age_at_least(
            session, local_player_slot_, constants::PlayerAge::Technology);
        options.unlocked_spy = sim::components::slot_age_at_least(
            session, local_player_slot_, constants::PlayerAge::Spirit);
        options.unlocked_farm = sim::components::slot_has_built_mill(session, local_player_slot_)
            || sim::player::player_has_completed_mill(registry, local_player_slot_);
        options.has_cartography = sim::components::slot_has_cartography(session, local_player_slot_);
        options.has_trades = sim::components::slot_has_trades(session, local_player_slot_);
        options.has_spy = sim::components::slot_has_spy(session, local_player_slot_);
        options.spy_money_cost = sim::player::count_enemy_units(registry, local_player_slot_)
            * constants::SPY_GOLD_PER_ENEMY_UNIT;
    }

    options.garden_limit = constants::GARDEN_MAX_PER_PLAYER;
    options.garden_count = sim::player::count_living_player_gardens(registry, local_player_slot_);
    options.garden_at_limit = options.garden_count >= options.garden_limit;

    if (world != entt::null && registry.any_of<sim::components::ContentPack>(world)) {
        const auto& content_pack = registry.get<sim::components::ContentPack>(world);
        const data::ArchetypeDefinition* town_center =
            data::find_archetype(content_pack.content, "town_center");
        if (town_center != nullptr && town_center->build_wood_cost > 0) {
            options.town_center_wood_cost = town_center->build_wood_cost;
        }
        if (town_center != nullptr && town_center->spawn_worker_food_cost > 0) {
            options.worker_food_cost = town_center->spawn_worker_food_cost;
        }
        if (town_center != nullptr && town_center->spawn_militia_food_cost > 0) {
            options.militia_food_cost = town_center->spawn_militia_food_cost;
        }
        if (town_center != nullptr && town_center->spawn_militia_money_cost > 0) {
            options.militia_money_cost = town_center->spawn_militia_money_cost;
        }

        const data::ArchetypeDefinition* house =
            data::find_archetype(content_pack.content, std::string(constants::HOUSE_BUILDING_ID));
        if (house != nullptr && house->build_wood_cost > 0) {
            options.house_wood_cost = house->build_wood_cost;
        }

        const data::ArchetypeDefinition* lumberjack = data::find_archetype(
            content_pack.content,
            std::string(constants::LUMBER_CAMP_BUILDING_ID));
        if (lumberjack != nullptr && lumberjack->build_wood_cost > 0) {
            options.lumber_camp_wood_cost = lumberjack->build_wood_cost;
        }

        const auto load_build_cost = [&](const std::string_view id, int& wood, int* money, int* mana) {
            const data::ArchetypeDefinition* archetype =
                data::find_archetype(content_pack.content, std::string(id));
            if (archetype == nullptr) {
                return;
            }

            if (archetype->build_wood_cost > 0) {
                wood = archetype->build_wood_cost;
            }
            if (money != nullptr && archetype->build_money_cost > 0) {
                *money = archetype->build_money_cost;
            }
            if (mana != nullptr && archetype->build_mana_cost > 0) {
                *mana = archetype->build_mana_cost;
            }
        };
        if (town_center != nullptr) {
            if (town_center->build_money_cost > 0) {
                options.town_center_money_cost = town_center->build_money_cost;
            }
            if (town_center->build_mana_cost > 0) {
                options.town_center_mana_cost = town_center->build_mana_cost;
            }
        }
        if (sim::player::player_town_center_gold_mana_waived(registry, local_player_slot_)) {
            options.town_center_money_cost = 0;
            options.town_center_mana_cost = 0;
        }
        load_build_cost(constants::MILL_BUILDING_ID, options.mill_wood_cost, nullptr, nullptr);
        load_build_cost(
            constants::MINING_CAMP_BUILDING_ID, options.mining_camp_wood_cost, nullptr, nullptr);
        load_build_cost(constants::BARRACKS_BUILDING_ID, options.barracks_wood_cost, nullptr, nullptr);
        load_build_cost(
            constants::MAGE_ACADEMY_BUILDING_ID,
            options.mage_academy_wood_cost,
            &options.mage_academy_money_cost,
            &options.mage_academy_mana_cost);
        load_build_cost(
            constants::TOWER_BUILDING_ID,
            options.tower_wood_cost,
            &options.tower_money_cost,
            nullptr);
        load_build_cost(constants::MARKET_BUILDING_ID, options.market_wood_cost, nullptr, nullptr);
        load_build_cost(
            constants::GARDEN_BUILDING_ID,
            options.garden_wood_cost,
            &options.garden_money_cost,
            &options.garden_mana_cost);
        load_build_cost(
            constants::RESERVOIR_BUILDING_ID,
            options.reservoir_wood_cost,
            &options.reservoir_money_cost,
            nullptr);
        load_build_cost(constants::FARM_BUILDING_ID, options.farm_wood_cost, nullptr, nullptr);
        const data::ArchetypeDefinition* barracks =
            data::find_archetype(content_pack.content, std::string(constants::BARRACKS_BUILDING_ID));
        if (barracks != nullptr && barracks->spawn_militia_food_cost > 0) {
            options.militia_food_cost = barracks->spawn_militia_food_cost;
        }
        if (barracks != nullptr && barracks->spawn_militia_money_cost > 0) {
            options.militia_money_cost = barracks->spawn_militia_money_cost;
        }
        const data::ArchetypeDefinition* academy = data::find_archetype(
            content_pack.content, std::string(constants::MAGE_ACADEMY_BUILDING_ID));
        if (academy != nullptr && academy->spawn_mage_money_cost > 0) {
            options.mage_money_cost = academy->spawn_mage_money_cost;
        }
        if (academy != nullptr && academy->spawn_mage_mana_cost > 0) {
            options.mage_mana_cost = academy->spawn_mage_mana_cost;
        }
    }

    if (render_snapshot != nullptr
        && local_player_slot_ < render_snapshot->hud_by_player.size()) {
        const int town_wood = render_snapshot->hud_by_player[local_player_slot_].town_wood;
        const int town_food = render_snapshot->hud_by_player[local_player_slot_].town_food;
        const int town_money = render_snapshot->hud_by_player[local_player_slot_].town_money;
        const int town_mana = render_snapshot->hud_by_player[local_player_slot_].town_mana;
        options.can_afford_town_center = town_wood >= options.town_center_wood_cost
            && town_money >= options.town_center_money_cost
            && town_mana >= options.town_center_mana_cost;
        options.can_afford_house = town_wood >= options.house_wood_cost;
        options.can_afford_lumber_camp = town_wood >= options.lumber_camp_wood_cost;
        options.can_afford_extractor = town_wood >= options.extractor_wood_cost
            && town_money >= options.extractor_money_cost;
        options.can_afford_mill = town_wood >= options.mill_wood_cost;
        options.can_afford_mining_camp = town_wood >= options.mining_camp_wood_cost;
        options.can_afford_barracks = town_wood >= options.barracks_wood_cost;
        options.can_afford_mage_academy = town_wood >= options.mage_academy_wood_cost
            && town_money >= options.mage_academy_money_cost
            && town_mana >= options.mage_academy_mana_cost;
        options.can_afford_tower = town_wood >= options.tower_wood_cost
            && town_money >= options.tower_money_cost;
        options.can_afford_market = town_wood >= options.market_wood_cost;
        options.can_afford_garden = town_wood >= options.garden_wood_cost
            && town_money >= options.garden_money_cost
            && town_mana >= options.garden_mana_cost;
        options.can_afford_reservoir = town_wood >= options.reservoir_wood_cost
            && town_money >= options.reservoir_money_cost;
        options.can_afford_farm = town_wood >= options.farm_wood_cost;
        options.can_afford_worker = town_food >= options.worker_food_cost;
        options.can_afford_militia = town_food >= options.militia_food_cost
            && town_money >= options.militia_money_cost;
        options.can_afford_mage = town_money >= options.mage_money_cost
            && town_mana >= options.mage_mana_cost;
        options.can_afford_age = options.can_advance_age
            && town_food >= options.age_food_cost
            && town_money >= options.age_money_cost
            && town_mana >= options.age_mana_cost;
        options.can_afford_cartography = !options.has_cartography
            && town_money >= options.cartography_money_cost;
        options.can_afford_trades = !options.has_trades
            && town_money >= options.trades_money_cost;
        options.can_afford_spy = !options.has_spy && town_money >= options.spy_money_cost;
        options.can_sell_wood = town_wood >= options.market_trade_amount;
        options.can_sell_food = town_food >= options.market_trade_amount;
        options.can_buy_wood = town_money >= options.market_buy_gold;
        options.can_buy_food = town_money >= options.market_buy_gold;
        if (world != entt::null && registry.any_of<sim::components::MatchSession>(world)) {
            options.can_afford_cartography = !options.has_cartography
                && town_money >= options.cartography_money_cost;
            options.can_afford_trades = !options.has_trades
                && town_money >= options.trades_money_cost;
            options.can_afford_spy = !options.has_spy && town_money >= options.spy_money_cost;
        }
        if (selection_.building != entt::null) {
            for (const render::RenderEntityPose& pose : render_snapshot->buildings) {
                if (pose.entity == selection_.building) {
                    options.building_busy = pose.has_process;
                    break;
                }
            }
        }
        return options;
    }

    options.can_afford_town_center = sim::player::can_afford_player_wood(
                                          registry,
                                          local_player_slot_,
                                          options.town_center_wood_cost)
        && sim::player::can_afford_player_money(
            registry, local_player_slot_, options.town_center_money_cost)
        && sim::player::can_afford_player_mana(
            registry, local_player_slot_, options.town_center_mana_cost);
    options.can_afford_house = sim::player::can_afford_player_wood(
        registry,
        local_player_slot_,
        options.house_wood_cost);
    options.can_afford_lumber_camp = sim::player::can_afford_player_wood(
        registry,
        local_player_slot_,
        options.lumber_camp_wood_cost);
    options.can_afford_extractor = sim::player::can_afford_player_wood(
                                       registry,
                                       local_player_slot_,
                                       options.extractor_wood_cost)
        && sim::player::can_afford_player_money(
                                       registry,
                                       local_player_slot_,
                                       options.extractor_money_cost);
    options.can_afford_worker = sim::player::can_afford_player_food(
        registry,
        local_player_slot_,
        options.worker_food_cost);
    options.can_afford_militia = sim::player::can_afford_player_food(
                                      registry,
                                      local_player_slot_,
                                      options.militia_food_cost)
        && sim::player::can_afford_player_money(
               registry,
               local_player_slot_,
               options.militia_money_cost);
    options.can_afford_mill = sim::player::can_afford_player_wood(
        registry, local_player_slot_, options.mill_wood_cost);
    options.can_afford_mining_camp = sim::player::can_afford_player_wood(
        registry, local_player_slot_, options.mining_camp_wood_cost);
    options.can_afford_barracks = sim::player::can_afford_player_wood(
        registry, local_player_slot_, options.barracks_wood_cost);
    options.can_afford_mage_academy = sim::player::can_afford_player_wood(
                                           registry, local_player_slot_, options.mage_academy_wood_cost)
        && sim::player::can_afford_player_money(
            registry, local_player_slot_, options.mage_academy_money_cost)
        && sim::player::can_afford_player_mana(
            registry, local_player_slot_, options.mage_academy_mana_cost);
    options.can_afford_tower = sim::player::can_afford_player_wood(
                                    registry, local_player_slot_, options.tower_wood_cost)
        && sim::player::can_afford_player_money(
            registry, local_player_slot_, options.tower_money_cost);
    options.can_afford_market = sim::player::can_afford_player_wood(
        registry, local_player_slot_, options.market_wood_cost);
    options.can_afford_garden = sim::player::can_afford_player_wood(
                                     registry, local_player_slot_, options.garden_wood_cost)
        && sim::player::can_afford_player_money(
            registry, local_player_slot_, options.garden_money_cost)
        && sim::player::can_afford_player_mana(
            registry, local_player_slot_, options.garden_mana_cost);
    options.can_afford_reservoir = sim::player::can_afford_player_wood(
                                        registry, local_player_slot_, options.reservoir_wood_cost)
        && sim::player::can_afford_player_money(
            registry, local_player_slot_, options.reservoir_money_cost);
    options.can_afford_farm = sim::player::can_afford_player_wood(
        registry, local_player_slot_, options.farm_wood_cost);
    options.can_afford_mage = sim::player::can_afford_player_money(
                                   registry, local_player_slot_, options.mage_money_cost)
        && sim::player::can_afford_player_mana(
            registry, local_player_slot_, options.mage_mana_cost);
    options.can_afford_age = options.can_advance_age
        && sim::player::can_afford_player_food(registry, local_player_slot_, options.age_food_cost)
        && sim::player::can_afford_player_money(registry, local_player_slot_, options.age_money_cost)
        && sim::player::can_afford_player_mana(registry, local_player_slot_, options.age_mana_cost);
    options.can_afford_cartography = !options.has_cartography
        && sim::player::can_afford_player_money(
            registry, local_player_slot_, options.cartography_money_cost);
    options.can_afford_trades = !options.has_trades
        && sim::player::can_afford_player_money(
            registry, local_player_slot_, options.trades_money_cost);
    options.can_afford_spy = !options.has_spy
        && sim::player::can_afford_player_money(
            registry, local_player_slot_, options.spy_money_cost);
    options.can_sell_wood = sim::player::can_afford_player_wood(
        registry, local_player_slot_, options.market_trade_amount);
    options.can_sell_food = sim::player::can_afford_player_food(
        registry, local_player_slot_, options.market_trade_amount);
    options.can_buy_wood = sim::player::can_afford_player_money(
        registry, local_player_slot_, options.market_buy_gold);
    options.can_buy_food = sim::player::can_afford_player_money(
        registry, local_player_slot_, options.market_buy_gold);
    if (selection_.building != entt::null) {
        options.building_busy =
            sim::components::building_has_active_process(registry, selection_.building);
    }
    return options;
}

void GameInput::sync_command_panel_mode(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot)
{
    if (is_build_tree_command_mode(command_panel_mode_)
        || is_placement_command_mode(command_panel_mode_)) {
        if (!selection_has_worker(simulation, render_snapshot)) {
            command_panel_mode_ = CommandPanelMode::Empty;
            placement_ghost_anchor_.reset();
            placement_ghost_valid_ = false;
            attack_targeting_mode_ = false;
        }
        return;
    }

    if (selection_has_worker(simulation, render_snapshot)) {
        command_panel_mode_ = CommandPanelMode::WorkerActions;
        return;
    }

    if (selection_has_militia(simulation, render_snapshot)) {
        command_panel_mode_ = CommandPanelMode::MilitiaActions;
        return;
    }

    if (selection_has_mage(simulation, render_snapshot)) {
        command_panel_mode_ = CommandPanelMode::MageActions;
        return;
    }

    if (selection_.building != entt::null) {
        bool under_construction = false;
        bool is_town_center = false;
        bool is_house = false;
        bool is_lumber_camp = false;
        bool is_mill = false;
        bool is_mining_camp = false;
        bool is_barracks = false;
        bool is_mage_academy = false;
        bool is_tower = false;
        bool is_market = false;
        bool is_extractor = false;
        bool is_garden = false;
        bool is_reservoir = false;
        bool is_farm = false;
        bool is_mana_lake = false;
        if (render_snapshot != nullptr) {
            for (const render::RenderEntityPose& pose : render_snapshot->buildings) {
                if (pose.entity != selection_.building) {
                    continue;
                }

                under_construction = pose.under_construction;
                is_town_center = pose.is_town_center;
                is_house = pose.is_house;
                is_lumber_camp = pose.is_lumber_camp;
                is_mill = pose.is_mill;
                is_mining_camp = pose.is_mining_camp;
                is_barracks = pose.is_barracks;
                is_mage_academy = pose.is_mage_academy;
                is_tower = pose.is_tower;
                is_market = pose.is_market;
                is_extractor = pose.is_extractor;
                is_garden = pose.is_garden;
                is_reservoir = pose.is_reservoir;
                is_farm = pose.is_farm;
                is_mana_lake = pose.is_mana_lake;
                break;
            }
        }
        else {
            auto& registry = simulation.registry();
            if (registry.valid(selection_.building)) {
                under_construction =
                    registry.any_of<sim::components::UnderConstructionTag>(selection_.building);
                is_town_center = registry.any_of<sim::components::TownCenterTag>(selection_.building);
                is_house = registry.any_of<sim::components::HouseTag>(selection_.building);
                is_lumber_camp = registry.any_of<sim::components::LumberCampTag>(selection_.building);
                is_mill = registry.any_of<sim::components::MillTag>(selection_.building);
                is_mining_camp = registry.any_of<sim::components::MiningCampTag>(selection_.building);
                is_barracks = registry.any_of<sim::components::BarracksTag>(selection_.building);
                is_mage_academy = registry.any_of<sim::components::MageAcademyTag>(selection_.building);
                is_tower = registry.any_of<sim::components::TowerTag>(selection_.building);
                is_market = registry.any_of<sim::components::MarketTag>(selection_.building);
                is_extractor = registry.any_of<sim::components::ExtractorTag>(selection_.building);
                is_garden = registry.any_of<sim::components::GardenTag>(selection_.building);
                is_reservoir = registry.any_of<sim::components::ReservoirTag>(selection_.building);
                is_farm = registry.any_of<sim::components::FarmTag>(selection_.building);
                is_mana_lake = registry.any_of<sim::components::ManaLakeTag>(selection_.building);
            }
        }

        // Lakes have no owner, so they short-circuit the ownership gate below.
        if (is_mana_lake) {
            command_panel_mode_ = CommandPanelMode::ManaLakeInfo;
            return;
        }

        std::uint8_t building_slot = local_player_slot_;
        if (render_snapshot != nullptr) {
            for (const render::RenderEntityPose& pose : render_snapshot->buildings) {
                if (pose.entity == selection_.building) {
                    building_slot = pose.player_slot;
                    break;
                }
            }
        }
        else if (simulation.registry().valid(selection_.building)) {
            building_slot =
                sim::components::entity_player_slot(simulation.registry(), selection_.building);
        }

        if (building_slot == local_player_slot_) {
            if (under_construction
                && (is_town_center || is_house || is_lumber_camp || is_mill || is_mining_camp
                    || is_barracks || is_mage_academy || is_tower || is_market || is_extractor
                    || is_garden || is_reservoir || is_farm)) {
                // Same Deselect/Destroy set as finished House.
                command_panel_mode_ = CommandPanelMode::HouseActions;
                return;
            }

            if (is_town_center && !under_construction) {
                command_panel_mode_ = CommandPanelMode::TownCenterActions;
                return;
            }

            if (is_house && !under_construction) {
                command_panel_mode_ = CommandPanelMode::HouseActions;
                return;
            }

            if (is_lumber_camp && !under_construction) {
                command_panel_mode_ = CommandPanelMode::LumberCampActions;
                return;
            }

            if (is_extractor && !under_construction) {
                command_panel_mode_ = CommandPanelMode::ExtractorActions;
                return;
            }

            if (is_mill && !under_construction) {
                command_panel_mode_ = CommandPanelMode::MillActions;
                return;
            }

            if (is_mining_camp && !under_construction) {
                command_panel_mode_ = CommandPanelMode::MiningCampActions;
                return;
            }

            if (is_barracks && !under_construction) {
                command_panel_mode_ = CommandPanelMode::BarracksActions;
                return;
            }

            if (is_mage_academy && !under_construction) {
                command_panel_mode_ = CommandPanelMode::MageAcademyActions;
                return;
            }

            if (is_tower && !under_construction) {
                command_panel_mode_ = CommandPanelMode::TowerActions;
                return;
            }

            if (is_market && !under_construction) {
                command_panel_mode_ = CommandPanelMode::MarketActions;
                return;
            }

            if (is_garden && !under_construction) {
                command_panel_mode_ = CommandPanelMode::GardenActions;
                return;
            }

            if (is_reservoir && !under_construction) {
                command_panel_mode_ = CommandPanelMode::ReservoirActions;
                return;
            }

            if (is_farm && !under_construction) {
                command_panel_mode_ = CommandPanelMode::FarmActions;
                return;
            }
        }
    }

    command_panel_mode_ = CommandPanelMode::Empty;
}

bool GameInput::apply_command_panel_action(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    const CommandPanelAction action)
{
    if (action != CommandPanelAction::None && game_audio_ != nullptr) {
        game_audio_->play_wooden_click();
    }
    if (action != CommandPanelAction::Garrison) {
        garrison_targeting_mode_ = false;
    }

    switch (action) {
    case CommandPanelAction::Build:
        command_panel_mode_ = CommandPanelMode::BuildMenu;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::OpenMilitaryBuild:
        command_panel_mode_ = CommandPanelMode::BuildMilitaryMenu;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::Back:
        command_panel_mode_ = CommandPanelMode::WorkerActions;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::Attack:
        attack_targeting_mode_ = true;
        return true;
    case CommandPanelAction::Stop: {
        if (selection_.units.empty()) {
            return true;
        }

        sim::player::PlayerCommand command = make_command(
            simulation,
            sim::player::PlayerCommandType::Stop,
            selection_.units,
            render_snapshot);
        submit_player_command(simulation, std::move(command));
        attack_targeting_mode_ = false;
        return true;
    }
    case CommandPanelAction::Deselect:
        if (is_placement_command_mode(command_panel_mode_)) {
            command_panel_mode_ = selection_has_worker(simulation, render_snapshot)
                ? build_tree_for_placement(command_panel_mode_)
                : CommandPanelMode::Empty;
            return true;
        }
        clear_selection();
        return true;
    case CommandPanelAction::Kill: {
        if (selection_.units.empty()) {
            return true;
        }

        sim::player::PlayerCommand command = make_command(
            simulation,
            sim::player::PlayerCommandType::KillUnits,
            selection_.units,
            render_snapshot);
        if (submit_player_command(simulation, std::move(command))) {
            clear_selection();
        }
        return true;
    }
    case CommandPanelAction::BuildTownCenter:
        if (!current_build_options(simulation, render_snapshot).can_afford_town_center) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceTownCenter;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::BuildHouse:
        if (!current_build_options(simulation, render_snapshot).can_afford_house) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceHouse;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::BuildLumberCamp:
        if (!current_build_options(simulation, render_snapshot).can_afford_lumber_camp) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceLumberCamp;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::BuildExtractor:
        if (!current_build_options(simulation, render_snapshot).unlocked_elemental_buildings
            || !current_build_options(simulation, render_snapshot).can_afford_extractor) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceExtractor;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::BuildMill:
        if (!current_build_options(simulation, render_snapshot).can_afford_mill) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceMill;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::BuildMiningCamp:
        if (!current_build_options(simulation, render_snapshot).can_afford_mining_camp) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceMiningCamp;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::BuildBarracks:
        if (!current_build_options(simulation, render_snapshot).can_afford_barracks) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceBarracks;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::BuildMageAcademy:
        if (!current_build_options(simulation, render_snapshot).unlocked_elemental_buildings
            || !current_build_options(simulation, render_snapshot).can_afford_mage_academy) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceMageAcademy;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::BuildTower:
        if (!current_build_options(simulation, render_snapshot).unlocked_elemental_buildings
            || !current_build_options(simulation, render_snapshot).can_afford_tower) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceTower;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::BuildMarket:
        if (!current_build_options(simulation, render_snapshot).unlocked_elemental_buildings
            || !current_build_options(simulation, render_snapshot).can_afford_market) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceMarket;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::BuildGarden:
        if (!current_build_options(simulation, render_snapshot).unlocked_garden
            || current_build_options(simulation, render_snapshot).garden_at_limit
            || !current_build_options(simulation, render_snapshot).can_afford_garden) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceGarden;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::BuildReservoir:
        if (!current_build_options(simulation, render_snapshot).unlocked_elemental_buildings
            || !current_build_options(simulation, render_snapshot).can_afford_reservoir) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceReservoir;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::BuildFarm:
        if (!current_build_options(simulation, render_snapshot).unlocked_farm
            || !current_build_options(simulation, render_snapshot).can_afford_farm) {
            return true;
        }
        command_panel_mode_ = CommandPanelMode::PlaceFarm;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::Garrison:
        garrison_targeting_mode_ = true;
        attack_targeting_mode_ = false;
        return true;
    case CommandPanelAction::AdvanceAge:
        if (selection_.building != entt::null) {
            sim::player::PlayerCommand command{};
            if (render_snapshot != nullptr) {
                command.execute_tick = render_snapshot->tick_count
                    + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
            }
            else {
                command.execute_tick = simulation.next_command_execute_tick();
            }
            command.type = sim::player::PlayerCommandType::AdvanceAge;
            bind_building_target(command, selection_.building, simulation, render_snapshot);
            submit_player_command(simulation, std::move(command));
        }
        return true;
    case CommandPanelAction::UnloadGarrison:
        if (selection_.building != entt::null) {
            sim::player::PlayerCommand command{};
            if (render_snapshot != nullptr) {
                command.execute_tick = render_snapshot->tick_count
                    + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
            }
            else {
                command.execute_tick = simulation.next_command_execute_tick();
            }
            command.type = sim::player::PlayerCommandType::UnloadGarrison;
            bind_building_target(command, selection_.building, simulation, render_snapshot);
            submit_player_command(simulation, std::move(command));
        }
        return true;
    case CommandPanelAction::SpawnMage:
        if (selection_.building != entt::null) {
            sim::player::PlayerCommand command{};
            if (render_snapshot != nullptr) {
                command.execute_tick = render_snapshot->tick_count
                    + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
            }
            else {
                command.execute_tick = simulation.next_command_execute_tick();
            }
            command.type = sim::player::PlayerCommandType::SpawnMage;
            bind_building_target(command, selection_.building, simulation, render_snapshot);
            submit_player_command(simulation, std::move(command));
        }
        return true;
    case CommandPanelAction::SpawnWorker:
        if (selection_.building != entt::null) {
            sim::player::PlayerCommand command{};
            if (render_snapshot != nullptr) {
                command.execute_tick = render_snapshot->tick_count
                    + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
            }
            else {
                command.execute_tick = simulation.next_command_execute_tick();
            }
            command.type = sim::player::PlayerCommandType::SpawnWorker;
            bind_building_target(command, selection_.building, simulation, render_snapshot);
            submit_player_command(simulation, std::move(command));
        }
        return true;
    case CommandPanelAction::SpawnMilitia:
        if (selection_.building != entt::null) {
            sim::player::PlayerCommand command{};
            if (render_snapshot != nullptr) {
                command.execute_tick = render_snapshot->tick_count
                    + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
            }
            else {
                command.execute_tick = simulation.next_command_execute_tick();
            }
            command.type = sim::player::PlayerCommandType::SpawnMilitia;
            bind_building_target(command, selection_.building, simulation, render_snapshot);
            submit_player_command(simulation, std::move(command));
        }
        return true;
    case CommandPanelAction::Destroy:
        if (selection_.building != entt::null) {
            sim::player::PlayerCommand command{};
            if (render_snapshot != nullptr) {
                command.execute_tick = render_snapshot->tick_count
                    + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
            }
            else {
                command.execute_tick = simulation.next_command_execute_tick();
            }
            command.type = sim::player::PlayerCommandType::DestroyBuilding;
            bind_building_target(command, selection_.building, simulation, render_snapshot);
            if (submit_player_command(simulation, std::move(command))) {
                clear_selection();
            }
        }
        return true;
    case CommandPanelAction::ResearchCartography:
    case CommandPanelAction::ResearchTrades:
    case CommandPanelAction::ResearchSpy:
    case CommandPanelAction::MarketSellWood:
    case CommandPanelAction::MarketSellFood:
    case CommandPanelAction::MarketBuyWood:
    case CommandPanelAction::MarketBuyFood:
        if (selection_.building != entt::null) {
            sim::player::PlayerCommand command{};
            if (render_snapshot != nullptr) {
                command.execute_tick = render_snapshot->tick_count
                    + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
            }
            else {
                command.execute_tick = simulation.next_command_execute_tick();
            }
            if (action == CommandPanelAction::ResearchCartography) {
                command.type = sim::player::PlayerCommandType::ResearchCartography;
            }
            else if (action == CommandPanelAction::ResearchTrades) {
                command.type = sim::player::PlayerCommandType::ResearchTrades;
            }
            else if (action == CommandPanelAction::ResearchSpy) {
                command.type = sim::player::PlayerCommandType::ResearchSpy;
            }
            else if (action == CommandPanelAction::MarketSellWood) {
                command.type = sim::player::PlayerCommandType::MarketSellWood;
            }
            else if (action == CommandPanelAction::MarketSellFood) {
                command.type = sim::player::PlayerCommandType::MarketSellFood;
            }
            else if (action == CommandPanelAction::MarketBuyWood) {
                command.type = sim::player::PlayerCommandType::MarketBuyWood;
            }
            else {
                command.type = sim::player::PlayerCommandType::MarketBuyFood;
            }
            bind_building_target(command, selection_.building, simulation, render_snapshot);
            submit_player_command(simulation, std::move(command));
        }
        return true;
    case CommandPanelAction::None:
        break;
    }

    return false;
}

bool GameInput::handle_command_panel_click(
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    const sf::Vector2f screen_position)
{
    (void)renderer;
    if (is_placement_command_mode(command_panel_mode_)) {
        return false;
    }

    const CommandPanelAction action = hit_test_command_panel(
        command_panel_mode_,
        window.getSize(),
        screen_position.x,
        screen_position.y,
        current_build_options(simulation, render_snapshot),
        game_menu_.hud_style);
    if (action != CommandPanelAction::None) {
        if (local_is_spectator_) {
            return true;
        }

        return apply_command_panel_action(simulation, render_snapshot, action);
    }

    if (hit_test_command_panel_frame(
            window.getSize(),
            screen_position.x,
            screen_position.y,
            game_menu_.hud_style)) {
        return true;
    }

    return false;
}

namespace {

[[nodiscard]] bool resolve_map_size(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    int& map_width,
    int& map_height)
{
    if (render_snapshot != nullptr) {
        map_width = render_snapshot->map_width;
        map_height = render_snapshot->map_height;
        return map_width > 0 && map_height > 0;
    }

    auto& registry = simulation.registry();
    const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
    if (world_view.begin() == world_view.end()) {
        return false;
    }

    const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
    map_width = map.width;
    map_height = map.height;
    return map_width > 0 && map_height > 0;
}

[[nodiscard]] bool selection_contains_entity(
    const std::vector<entt::entity>& units,
    const entt::entity entity)
{
    return std::find(units.begin(), units.end(), entity) != units.end();
}

[[nodiscard]] bool hit_test_hud_blocks_world_pick(
    const sf::Vector2u window_size,
    const float mouse_x,
    const float mouse_y,
    const constants::HudStyle hud_style)
{
    if (hit_test_command_panel_frame(window_size, mouse_x, mouse_y, hud_style)) {
        return true;
    }

    if (hit_test_minimap_panel_frame(window_size, mouse_x, mouse_y, hud_style)) {
        return true;
    }

    if (hit_test_status_panel_frame(window_size, mouse_x, mouse_y, hud_style)) {
        return true;
    }

    if (hit_test_resource_bar_frame(window_size, mouse_x, mouse_y, hud_style)) {
        return true;
    }

    if (!hud_is_classic_aoe(hud_style)
        && (default_left_decor_rect(window_size).contains(mouse_x, mouse_y)
            || default_right_decor_rect(window_size).contains(mouse_x, mouse_y))) {
        return true;
    }

    return hud_menu_button_rect(window_size, hud_style).contains(mouse_x, mouse_y)
        || hud_diplomacy_button_rect(window_size, hud_style).contains(mouse_x, mouse_y);
}

void apply_hover_stick(
    HoverHighlight& hover,
    const HoverHighlight& previous,
    const render::SimRenderSnapshot& snapshot,
    const render::GameRenderer& renderer,
    const sf::Vector2f screen,
    const float pick_radius_px,
    const std::optional<core::GridPos>& hovered_cell)
{
    const float stick_radius = pick_radius_px * constants::HUD_HOVER_STICK_SCALE;
    const float stick_sq = stick_radius * stick_radius;
    const float switch_margin = constants::HUD_HOVER_SWITCH_MARGIN_PX;

    if (previous.unit != entt::null) {
        const render::RenderEntityPose* previous_pose = nullptr;
        for (const render::RenderEntityPose& pose : snapshot.units) {
            if (pose.entity == previous.unit && pose.health_current > 0) {
                previous_pose = &pose;
                break;
            }
        }

        if (previous_pose != nullptr) {
            const sf::Vector2f previous_screen =
                render::render_pose_screen_position(renderer, *previous_pose, 1.0F);
            const float previous_dx = previous_screen.x - screen.x;
            const float previous_dy = previous_screen.y - screen.y;
            const float previous_dist_sq = previous_dx * previous_dx + previous_dy * previous_dy;
            if (previous_dist_sq <= stick_sq) {
                bool keep_previous = hover.unit == entt::null || hover.unit == previous.unit;
                if (!keep_previous) {
                    for (const render::RenderEntityPose& pose : snapshot.units) {
                        if (pose.entity != hover.unit) {
                            continue;
                        }

                        const sf::Vector2f next_screen =
                            render::render_pose_screen_position(renderer, pose, 1.0F);
                        const float next_dx = next_screen.x - screen.x;
                        const float next_dy = next_screen.y - screen.y;
                        const float next_dist = std::sqrt(next_dx * next_dx + next_dy * next_dy);
                        const float previous_dist = std::sqrt(previous_dist_sq);
                        keep_previous = next_dist + switch_margin >= previous_dist;
                        break;
                    }
                }

                if (keep_previous) {
                    hover.unit = previous.unit;
                    hover.unit_is_enemy = previous.unit_is_enemy;
                    hover.building = entt::null;
                    hover.building_is_enemy = false;
                    hover.resource_cell.reset();
                    return;
                }
            }
        }
    }

    if (previous.building == entt::null || hover.unit != entt::null) {
        return;
    }

    for (const render::RenderEntityPose& pose : snapshot.buildings) {
        if (pose.entity != previous.building || pose.health_current <= 0) {
            continue;
        }

        bool still_over = false;
        if (hovered_cell.has_value()) {
            const sim::components::BuildingFootprint footprint{
                pose.footprint_width,
                pose.footprint_height,
            };
            const sim::components::GridPosition anchor{{pose.grid_x, pose.grid_y}};
            still_over = sim::components::building_contains_cell(anchor, footprint, *hovered_cell);
        }

        if (!still_over) {
            const sf::Vector2f building_screen = renderer.tile_center_screen(
                pose.grid_x,
                pose.grid_y,
                constants::RENDER_ENTITY_BASE_LIFT);
            const float dx = building_screen.x - screen.x;
            const float dy = building_screen.y - screen.y;
            still_over = (dx * dx + dy * dy) <= stick_sq;
        }

        if (!still_over) {
            return;
        }

        if (hover.building == entt::null || hover.building == previous.building) {
            hover.building = previous.building;
            hover.building_is_enemy = previous.building_is_enemy;
            hover.resource_cell.reset();
        }
        return;
    }
}

[[nodiscard]] bool footprint_on_map(
    const int map_width,
    const int map_height,
    const core::GridPos anchor,
    const int footprint)
{
    for (int y = 0; y < footprint; ++y) {
        for (int x = 0; x < footprint; ++x) {
            if (!core::is_inside_grid({anchor.x + x, anchor.y + y}, map_width, map_height)) {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] bool can_place_ghost_footprint(
    const sim::components::MapGrid& map,
    entt::registry& registry,
    const core::GridPos anchor,
    const int footprint,
    const std::vector<entt::entity>& ignore_units,
    const std::uint8_t local_player_slot,
    const bool units_block_placement = true)
{
    (void)ignore_units;
    if (!footprint_on_map(map.width, map.height, anchor, footprint)) {
        return false;
    }

    for (int y = 0; y < footprint; ++y) {
        for (int x = 0; x < footprint; ++x) {
            const core::GridPos cell{anchor.x + x, anchor.y + y};
            if (!sim::systems::is_tile_walkable(map, cell, false)) {
                return false;
            }

            const auto building_view = registry.view<
                sim::components::BuildingTag,
                sim::components::GridPosition,
                sim::components::Health>();
            for (const entt::entity building : building_view) {
                if (building_view.get<sim::components::Health>(building).current.raw() <= 0) {
                    continue;
                }

                sim::components::BuildingFootprint other_footprint{};
                if (registry.any_of<sim::components::BuildingFootprint>(building)) {
                    other_footprint = registry.get<sim::components::BuildingFootprint>(building);
                }
                other_footprint = sim::components::effective_building_footprint(
                    other_footprint,
                    registry.any_of<sim::components::TownCenterTag>(building));
                if (sim::components::building_contains_cell(
                        building_view.get<sim::components::GridPosition>(building),
                        other_footprint,
                        cell)) {
                    return false;
                }
            }

            // Lakes carry no BuildingTag, so they need their own overlap rejection.
            const auto lake_view = registry.view<
                sim::components::ManaLakeTag,
                sim::components::GridPosition,
                sim::components::BuildingFootprint>();
            for (const entt::entity lake : lake_view) {
                if (sim::components::building_contains_cell(
                        lake_view.get<sim::components::GridPosition>(lake),
                        lake_view.get<sim::components::BuildingFootprint>(lake),
                        cell)) {
                    return false;
                }
            }

            if (units_block_placement && sim::systems::is_cell_blocked_for_building(registry, cell)) {
                return false;
            }
        }
    }

    const auto world_view =
        registry.view<sim::components::WorldTag, sim::components::FogOfWarState>();
    if (world_view.begin() != world_view.end()) {
        const auto& fog = world_view.get<sim::components::FogOfWarState>(*world_view.begin());
        for (int y = 0; y < footprint; ++y) {
            for (int x = 0; x < footprint; ++x) {
                const core::GridPos cell{anchor.x + x, anchor.y + y};
                if (!sim::systems::is_cell_explored_to_slot(fog, cell, local_player_slot)) {
                    return false;
                }
            }
        }
    }

    return true;
}

} // namespace

bool GameInput::handle_minimap_navigation(
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    const sf::Vector2f screen_position)
{
    int map_width = 0;
    int map_height = 0;
    if (!resolve_map_size(simulation, render_snapshot, map_width, map_height)) {
        return false;
    }

    const auto world = minimap_screen_to_world(
        window.getSize(),
        screen_position.x,
        screen_position.y,
        map_width,
        map_height,
        game_menu_.hud_style);
    if (!world.has_value()) {
        return false;
    }

    renderer.center_camera_on_world_keep_zoom(world->first, world->second);
    return true;
}

sim::player::SelectionModifyMode GameInput::current_modify_mode() const
{
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl)
        || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RControl)) {
        return sim::player::SelectionModifyMode::Toggle;
    }

    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift)
        || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)) {
        return sim::player::SelectionModifyMode::Add;
    }

    return sim::player::SelectionModifyMode::Replace;
}

void GameInput::reset_frame_clock()
{
    previous_frame_time_ = std::chrono::steady_clock::now();
    frame_clock_initialized_ = true;
}

void GameInput::update_hover(
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot)
{
    const HoverHighlight previous_hover = hover_;
    hover_ = HoverHighlight{};

    if (left_button_down_) {
        return;
    }

    const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
    const sf::Vector2f screen_position{
        static_cast<float>(mouse_position.x),
        static_cast<float>(mouse_position.y),
    };
    if (hit_test_hud_blocks_world_pick(
            window.getSize(),
            screen_position.x,
            screen_position.y,
            game_menu_.hud_style)) {
        return;
    }

    const float pick_radius_px = renderer.selection_pick_radius_px();

    const auto hovered_grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
    if (hovered_grid_cell.has_value()) {
        if (render_snapshot != nullptr) {
            if (render::snapshot_cell_is_unexplored(*render_snapshot, *hovered_grid_cell)) {
                return;
            }
        }
        else {
            const auto world_view = simulation.registry().view<
                sim::components::WorldTag,
                sim::components::FogOfWarState>();
            if (world_view.begin() != world_view.end()) {
                const auto& fog = world_view.get<sim::components::FogOfWarState>(*world_view.begin());
                if (!sim::systems::is_cell_visible_to_slot(fog, *hovered_grid_cell, local_player_slot_)
                    && !sim::systems::is_cell_explored_to_slot(fog, *hovered_grid_cell, local_player_slot_)) {
                    return;
                }
            }
        }
    }

    struct StickHoverOnExit {
        HoverHighlight& hover;
        HoverHighlight previous;
        const render::SimRenderSnapshot* snapshot;
        const render::GameRenderer& renderer;
        sf::Vector2f screen;
        float pick_radius;
        std::optional<core::GridPos> cell;

        ~StickHoverOnExit()
        {
            if (snapshot == nullptr) {
                return;
            }

            apply_hover_stick(
                hover,
                previous,
                *snapshot,
                renderer,
                screen,
                pick_radius,
                cell);
        }
    } stick_guard{
        hover_,
        previous_hover,
        render_snapshot,
        renderer,
        screen_position,
        pick_radius_px,
        hovered_grid_cell,
    };

    if (render_snapshot != nullptr) {
        const entt::entity hovered_unit = render::pick_hovered_unit_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px);
        if (hovered_unit != entt::null) {
            hover_.unit = hovered_unit;
            for (const render::RenderEntityPose& pose : render_snapshot->units) {
                if (pose.entity == hovered_unit) {
                    hover_.unit_is_enemy = pose.is_enemy;
                    break;
                }
            }
            return;
        }

        if (hovered_grid_cell.has_value()) {
            for (const render::RenderEntityPose& pose : render_snapshot->buildings) {
                if (pose.health_current <= 0) {
                    continue;
                }

                const bool enemy_building =
                    !pose.is_nature && pose.player_slot != local_player_slot_;
                if (enemy_building && !pose.shrouded
                    && !render::snapshot_cell_is_visible(*render_snapshot, *hovered_grid_cell)) {
                    continue;
                }

                const sim::components::BuildingFootprint footprint{
                    pose.footprint_width,
                    pose.footprint_height,
                };
                const sim::components::GridPosition anchor{{pose.grid_x, pose.grid_y}};
                if (sim::components::building_contains_cell(anchor, footprint, *hovered_grid_cell)) {
                    hover_.building = pose.entity;
                    hover_.building_is_enemy = enemy_building;
                    return;
                }
            }
        }

        const entt::entity hovered_building = render::pick_player_building_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);
        if (hovered_building != entt::null) {
            hover_.building = hovered_building;
            hover_.building_is_enemy = false;
            return;
        }

        const entt::entity hovered_enemy_building = render::pick_enemy_building_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);
        if (hovered_enemy_building != entt::null) {
            hover_.building = hovered_enemy_building;
            hover_.building_is_enemy = true;
            return;
        }

        bool allow_resource_hover = !selection_.has_units();
        if (!allow_resource_hover) {
            for (const entt::entity entity : selection_.units) {
                for (const render::RenderEntityPose& pose : render_snapshot->units) {
                    if (pose.entity == entity && pose.is_worker) {
                        allow_resource_hover = true;
                        break;
                    }
                }

                if (allow_resource_hover) {
                    break;
                }
            }
        }

        if (allow_resource_hover) {
            hover_.resource_cell = render::pick_resource_forest_at_screen(
                *render_snapshot,
                renderer,
                screen_position,
                pick_radius_px,
                local_player_slot_);
        }

        return;
    }

    auto& registry = simulation.registry();

    const entt::entity hovered_unit = sim::player::pick_hovered_unit_at_screen(
        registry,
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);
    if (hovered_unit != entt::null) {
        hover_.unit = hovered_unit;
        hover_.unit_is_enemy =
            sim::components::is_opponent_entity(registry, hovered_unit, local_player_slot_);
        return;
    }

    if (hovered_grid_cell.has_value()) {
        const sim::components::FogOfWarState* fog = nullptr;
        const auto world_view = registry.view<
            sim::components::WorldTag,
            sim::components::FogOfWarState>();
        if (world_view.begin() != world_view.end()) {
            fog = &world_view.get<sim::components::FogOfWarState>(*world_view.begin());
        }

        const auto building_view = registry.view<
            sim::components::BuildingTag,
            sim::components::PlayerOwnedTag,
            sim::components::GridPosition,
            sim::components::Health>();
        for (const entt::entity entity : building_view) {
            const auto& health = building_view.get<sim::components::Health>(entity);
            if (health.current.raw() <= 0) {
                continue;
            }

            const bool enemy_building =
                sim::components::is_opponent_entity(registry, entity, local_player_slot_);
            if (enemy_building && fog != nullptr
                && !sim::systems::is_entity_visible_to_slot(
                       registry, *fog, entity, local_player_slot_)
                && !renderer.local_player_has_seen_building(entity)) {
                continue;
            }

            const auto& anchor = building_view.get<sim::components::GridPosition>(entity);
            sim::components::BuildingFootprint footprint{};
            if (registry.any_of<sim::components::BuildingFootprint>(entity)) {
                footprint = registry.get<sim::components::BuildingFootprint>(entity);
            }
            footprint = sim::components::effective_building_footprint(
                footprint,
                registry.any_of<sim::components::TownCenterTag>(entity));
            if (sim::components::building_contains_cell(anchor, footprint, *hovered_grid_cell)) {
                hover_.building = entity;
                hover_.building_is_enemy = enemy_building;
                return;
            }
        }
    }

    const entt::entity hovered_building = sim::player::pick_player_building_at_screen(
        registry,
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);
    if (hovered_building != entt::null) {
        hover_.building = hovered_building;
        hover_.building_is_enemy = false;
        return;
    }

    const entt::entity hovered_enemy_building = sim::player::pick_enemy_building_at_screen(
        registry,
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);
    if (hovered_enemy_building != entt::null) {
        hover_.building = hovered_enemy_building;
        hover_.building_is_enemy = true;
        return;
    }

    bool allow_resource_hover = !selection_.has_units();
    if (!allow_resource_hover) {
        for (const entt::entity entity : selection_.units) {
            if (registry.any_of<sim::components::WorkerUnitTag>(entity)) {
                allow_resource_hover = true;
                break;
            }
        }
    }

    if (allow_resource_hover) {
        hover_.resource_cell = sim::player::pick_resource_forest_at_screen(
            registry,
            renderer,
            screen_position,
            renderer.selection_pick_radius_px(),
            local_player_slot_);
    }
}

void GameInput::sync_audio_volumes_from_menu()
{
    if (game_audio_ == nullptr) {
        return;
    }

    game_audio_->set_master_volume(game_menu_.master_volume);
    game_audio_->set_music_volume(game_menu_.music_volume);
    game_audio_->set_sfx_volume(game_menu_.sfx_volume);
}

void GameInput::apply_game_menu_action(
    const GameMenuAction action,
    sim::Simulation& simulation)
{
    const auto refresh_save_entries = [this]() {
        game_menu_.save_entries = sim::persistence::list_save_stems();
        game_menu_.selected_save_index = -1;
        game_menu_.save_list_scroll = 0;
    };

    const auto open_save_screen = [&]() {
        refresh_save_entries();
        game_menu_.filename_draft.clear();
        game_menu_.filename_focused = true;
        game_menu_.filename_all_selected = false;
        game_menu_.screen = GameMenuScreen::Save;
        game_menu_.dragging_slider = GameMenuSlider::None;
    };

    const auto open_load_screen = [&]() {
        refresh_save_entries();
        game_menu_.filename_draft.clear();
        game_menu_.filename_focused = true;
        game_menu_.filename_all_selected = false;
        game_menu_.screen = GameMenuScreen::Load;
        game_menu_.dragging_slider = GameMenuSlider::None;
    };

    const auto write_save = [&](const std::string& stem) -> bool {
        const std::filesystem::path path = sim::persistence::save_path_for_stem(stem);
        if (lockstep_session_ != nullptr) {
            std::vector<std::byte> save_bytes{};
            {
                std::lock_guard lock(lockstep_session_->simulation_access_mutex());
                save_bytes = sim::persistence::encode_save_bytes(simulation);
            }
            return sim::persistence::write_save_bytes(path, save_bytes);
        }

        return sim::persistence::save_simulation_to_file(simulation, path);
    };

    const auto read_save = [&](const std::string& stem) -> bool {
        if (lockstep_session_ != nullptr) {
            return false;
        }

        return sim::persistence::load_simulation_from_file(
            simulation,
            sim::persistence::save_path_for_stem(stem));
    };

    switch (action) {
    case GameMenuAction::None:
        return;
    case GameMenuAction::ToggleMenu:
        game_menu_.multiplayer = lockstep_session_ != nullptr;
        game_menu_.toggle();
        return;
    case GameMenuAction::Resume:
        match_paused_ = false;
        if (lockstep_session_ != nullptr) {
            lockstep_session_->request_match_pause(false);
        }
        game_menu_.close();
        return;
    case GameMenuAction::Pause:
        match_paused_ = true;
        if (lockstep_session_ != nullptr) {
            lockstep_session_->request_match_pause(true);
        }
        return;
    case GameMenuAction::Save:
        open_save_screen();
        return;
    case GameMenuAction::Load:
        if (lockstep_session_ != nullptr) {
            return;
        }
        open_load_screen();
        return;
    case GameMenuAction::Resign:
        game_menu_.screen = GameMenuScreen::ConfirmResign;
        game_menu_.dragging_slider = GameMenuSlider::None;
        game_menu_.filename_focused = false;
        return;
    case GameMenuAction::ExitToMainMenu:
        game_menu_.screen = GameMenuScreen::ConfirmLeave;
        game_menu_.dragging_slider = GameMenuSlider::None;
        game_menu_.filename_focused = false;
        return;
    case GameMenuAction::OpenSettings:
        if (game_menu_.is_settings_screen()) {
            game_menu_.open_main();
            return;
        }
        game_menu_.screen = GameMenuScreen::SettingsGame;
        game_menu_.dragging_slider = GameMenuSlider::None;
        game_menu_.filename_focused = false;
        return;
    case GameMenuAction::ExitGame:
        exit_game_requested_ = true;
        game_menu_.close();
        return;
    case GameMenuAction::SettingsBack:
        game_menu_.open_main();
        return;
    case GameMenuAction::SettingsTabGame:
        game_menu_.screen = GameMenuScreen::SettingsGame;
        game_menu_.dragging_slider = GameMenuSlider::None;
        return;
    case GameMenuAction::SettingsTabVideo:
        game_menu_.screen = GameMenuScreen::SettingsVideo;
        game_menu_.dragging_slider = GameMenuSlider::None;
        return;
    case GameMenuAction::SettingsTabAudio:
        game_menu_.screen = GameMenuScreen::SettingsAudio;
        game_menu_.dragging_slider = GameMenuSlider::None;
        return;
    case GameMenuAction::ToggleFullscreen:
        fullscreen_toggle_requested_ = true;
        return;
    case GameMenuAction::ToggleMouseCapture:
        if (game_menu_.fullscreen) {
            return;
        }
        game_menu_.mouse_capture = !game_menu_.mouse_capture;
        video_apply_requested_ = true;
        return;
    case GameMenuAction::ToggleVsync:
        game_menu_.vsync = !game_menu_.vsync;
        video_apply_requested_ = true;
        return;
    case GameMenuAction::CycleFps:
        if (game_menu_.vsync) {
            return;
        }
        game_menu_.fps_limit = next_video_fps_limit(game_menu_.fps_limit);
        video_apply_requested_ = true;
        return;
    case GameMenuAction::CycleBuildingRangeDisplay:
        game_menu_.building_range_display = next_building_range_display(
            game_menu_.building_range_display);
        return;
    case GameMenuAction::FocusPlayerName:
        game_menu_.player_name_focused = true;
        game_menu_.player_name_all_selected = false;
        return;
    case GameMenuAction::CycleHudStyle:
        game_menu_.hud_style = next_hud_style(game_menu_.hud_style);
        return;
    case GameMenuAction::BeginDragMaster:
        game_menu_.dragging_slider = GameMenuSlider::Master;
        return;
    case GameMenuAction::BeginDragMusic:
        game_menu_.dragging_slider = GameMenuSlider::Music;
        return;
    case GameMenuAction::BeginDragSfx:
        game_menu_.dragging_slider = GameMenuSlider::Sfx;
        return;
    case GameMenuAction::BeginDragScrollSpeed:
        game_menu_.dragging_slider = GameMenuSlider::ScrollSpeed;
        return;
    case GameMenuAction::SaveLoadFocusFilename:
        game_menu_.filename_focused = true;
        return;
    case GameMenuAction::SaveLoadBack:
        game_menu_.open_main();
        return;
    case GameMenuAction::SaveLoadConfirm: {
        const auto stem = sim::persistence::normalize_save_stem(game_menu_.filename_draft);
        if (!stem.has_value()) {
            return;
        }

        game_menu_.filename_draft = *stem;
        if (game_menu_.screen == GameMenuScreen::Save) {
            if (sim::persistence::save_file_exists(*stem)) {
                game_menu_.dialog_return_screen = GameMenuScreen::Save;
                game_menu_.screen = GameMenuScreen::ConfirmOverwrite;
                game_menu_.filename_focused = false;
                return;
            }

            if (write_save(*stem)) {
                game_menu_.open_main();
            }
            return;
        }

        if (game_menu_.screen != GameMenuScreen::Load) {
            return;
        }

        if (!sim::persistence::save_file_exists(*stem)) {
            game_menu_.dialog_return_screen = GameMenuScreen::Load;
            game_menu_.screen = GameMenuScreen::ErrorMissingSave;
            game_menu_.filename_focused = false;
            return;
        }

        game_menu_.dialog_return_screen = GameMenuScreen::Load;
        game_menu_.screen = GameMenuScreen::ConfirmLoad;
        game_menu_.filename_focused = false;
        return;
    }
    case GameMenuAction::DialogYes: {
        if (game_menu_.screen == GameMenuScreen::ConfirmResign) {
            if (lockstep_session_ != nullptr) {
                (void)lockstep_session_->request_match_resign();
            }
            else {
                sim::player::PlayerCommand command{};
                command.player_slot = local_player_slot_;
                command.type = sim::player::PlayerCommandType::Resign;
                command.execute_tick = simulation.next_command_execute_tick();
                submit_player_command(simulation, std::move(command));
            }
            match_paused_ = false;
            game_menu_.close();
            return;
        }

        if (game_menu_.screen == GameMenuScreen::ConfirmLeave) {
            if (!local_is_spectator_) {
                if (lockstep_session_ != nullptr) {
                    (void)lockstep_session_->request_voluntary_resign();
                }
                else {
                    sim::player::PlayerCommand command{};
                    command.player_slot = local_player_slot_;
                    command.type = sim::player::PlayerCommandType::Resign;
                    command.execute_tick = simulation.next_command_execute_tick();
                    submit_player_command(simulation, std::move(command));
                }
            }
            match_paused_ = false;
            exit_to_main_menu_requested_ = true;
            game_menu_.close();
            return;
        }

        const auto stem = sim::persistence::normalize_save_stem(game_menu_.filename_draft);
        if (!stem.has_value()) {
            game_menu_.screen = game_menu_.dialog_return_screen;
            return;
        }

        if (game_menu_.screen == GameMenuScreen::ConfirmOverwrite) {
            if (write_save(*stem)) {
                game_menu_.open_main();
            }
            else {
                game_menu_.screen = game_menu_.dialog_return_screen;
            }
            return;
        }

        if (game_menu_.screen == GameMenuScreen::ConfirmLoad) {
            if (read_save(*stem)) {
                game_menu_.close();
            }
            else {
                game_menu_.screen = GameMenuScreen::ErrorMissingSave;
            }
            return;
        }

        game_menu_.screen = game_menu_.dialog_return_screen;
        return;
    }
    case GameMenuAction::DialogCancel:
    case GameMenuAction::DialogOk:
        game_menu_.screen = game_menu_.dialog_return_screen;
        game_menu_.filename_focused = game_menu_.is_save_load_screen();
        return;
    }
}

bool GameInput::handle_game_menu_event(
    const sf::Event& event,
    const sf::Window& window,
    sim::Simulation& simulation)
{
    const sf::Vector2u window_size = window.getSize();

    if (const auto* key_pressed = event.getIf<sf::Event::KeyPressed>()) {
        if (game_menu_.screen == GameMenuScreen::SettingsGame && game_menu_.player_name_focused) {
            TextFieldEdit name_field{
                .text = game_menu_.player_name,
                .max_length = static_cast<std::size_t>(constants::MAIN_MENU_MAX_NAME_LENGTH),
                .all_selected = game_menu_.player_name_all_selected,
                .filter = TextFieldFilter::Printable,
            };
            if (apply_text_field_hotkey(
                    key_pressed->code, key_pressed->control, name_field)) {
                return true;
            }

            if (key_pressed->code == sf::Keyboard::Key::Backspace) {
                apply_text_field_backspace(name_field);
                return true;
            }
        }

        if (game_menu_.is_save_load_screen() && game_menu_.filename_focused) {
            TextFieldEdit filename_field{
                .text = game_menu_.filename_draft,
                .max_length = static_cast<std::size_t>(constants::HUD_SAVE_LOAD_MAX_FILENAME_LENGTH),
                .all_selected = game_menu_.filename_all_selected,
                .filter = TextFieldFilter::Printable,
            };
            if (apply_text_field_hotkey(
                    key_pressed->code, key_pressed->control, filename_field)) {
                return true;
            }

            if (key_pressed->code == sf::Keyboard::Key::Backspace) {
                apply_text_field_backspace(filename_field);
                return true;
            }

            if (key_pressed->code == sf::Keyboard::Key::Enter) {
                apply_game_menu_action(GameMenuAction::SaveLoadConfirm, simulation);
                return true;
            }
        }

        if (key_pressed->code != sf::Keyboard::Key::Escape) {
            return game_menu_.is_open();
        }

        if (game_menu_.is_dialog_screen()) {
            apply_game_menu_action(GameMenuAction::DialogCancel, simulation);
            return true;
        }

        if (game_menu_.is_save_load_screen() || game_menu_.is_settings_screen()) {
            game_menu_.open_main();
            return true;
        }

        game_menu_.multiplayer = lockstep_session_ != nullptr;
        game_menu_.toggle();
        return true;
    }

    if (const auto* text_entered = event.getIf<sf::Event::TextEntered>()) {
        if (game_menu_.screen == GameMenuScreen::SettingsGame && game_menu_.player_name_focused) {
            const char32_t unicode = text_entered->unicode;
            if (unicode == 8U || unicode == 13U || unicode == 27U) {
                return true;
            }

            if (unicode < constants::MAIN_MENU_MIN_PRINTABLE_CHAR
                || unicode > constants::MAIN_MENU_MAX_PRINTABLE_CHAR) {
                return true;
            }

            const char character = static_cast<char>(unicode);
            TextFieldEdit name_field{
                .text = game_menu_.player_name,
                .max_length = static_cast<std::size_t>(constants::MAIN_MENU_MAX_NAME_LENGTH),
                .all_selected = game_menu_.player_name_all_selected,
                .filter = TextFieldFilter::Printable,
            };
            append_text_field_chars(name_field, std::string_view(&character, 1U));
            return true;
        }

        if (!game_menu_.is_save_load_screen() || !game_menu_.filename_focused) {
            return game_menu_.is_open();
        }

        const char32_t unicode = text_entered->unicode;
        if (unicode == 8U || unicode == 13U || unicode == 27U) {
            return true;
        }

        if (unicode < constants::MAIN_MENU_MIN_PRINTABLE_CHAR
            || unicode > constants::MAIN_MENU_MAX_PRINTABLE_CHAR) {
            return true;
        }

        const char character = static_cast<char>(unicode);
        TextFieldEdit filename_field{
            .text = game_menu_.filename_draft,
            .max_length = static_cast<std::size_t>(constants::HUD_SAVE_LOAD_MAX_FILENAME_LENGTH),
            .all_selected = game_menu_.filename_all_selected,
            .filter = TextFieldFilter::Printable,
        };
        append_text_field_chars(filename_field, std::string_view(&character, 1U));
        return true;
    }

    if (const auto* mouse_pressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (mouse_pressed->button != sf::Mouse::Button::Left) {
            return game_menu_.is_open();
        }

        const sf::Vector2i mouse = sf::Mouse::getPosition(window);
        const float mouse_x = static_cast<float>(mouse.x);
        const float mouse_y = static_cast<float>(mouse.y);

        if (!game_menu_.is_open()) {
            if (hud_menu_button_rect(window_size, game_menu_.hud_style)
                    .contains(mouse_x, mouse_y)) {
                game_menu_.multiplayer = lockstep_session_ != nullptr;
                game_menu_.open_main();
                left_button_down_ = false;
                left_press_position_.reset();
                selection_box_.active = false;
                return true;
            }
            return false;
        }

        const GameMenuAction rail_action = hit_test_menu_button(
            build_main_menu_buttons(
                window_size,
                lockstep_session_ != nullptr,
                false),
            mouse_x,
            mouse_y);
        if (rail_action != GameMenuAction::None) {
            apply_game_menu_action(rail_action, simulation);
            return true;
        }

        if (game_menu_.screen == GameMenuScreen::Main) {
            return true;
        }

        if (game_menu_.is_save_load_screen()) {
            if (save_load_filename_rect(window_size).contains(mouse_x, mouse_y)) {
                game_menu_.filename_focused = true;
                game_menu_.filename_all_selected = false;
                return true;
            }

            const int row = hit_test_save_list_row(game_menu_, window_size, mouse_x, mouse_y);
            if (row >= 0) {
                game_menu_.selected_save_index = row;
                game_menu_.filename_draft = game_menu_.save_entries[static_cast<std::size_t>(row)];
                game_menu_.filename_focused = false;
                return true;
            }

            game_menu_.filename_focused = false;
            const GameMenuAction action = hit_test_menu_button(
                build_save_load_buttons(game_menu_, window_size),
                mouse_x,
                mouse_y);
            if (action != GameMenuAction::None) {
                apply_game_menu_action(action, simulation);
            }
            return true;
        }

        if (game_menu_.is_dialog_screen()) {
            const GameMenuAction action = hit_test_menu_button(
                build_dialog_buttons(game_menu_, window_size),
                mouse_x,
                mouse_y);
            if (action != GameMenuAction::None) {
                apply_game_menu_action(action, simulation);
            }
            return true;
        }

        if (game_menu_.screen == GameMenuScreen::SettingsGame) {
            if (settings_player_name_field_rect(window_size, game_menu_.center_settings_panel)
                    .contains(mouse_x, mouse_y)) {
                game_menu_.player_name_focused = true;
                game_menu_.player_name_all_selected = false;
                return true;
            }

            game_menu_.player_name_focused = false;
            game_menu_.player_name_all_selected = false;
            if (scroll_speed_slider_rect(window_size, game_menu_.center_settings_panel)
                    .contains(mouse_x, mouse_y)) {
                game_menu_.dragging_slider = GameMenuSlider::ScrollSpeed;
                apply_slider_drag(game_menu_, window_size, mouse_x);
                return true;
            }
        }

        if (game_menu_.screen == GameMenuScreen::SettingsAudio) {
            const GameMenuSlider slider = hit_test_volume_slider(
                window_size, mouse_x, mouse_y, game_menu_.center_settings_panel);
            if (slider != GameMenuSlider::None) {
                game_menu_.dragging_slider = slider;
                apply_slider_drag(game_menu_, window_size, mouse_x);
                sync_audio_volumes_from_menu();
                return true;
            }
        }

        const GameMenuAction action = hit_test_menu_button(
            build_settings_buttons(game_menu_, window_size),
            mouse_x,
            mouse_y);
        if (action != GameMenuAction::None) {
            game_menu_.player_name_focused = false;
            game_menu_.player_name_all_selected = false;
            apply_game_menu_action(action, simulation);
        }
        return true;
    }

    if (const auto* mouse_released = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (mouse_released->button == sf::Mouse::Button::Left) {
            game_menu_.dragging_slider = GameMenuSlider::None;
        }
        return game_menu_.is_open();
    }

    if (event.getIf<sf::Event::MouseMoved>() != nullptr) {
        if (game_menu_.dragging_slider != GameMenuSlider::None) {
            const sf::Vector2i mouse = sf::Mouse::getPosition(window);
            apply_slider_drag(game_menu_, window_size, static_cast<float>(mouse.x));
            sync_audio_volumes_from_menu();
        }
        return game_menu_.is_open();
    }

    if (const auto* wheel = event.getIf<sf::Event::MouseWheelScrolled>()) {
        if (!game_menu_.is_save_load_screen()) {
            return game_menu_.is_open();
        }

        const int max_scroll = std::max(
            0,
            static_cast<int>(game_menu_.save_entries.size())
                - constants::HUD_SAVE_LOAD_VISIBLE_ROWS);
        if (wheel->delta > 0.0F) {
            game_menu_.save_list_scroll = std::max(0, game_menu_.save_list_scroll - 1);
        }
        else if (wheel->delta < 0.0F) {
            game_menu_.save_list_scroll = std::min(max_scroll, game_menu_.save_list_scroll + 1);
        }
        return true;
    }

    return game_menu_.is_open();
}

void GameInput::update_continuous(
    sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot)
{
    if (lockstep_session_ != nullptr) {
        const bool paused = lockstep_session_->is_match_paused();
        if (paused && !match_paused_ && !game_menu_.is_open()) {
            game_menu_.multiplayer = true;
            game_menu_.open_main();
        }
        match_paused_ = paused;
    }

    if (game_menu_.is_open()) {
        selection_box_.active = false;
        left_button_down_ = false;
        left_press_position_.reset();
        minimap_navigation_active_ = false;
        placement_ghost_anchor_.reset();
        placement_ghost_valid_ = false;
        hover_ = {};
        if (game_menu_.dragging_slider != GameMenuSlider::None) {
            const sf::Vector2i mouse = sf::Mouse::getPosition(window);
            apply_slider_drag(game_menu_, window.getSize(), static_cast<float>(mouse.x));
            sync_audio_volumes_from_menu();
        }
        update_game_cursor(window, simulation, render_snapshot);
        return;
    }

    if (sim::systems::match_is_finished(simulation.registry())) {
        selection_box_.active = false;
        left_button_down_ = false;
        left_press_position_.reset();
        minimap_navigation_active_ = false;
        placement_ghost_anchor_.reset();
        placement_ghost_valid_ = false;
        hover_ = {};
        update_game_cursor(window, simulation, render_snapshot);
        return;
    }

    if (render_snapshot != nullptr) {
        render::prune_dead_selection(selection_.units, *render_snapshot, local_player_slot_);
    }
    else {
        sim::player::prune_dead_selection(selection_.units, simulation.registry());
    }

    const auto now = std::chrono::steady_clock::now();
    if (!frame_clock_initialized_) {
        previous_frame_time_ = now;
        frame_clock_initialized_ = true;
        return;
    }

    const float delta_seconds = std::clamp(
        static_cast<float>(
            std::chrono::duration<double>(now - previous_frame_time_).count()),
        0.0F,
        0.1F);
    previous_frame_time_ = now;

    if (left_button_down_ && minimap_navigation_active_) {
        selection_box_.active = false;
        const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
        const sf::Vector2f screen_position{
            static_cast<float>(mouse_position.x),
            static_cast<float>(mouse_position.y),
        };
        (void)handle_minimap_navigation(
            window,
            renderer,
            simulation,
            render_snapshot,
            screen_position);
    }
    else if (left_button_down_ && left_press_position_.has_value()) {
        const sf::Vector2f press_screen{
            static_cast<float>(left_press_position_->x),
            static_cast<float>(left_press_position_->y),
        };
        if (hit_test_hud_blocks_world_pick(
                window.getSize(),
                press_screen.x,
                press_screen.y,
                game_menu_.hud_style)) {
            selection_box_.active = false;
        }
        else {
            selection_box_.active = true;
            selection_box_.start = press_screen;
            const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
            selection_box_.current = sf::Vector2f{
                static_cast<float>(mouse_position.x),
                static_cast<float>(mouse_position.y),
            };
        }
    }
    else {
        selection_box_.active = false;
    }

    update_hover(window, renderer, simulation, render_snapshot);

    placement_ghost_anchor_.reset();
    placement_ghost_valid_ = false;
    if (is_placement_command_mode(command_panel_mode_)) {
        const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
        const auto center_cell = renderer.screen_to_grid(
            static_cast<float>(mouse_position.x),
            static_cast<float>(mouse_position.y));
        if (center_cell.has_value()) {
            auto& registry = simulation.registry();
            const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
            core::GridPos anchor =
                placement_anchor_from_center_cell(command_panel_mode_, *center_cell);
            if (command_panel_mode_ == CommandPanelMode::PlaceExtractor) {
                if (world_view.begin() != world_view.end()) {
                    if (const auto snapped =
                            sim::player::extractor_snap_anchor(registry, *center_cell)) {
                        anchor = *snapped;
                    }
                }
                else if (render_snapshot != nullptr) {
                    if (const auto snapped =
                            render::snapshot_extractor_snap_anchor(*render_snapshot, *center_cell)) {
                        anchor = *snapped;
                    }
                }
            }
            const int footprint = placement_footprint_tiles(command_panel_mode_);
            int map_width = 0;
            int map_height = 0;
            if (world_view.begin() != world_view.end()) {
                const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
                map_width = map.width;
                map_height = map.height;
            }
            else if (render_snapshot != nullptr) {
                map_width = render_snapshot->map_width;
                map_height = render_snapshot->map_height;
            }

            // Off-map footprints: no ghost texture/highlight at all.
            if (map_width > 0 && map_height > 0
                && footprint_on_map(map_width, map_height, anchor, footprint)) {
                placement_ghost_anchor_ = anchor;
                const bool placing_extractor =
                    command_panel_mode_ == CommandPanelMode::PlaceExtractor;
                    if (placing_extractor) {
                        placement_ghost_valid_ = world_view.begin() != world_view.end()
                            ? sim::player::can_build_extractor_at(registry, anchor, local_player_slot_)
                            : (render_snapshot != nullptr
                                && render::snapshot_can_place_extractor_at(*render_snapshot, anchor));
                    }
                else if (world_view.begin() != world_view.end()) {
                    const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
                    placement_ghost_valid_ = can_place_ghost_footprint(
                        map,
                        registry,
                        anchor,
                        footprint,
                        selection_.units,
                        local_player_slot_,
                        false);
                }
                else if (render_snapshot != nullptr) {
                    placement_ghost_valid_ = true;
                    for (int y = 0; y < footprint && placement_ghost_valid_; ++y) {
                        for (int x = 0; x < footprint; ++x) {
                            const core::GridPos cell{anchor.x + x, anchor.y + y};
                            if (render::snapshot_cell_is_unexplored(*render_snapshot, cell)
                                || render::snapshot_cell_covered_by_mana_lake(
                                    *render_snapshot,
                                    cell)) {
                                placement_ghost_valid_ = false;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    if (!left_button_down_) {
        float pan_x = 0.0F;
        float pan_y = 0.0F;

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left)) {
            pan_x += 1.0F;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right)) {
            pan_x -= 1.0F;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up)) {
            pan_y += 1.0F;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down)) {
            pan_y -= 1.0F;
        }

        const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
        const sf::Vector2u window_size = window.getSize();
        const int edge_margin = constants::CAMERA_EDGE_SCROLL_MARGIN_PX;

        if (mouse_position.x >= 0
            && mouse_position.y >= 0
            && static_cast<unsigned int>(mouse_position.x) < window_size.x
            && static_cast<unsigned int>(mouse_position.y) < window_size.y) {
            if (mouse_position.x < edge_margin) {
                pan_x += 1.0F;
            }
            else if (mouse_position.x >= static_cast<int>(window_size.x) - edge_margin) {
                pan_x -= 1.0F;
            }

            if (mouse_position.y < edge_margin) {
                pan_y += 1.0F;
            }
            else if (mouse_position.y >= static_cast<int>(window_size.y) - edge_margin) {
                pan_y -= 1.0F;
            }
        }

        const float pan_length = std::sqrt(pan_x * pan_x + pan_y * pan_y);
        if (pan_length > 0.0F) {
            pan_x /= pan_length;
            pan_y /= pan_length;

            const bool keyboard_active = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left)
                || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right)
                || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up)
                || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down);

            const float speed = (keyboard_active
                    ? constants::CAMERA_KEYBOARD_PAN_SPEED_PX_PER_SEC
                    : constants::CAMERA_EDGE_SCROLL_SPEED_PX_PER_SEC)
                * game_menu_.scroll_speed;

            renderer.pan_camera(
                pan_x * speed * delta_seconds,
                pan_y * speed * delta_seconds);
        }
    }

    renderer.update_camera(delta_seconds);
    update_game_cursor(window, simulation, render_snapshot);
}

CursorShape GameInput::resolve_cursor_shape(
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot) const
{
    if (is_placement_command_mode(command_panel_mode_)) {
        if (!placement_ghost_anchor_.has_value()) {
            return CursorShape::Cross;
        }

        return placement_ghost_valid_ ? CursorShape::Check : CursorShape::Cross;
    }

    if (selection_.units.empty()) {
        return CursorShape::Normal;
    }

    if (hover_.unit != entt::null) {
        return hover_.unit_is_enemy ? CursorShape::Attack : CursorShape::AttackRestricted;
    }

    if (hover_.building != entt::null) {
        if (hover_.building_is_enemy) {
            return CursorShape::Attack;
        }

        if (selection_has_worker(simulation, render_snapshot)) {
            return CursorShape::Target;
        }

        return CursorShape::AttackRestricted;
    }

    if (hover_.resource_cell.has_value()
        && selection_has_worker(simulation, render_snapshot)) {
        return CursorShape::Target;
    }

    if (attack_targeting_mode_) {
        return CursorShape::Restricted;
    }

    return CursorShape::Normal;
}

void GameInput::update_game_cursor(
    sf::Window& window,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot)
{
    if (game_cursor_ == nullptr) {
        return;
    }

    CursorPlayerColor cursor_color = cursor_color_for_player_slot(local_player_slot_);
    const auto session_view =
        simulation.registry().view<sim::components::WorldTag, sim::components::MatchSession>();
    if (session_view.begin() != session_view.end()) {
        const auto& session =
            session_view.get<sim::components::MatchSession>(*session_view.begin());
        cursor_color = static_cast<CursorPlayerColor>(
            sim::components::player_color_index(session, local_player_slot_));
    }
    game_cursor_->set_player_color(cursor_color);
    game_cursor_->set_shape(resolve_cursor_shape(simulation, render_snapshot));
    game_cursor_->apply(window);
}

void GameInput::finalize_left_release(
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const sf::Vector2i mouse_position,
    const render::SimRenderSnapshot* render_snapshot)
{
    struct PanelSyncGuard {
        std::function<void()> on_exit{};
        ~PanelSyncGuard()
        {
            if (on_exit) {
                on_exit();
            }
        }
    } panel_sync_guard{
        [this, &simulation, render_snapshot]() {
            sync_command_panel_mode(simulation, render_snapshot);
        },
    };

    const sim::player::SelectionModifyMode mode = current_modify_mode();
    const sf::Vector2f screen_position{
        static_cast<float>(mouse_position.x),
        static_cast<float>(mouse_position.y),
    };
    const float pick_radius_px = renderer.selection_pick_radius_px();

    if (minimap_navigation_active_) {
        minimap_navigation_active_ = false;
        (void)handle_minimap_navigation(
            window,
            renderer,
            simulation,
            render_snapshot,
            screen_position);
        return;
    }

    if (handle_command_panel_click(
            window,
            renderer,
            simulation,
            render_snapshot,
            screen_position)) {
        return;
    }

    if (left_press_position_.has_value()
        && hit_test_hud_blocks_world_pick(
            window.getSize(),
            static_cast<float>(left_press_position_->x),
            static_cast<float>(left_press_position_->y),
            game_menu_.hud_style)) {
        return;
    }

    if (attack_targeting_mode_) {
        attack_targeting_mode_ = false;
        if (try_issue_attack_at_screen(
                window,
                renderer,
                simulation,
                render_snapshot,
                screen_position)) {
            return;
        }
    }

    if (garrison_targeting_mode_) {
        garrison_targeting_mode_ = false;
    }

    if (is_placement_command_mode(command_panel_mode_)) {
        if (hit_test_hud_blocks_world_pick(
                window.getSize(),
                screen_position.x,
                screen_position.y,
                game_menu_.hud_style)) {
            return;
        }

        const auto center_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
        if (!center_cell.has_value() || selection_.units.empty() || !placement_ghost_valid_) {
            return;
        }

        sim::player::PlayerCommandType build_type = sim::player::PlayerCommandType::BuildTownCenter;
        const core::GridPos anchor =
            placement_anchor_from_center_cell(command_panel_mode_, *center_cell);
        switch (command_panel_mode_) {
        case CommandPanelMode::PlaceHouse:
            build_type = sim::player::PlayerCommandType::BuildHouse;
            break;
        case CommandPanelMode::PlaceLumberCamp:
            build_type = sim::player::PlayerCommandType::BuildLumberCamp;
            break;
        case CommandPanelMode::PlaceExtractor:
            build_type = sim::player::PlayerCommandType::BuildExtractor;
            break;
        case CommandPanelMode::PlaceMill:
            build_type = sim::player::PlayerCommandType::BuildMill;
            break;
        case CommandPanelMode::PlaceMiningCamp:
            build_type = sim::player::PlayerCommandType::BuildMiningCamp;
            break;
        case CommandPanelMode::PlaceBarracks:
            build_type = sim::player::PlayerCommandType::BuildBarracks;
            break;
        case CommandPanelMode::PlaceMageAcademy:
            build_type = sim::player::PlayerCommandType::BuildMageAcademy;
            break;
        case CommandPanelMode::PlaceTower:
            build_type = sim::player::PlayerCommandType::BuildTower;
            break;
        case CommandPanelMode::PlaceMarket:
            build_type = sim::player::PlayerCommandType::BuildMarket;
            break;
        case CommandPanelMode::PlaceGarden:
            build_type = sim::player::PlayerCommandType::BuildGarden;
            break;
        case CommandPanelMode::PlaceReservoir:
            build_type = sim::player::PlayerCommandType::BuildReservoir;
            break;
        case CommandPanelMode::PlaceFarm:
            build_type = sim::player::PlayerCommandType::BuildFarm;
            break;
        default:
            break;
        }

        sim::player::PlayerCommand command =
            make_command(simulation, build_type, selection_.units, render_snapshot);
        command.cell = anchor;
        submit_player_command(simulation, std::move(command));
        command_panel_mode_ = CommandPanelMode::WorkerActions;
        placement_ghost_anchor_.reset();
        placement_ghost_valid_ = false;
        return;
    }

    if (render_snapshot != nullptr) {
        if (left_press_position_.has_value()) {
            const int dx = mouse_position.x - left_press_position_->x;
            const int dy = mouse_position.y - left_press_position_->y;
            const int drag_distance_sq = dx * dx + dy * dy;
            const int threshold = constants::SELECTION_BOX_DRAG_THRESHOLD_PX;

            if (drag_distance_sq >= threshold * threshold) {
                const sf::Vector2f start{
                    static_cast<float>(left_press_position_->x),
                    static_cast<float>(left_press_position_->y),
                };
                const std::vector<entt::entity> picked = render::pick_player_units_in_screen_rect(
                    *render_snapshot,
                    renderer,
                    start,
                    screen_position,
                    local_player_slot_);
                render::apply_selection_from_snapshot(
                    selection_.units,
                    *render_snapshot,
                    picked,
                    mode,
                    local_player_slot_);
                if (mode == sim::player::SelectionModifyMode::Replace && !picked.empty()) {
                    selection_.clear_resource();
                    selection_.clear_building();
                }
                if (!picked.empty()) {
                    play_select_ack_if_own_units(simulation, render_snapshot);
                }
                return;
            }
        }

        if (hit_test_hud_blocks_world_pick(
                window.getSize(),
                screen_position.x,
                screen_position.y,
                game_menu_.hud_style)) {
            return;
        }

        if (mode != sim::player::SelectionModifyMode::Replace) {
            const entt::entity picked = render::pick_player_unit_at_screen(
                *render_snapshot,
                renderer,
                screen_position,
                pick_radius_px,
                local_player_slot_);

            if (picked == entt::null) {
                return;
            }

            render::apply_selection_from_snapshot(
                selection_.units,
                *render_snapshot,
                {picked},
                mode,
                local_player_slot_);
            selection_.clear_resource();
            selection_.clear_building();
            play_select_ack_if_own_units(simulation, render_snapshot);
            return;
        }

        const entt::entity picked_lake =
            render::pick_mana_lake_at_screen(*render_snapshot, renderer, screen_position);
        if (picked_lake != entt::null) {
            selection_.clear_units();
            selection_.clear_resource();
            selection_.building = picked_lake;
            return;
        }

        const entt::entity picked_unit = render::pick_player_unit_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);

        if (picked_unit != entt::null) {
            selection_.units = {picked_unit};
            selection_.clear_resource();
            selection_.clear_building();
            play_select_ack_if_own_units(simulation, render_snapshot);
            return;
        }

        const entt::entity picked_enemy_unit = render::pick_enemy_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);
        if (picked_enemy_unit != entt::null) {
            selection_.units = {picked_enemy_unit};
            selection_.clear_resource();
            selection_.clear_building();
            return;
        }

        const entt::entity picked_building = render::pick_player_building_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);

        if (picked_building != entt::null) {
            selection_.clear_units();
            selection_.clear_resource();
            selection_.building = picked_building;
            return;
        }

        const entt::entity picked_enemy_building = render::pick_enemy_building_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);
        if (picked_enemy_building != entt::null) {
            selection_.clear_units();
            selection_.clear_resource();
            selection_.building = picked_enemy_building;
            return;
        }

        const std::optional<core::GridPos> picked_resource = render::pick_resource_forest_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);

        if (picked_resource.has_value()) {
            selection_.clear_units();
            selection_.clear_building();
            selection_.resource_cell = picked_resource;
            return;
        }

        selection_.clear();
        return;
    }

    if (left_press_position_.has_value()) {
        const int dx = mouse_position.x - left_press_position_->x;
        const int dy = mouse_position.y - left_press_position_->y;
        const int drag_distance_sq = dx * dx + dy * dy;
        const int threshold = constants::SELECTION_BOX_DRAG_THRESHOLD_PX;

        if (drag_distance_sq >= threshold * threshold) {
            const sf::Vector2f start{
                static_cast<float>(left_press_position_->x),
                static_cast<float>(left_press_position_->y),
            };
            const std::vector<entt::entity> picked = sim::player::pick_player_units_in_screen_rect(
                simulation.registry(),
                renderer,
                start,
                screen_position,
                local_player_slot_);
            sim::player::apply_selection(selection_.units, simulation.registry(), picked, mode);
            if (mode == sim::player::SelectionModifyMode::Replace && !picked.empty()) {
                selection_.clear_resource();
                selection_.clear_building();
            }
            if (!picked.empty()) {
                play_select_ack_if_own_units(simulation, nullptr);
            }
            return;
        }
    }

    if (hit_test_hud_blocks_world_pick(
            window.getSize(),
            screen_position.x,
            screen_position.y,
            game_menu_.hud_style)) {
        return;
    }

    if (mode != sim::player::SelectionModifyMode::Replace) {
        const entt::entity picked = sim::player::pick_player_unit_at_screen(
            simulation.registry(),
            renderer,
            screen_position,
            renderer.selection_pick_radius_px(),
            local_player_slot_);

        if (picked == entt::null) {
            return;
        }

        sim::player::apply_selection(
            selection_.units,
            simulation.registry(),
            {picked},
            mode);
        selection_.clear_resource();
        selection_.clear_building();
        play_select_ack_if_own_units(simulation, nullptr);
        return;
    }

    const entt::entity picked_lake = sim::player::pick_mana_lake_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        local_player_slot_);
    if (picked_lake != entt::null) {
        selection_.clear_units();
        selection_.clear_resource();
        selection_.building = picked_lake;
        return;
    }

    const entt::entity picked_unit = sim::player::pick_player_unit_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);

    if (picked_unit != entt::null) {
        selection_.units = {picked_unit};
        selection_.clear_resource();
        selection_.clear_building();
        play_select_ack_if_own_units(simulation, nullptr);
        return;
    }

    const entt::entity picked_enemy_unit = sim::player::pick_enemy_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);
    if (picked_enemy_unit != entt::null) {
        selection_.units = {picked_enemy_unit};
        selection_.clear_resource();
        selection_.clear_building();
        return;
    }

    const entt::entity picked_building = sim::player::pick_player_building_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);

    if (picked_building != entt::null) {
        selection_.clear_units();
        selection_.clear_resource();
        selection_.building = picked_building;
        return;
    }

    const entt::entity picked_enemy_building = sim::player::pick_enemy_building_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);
    if (picked_enemy_building != entt::null) {
        selection_.clear_units();
        selection_.clear_resource();
        selection_.building = picked_enemy_building;
        return;
    }

    const std::optional<core::GridPos> picked_resource = sim::player::pick_resource_forest_at_screen(
        simulation.registry(),
        renderer,
        screen_position,
        renderer.selection_pick_radius_px(),
        local_player_slot_);

    if (picked_resource.has_value()) {
        selection_.clear_units();
        selection_.clear_building();
        selection_.resource_cell = picked_resource;
        return;
    }

    selection_.clear();
}

void GameInput::sync_diplomacy_draft(sim::Simulation& simulation)
{
    const auto session_view =
        simulation.registry().view<sim::components::WorldTag, sim::components::MatchSession>();
    if (session_view.begin() == session_view.end()) {
        return;
    }

    const auto& session = session_view.get<sim::components::MatchSession>(*session_view.begin());
    if (local_player_slot_ < session.player_ally_mask.size()) {
        const std::uint8_t mask = session.player_ally_mask[local_player_slot_];
        for (std::uint8_t slot = 0U; slot < static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS);
             ++slot) {
            diplomacy_.draft_ally[slot] =
                sim::components::player_slot_bit_is_set(mask, slot) ? 1U : 0U;
        }
        diplomacy_.draft_ally_victory = session.player_ally_victory[local_player_slot_] != 0U;
    }
    diplomacy_.draft_initialized = true;
}

void GameInput::sync_diplomacy_chat_focus()
{
    if (diplomacy_.open && diplomacy_.tab == DiplomacyTab::Chat) {
        diplomacy_.chat_input_focused = true;
        chat_composing_ = true;
        return;
    }

    diplomacy_.chat_input_focused = false;
    chat_composing_ = false;
}

bool GameInput::handle_diplomacy_click(
    const sf::Window& window,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    const float mouse_x,
    const float mouse_y,
    const bool subtract)
{
    const sf::Vector2u window_size = window.getSize();
    if (hud_diplomacy_button_rect(window_size, game_menu_.hud_style)
            .contains(mouse_x, mouse_y)) {
        diplomacy_.open = !diplomacy_.open;
        if (diplomacy_.open) {
            diplomacy_.tab = diplomacy_.last_tab;
            if (!diplomacy_.draft_initialized) {
                sync_diplomacy_draft(simulation);
            }
        }
        sync_diplomacy_chat_focus();
        return true;
    }

    if (!diplomacy_.open) {
        return false;
    }

    if (diplomacy_close_rect(window_size).contains(mouse_x, mouse_y)) {
        diplomacy_.open = false;
        sync_diplomacy_chat_focus();
        return true;
    }

    for (int tab_index = 0; tab_index < 3; ++tab_index) {
        if (diplomacy_tab_rect(window_size, tab_index).contains(mouse_x, mouse_y)) {
            diplomacy_.tab = static_cast<DiplomacyTab>(tab_index);
            diplomacy_.last_tab = diplomacy_.tab;
            sync_diplomacy_chat_focus();
            return true;
        }
    }

    const auto session_view =
        simulation.registry().view<sim::components::WorldTag, sim::components::MatchSession>();
    const sim::components::MatchSession* session = nullptr;
    if (session_view.begin() != session_view.end()) {
        session = &session_view.get<sim::components::MatchSession>(*session_view.begin());
    }

    const auto other_slots = [&]() {
        std::vector<std::uint8_t> slots{};
        for (std::uint8_t slot = 0U; slot < static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS);
             ++slot) {
            if (slot == local_player_slot_) {
                continue;
            }

            if (session != nullptr
                && !sim::components::player_slot_bit_is_set(session->playing_slots_mask, slot)) {
                continue;
            }

            slots.push_back(slot);
        }
        return slots;
    }();

    if (diplomacy_.tab == DiplomacyTab::Chat) {
        for (int subtab = 0; subtab < 2; ++subtab) {
            if (diplomacy_subtab_rect(window_size, subtab).contains(mouse_x, mouse_y)) {
                diplomacy_.chat_subtab = static_cast<DiplomacyChatSubtab>(subtab);
                diplomacy_.hud_send_allies = diplomacy_.chat_subtab == DiplomacyChatSubtab::Allies;
                return true;
            }
        }

        if (diplomacy_chat_send_rect(window_size).contains(mouse_x, mouse_y)) {
            const std::string message = chat_draft_;
            chat_draft_.clear();
            if (!message.empty()) {
                submit_chat_message(message, simulation, render_snapshot);
            }
            sync_diplomacy_chat_focus();
            return true;
        }

        if (diplomacy_chat_input_rect(window_size).contains(mouse_x, mouse_y)) {
            chat_composing_ = true;
            diplomacy_.chat_input_focused = true;
            return true;
        }
    }

    if (diplomacy_.tab == DiplomacyTab::Trades && session != nullptr) {
        if (!sim::components::slot_has_trades(*session, local_player_slot_)) {
            return true;
        }

        const sim::components::Stockpile stockpile =
            sim::player::sum_player_stockpile(simulation.registry(), local_player_slot_);
        int reserved_wood = 0;
        int reserved_food = 0;
        int reserved_gold = 0;
        int reserved_mana = 0;
        for (const std::uint8_t slot : other_slots) {
            reserved_wood += diplomacy_.trade_wood[slot];
            reserved_food += diplomacy_.trade_food[slot];
            reserved_gold += diplomacy_.trade_gold[slot];
            reserved_mana += diplomacy_.trade_mana[slot];
        }

        const bool subtract_amount = subtract;
        for (int row = 0; row < static_cast<int>(other_slots.size()); ++row) {
            const std::uint8_t slot = other_slots[static_cast<std::size_t>(row)];
            const GameMenuRect row_rect = diplomacy_player_row_rect(window_size, row);
            for (int button = 0; button < 4; ++button) {
                if (!diplomacy_row_button_rect(row_rect, button, 4).contains(mouse_x, mouse_y)) {
                    continue;
                }

                int* field = &diplomacy_.trade_wood[slot];
                int step = constants::TRADE_WOOD_STEP;
                int reserved = reserved_wood;
                int available = stockpile.wood;
                if (button == 1) {
                    field = &diplomacy_.trade_food[slot];
                    step = constants::TRADE_FOOD_STEP;
                    reserved = reserved_food;
                    available = stockpile.food;
                }
                else if (button == 2) {
                    field = &diplomacy_.trade_gold[slot];
                    step = constants::TRADE_GOLD_STEP;
                    reserved = reserved_gold;
                    available = stockpile.money;
                }
                else if (button == 3) {
                    field = &diplomacy_.trade_mana[slot];
                    step = constants::TRADE_MANA_STEP;
                    reserved = reserved_mana;
                    available = stockpile.mana;
                }

                if (subtract_amount) {
                    *field = std::max(0, *field - step);
                }
                else if (available - reserved >= step) {
                    *field += step;
                }
                return true;
            }
        }

        if (diplomacy_action_rect(window_size).contains(mouse_x, mouse_y)) {
            for (const std::uint8_t slot : other_slots) {
                const int wood = diplomacy_.trade_wood[slot];
                const int food = diplomacy_.trade_food[slot];
                const int gold = diplomacy_.trade_gold[slot];
                const int mana = diplomacy_.trade_mana[slot];
                if (wood == 0 && food == 0 && gold == 0 && mana == 0) {
                    continue;
                }

                sim::player::PlayerCommand command{};
                if (render_snapshot != nullptr) {
                    command.execute_tick = render_snapshot->tick_count
                        + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
                }
                else {
                    command.execute_tick = simulation.next_command_execute_tick();
                }
                command.player_slot = local_player_slot_;
                command.type = sim::player::PlayerCommandType::SendTrade;
                command.cell = core::GridPos{static_cast<int>(slot), wood};
                command.goal_world_x = math::Fixed::from_int(food);
                command.goal_world_y = math::Fixed::from_int(gold);
                command.target_entity = entt::entity{static_cast<std::uint32_t>(mana)};
                submit_player_command(simulation, std::move(command));
                diplomacy_.trade_wood[slot] = 0;
                diplomacy_.trade_food[slot] = 0;
                diplomacy_.trade_gold[slot] = 0;
                diplomacy_.trade_mana[slot] = 0;
            }
            return true;
        }
    }

    if (diplomacy_.tab == DiplomacyTab::Teams) {
        if (session != nullptr && session->block_team_changes) {
            return true;
        }

        for (int row = 0; row < static_cast<int>(other_slots.size()); ++row) {
            const std::uint8_t slot = other_slots[static_cast<std::size_t>(row)];
            const GameMenuRect row_rect = diplomacy_player_row_rect(window_size, row);
            if (diplomacy_row_button_rect(row_rect, 0, 2).contains(mouse_x, mouse_y)) {
                diplomacy_.draft_ally[slot] = 1U;
                return true;
            }
            if (diplomacy_row_button_rect(row_rect, 1, 2).contains(mouse_x, mouse_y)) {
                diplomacy_.draft_ally[slot] = 0U;
                return true;
            }
        }

        if (diplomacy_ally_victory_rect(window_size).contains(mouse_x, mouse_y)) {
            diplomacy_.draft_ally_victory = !diplomacy_.draft_ally_victory;
            return true;
        }

        if (diplomacy_action_rect(window_size).contains(mouse_x, mouse_y)) {
            std::uint8_t mask = sim::components::player_slot_bit(local_player_slot_);
            for (const std::uint8_t slot : other_slots) {
                if (diplomacy_.draft_ally[slot] != 0U) {
                    mask = static_cast<std::uint8_t>(
                        mask | sim::components::player_slot_bit(slot));
                }
            }

            sim::player::PlayerCommand command{};
            if (render_snapshot != nullptr) {
                command.execute_tick = render_snapshot->tick_count
                    + static_cast<std::uint64_t>(net::constants::LOCKSTEP_COMMAND_DELAY_TICKS);
            }
            else {
                command.execute_tick = simulation.next_command_execute_tick();
            }
            command.player_slot = local_player_slot_;
            command.type = sim::player::PlayerCommandType::SetDiplomacy;
            command.cell = core::GridPos{
                static_cast<int>(mask),
                diplomacy_.draft_ally_victory ? 1 : 0,
            };
            submit_player_command(simulation, std::move(command));
            return true;
        }
    }

    return diplomacy_panel_rect(window_size).contains(mouse_x, mouse_y);
}

void GameInput::submit_chat_message(
    std::string text,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot)
{
    if (text.empty()) {
        return;
    }

    if (text.size() > static_cast<std::size_t>(constants::CHAT_MAX_MESSAGE_LENGTH)) {
        text.resize(static_cast<std::size_t>(constants::CHAT_MAX_MESSAGE_LENGTH));
    }

    bool cheats_enabled = false;
    {
        auto& registry = simulation.registry();
        const auto world_view =
            registry.view<sim::components::WorldTag, sim::components::MatchSession>();
        if (world_view.begin() != world_view.end()) {
            cheats_enabled =
                world_view.get<sim::components::MatchSession>(*world_view.begin()).cheats_enabled;
        }
    }

    if (cheats_enabled && text == constants::CHEAT_OKNOCRAFT_INFINITY) {
        sim::player::PlayerCommand command = make_command(
            simulation,
            sim::player::PlayerCommandType::CheatGrantResources,
            {},
            render_snapshot);
        command.player_slot = local_player_slot_;
        submit_player_command(simulation, std::move(command));
    }

    if (lockstep_session_ != nullptr) {
        const bool allies = (diplomacy_.open && diplomacy_.tab == DiplomacyTab::Chat)
            ? diplomacy_.chat_subtab == DiplomacyChatSubtab::Allies
            : diplomacy_.hud_send_allies;
        lockstep_session_->send_chat_message(
            text, allies ? net::ChatChannel::Allies : net::ChatChannel::All);
        return;
    }

    if (chat_state_ != nullptr) {
        const bool allies = (diplomacy_.open && diplomacy_.tab == DiplomacyTab::Chat)
            ? diplomacy_.chat_subtab == DiplomacyChatSubtab::Allies
            : diplomacy_.hud_send_allies;
        chat_state_->push_message(
            local_player_slot_,
            std::move(text),
            allies ? ChatChannel::Allies : ChatChannel::All);
    }
}

bool GameInput::handle_chat_event(
    const sf::Event& event,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot)
{
    const bool diplomacy_chat = diplomacy_.open && diplomacy_.tab == DiplomacyTab::Chat;
    if (diplomacy_chat) {
        chat_composing_ = true;
        diplomacy_.chat_input_focused = true;
    }

    if (const auto* key_pressed = event.getIf<sf::Event::KeyPressed>()) {
        if (game_menu_.is_open()) {
            return false;
        }

        if (!chat_composing_ && key_pressed->code == sf::Keyboard::Key::Enter) {
            chat_composing_ = true;
            chat_all_selected_ = false;
            return true;
        }

        if (!chat_composing_) {
            return false;
        }

        TextFieldEdit chat_field{
            .text = chat_draft_,
            .max_length = static_cast<std::size_t>(constants::CHAT_MAX_MESSAGE_LENGTH),
            .all_selected = chat_all_selected_,
            .filter = TextFieldFilter::Printable,
        };
        if (apply_text_field_hotkey(key_pressed->code, key_pressed->control, chat_field)) {
            return true;
        }

        if (key_pressed->code == sf::Keyboard::Key::Escape) {
            chat_draft_.clear();
            if (!diplomacy_chat) {
                chat_composing_ = false;
                diplomacy_.chat_input_focused = false;
            }
            return true;
        }

        if (key_pressed->code == sf::Keyboard::Key::Enter) {
            const std::string message = chat_draft_;
            chat_draft_.clear();
            if (!message.empty()) {
                submit_chat_message(message, simulation, render_snapshot);
            }
            if (!diplomacy_chat) {
                chat_composing_ = false;
                diplomacy_.chat_input_focused = false;
            }
            return true;
        }

        if (key_pressed->code == sf::Keyboard::Key::Backspace) {
            apply_text_field_backspace(chat_field);
            return true;
        }

        return true;
    }

    if (const auto* text_entered = event.getIf<sf::Event::TextEntered>()) {
        if (!chat_composing_) {
            return false;
        }

        const auto unicode = text_entered->unicode;
        if (unicode == 8U || unicode == 13U || unicode == 27U) {
            return true;
        }

        if (unicode < 32U || unicode > 126U) {
            return true;
        }

        const char character = static_cast<char>(unicode);
        TextFieldEdit chat_field{
            .text = chat_draft_,
            .max_length = static_cast<std::size_t>(constants::CHAT_MAX_MESSAGE_LENGTH),
            .all_selected = chat_all_selected_,
            .filter = TextFieldFilter::Printable,
        };
        append_text_field_chars(chat_field, std::string_view(&character, 1U));
        return true;
    }

    return false;
}

bool GameInput::try_issue_attack_at_screen(
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot,
    const sf::Vector2f screen_position)
{
    if (hit_test_hud_blocks_world_pick(
            window.getSize(),
            screen_position.x,
            screen_position.y,
            game_menu_.hud_style)) {
        return false;
    }

    if (selection_.units.empty()) {
        return false;
    }

    const float pick_radius_px = renderer.selection_pick_radius_px();
    if (render_snapshot != nullptr) {
        const entt::entity enemy = render::pick_enemy_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);
        if (enemy != entt::null) {
            sim::player::PlayerCommand command = make_command(
                simulation,
                sim::player::PlayerCommandType::Attack,
                selection_.units,
                render_snapshot);
            command.target_entity = enemy;
            submit_player_command(simulation, std::move(command));
            return true;
        }

        const entt::entity enemy_building = render::pick_enemy_building_at_screen(
            *render_snapshot,
            renderer,
            screen_position,
            pick_radius_px,
            local_player_slot_);
        if (enemy_building != entt::null) {
            sim::player::PlayerCommand command = make_command(
                simulation,
                sim::player::PlayerCommandType::Attack,
                selection_.units,
                render_snapshot);
            command.target_entity = enemy_building;
            submit_player_command(simulation, std::move(command));
            return true;
        }

        return false;
    }

    auto& registry = simulation.registry();
    const entt::entity enemy = sim::player::pick_enemy_at_screen(
        registry,
        renderer,
        screen_position,
        pick_radius_px,
        local_player_slot_);
    if (enemy != entt::null) {
        sim::player::PlayerCommand command =
            make_command(simulation, sim::player::PlayerCommandType::Attack, selection_.units, nullptr);
        command.target_entity = enemy;
        submit_player_command(simulation, std::move(command));
        return true;
    }

    const entt::entity enemy_building = sim::player::pick_enemy_building_at_screen(
        registry,
        renderer,
        screen_position,
        pick_radius_px,
        local_player_slot_);
    if (enemy_building != entt::null) {
        sim::player::PlayerCommand command =
            make_command(simulation, sim::player::PlayerCommandType::Attack, selection_.units, nullptr);
        command.target_entity = enemy_building;
        submit_player_command(simulation, std::move(command));
        return true;
    }

    return false;
}

render::HudUnitContext GameInput::make_hud_context(
    const sf::Window& window,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot) const
{
    render::HudUnitContext context{};
    context.command_panel_mode = command_panel_mode_;
    context.build_options = current_build_options(simulation, render_snapshot);
    const sf::Vector2i mouse = sf::Mouse::getPosition(window);
    context.mouse_screen_position = sf::Vector2f{
        static_cast<float>(mouse.x),
        static_cast<float>(mouse.y),
    };
    context.chat_composing = chat_composing_;
    context.chat_draft = chat_draft_;
    context.chat_all_selected = chat_all_selected_;
    context.game_menu = game_menu_;
    context.diplomacy = diplomacy_;
    context.tab_scoreboard = tab_scoreboard_ && !chat_composing_;
    context.local_player_slot = local_player_slot_;
    context.local_is_spectator = local_is_spectator_;
    context.minimap_show_units = minimap_show_units_;
    context.pointer_attack_mode = pointer_targeting_mode_;
    if (chat_state_ != nullptr) {
        context.chat_lines = chat_state_->snapshot();
    }

    context.multiplayer = multiplayer_;
    context.player_names = player_names_;
    {
        const auto session_view = simulation.registry()
            .view<sim::components::WorldTag, sim::components::MatchSession>();
        if (session_view.begin() != session_view.end()) {
            context.has_match_session = true;
            context.match_session =
                session_view.get<sim::components::MatchSession>(*session_view.begin());
        }
    }

    if (command_panel_pressed_slot_ >= 0
        && std::chrono::steady_clock::now() < command_panel_press_until_) {
        context.command_panel_pressed_slot = command_panel_pressed_slot_;
    }

    context.selected_resource_cell = selection_.resource_cell;
    context.hover_unit = hover_.unit;
    context.hover_unit_is_enemy = hover_.unit_is_enemy;
    if (selection_.units.size() == 1U) {
        context.selected_single_unit = selection_.units.front();
    }
    else if (selection_.building != entt::null) {
        context.selected_single_unit = selection_.building;
    }

    if (selection_.building == entt::null) {
        return context;
    }

    if (render_snapshot != nullptr) {
        for (const render::RenderEntityPose& pose : render_snapshot->buildings) {
            if (pose.entity != selection_.building) {
                continue;
            }

            context.selected_building_is_mana_lake = pose.is_mana_lake;
            if (pose.health_current > 0) {
                context.has_selected_building_health = true;
                context.selected_building_health_current = pose.health_current;
                context.selected_building_health_max = pose.health_max;
                context.selected_building_is_house = pose.is_house;
                context.selected_building_is_lumber_camp = pose.is_lumber_camp;
                context.selected_building_is_mill = pose.is_mill;
                context.selected_building_is_mining_camp = pose.is_mining_camp;
                context.selected_building_is_barracks = pose.is_barracks;
                context.selected_building_is_mage_academy = pose.is_mage_academy;
                context.selected_building_is_tower = pose.is_tower;
                context.selected_building_is_market = pose.is_market;
                context.selected_building_is_extractor = pose.is_extractor;
                context.selected_building_is_garden = pose.is_garden;
                context.selected_building_is_reservoir = pose.is_reservoir;
                context.selected_building_is_farm = pose.is_farm;
                context.selected_farm_food_remaining = pose.farm_food_remaining;
                context.selected_farm_food_max = pose.farm_food_max;
                context.selected_garden_percent =
                    constants::GARDEN_PROD_INTERVAL_TICKS <= 0
                    ? 100
                    : ((constants::GARDEN_PROD_INTERVAL_TICKS
                           - std::clamp(
                               pose.mana_gen_ticks_remaining,
                               0,
                               constants::GARDEN_PROD_INTERVAL_TICKS))
                        * 100)
                        / constants::GARDEN_PROD_INTERVAL_TICKS;
                context.selected_building_is_town_center = pose.is_town_center;
                context.selected_building_under_construction = pose.under_construction;
                if (!pose.under_construction) {
                    context.selected_garrison_count = pose.garrison_count;
                    context.selected_garrison_capacity = pose.garrison_capacity;
                }
                context.has_selected_building_owner = true;
                context.selected_building_player_slot = pose.player_slot;
            }
            break;
        }
        return context;
    }

    auto& registry = simulation.registry();
    if (registry.valid(selection_.building)) {
        context.selected_building_is_mana_lake =
            registry.any_of<sim::components::ManaLakeTag>(selection_.building);
        if (registry.any_of<sim::components::Health>(selection_.building)) {
            const auto& health = registry.get<sim::components::Health>(selection_.building);
            if (health.current.raw() > 0) {
                context.has_selected_building_health = true;
                context.selected_building_health_current = health.current.to_int();
                context.selected_building_health_max = health.max.to_int();
                context.selected_building_is_house =
                    registry.any_of<sim::components::HouseTag>(selection_.building);
                context.selected_building_is_lumber_camp =
                    registry.any_of<sim::components::LumberCampTag>(selection_.building);
                context.selected_building_is_mill =
                    registry.any_of<sim::components::MillTag>(selection_.building);
                context.selected_building_is_mining_camp =
                    registry.any_of<sim::components::MiningCampTag>(selection_.building);
                context.selected_building_is_barracks =
                    registry.any_of<sim::components::BarracksTag>(selection_.building);
                context.selected_building_is_mage_academy =
                    registry.any_of<sim::components::MageAcademyTag>(selection_.building);
                context.selected_building_is_tower =
                    registry.any_of<sim::components::TowerTag>(selection_.building);
                context.selected_building_is_market =
                    registry.any_of<sim::components::MarketTag>(selection_.building);
                context.selected_building_is_extractor =
                    registry.any_of<sim::components::ExtractorTag>(selection_.building);
                context.selected_building_is_garden =
                    registry.any_of<sim::components::GardenTag>(selection_.building);
                context.selected_building_is_reservoir =
                    registry.any_of<sim::components::ReservoirTag>(selection_.building);
                context.selected_building_is_farm =
                    registry.any_of<sim::components::FarmTag>(selection_.building);
                if (registry.any_of<sim::components::FarmFood>(selection_.building)) {
                    const auto& farm_food =
                        registry.get<sim::components::FarmFood>(selection_.building);
                    context.selected_farm_food_remaining = farm_food.remaining;
                    context.selected_farm_food_max = farm_food.max;
                }
                if (registry.any_of<sim::components::ManaGenerationCooldown>(selection_.building)
                    && context.selected_building_is_garden) {
                    const int remaining = registry.get<sim::components::ManaGenerationCooldown>(
                        selection_.building).ticks_remaining;
                    context.selected_garden_percent =
                        constants::GARDEN_PROD_INTERVAL_TICKS <= 0
                        ? 100
                        : ((constants::GARDEN_PROD_INTERVAL_TICKS
                               - std::clamp(remaining, 0, constants::GARDEN_PROD_INTERVAL_TICKS))
                            * 100)
                            / constants::GARDEN_PROD_INTERVAL_TICKS;
                }
                context.selected_building_is_town_center =
                    registry.any_of<sim::components::TownCenterTag>(selection_.building);
                context.selected_building_under_construction =
                    registry.any_of<sim::components::UnderConstructionTag>(selection_.building);
                if (registry.any_of<sim::components::GarrisonHold>(selection_.building)
                    && !registry.any_of<sim::components::UnderConstructionTag>(selection_.building)) {
                    const auto& hold =
                        registry.get<sim::components::GarrisonHold>(selection_.building);
                    context.selected_garrison_count = static_cast<int>(hold.units.size());
                    context.selected_garrison_capacity = static_cast<int>(hold.capacity);
                }
                if (registry.any_of<sim::components::PlayerOwnedTag>(selection_.building)) {
                    context.has_selected_building_owner = true;
                    context.selected_building_player_slot =
                        sim::components::entity_player_slot(registry, selection_.building);
                }
            }
        }
    }

    return context;
}

bool GameInput::handle_event(
    const sf::Event& event,
    const sf::Window& window,
    render::GameRenderer& renderer,
    sim::Simulation& simulation,
    const render::SimRenderSnapshot* render_snapshot)
{
    if (handle_chat_event(event, simulation, render_snapshot)) {
        return true;
    }

    if (const auto* key_pressed = event.getIf<sf::Event::KeyPressed>()) {
        if (key_pressed->code == sf::Keyboard::Key::Tab && !chat_composing_) {
            tab_scoreboard_ = true;
            return true;
        }
    }

    if (const auto* key_released = event.getIf<sf::Event::KeyReleased>()) {
        if (key_released->code == sf::Keyboard::Key::Tab) {
            tab_scoreboard_ = false;
            return true;
        }
    }

    if (game_menu_.is_open()) {
        return handle_game_menu_event(event, window, simulation);
    }

    if (sim::systems::match_is_finished(simulation.registry())) {
        if (const auto* mouse_pressed = event.getIf<sf::Event::MouseButtonPressed>()) {
            if (mouse_pressed->button == sf::Mouse::Button::Left) {
                const sf::Vector2f press_screen{
                    static_cast<float>(mouse_pressed->position.x),
                    static_cast<float>(mouse_pressed->position.y),
                };
                if (match_result_exit_button_rect(window.getSize())
                        .contains(press_screen.x, press_screen.y)) {
                    exit_to_main_menu_requested_ = true;
                    return true;
                }
            }
        }

        return event.getIf<sf::Event::MouseButtonPressed>() != nullptr
            || event.getIf<sf::Event::MouseButtonReleased>() != nullptr
            || event.getIf<sf::Event::MouseMoved>() != nullptr
            || event.getIf<sf::Event::KeyPressed>() != nullptr;
    }

    if (const auto* key_pressed = event.getIf<sf::Event::KeyPressed>()) {
        if (key_pressed->code == sf::Keyboard::Key::Escape) {
            if (attack_targeting_mode_ || garrison_targeting_mode_ || pointer_targeting_mode_) {
                attack_targeting_mode_ = false;
                garrison_targeting_mode_ = false;
                pointer_targeting_mode_ = false;
                return true;
            }

            if (is_placement_command_mode(command_panel_mode_)) {
                command_panel_mode_ = selection_has_worker(simulation, render_snapshot)
                    ? build_tree_for_placement(command_panel_mode_)
                    : CommandPanelMode::Empty;
                return true;
            }

            if (is_build_tree_command_mode(command_panel_mode_)) {
                command_panel_mode_ = selection_has_worker(simulation, render_snapshot)
                    ? CommandPanelMode::WorkerActions
                    : CommandPanelMode::Empty;
                return true;
            }

            game_menu_.multiplayer = lockstep_session_ != nullptr;
            game_menu_.open_main();
            return true;
        }

        if (const std::optional<int> slot = command_panel_slot_for_key(key_pressed->code);
            slot.has_value() && command_panel_mode_ != CommandPanelMode::Empty) {
            const CommandPanelAction action = action_for_command_panel_slot(
                command_panel_mode_,
                *slot,
                current_build_options(simulation, render_snapshot));
            if (action == CommandPanelAction::Kill || action == CommandPanelAction::Destroy) {
                return true;
            }
            if (action != CommandPanelAction::None) {
                command_panel_pressed_slot_ = *slot;
                command_panel_press_until_ = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(constants::HUD_COMMAND_PANEL_KEY_PRESS_TTL_MS);
                return apply_command_panel_action(simulation, render_snapshot, action);
            }
        }

        if (key_pressed->code == sf::Keyboard::Key::Delete) {
            const auto slot_actions = command_panel_slot_actions(command_panel_mode_);
            const bool has_kill = std::any_of(
                slot_actions.begin(),
                slot_actions.end(),
                [](const auto& pair) { return pair.second == CommandPanelAction::Kill; });
            const bool has_destroy = std::any_of(
                slot_actions.begin(),
                slot_actions.end(),
                [](const auto& pair) { return pair.second == CommandPanelAction::Destroy; });
            if (has_kill && !selection_.units.empty()) {
                return apply_command_panel_action(
                    simulation, render_snapshot, CommandPanelAction::Kill);
            }
            if (has_destroy && selection_.building != entt::null) {
                return apply_command_panel_action(
                    simulation, render_snapshot, CommandPanelAction::Destroy);
            }
        }
    }

    if (const auto* scroll = event.getIf<sf::Event::MouseWheelScrolled>()) {
        const sf::Vector2u window_size = window.getSize();
        const int direction = scroll->delta > 0.0F ? 1 : (scroll->delta < 0.0F ? -1 : 0);
        renderer.step_zoom_camera(
            direction,
            static_cast<float>(window_size.x) * 0.5F,
            static_cast<float>(window_size.y) * 0.5F);
    }

    if (const auto* mouse_pressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (mouse_pressed->button == sf::Mouse::Button::Left) {
            left_press_position_ = sf::Mouse::getPosition(window);
            const sf::Vector2f press_screen{
                static_cast<float>(left_press_position_->x),
                static_cast<float>(left_press_position_->y),
            };
            if (hud_menu_button_rect(window.getSize(), game_menu_.hud_style)
                    .contains(press_screen.x, press_screen.y)) {
                game_menu_.multiplayer = lockstep_session_ != nullptr;
                game_menu_.open_main();
                left_button_down_ = false;
                left_press_position_.reset();
                selection_box_.active = false;
                return true;
            }

            if (handle_diplomacy_click(
                    window,
                    simulation,
                    render_snapshot,
                    press_screen.x,
                    press_screen.y)) {
                left_button_down_ = false;
                left_press_position_.reset();
                selection_box_.active = false;
                return true;
            }

            if (chat_composing_
                && chat_channel_toggle_rect(window.getSize())
                    .contains(press_screen.x, press_screen.y)) {
                diplomacy_.hud_send_allies = !diplomacy_.hud_send_allies;
                left_button_down_ = false;
                left_press_position_.reset();
                selection_box_.active = false;
                return true;
            }

            left_button_down_ = true;
            const constants::HudStyle hud_style = game_menu_.hud_style;
            if (!game_menu_.is_open()
                && hit_test_mode_button(
                    minimap_mode_button_rect(window.getSize(), hud_style),
                    press_screen.x,
                    press_screen.y)) {
                minimap_show_units_ = !minimap_show_units_;
                selection_box_.active = false;
                left_button_down_ = false;
                left_press_position_.reset();
                return true;
            }

            if (!game_menu_.is_open()
                && hit_test_mode_button(
                    pointer_mode_button_rect(window.getSize(), hud_style),
                    press_screen.x,
                    press_screen.y)) {
                pointer_targeting_mode_ = !pointer_targeting_mode_;
                attack_targeting_mode_ = false;
                selection_box_.active = false;
                left_button_down_ = false;
                left_press_position_.reset();
                return true;
            }

            if (pointer_targeting_mode_ && !game_menu_.is_open()) {
                selection_box_.active = false;
                left_button_down_ = false;
                left_press_position_.reset();
                if (hit_test_minimap_panel_frame(
                        window.getSize(),
                        press_screen.x,
                        press_screen.y,
                        hud_style)) {
                    int map_width = 0;
                    int map_height = 0;
                    if (resolve_map_size(simulation, render_snapshot, map_width, map_height)) {
                        const auto world = minimap_screen_to_world(
                            window.getSize(),
                            press_screen.x,
                            press_screen.y,
                            map_width,
                            map_height,
                            hud_style);
                        if (world.has_value()
                            && submit_map_ping(
                                simulation,
                                render_snapshot,
                                core::GridPos{
                                    static_cast<int>(std::floor(world->first)),
                                    static_cast<int>(std::floor(world->second)),
                                })) {
                            pointer_targeting_mode_ = false;
                        }
                    }
                    return true;
                }

                if (!hit_test_hud_blocks_world_pick(
                        window.getSize(),
                        press_screen.x,
                        press_screen.y,
                        hud_style)) {
                    const auto cell = renderer.screen_to_grid(press_screen.x, press_screen.y);
                    if (cell.has_value()
                        && submit_map_ping(simulation, render_snapshot, *cell)) {
                        pointer_targeting_mode_ = false;
                    }
                }
                return true;
            }

            minimap_navigation_active_ = !game_menu_.is_open()
                && hit_test_minimap_panel_frame(
                    window.getSize(),
                    press_screen.x,
                    press_screen.y,
                    hud_style);
            if (minimap_navigation_active_) {
                selection_box_.active = false;
                (void)handle_minimap_navigation(
                    window,
                    renderer,
                    simulation,
                    render_snapshot,
                    press_screen);
            }
            else if (hit_test_hud_blocks_world_pick(
                         window.getSize(),
                         press_screen.x,
                         press_screen.y,
                         game_menu_.hud_style)) {
                selection_box_.active = false;
            }
            else {
                selection_box_.active = true;
                selection_box_.start = press_screen;
                selection_box_.current = selection_box_.start;
            }
        }
        else if (mouse_pressed->button == sf::Mouse::Button::Right) {
            if (pointer_targeting_mode_) {
                pointer_targeting_mode_ = false;
                return true;
            }

            const sf::Vector2i mouse_position = sf::Mouse::getPosition(window);
            const sf::Vector2f screen_position{
                static_cast<float>(mouse_position.x),
                static_cast<float>(mouse_position.y),
            };
            if (handle_diplomacy_click(
                    window,
                    simulation,
                    render_snapshot,
                    screen_position.x,
                    screen_position.y,
                    true)) {
                return true;
            }

            if (is_placement_command_mode(command_panel_mode_)) {
                command_panel_mode_ = selection_has_worker(simulation, render_snapshot)
                    ? build_tree_for_placement(command_panel_mode_)
                    : CommandPanelMode::Empty;
                return true;
            }

            if (is_build_tree_command_mode(command_panel_mode_)) {
                command_panel_mode_ = selection_has_worker(simulation, render_snapshot)
                    ? CommandPanelMode::WorkerActions
                    : CommandPanelMode::Empty;
                return true;
            }

            if (selection_.units.empty()) {
                return true;
            }

            if (!selection_has_worker(simulation, render_snapshot)
                && !selection_has_militia(simulation, render_snapshot)
                && !selection_has_mage(simulation, render_snapshot)) {
                return true;
            }

            const float pick_radius_px = renderer.selection_pick_radius_px();

            if (hit_test_minimap_panel_frame(
                    window.getSize(),
                    screen_position.x,
                    screen_position.y,
                    game_menu_.hud_style)) {
                int map_width = 0;
                int map_height = 0;
                if (!resolve_map_size(simulation, render_snapshot, map_width, map_height)) {
                    return true;
                }

                const auto world = minimap_screen_to_world(
                    window.getSize(),
                    screen_position.x,
                    screen_position.y,
                    map_width,
                    map_height,
                    game_menu_.hud_style);
                if (!world.has_value()) {
                    return true;
                }

                const core::GridPos goal{
                    static_cast<int>(std::floor(world->first)),
                    static_cast<int>(std::floor(world->second)),
                };
                if (!core::is_inside_grid(goal, map_width, map_height)) {
                    return true;
                }

                sim::player::PlayerCommand command = make_command(
                    simulation,
                    sim::player::PlayerCommandType::Move,
                    selection_.units,
                    render_snapshot);
                command.cell = goal;
                command.has_goal_world = true;
                command.goal_world_x = math::Fixed::from_float(world->first);
                command.goal_world_y = math::Fixed::from_float(world->second);
                submit_player_command(simulation, std::move(command));
                return true;
            }

            if (hit_test_hud_blocks_world_pick(
                    window.getSize(),
                    screen_position.x,
                    screen_position.y,
                    game_menu_.hud_style)) {
                return true;
            }

            if (render_snapshot != nullptr) {
                const entt::entity enemy = render::pick_enemy_at_screen(
                    *render_snapshot,
                    renderer,
                    screen_position,
                    pick_radius_px,
                    local_player_slot_);
                if (enemy != entt::null) {
                    sim::player::PlayerCommand command = make_command(
                        simulation,
                        sim::player::PlayerCommandType::Attack,
                        selection_.units,
                        render_snapshot);
                    command.target_entity = enemy;
                    submit_player_command(simulation, std::move(command));
                    return true;
                }

                const entt::entity enemy_building = render::pick_enemy_building_at_screen(
                    *render_snapshot,
                    renderer,
                    screen_position,
                    pick_radius_px,
                    local_player_slot_);
                if (enemy_building != entt::null) {
                    sim::player::PlayerCommand command = make_command(
                        simulation,
                        sim::player::PlayerCommandType::Attack,
                        selection_.units,
                        render_snapshot);
                    command.target_entity = enemy_building;
                    submit_player_command(simulation, std::move(command));
                    return true;
                }

                const std::optional<core::GridPos> forest_cell = render::pick_resource_forest_at_screen(
                    *render_snapshot,
                    renderer,
                    screen_position,
                    pick_radius_px,
                    local_player_slot_);
                if (forest_cell.has_value()
                    && selection_has_worker(simulation, render_snapshot)) {
                    sim::player::PlayerCommand command = make_command(
                        simulation,
                        sim::player::PlayerCommandType::Gather,
                        selection_.units,
                        render_snapshot);
                    command.cell = *forest_cell;
                    submit_player_command(simulation, std::move(command));
                    return true;
                }

                const entt::entity player_building = render::pick_player_building_at_screen(
                    *render_snapshot,
                    renderer,
                    screen_position,
                    pick_radius_px,
                    local_player_slot_);
                if (garrison_targeting_mode_) {
                    if (try_issue_garrison_on_building(
                            simulation, render_snapshot, player_building)) {
                        return true;
                    }

                    garrison_targeting_mode_ = false;
                }

                if (player_building != entt::null) {
                    bool under_construction = false;
                    bool is_town_center = false;
                    bool is_farm = false;
                    int farm_food_remaining = 0;
                    core::GridPos farm_cell{};
                    for (const render::RenderEntityPose& pose : render_snapshot->buildings) {
                        if (pose.entity != player_building) {
                            continue;
                        }

                        under_construction = pose.under_construction;
                        is_town_center = pose.is_town_center;
                        is_farm = pose.is_farm;
                        farm_food_remaining = pose.farm_food_remaining;
                        farm_cell = core::GridPos{pose.grid_x, pose.grid_y};
                        break;
                    }

                    if (is_farm && selection_has_worker(simulation, render_snapshot)) {
                        if (under_construction) {
                            sim::player::PlayerCommand command = make_command(
                                simulation,
                                sim::player::PlayerCommandType::ResumeBuild,
                                selection_.units,
                                render_snapshot);
                            command.target_entity = player_building;
                            submit_player_command(simulation, std::move(command));
                            return true;
                        }

                        if (farm_food_remaining > 0) {
                            sim::player::PlayerCommand command = make_command(
                                simulation,
                                sim::player::PlayerCommandType::Gather,
                                selection_.units,
                                render_snapshot);
                            command.cell = farm_cell;
                            submit_player_command(simulation, std::move(command));
                            return true;
                        }

                        sim::player::PlayerCommand command = make_command(
                            simulation,
                            sim::player::PlayerCommandType::RenewFarm,
                            selection_.units,
                            render_snapshot);
                        command.target_entity = player_building;
                        submit_player_command(simulation, std::move(command));
                        return true;
                    }

                    if (under_construction && selection_has_worker(simulation, render_snapshot)) {
                        sim::player::PlayerCommand command = make_command(
                            simulation,
                            sim::player::PlayerCommandType::ResumeBuild,
                            selection_.units,
                            render_snapshot);
                        command.target_entity = player_building;
                        submit_player_command(simulation, std::move(command));
                        return true;
                    }

                    if (is_town_center) {
                        submit_player_command(
                            simulation,
                            make_command(
                                simulation,
                                sim::player::PlayerCommandType::Deposit,
                                selection_.units,
                                render_snapshot));
                        return true;
                    }
                }

                const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
                if (!grid_cell.has_value()) {
                    return true;
                }

                if (!core::is_inside_grid(
                        *grid_cell,
                        render_snapshot->map_width,
                        render_snapshot->map_height)) {
                    return true;
                }

                if (render::snapshot_has_town_center_at_cell(
                        *render_snapshot,
                        *grid_cell,
                        local_player_slot_)) {
                    submit_player_command(
                        simulation,
                        make_command(
                            simulation,
                            sim::player::PlayerCommandType::Deposit,
                            selection_.units,
                            render_snapshot));
                    return true;
                }

                sim::player::PlayerCommand command = make_command(
                    simulation,
                    sim::player::PlayerCommandType::Move,
                    selection_.units,
                    render_snapshot);
                fill_move_command_from_screen(command, renderer, *grid_cell, screen_position);
                submit_player_command(simulation, std::move(command));
                return true;
            }

            auto& registry = simulation.registry();
            const entt::entity enemy = sim::player::pick_enemy_at_screen(
                registry,
                renderer,
                screen_position,
                renderer.selection_pick_radius_px(),
                local_player_slot_);
            if (enemy != entt::null) {
                sim::player::PlayerCommand command =
                    make_command(simulation, sim::player::PlayerCommandType::Attack, selection_.units, nullptr);
                command.target_entity = enemy;
                submit_player_command(simulation, std::move(command));
                return true;
            }

            const entt::entity enemy_building = sim::player::pick_enemy_building_at_screen(
                registry,
                renderer,
                screen_position,
                pick_radius_px,
                local_player_slot_);
            if (enemy_building != entt::null) {
                sim::player::PlayerCommand command =
                    make_command(simulation, sim::player::PlayerCommandType::Attack, selection_.units, nullptr);
                command.target_entity = enemy_building;
                submit_player_command(simulation, std::move(command));
                return true;
            }

            const std::optional<core::GridPos> forest_cell = sim::player::pick_resource_forest_at_screen(
                registry,
                renderer,
                screen_position,
                pick_radius_px,
                local_player_slot_);
            if (forest_cell.has_value() && selection_has_worker(simulation, nullptr)) {
                sim::player::PlayerCommand command =
                    make_command(simulation, sim::player::PlayerCommandType::Gather, selection_.units, nullptr);
                command.cell = *forest_cell;
                submit_player_command(simulation, std::move(command));
                return true;
            }

            const entt::entity player_building = sim::player::pick_player_building_at_screen(
                registry,
                renderer,
                screen_position,
                pick_radius_px,
                local_player_slot_);
            if (garrison_targeting_mode_) {
                if (try_issue_garrison_on_building(simulation, nullptr, player_building)) {
                    return true;
                }

                garrison_targeting_mode_ = false;
            }

            if (player_building != entt::null) {
                if (registry.any_of<sim::components::FarmTag>(player_building)
                    && selection_has_worker(simulation, nullptr)) {
                    if (registry.any_of<sim::components::UnderConstructionTag>(player_building)) {
                        sim::player::PlayerCommand command = make_command(
                            simulation,
                            sim::player::PlayerCommandType::ResumeBuild,
                            selection_.units,
                            nullptr);
                        command.target_entity = player_building;
                        submit_player_command(simulation, std::move(command));
                        return true;
                    }

                    const int farm_food =
                        registry.any_of<sim::components::FarmFood>(player_building)
                        ? registry.get<sim::components::FarmFood>(player_building).remaining
                        : 0;
                    if (farm_food > 0) {
                        sim::player::PlayerCommand command = make_command(
                            simulation,
                            sim::player::PlayerCommandType::Gather,
                            selection_.units,
                            nullptr);
                        command.cell = registry.get<sim::components::GridPosition>(player_building).cell;
                        submit_player_command(simulation, std::move(command));
                        return true;
                    }

                    sim::player::PlayerCommand command = make_command(
                        simulation,
                        sim::player::PlayerCommandType::RenewFarm,
                        selection_.units,
                        nullptr);
                    command.target_entity = player_building;
                    submit_player_command(simulation, std::move(command));
                    return true;
                }

                if (registry.any_of<sim::components::UnderConstructionTag>(player_building)
                    && selection_has_worker(simulation, nullptr)) {
                    sim::player::PlayerCommand command = make_command(
                        simulation,
                        sim::player::PlayerCommandType::ResumeBuild,
                        selection_.units,
                        nullptr);
                    command.target_entity = player_building;
                    submit_player_command(simulation, std::move(command));
                    return true;
                }

                if (registry.any_of<sim::components::TownCenterTag>(player_building)) {
                    submit_player_command(
                        simulation,
                        make_command(
                            simulation,
                            sim::player::PlayerCommandType::Deposit,
                            selection_.units,
                            nullptr));
                    return true;
                }
            }

            const auto grid_cell = renderer.screen_to_grid(screen_position.x, screen_position.y);
            if (!grid_cell.has_value()) {
                return true;
            }

            const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
            if (world_view.begin() == world_view.end()) {
                return true;
            }

            const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
            if (!core::is_inside_grid(*grid_cell, map.width, map.height)) {
                return true;
            }

            const auto town_center_at_cell = registry.view<
                sim::components::TownCenterTag,
                sim::components::PlayerOwnedTag,
                sim::components::PlayerSlot,
                sim::components::GridPosition>();
            for (const entt::entity entity : town_center_at_cell) {
                if (town_center_at_cell.get<sim::components::PlayerSlot>(entity).value != local_player_slot_) {
                    continue;
                }

                if (town_center_at_cell.get<sim::components::GridPosition>(entity).cell != *grid_cell) {
                    continue;
                }

                submit_player_command(
                    simulation,
                    make_command(
                        simulation,
                        sim::player::PlayerCommandType::Deposit,
                        selection_.units,
                        nullptr));
                return true;
            }

            sim::player::PlayerCommand command =
                make_command(simulation, sim::player::PlayerCommandType::Move, selection_.units, nullptr);
            fill_move_command_from_screen(command, renderer, *grid_cell, screen_position);
            submit_player_command(simulation, std::move(command));
        }
    }

    if (const auto* mouse_released = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (mouse_released->button == sf::Mouse::Button::Left && left_button_down_) {
            left_button_down_ = false;
            finalize_left_release(
                window,
                renderer,
                simulation,
                sf::Mouse::getPosition(window),
                render_snapshot);
            left_press_position_.reset();
            selection_box_.active = false;
            return true;
        }
    }

    return false;
}

} // namespace aoa::app
