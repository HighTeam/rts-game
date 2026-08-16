#pragma once

namespace aoa::sim::components {

struct WorldTag {};
struct UnitTag {};
struct BuildingTag {};
struct PlayerOwnedTag {};
struct ManualControlTag {};
struct EnemyTag {};

struct TownCenterTag {};
struct HouseTag {};
struct LumberjackTag {};
struct ExtractorTag {};
// Nature mana pool: no owner, no health, never destroyed by the extractor built on it.
struct ManaLakeTag {};
// Completed buildings that accept wood drop-off (TC + Lumberjack).
struct WoodDropOffTag {};
struct WorkerUnitTag {};
struct MilitiaUnitTag {};
struct UnderConstructionTag {};

} // namespace aoa::sim::components
