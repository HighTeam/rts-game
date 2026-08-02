#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace aoa::harness {

struct ScenarioDefinition {
    std::string scenario_id{};
    std::uint64_t ticks{0U};
    std::uint64_t expected_state_hash{0U};
};

[[nodiscard]] ScenarioDefinition load_scenario_definition(const std::filesystem::path& scenario_json_path);

[[nodiscard]] int run_scenario(const ScenarioDefinition& scenario);
[[nodiscard]] int run_all_scenarios(const std::filesystem::path& scenarios_directory);

[[nodiscard]] std::filesystem::path default_scenarios_directory();

} // namespace aoa::harness
