#pragma once

#include "core/grid.hpp"
#include "sim/player/player_command.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace aoa::harness {

struct ScenarioCommandDefinition {
    std::uint64_t execute_tick{0U};
    sim::player::PlayerCommandType type{sim::player::PlayerCommandType::Move};
    std::vector<std::string> unit_roles{};
    core::GridPos cell{};
    std::string target_role{};
};

struct ScenarioDefinition {
    std::string scenario_id{};
    std::uint64_t ticks{0U};
    std::uint64_t expected_state_hash{0U};
    std::vector<ScenarioCommandDefinition> commands{};
};

[[nodiscard]] ScenarioDefinition load_scenario_definition(const std::filesystem::path& scenario_json_path);

[[nodiscard]] int run_scenario(const ScenarioDefinition& scenario);
[[nodiscard]] int run_all_scenarios(const std::filesystem::path& scenarios_directory);

[[nodiscard]] std::filesystem::path default_scenarios_directory();

} // namespace aoa::harness
