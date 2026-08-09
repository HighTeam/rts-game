#include "sim/player/player_economy.hpp"

#include "core/constants.hpp"
#include "sim/components/health.hpp"
#include "sim/components/match_session.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/tags.hpp"

#include <algorithm>

namespace aoa::sim::player {

int count_player_units(const entt::registry& registry, const std::uint8_t player_slot)
{
    int count = 0;
    const auto view = registry.view<components::UnitTag, components::PlayerOwnedTag>();
    for (const entt::entity entity : view) {
        if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        ++count;
    }

    return count;
}

int count_completed_town_centers(const entt::registry& registry, const std::uint8_t player_slot)
{
    int count = 0;
    const auto view = registry.view<
        components::TownCenterTag,
        components::PlayerOwnedTag,
        components::Health>();
    for (const entt::entity entity : view) {
        if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        if (registry.any_of<components::UnderConstructionTag>(entity)) {
            continue;
        }

        if (view.get<components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        ++count;
    }

    return count;
}

int count_completed_houses(const entt::registry& registry, const std::uint8_t player_slot)
{
    int count = 0;
    const auto view = registry.view<
        components::HouseTag,
        components::PlayerOwnedTag,
        components::Health>();
    for (const entt::entity entity : view) {
        if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        if (registry.any_of<components::UnderConstructionTag>(entity)) {
            continue;
        }

        if (view.get<components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        ++count;
    }

    return count;
}

int player_civil_cap_max(const entt::registry& registry, const std::uint8_t player_slot)
{
    const int from_town_centers =
        count_completed_town_centers(registry, player_slot)
        * constants::CIVIL_POPULATION_CAP_PER_TOWN_CENTER;
    const int from_houses =
        count_completed_houses(registry, player_slot) * constants::CIVIL_POPULATION_CAP_PER_HOUSE;
    const int uncapped = constants::CIVIL_POPULATION_CAP_BASE + from_town_centers + from_houses;
    int hard_max = constants::CIVIL_POPULATION_CAP_MAX;

    const auto world_view = registry.view<components::WorldTag, components::MatchSession>();
    if (world_view.begin() != world_view.end()) {
        const int map_cap =
            world_view.get<components::MatchSession>(*world_view.begin()).civil_population_map_cap;
        if (map_cap > 0) {
            hard_max = std::min(hard_max, map_cap);
        }
    }

    return std::min(uncapped, hard_max);
}

bool player_can_spawn_units(const entt::registry& registry, const std::uint8_t player_slot)
{
    const int cap_max = player_civil_cap_max(registry, player_slot);
    if (cap_max <= 0) {
        return false;
    }

    return count_player_units(registry, player_slot) < cap_max;
}

components::Stockpile sum_player_stockpile(
    const entt::registry& registry,
    const std::uint8_t player_slot)
{
    components::Stockpile total{};
    const auto view = registry.view<
        components::TownCenterTag,
        components::PlayerOwnedTag,
        components::Stockpile>();
    for (const entt::entity entity : view) {
        if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        if (registry.any_of<components::UnderConstructionTag>(entity)) {
            continue;
        }

        const auto& stockpile = view.get<components::Stockpile>(entity);
        total.wood += stockpile.wood;
        total.food += stockpile.food;
        total.money += stockpile.money;
        total.mana += stockpile.mana;
    }

    return total;
}

int player_mana_total(const entt::registry& registry, const std::uint8_t player_slot)
{
    return sum_player_stockpile(registry, player_slot).mana;
}

bool can_afford_player_wood(
    const entt::registry& registry,
    const std::uint8_t player_slot,
    const int amount)
{
    if (amount <= 0) {
        return true;
    }

    return sum_player_stockpile(registry, player_slot).wood >= amount;
}

bool try_deduct_player_wood(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const int amount)
{
    if (amount <= 0) {
        return true;
    }

    if (!can_afford_player_wood(registry, player_slot, amount)) {
        return false;
    }

    int remaining = amount;
    const auto view = registry.view<
        components::TownCenterTag,
        components::PlayerOwnedTag,
        components::Stockpile>();
    for (const entt::entity entity : view) {
        if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        if (registry.any_of<components::UnderConstructionTag>(entity)) {
            continue;
        }

        auto& stockpile = view.get<components::Stockpile>(entity);
        const int take = std::min(stockpile.wood, remaining);
        stockpile.wood -= take;
        remaining -= take;
        if (remaining <= 0) {
            return true;
        }
    }

    return remaining <= 0;
}

bool can_afford_player_food(
    const entt::registry& registry,
    const std::uint8_t player_slot,
    const int amount)
{
    if (amount <= 0) {
        return true;
    }

    return sum_player_stockpile(registry, player_slot).food >= amount;
}

bool try_deduct_player_food(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const int amount)
{
    if (amount <= 0) {
        return true;
    }

    if (!can_afford_player_food(registry, player_slot, amount)) {
        return false;
    }

    int remaining = amount;
    const auto view = registry.view<
        components::TownCenterTag,
        components::PlayerOwnedTag,
        components::Stockpile>();
    for (const entt::entity entity : view) {
        if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        if (registry.any_of<components::UnderConstructionTag>(entity)) {
            continue;
        }

        auto& stockpile = view.get<components::Stockpile>(entity);
        const int take = std::min(stockpile.food, remaining);
        stockpile.food -= take;
        remaining -= take;
        if (remaining <= 0) {
            return true;
        }
    }

    return remaining <= 0;
}

void add_player_wood(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const int amount)
{
    if (amount <= 0) {
        return;
    }

    const auto view = registry.view<
        components::TownCenterTag,
        components::PlayerOwnedTag,
        components::Stockpile>();
    for (const entt::entity entity : view) {
        if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        if (registry.any_of<components::UnderConstructionTag>(entity)) {
            continue;
        }

        view.get<components::Stockpile>(entity).wood += amount;
        return;
    }
}

void add_player_mana(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const int amount)
{
    if (amount <= 0) {
        return;
    }

    const int current_total = player_mana_total(registry, player_slot);
    int remaining_capacity = constants::PLAYER_MANA_MAX - current_total;
    if (remaining_capacity <= 0) {
        return;
    }

    int to_add = std::min(amount, remaining_capacity);
    const auto view = registry.view<
        components::TownCenterTag,
        components::PlayerOwnedTag,
        components::Stockpile>();
    for (const entt::entity entity : view) {
        if (components::entity_player_slot(registry, entity) != player_slot) {
            continue;
        }

        if (registry.any_of<components::UnderConstructionTag>(entity)) {
            continue;
        }

        view.get<components::Stockpile>(entity).mana += to_add;
        return;
    }
}

} // namespace aoa::sim::player
