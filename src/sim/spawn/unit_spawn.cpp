#include "sim/spawn/unit_spawn.hpp"

#include "core/constants.hpp"
#include "math/fixed.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/components/combat.hpp"
#include "sim/components/definition_ref.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"
#include "sim/systems/match_outcome.hpp"

#include <algorithm>
#include <string>

namespace aoa::sim::spawn {

namespace {

entt::entity spawn_owned_building(
    entt::registry& registry,
    const data::ArchetypeDefinition& archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const bool under_construction,
    const snapshot::EntitySnapshotCategory category)
{
    const entt::entity building = registry.create();
    registry.emplace<components::BuildingTag>(building);
    registry.emplace<components::PlayerOwnedTag>(building);
    registry.emplace<components::PlayerSlot>(building, components::PlayerSlot{player_slot});
    registry.emplace<components::GridPosition>(building, components::GridPosition{spawn_cell});
    registry.emplace<components::BuildingFootprint>(
        building,
        components::BuildingFootprint{
            std::max(1, archetype.footprint_width),
            std::max(1, archetype.footprint_height),
        });
    registry.emplace<components::DefinitionRef>(
        building,
        components::DefinitionRef{std::string(archetype.id)});
    if (under_construction) {
        registry.emplace<components::UnderConstructionTag>(building);
        registry.emplace<components::Health>(
            building,
            components::Health{math::Fixed::from_int(1), archetype.max_hp});
    }
    else {
        registry.emplace<components::Health>(
            building,
            components::Health{archetype.max_hp, archetype.max_hp});
    }

    snapshot::set_entity_snapshot_identity(
        registry,
        building,
        snapshot::EntitySnapshotKey{
            player_slot,
            category,
            snapshot::next_entity_snapshot_ordinal(registry, player_slot, category)});
    systems::note_building_created(registry, player_slot);
    return building;
}

} // namespace

entt::entity spawn_player_worker(
    entt::registry& registry,
    const data::ArchetypeDefinition& worker_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot)
{
    const entt::entity entity = registry.create();
    registry.emplace<components::UnitTag>(entity);
    registry.emplace<components::PlayerOwnedTag>(entity);
    registry.emplace<components::PlayerSlot>(entity, components::PlayerSlot{player_slot});
    registry.emplace<components::WorkerUnitTag>(entity);
    registry.emplace<components::WorkerBrain>(entity);
    // Stay idle until the player (or AI takeover) issues an order.
    registry.emplace<components::ManualControlTag>(entity);
    registry.emplace<components::CarriedWood>(entity);
    registry.emplace<components::CarriedFood>(entity);
    registry.emplace<components::CarriedMoney>(entity);
    registry.emplace<components::GatherCooldown>(entity);
    registry.emplace<components::GridPosition>(entity, components::GridPosition{spawn_cell});
    registry.emplace<components::DefinitionRef>(
        entity,
        components::DefinitionRef{std::string(worker_archetype.id)});
    registry.emplace<components::Health>(
        entity,
        components::Health{worker_archetype.max_hp, worker_archetype.max_hp});
    registry.emplace<components::MoveCooldown>(entity);
    registry.emplace<components::AttackCooldown>(entity);
    registry.emplace<components::WorldPosition>(
        entity,
        components::WorldPosition{
            math::tile_center_coord(spawn_cell.x),
            math::tile_center_coord(spawn_cell.y)});
    registry.emplace<components::PreviousWorldPosition>(
        entity,
        components::PreviousWorldPosition{
            math::tile_center_coord(spawn_cell.x),
            math::tile_center_coord(spawn_cell.y)});

    snapshot::set_entity_snapshot_identity(
        registry,
        entity,
        snapshot::EntitySnapshotKey{
            player_slot,
            snapshot::EntitySnapshotCategory::Worker,
            snapshot::next_entity_snapshot_ordinal(
                registry,
                player_slot,
                snapshot::EntitySnapshotCategory::Worker)});

    systems::note_unit_created(registry, player_slot);
    return entity;
}

entt::entity spawn_player_militia(
    entt::registry& registry,
    const data::ArchetypeDefinition& militia_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot)
{
    const entt::entity entity = registry.create();
    registry.emplace<components::UnitTag>(entity);
    registry.emplace<components::PlayerOwnedTag>(entity);
    registry.emplace<components::PlayerSlot>(entity, components::PlayerSlot{player_slot});
    registry.emplace<components::MilitiaUnitTag>(entity);
    registry.emplace<components::GridPosition>(entity, components::GridPosition{spawn_cell});
    registry.emplace<components::DefinitionRef>(
        entity,
        components::DefinitionRef{std::string(militia_archetype.id)});
    registry.emplace<components::Health>(
        entity,
        components::Health{militia_archetype.max_hp, militia_archetype.max_hp});
    registry.emplace<components::MoveCooldown>(entity);
    registry.emplace<components::AttackCooldown>(entity);
    registry.emplace<components::WorldPosition>(
        entity,
        components::WorldPosition{
            math::tile_center_coord(spawn_cell.x),
            math::tile_center_coord(spawn_cell.y)});
    registry.emplace<components::PreviousWorldPosition>(
        entity,
        components::PreviousWorldPosition{
            math::tile_center_coord(spawn_cell.x),
            math::tile_center_coord(spawn_cell.y)});

    snapshot::set_entity_snapshot_identity(
        registry,
        entity,
        snapshot::EntitySnapshotKey{
            player_slot,
            snapshot::EntitySnapshotCategory::Militia,
            snapshot::next_entity_snapshot_ordinal(
                registry,
                player_slot,
                snapshot::EntitySnapshotCategory::Militia)});

    systems::note_unit_created(registry, player_slot);
    return entity;
}

entt::entity spawn_player_mage(
    entt::registry& registry,
    const data::ArchetypeDefinition& mage_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot)
{
    const entt::entity entity = registry.create();
    registry.emplace<components::UnitTag>(entity);
    registry.emplace<components::PlayerOwnedTag>(entity);
    registry.emplace<components::PlayerSlot>(entity, components::PlayerSlot{player_slot});
    registry.emplace<components::MageUnitTag>(entity);
    registry.emplace<components::GridPosition>(entity, components::GridPosition{spawn_cell});
    registry.emplace<components::DefinitionRef>(
        entity,
        components::DefinitionRef{std::string(mage_archetype.id)});
    registry.emplace<components::Health>(
        entity,
        components::Health{mage_archetype.max_hp, mage_archetype.max_hp});
    registry.emplace<components::MoveCooldown>(entity);
    registry.emplace<components::AttackCooldown>(entity);
    registry.emplace<components::WorldPosition>(
        entity,
        components::WorldPosition{
            math::tile_center_coord(spawn_cell.x),
            math::tile_center_coord(spawn_cell.y)});
    registry.emplace<components::PreviousWorldPosition>(
        entity,
        components::PreviousWorldPosition{
            math::tile_center_coord(spawn_cell.x),
            math::tile_center_coord(spawn_cell.y)});

    snapshot::set_entity_snapshot_identity(
        registry,
        entity,
        snapshot::EntitySnapshotKey{
            player_slot,
            snapshot::EntitySnapshotCategory::Mage,
            snapshot::next_entity_snapshot_ordinal(
                registry,
                player_slot,
                snapshot::EntitySnapshotCategory::Mage)});

    systems::note_unit_created(registry, player_slot);
    return entity;
}

entt::entity spawn_player_town_center(
    entt::registry& registry,
    const data::ArchetypeDefinition& town_center_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const components::Stockpile starting_stockpile,
    const bool under_construction)
{
    const entt::entity town_center = registry.create();
    registry.emplace<components::BuildingTag>(town_center);
    registry.emplace<components::TownCenterTag>(town_center);
    registry.emplace<components::WoodDropOffTag>(town_center);
    registry.emplace<components::FoodDropOffTag>(town_center);
    registry.emplace<components::MoneyDropOffTag>(town_center);
    registry.emplace<components::GarrisonHold>(
        town_center,
        components::GarrisonHold{{}, static_cast<std::uint8_t>(constants::TC_GARRISON_CAPACITY)});
    registry.emplace<components::AttackCooldown>(town_center);
    registry.emplace<components::PlayerOwnedTag>(town_center);
    registry.emplace<components::PlayerSlot>(town_center, components::PlayerSlot{player_slot});
    registry.emplace<components::GridPosition>(town_center, components::GridPosition{spawn_cell});
    registry.emplace<components::BuildingFootprint>(
        town_center,
        components::BuildingFootprint{
            std::max(1, town_center_archetype.footprint_width),
            std::max(1, town_center_archetype.footprint_height),
        });
    registry.emplace<components::DefinitionRef>(
        town_center,
        components::DefinitionRef{std::string(town_center_archetype.id)});
    if (under_construction) {
        registry.emplace<components::UnderConstructionTag>(town_center);
        registry.emplace<components::Health>(
            town_center,
            components::Health{math::Fixed::from_int(1), town_center_archetype.max_hp});
        registry.emplace<components::Stockpile>(town_center, components::Stockpile{});
    }
    else {
        registry.emplace<components::Health>(
            town_center,
            components::Health{town_center_archetype.max_hp, town_center_archetype.max_hp});
        registry.emplace<components::Stockpile>(town_center, starting_stockpile);
    }
    snapshot::set_entity_snapshot_identity(
        registry,
        town_center,
        snapshot::EntitySnapshotKey{
            player_slot,
            snapshot::EntitySnapshotCategory::TownCenter,
            snapshot::next_entity_snapshot_ordinal(
                registry,
                player_slot,
                snapshot::EntitySnapshotCategory::TownCenter)});
    systems::note_building_created(registry, player_slot);
    return town_center;
}

entt::entity spawn_player_house(
    entt::registry& registry,
    const data::ArchetypeDefinition& house_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const bool under_construction)
{
    const entt::entity house = registry.create();
    registry.emplace<components::BuildingTag>(house);
    registry.emplace<components::HouseTag>(house);
    registry.emplace<components::PlayerOwnedTag>(house);
    registry.emplace<components::PlayerSlot>(house, components::PlayerSlot{player_slot});
    registry.emplace<components::GridPosition>(house, components::GridPosition{spawn_cell});
    registry.emplace<components::BuildingFootprint>(
        house,
        components::BuildingFootprint{
            std::max(1, house_archetype.footprint_width),
            std::max(1, house_archetype.footprint_height),
        });
    registry.emplace<components::DefinitionRef>(
        house,
        components::DefinitionRef{std::string(house_archetype.id)});
    const unsigned variant_hash =
        static_cast<unsigned>(spawn_cell.x) * 73856093U
        ^ static_cast<unsigned>(spawn_cell.y) * 19349663U
        ^ static_cast<unsigned>(player_slot) * 83492791U;
    registry.emplace<components::BuildingVisualVariant>(
        house,
        components::BuildingVisualVariant{
            static_cast<std::uint8_t>(
                variant_hash % static_cast<unsigned>(constants::HOUSE_VISUAL_VARIANT_COUNT))});
    if (under_construction) {
        registry.emplace<components::UnderConstructionTag>(house);
        registry.emplace<components::Health>(
            house,
            components::Health{math::Fixed::from_int(1), house_archetype.max_hp});
    }
    else {
        registry.emplace<components::Health>(
            house,
            components::Health{house_archetype.max_hp, house_archetype.max_hp});
    }

    snapshot::set_entity_snapshot_identity(
        registry,
        house,
        snapshot::EntitySnapshotKey{
            player_slot,
            snapshot::EntitySnapshotCategory::House,
            snapshot::next_entity_snapshot_ordinal(
                registry,
                player_slot,
                snapshot::EntitySnapshotCategory::House)});
    systems::note_building_created(registry, player_slot);
    return house;
}

entt::entity spawn_player_lumber_camp(
    entt::registry& registry,
    const data::ArchetypeDefinition& lumber_camp_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const bool under_construction)
{
    const entt::entity lumberjack = registry.create();
    registry.emplace<components::BuildingTag>(lumberjack);
    registry.emplace<components::LumberCampTag>(lumberjack);
    registry.emplace<components::WoodDropOffTag>(lumberjack);
    registry.emplace<components::PlayerOwnedTag>(lumberjack);
    registry.emplace<components::PlayerSlot>(lumberjack, components::PlayerSlot{player_slot});
    registry.emplace<components::GridPosition>(lumberjack, components::GridPosition{spawn_cell});
    registry.emplace<components::BuildingFootprint>(
        lumberjack,
        components::BuildingFootprint{
            std::max(1, lumber_camp_archetype.footprint_width),
            std::max(1, lumber_camp_archetype.footprint_height),
        });
    registry.emplace<components::DefinitionRef>(
        lumberjack,
        components::DefinitionRef{std::string(lumber_camp_archetype.id)});
    if (under_construction) {
        registry.emplace<components::UnderConstructionTag>(lumberjack);
        registry.emplace<components::Health>(
            lumberjack,
            components::Health{math::Fixed::from_int(1), lumber_camp_archetype.max_hp});
    }
    else {
        registry.emplace<components::Health>(
            lumberjack,
            components::Health{lumber_camp_archetype.max_hp, lumber_camp_archetype.max_hp});
    }

    snapshot::set_entity_snapshot_identity(
        registry,
        lumberjack,
        snapshot::EntitySnapshotKey{
            player_slot,
            snapshot::EntitySnapshotCategory::LumberCamp,
            snapshot::next_entity_snapshot_ordinal(
                registry,
                player_slot,
                snapshot::EntitySnapshotCategory::LumberCamp)});
    systems::note_building_created(registry, player_slot);
    return lumberjack;
}

entt::entity spawn_mana_lake(
    entt::registry& registry,
    const data::ArchetypeDefinition& mana_lake_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t near_player_slot)
{
    const entt::entity lake = registry.create();
    registry.emplace<components::ManaLakeTag>(lake);
    registry.emplace<components::PlayerSlot>(lake, components::PlayerSlot{near_player_slot});
    registry.emplace<components::GridPosition>(lake, components::GridPosition{spawn_cell});
    registry.emplace<components::BuildingFootprint>(
        lake,
        components::BuildingFootprint{
            std::max(1, mana_lake_archetype.footprint_width),
            std::max(1, mana_lake_archetype.footprint_height),
        });
    registry.emplace<components::DefinitionRef>(
        lake,
        components::DefinitionRef{std::string(mana_lake_archetype.id)});

    snapshot::set_entity_snapshot_identity(
        registry,
        lake,
        snapshot::EntitySnapshotKey{
            near_player_slot,
            snapshot::EntitySnapshotCategory::ManaLake,
            snapshot::next_entity_snapshot_ordinal(
                registry,
                near_player_slot,
                snapshot::EntitySnapshotCategory::ManaLake)});
    return lake;
}

entt::entity spawn_player_extractor(
    entt::registry& registry,
    const data::ArchetypeDefinition& extractor_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const bool under_construction)
{
    const entt::entity extractor = registry.create();
    registry.emplace<components::BuildingTag>(extractor);
    registry.emplace<components::ExtractorTag>(extractor);
    registry.emplace<components::PlayerOwnedTag>(extractor);
    registry.emplace<components::PlayerSlot>(extractor, components::PlayerSlot{player_slot});
    registry.emplace<components::GridPosition>(extractor, components::GridPosition{spawn_cell});
    registry.emplace<components::BuildingFootprint>(
        extractor,
        components::BuildingFootprint{
            std::max(1, extractor_archetype.footprint_width),
            std::max(1, extractor_archetype.footprint_height),
        });
    registry.emplace<components::DefinitionRef>(
        extractor,
        components::DefinitionRef{std::string(extractor_archetype.id)});
    registry.emplace<components::ManaLakeRef>(
        extractor,
        components::ManaLakeRef{find_mana_lake_at_anchor(registry, spawn_cell)});
    if (under_construction) {
        registry.emplace<components::UnderConstructionTag>(extractor);
        registry.emplace<components::Health>(
            extractor,
            components::Health{math::Fixed::from_int(1), extractor_archetype.max_hp});
    }
    else {
        registry.emplace<components::Health>(
            extractor,
            components::Health{extractor_archetype.max_hp, extractor_archetype.max_hp});
        registry.emplace<components::ManaGenerationCooldown>(
            extractor,
            components::ManaGenerationCooldown{constants::EXTRACTOR_MANA_GEN_INTERVAL_TICKS});
    }

    snapshot::set_entity_snapshot_identity(
        registry,
        extractor,
        snapshot::EntitySnapshotKey{
            player_slot,
            snapshot::EntitySnapshotCategory::Extractor,
            snapshot::next_entity_snapshot_ordinal(
                registry,
                player_slot,
                snapshot::EntitySnapshotCategory::Extractor)});
    systems::note_building_created(registry, player_slot);
    return extractor;
}

entt::entity find_mana_lake_at_anchor(entt::registry& registry, const core::GridPos anchor)
{
    const auto lakes = registry.view<components::ManaLakeTag, components::GridPosition>();
    for (const entt::entity lake : lakes) {
        if (lakes.get<components::GridPosition>(lake).cell == anchor) {
            return lake;
        }
    }

    return entt::null;
}

entt::entity find_extractor_on_mana_lake(const entt::registry& registry, const entt::entity lake)
{
    if (lake == entt::null || !registry.valid(lake)) {
        return entt::null;
    }

    const auto extractors = registry.view<components::ExtractorTag, components::ManaLakeRef>();
    for (const entt::entity extractor : extractors) {
        if (extractors.get<components::ManaLakeRef>(extractor).lake == lake) {
            return extractor;
        }
    }

    return entt::null;
}

bool mana_lake_footprint_contains_cell(
    entt::registry& registry,
    const entt::entity lake,
    const core::GridPos cell)
{
    if (lake == entt::null || !registry.valid(lake)) {
        return false;
    }

    const auto* position = registry.try_get<components::GridPosition>(lake);
    const auto* footprint = registry.try_get<components::BuildingFootprint>(lake);
    if (position == nullptr || footprint == nullptr) {
        return false;
    }

    return components::building_contains_cell(*position, *footprint, cell);
}

void relink_extractor_mana_lakes(entt::registry& registry)
{
    const auto extractors = registry.view<components::ExtractorTag, components::GridPosition>();
    for (const entt::entity extractor : extractors) {
        const core::GridPos anchor = extractors.get<components::GridPosition>(extractor).cell;
        registry.emplace_or_replace<components::ManaLakeRef>(
            extractor,
            components::ManaLakeRef{find_mana_lake_at_anchor(registry, anchor)});
    }
}

entt::entity spawn_player_mill(
    entt::registry& registry,
    const data::ArchetypeDefinition& mill_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const bool under_construction)
{
    const entt::entity mill = spawn_owned_building(
        registry,
        mill_archetype,
        spawn_cell,
        player_slot,
        under_construction,
        snapshot::EntitySnapshotCategory::Mill);
    registry.emplace<components::MillTag>(mill);
    registry.emplace<components::FoodDropOffTag>(mill);
    return mill;
}

entt::entity spawn_player_mining_camp(
    entt::registry& registry,
    const data::ArchetypeDefinition& mining_camp_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const bool under_construction)
{
    const entt::entity camp = spawn_owned_building(
        registry,
        mining_camp_archetype,
        spawn_cell,
        player_slot,
        under_construction,
        snapshot::EntitySnapshotCategory::MiningCamp);
    registry.emplace<components::MiningCampTag>(camp);
    registry.emplace<components::MoneyDropOffTag>(camp);
    return camp;
}

entt::entity spawn_player_barracks(
    entt::registry& registry,
    const data::ArchetypeDefinition& barracks_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const bool under_construction)
{
    const entt::entity barracks = spawn_owned_building(
        registry,
        barracks_archetype,
        spawn_cell,
        player_slot,
        under_construction,
        snapshot::EntitySnapshotCategory::Barracks);
    registry.emplace<components::BarracksTag>(barracks);
    return barracks;
}

entt::entity spawn_player_mage_academy(
    entt::registry& registry,
    const data::ArchetypeDefinition& mage_academy_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const bool under_construction)
{
    const entt::entity academy = spawn_owned_building(
        registry,
        mage_academy_archetype,
        spawn_cell,
        player_slot,
        under_construction,
        snapshot::EntitySnapshotCategory::MageAcademy);
    registry.emplace<components::MageAcademyTag>(academy);
    return academy;
}

entt::entity spawn_player_tower(
    entt::registry& registry,
    const data::ArchetypeDefinition& tower_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const bool under_construction)
{
    const entt::entity tower = spawn_owned_building(
        registry,
        tower_archetype,
        spawn_cell,
        player_slot,
        under_construction,
        snapshot::EntitySnapshotCategory::Tower);
    registry.emplace<components::TowerTag>(tower);
    registry.emplace<components::AttackCooldown>(tower);
    return tower;
}

entt::entity spawn_player_market(
    entt::registry& registry,
    const data::ArchetypeDefinition& market_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const bool under_construction)
{
    const entt::entity market = spawn_owned_building(
        registry,
        market_archetype,
        spawn_cell,
        player_slot,
        under_construction,
        snapshot::EntitySnapshotCategory::Market);
    registry.emplace<components::MarketTag>(market);
    return market;
}

entt::entity spawn_player_garden(
    entt::registry& registry,
    const data::ArchetypeDefinition& garden_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const bool under_construction)
{
    const entt::entity garden = spawn_owned_building(
        registry,
        garden_archetype,
        spawn_cell,
        player_slot,
        under_construction,
        snapshot::EntitySnapshotCategory::Garden);
    registry.emplace<components::GardenTag>(garden);
    if (!under_construction) {
        registry.emplace<components::ManaGenerationCooldown>(
            garden,
            components::ManaGenerationCooldown{constants::GARDEN_PROD_INTERVAL_TICKS});
    }
    return garden;
}

entt::entity spawn_player_reservoir(
    entt::registry& registry,
    const data::ArchetypeDefinition& reservoir_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const bool under_construction)
{
    const entt::entity reservoir = spawn_owned_building(
        registry,
        reservoir_archetype,
        spawn_cell,
        player_slot,
        under_construction,
        snapshot::EntitySnapshotCategory::Reservoir);
    registry.emplace<components::ReservoirTag>(reservoir);
    return reservoir;
}

entt::entity spawn_player_farm(
    entt::registry& registry,
    const data::ArchetypeDefinition& farm_archetype,
    const core::GridPos spawn_cell,
    const std::uint8_t player_slot,
    const bool under_construction)
{
    const entt::entity farm = spawn_owned_building(
        registry,
        farm_archetype,
        spawn_cell,
        player_slot,
        under_construction,
        snapshot::EntitySnapshotCategory::Farm);
    registry.emplace<components::FarmTag>(farm);
    registry.emplace<components::FarmFood>(
        farm,
        components::FarmFood{
            under_construction ? 0 : constants::FARM_FOOD_AMOUNT,
            constants::FARM_FOOD_AMOUNT});
    return farm;
}

entt::entity find_farm_at_cell(entt::registry& registry, const core::GridPos cell)
{
    const auto farm_view = registry.view<
        components::FarmTag,
        components::GridPosition,
        components::Health>();
    for (const entt::entity farm : farm_view) {
        if (farm_view.get<components::Health>(farm).current.raw() <= 0) {
            continue;
        }

        components::BuildingFootprint footprint{};
        if (registry.any_of<components::BuildingFootprint>(farm)) {
            footprint = registry.get<components::BuildingFootprint>(farm);
        }
        if (components::building_contains_cell(
                farm_view.get<components::GridPosition>(farm),
                footprint,
                cell)) {
            return farm;
        }
    }

    return entt::null;
}

entt::entity spawn_rock_projectile(
    entt::registry& registry,
    const math::Fixed world_x,
    const math::Fixed world_y,
    const entt::entity target,
    const std::uint8_t owner_slot,
    const int pierce_damage)
{
    const entt::entity projectile = registry.create();
    registry.emplace<components::Projectile>(
        projectile,
        components::Projectile{target, owner_slot, pierce_damage});
    registry.emplace<components::PlayerSlot>(projectile, components::PlayerSlot{owner_slot});
    registry.emplace<components::WorldPosition>(projectile, components::WorldPosition{world_x, world_y});
    registry.emplace<components::PreviousWorldPosition>(
        projectile,
        components::PreviousWorldPosition{world_x, world_y});
    registry.emplace<components::GridPosition>(
        projectile,
        components::GridPosition{core::GridPos{world_x.to_int(), world_y.to_int()}});
    snapshot::set_entity_snapshot_identity(
        registry,
        projectile,
        snapshot::EntitySnapshotKey{
            owner_slot,
            snapshot::EntitySnapshotCategory::Projectile,
            snapshot::next_entity_snapshot_ordinal(
                registry,
                owner_slot,
                snapshot::EntitySnapshotCategory::Projectile)});
    return projectile;
}

} // namespace aoa::sim::spawn

