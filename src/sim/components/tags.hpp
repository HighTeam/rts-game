#pragma once

#include "core/constants.hpp"

#include <cstdint>
#include <entt/entt.hpp>
#include <vector>

namespace aoa::sim::components {

struct WorldTag {};
struct UnitTag {};
struct BuildingTag {};
struct PlayerOwnedTag {};
struct ManualControlTag {};
struct EnemyTag {};

struct TownCenterTag {};
struct HouseTag {};
struct LumberCampTag {};
struct MillTag {};
struct MiningCampTag {};
struct BarracksTag {};
struct MageAcademyTag {};
struct TowerTag {};
struct MarketTag {};
struct ExtractorTag {};
struct GardenTag {};
struct ReservoirTag {};
struct FarmTag {};
// Nature mana pool: no owner, no health, never destroyed by the extractor built on it.
struct ManaLakeTag {};
struct WoodDropOffTag {};
struct FoodDropOffTag {};
struct MoneyDropOffTag {};
struct WorkerUnitTag {};
struct MilitiaUnitTag {};
struct MageUnitTag {};
struct UnitSex {
    constants::UnitSex value{constants::UnitSex::Male};
};
struct UnderConstructionTag {};
struct GarrisonedTag {
    entt::entity building{entt::null};
};
struct BuildingVisualVariant {
    std::uint8_t index{0U};
};
struct GarrisonHold {
    std::vector<entt::entity> units{};
    std::uint8_t capacity{5U};
};

} // namespace aoa::sim::components
