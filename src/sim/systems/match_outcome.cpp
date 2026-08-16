#include "sim/systems/match_outcome.hpp"

#include "core/constants.hpp"
#include "sim/components/health.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/tags.hpp"

#include <bit>

namespace aoa::sim::systems {

namespace {

[[nodiscard]] components::PlayerMatchStats* stats_for_slot(
    components::MatchSession& session,
    const std::uint8_t player_slot)
{
    if (player_slot >= session.player_stats.size()) {
        return nullptr;
    }

    return &session.player_stats[player_slot];
}

[[nodiscard]] bool slot_has_live_presence(
    const entt::registry& registry,
    const std::uint8_t player_slot)
{
    const auto unit_view = registry.view<components::UnitTag, components::Health, components::PlayerSlot>();
    for (const entt::entity entity : unit_view) {
        if (unit_view.get<components::PlayerSlot>(entity).value != player_slot) {
            continue;
        }

        if (unit_view.get<components::Health>(entity).current.raw() > 0) {
            return true;
        }
    }

    const auto building_view =
        registry.view<components::BuildingTag, components::Health, components::PlayerSlot>();
    for (const entt::entity entity : building_view) {
        if (building_view.get<components::PlayerSlot>(entity).value != player_slot) {
            continue;
        }

        if (building_view.get<components::Health>(entity).current.raw() > 0) {
            return true;
        }
    }

    return false;
}

} // namespace

components::MatchSession* match_session(entt::registry& registry)
{
    const auto view = registry.view<components::WorldTag, components::MatchSession>();
    if (view.begin() == view.end()) {
        return nullptr;
    }

    return &view.get<components::MatchSession>(*view.begin());
}

const components::MatchSession* match_session(const entt::registry& registry)
{
    const auto view = registry.view<components::WorldTag, components::MatchSession>();
    if (view.begin() == view.end()) {
        return nullptr;
    }

    return &view.get<components::MatchSession>(*view.begin());
}

void note_unit_created(entt::registry& registry, const std::uint8_t player_slot)
{
    components::MatchSession* session = match_session(registry);
    if (session == nullptr) {
        return;
    }

    components::PlayerMatchStats* stats = stats_for_slot(*session, player_slot);
    if (stats == nullptr) {
        return;
    }

    ++stats->units_created;
}

void note_building_created(entt::registry& registry, const std::uint8_t player_slot)
{
    components::MatchSession* session = match_session(registry);
    if (session == nullptr) {
        return;
    }

    components::PlayerMatchStats* stats = stats_for_slot(*session, player_slot);
    if (stats == nullptr) {
        return;
    }

    ++stats->buildings_created;
}

void note_resources_collected(
    entt::registry& registry,
    const std::uint8_t player_slot,
    const int wood,
    const int food,
    const int money)
{
    components::MatchSession* session = match_session(registry);
    if (session == nullptr) {
        return;
    }

    components::PlayerMatchStats* stats = stats_for_slot(*session, player_slot);
    if (stats == nullptr) {
        return;
    }

    stats->wood_collected += wood;
    stats->food_collected += food;
    stats->money_collected += money;
}

void note_mana_collected(entt::registry& registry, const std::uint8_t player_slot, const int amount)
{
    components::MatchSession* session = match_session(registry);
    if (session == nullptr || amount <= 0) {
        return;
    }

    components::PlayerMatchStats* stats = stats_for_slot(*session, player_slot);
    if (stats == nullptr) {
        return;
    }

    stats->mana_collected += amount;
}

void note_entity_killed(
    entt::registry& registry,
    const entt::entity victim,
    const entt::entity killer)
{
    if (!registry.valid(victim)) {
        return;
    }

    components::MatchSession* session = match_session(registry);
    if (session == nullptr) {
        return;
    }

    const std::uint8_t victim_slot = components::entity_player_slot(registry, victim);
    components::PlayerMatchStats* victim_stats = stats_for_slot(*session, victim_slot);
    if (victim_stats == nullptr) {
        return;
    }

    const bool victim_is_building = registry.any_of<components::BuildingTag>(victim);
    if (victim_is_building) {
        ++victim_stats->buildings_lost;
    }
    else {
        ++victim_stats->units_lost;
    }

    if (!registry.valid(killer)) {
        return;
    }

    const std::uint8_t killer_slot = components::entity_player_slot(registry, killer);
    if (killer_slot == victim_slot) {
        return;
    }

    if (components::player_side_index(*session, killer_slot)
        != components::player_side_index(*session, victim_slot)) {
        session->last_eliminating_slot = killer_slot;
    }

    components::PlayerMatchStats* killer_stats = stats_for_slot(*session, killer_slot);
    if (killer_stats == nullptr) {
        return;
    }

    if (victim_is_building) {
        ++killer_stats->buildings_destroyed;
        return;
    }

    ++killer_stats->units_killed;
}

bool match_is_finished(const entt::registry& registry)
{
    const components::MatchSession* session = match_session(registry);
    return session != nullptr && session->match_finished;
}

void update_match_outcome(entt::registry& registry, const std::uint64_t tick_count)
{
    components::MatchSession* session = match_session(registry);
    if (session == nullptr || session->match_finished) {
        return;
    }

    if (session->victory_condition != components::VictoryCondition::Normal) {
        return;
    }

    if (session->playing_slots_mask == 0U || tick_count < constants::MATCH_OUTCOME_MIN_TICK) {
        return;
    }

    std::uint8_t living_sides_mask = 0U;
    std::uint8_t last_alive_slot = constants::MATCH_WINNER_NONE;
    for (std::uint8_t slot = 0U; slot < static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS);
         ++slot) {
        const std::uint8_t bit = static_cast<std::uint8_t>(1U << slot);
        if ((session->playing_slots_mask & bit) == 0U) {
            continue;
        }

        if ((session->eliminated_slots_mask & bit) != 0U) {
            continue;
        }

        if (!slot_has_live_presence(registry, slot)) {
            session->eliminated_slots_mask =
                static_cast<std::uint8_t>(session->eliminated_slots_mask | bit);
            continue;
        }

        const std::uint8_t side = components::player_side_index(*session, slot);
        living_sides_mask =
            static_cast<std::uint8_t>(living_sides_mask | static_cast<std::uint8_t>(1U << side));
        last_alive_slot = slot;
    }

    const std::uint8_t remaining_sides =
        static_cast<std::uint8_t>(std::popcount(living_sides_mask));
    if (remaining_sides > 1U) {
        return;
    }

    if (remaining_sides == 1U && session->eliminated_slots_mask == 0U) {
        return;
    }

    std::uint8_t winner_slot = last_alive_slot;
    if (remaining_sides == 0U) {
        winner_slot = session->last_eliminating_slot;
        if (winner_slot == constants::MATCH_WINNER_NONE
            || winner_slot >= static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS)
            || (session->playing_slots_mask
                & static_cast<std::uint8_t>(1U << winner_slot))
                == 0U) {
            for (std::uint8_t slot = 0U;
                 slot < static_cast<std::uint8_t>(constants::MAX_PLAYER_SLOTS);
                 ++slot) {
                if ((session->playing_slots_mask & static_cast<std::uint8_t>(1U << slot))
                    == 0U) {
                    continue;
                }

                winner_slot = slot;
                break;
            }
        }
    }

    if (winner_slot == constants::MATCH_WINNER_NONE) {
        return;
    }

    session->match_finished = true;
    session->finished_tick = tick_count;
    session->winner_slot = winner_slot;
}

} // namespace aoa::sim::systems
