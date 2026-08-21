#include "sim/player/player_economy.hpp"

#include "core/constants.hpp"
#include "sim/components/health.hpp"
#include "sim/components/match_session.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/tags.hpp"
#include "sim/systems/match_outcome.hpp"

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

int count_enemy_units(const entt::registry& registry, const std::uint8_t player_slot)
{
    int count = 0;
    const auto view = registry.view<components::UnitTag, components::PlayerOwnedTag, components::Health>();
    for (const entt::entity entity : view) {
        if (view.get<components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        if (!components::is_opponent_entity(registry, entity, player_slot)) {
            continue;
        }

        ++count;
    }

    return count;
}

bool player_has_completed_mill(const entt::registry& registry, const std::uint8_t player_slot)
{
    const auto view = registry.view<
        components::MillTag,
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

        return true;
    }

    return false;
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

int count_completed_extractors(const entt::registry& registry, const std::uint8_t player_slot)
{
    int count = 0;
    const auto view = registry.view<
        components::ExtractorTag,
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

int count_completed_reservoirs(const entt::registry& registry, const std::uint8_t player_slot)
{
    int count = 0;
    const auto view = registry.view<
        components::ReservoirTag,
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

int player_mana_cap_max(const entt::registry& registry, const std::uint8_t player_slot)
{
    const int from_extractors =
        count_completed_extractors(registry, player_slot) * constants::MANA_CAP_PER_EXTRACTOR;
    const int from_reservoirs =
        count_completed_reservoirs(registry, player_slot) * constants::MANA_CAP_PER_RESERVOIR;
    return std::min(from_extractors + from_reservoirs, constants::PLAYER_MANA_MAX);
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

[[nodiscard]] components::Stockpile* mutable_player_stockpile(
    entt::registry& registry,
    const std::uint8_t player_slot)
{
    components::MatchSession* session = systems::match_session(registry);
    if (session == nullptr || player_slot >= session->player_stockpiles.size()) {
        return nullptr;
    }

    return &session->player_stockpiles[player_slot];
}

[[nodiscard]] const components::Stockpile* player_stockpile(
    const entt::registry& registry,
    const std::uint8_t player_slot)
{
    const components::MatchSession* session = systems::match_session(registry);
    if (session == nullptr || player_slot >= session->player_stockpiles.size()) {
        return nullptr;
    }

    return &session->player_stockpiles[player_slot];
}

components::Stockpile sum_player_stockpile(
    const entt::registry& registry,
    const std::uint8_t player_slot)
{
    const components::Stockpile* stockpile = player_stockpile(registry, player_slot);
    if (stockpile == nullptr) {
        return {};
    }

    return *stockpile;
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

    components::Stockpile* stockpile = mutable_player_stockpile(registry, player_slot);
    if (stockpile == nullptr) {
        return false;
    }

    stockpile->wood -= amount;
    return true;
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

    components::Stockpile* stockpile = mutable_player_stockpile(registry, player_slot);
    if (stockpile == nullptr) {
        return false;
    }

    stockpile->food -= amount;
    return true;
}

bool can_afford_player_money(
    const entt::registry& registry,
    const std::uint8_t player_slot,
    const int amount)
{
    if (amount <= 0) {
        return true;
    }

    return sum_player_stockpile(registry, player_slot).money >= amount;
}

bool try_deduct_player_money(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const int amount)
{
    if (amount <= 0) {
        return true;
    }

    if (!can_afford_player_money(registry, player_slot, amount)) {
        return false;
    }

    components::Stockpile* stockpile = mutable_player_stockpile(registry, player_slot);
    if (stockpile == nullptr) {
        return false;
    }

    stockpile->money -= amount;
    return true;
}

bool can_afford_player_mana(
    const entt::registry& registry,
    const std::uint8_t player_slot,
    const int amount)
{
    if (amount <= 0) {
        return true;
    }

    return player_mana_total(registry, player_slot) >= amount;
}

bool try_deduct_player_mana(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const int amount)
{
    if (amount <= 0) {
        return true;
    }

    if (!can_afford_player_mana(registry, player_slot, amount)) {
        return false;
    }

    components::Stockpile* stockpile = mutable_player_stockpile(registry, player_slot);
    if (stockpile == nullptr) {
        return false;
    }

    stockpile->mana -= amount;
    return true;
}

void add_player_wood(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const int amount)
{
    if (amount <= 0) {
        return;
    }

    components::Stockpile* stockpile = mutable_player_stockpile(registry, player_slot);
    if (stockpile == nullptr) {
        return;
    }

    stockpile->wood += amount;
}

void add_player_food(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const int amount)
{
    if (amount <= 0) {
        return;
    }

    components::Stockpile* stockpile = mutable_player_stockpile(registry, player_slot);
    if (stockpile == nullptr) {
        return;
    }

    stockpile->food += amount;
}

void add_player_money(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const int amount)
{
    if (amount <= 0) {
        return;
    }

    components::Stockpile* stockpile = mutable_player_stockpile(registry, player_slot);
    if (stockpile == nullptr) {
        return;
    }

    stockpile->money += amount;
}

void clamp_player_mana_to_cap(entt::registry& registry, const std::uint8_t player_slot)
{
    int excess = player_mana_total(registry, player_slot)
        - player_mana_cap_max(registry, player_slot);
    if (excess <= 0) {
        return;
    }

    components::Stockpile* stockpile = mutable_player_stockpile(registry, player_slot);
    if (stockpile == nullptr) {
        return;
    }

    const int take = std::min(stockpile->mana, excess);
    stockpile->mana -= take;
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
    const int remaining_capacity = player_mana_cap_max(registry, player_slot) - current_total;
    if (remaining_capacity <= 0) {
        return;
    }

    const int to_add = std::min(amount, remaining_capacity);
    components::Stockpile* stockpile = mutable_player_stockpile(registry, player_slot);
    if (stockpile == nullptr) {
        return;
    }

    stockpile->mana += to_add;
    systems::note_mana_collected(registry, player_slot, to_add);
}

} // namespace aoa::sim::player
