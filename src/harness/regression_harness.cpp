#include "harness/regression_harness.hpp"

#include "core/fixed_timestep_loop.hpp"
#include "data/content_loader.hpp"
#include "sim/simulation.hpp"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace aoa::harness {

namespace {

std::uint64_t parse_hash_string(const std::string& hash_text)
{
    std::string normalized = hash_text;
    if (normalized.starts_with("0x") || normalized.starts_with("0X")) {
        normalized = normalized.substr(2U);
    }

    if (normalized.empty()) {
        throw std::invalid_argument("Expected state hash is empty");
    }

    return std::stoull(normalized, nullptr, 16);
}

void print_hash_line(const ScenarioDefinition& scenario, const std::uint64_t actual_hash)
{
    std::cout << scenario.scenario_id << ": ticks=" << scenario.ticks << " hash=0x" << std::hex
              << actual_hash << std::dec << '\n';
}

} // namespace

ScenarioDefinition load_scenario_definition(const std::filesystem::path& scenario_json_path)
{
    std::ifstream input(scenario_json_path);
    if (!input) {
        throw std::runtime_error("Failed to open scenario: " + scenario_json_path.string());
    }

    const nlohmann::json json = nlohmann::json::parse(input);

    ScenarioDefinition scenario{};
    scenario.scenario_id = json.at("scenario_id").get<std::string>();
    scenario.ticks = json.at("ticks").get<std::uint64_t>();
    scenario.expected_state_hash = parse_hash_string(json.at("expected_state_hash").get<std::string>());
    return scenario;
}

int run_scenario(const ScenarioDefinition& scenario)
{
    if (scenario.scenario_id != "earth_default") {
        std::cerr << "Unsupported scenario_id: " << scenario.scenario_id << '\n';
        return 1;
    }

    sim::Simulation simulation{};
    core::FixedTimestepLoop loop{};
    loop.run_headless([&simulation]() { simulation.tick(); }, scenario.ticks);

    const std::uint64_t actual_hash = simulation.state_hash();
    print_hash_line(scenario, actual_hash);

    if (actual_hash != scenario.expected_state_hash) {
        std::cerr << "Hash mismatch for " << scenario.scenario_id << ": expected 0x" << std::hex
                  << scenario.expected_state_hash << " got 0x" << actual_hash << std::dec << '\n';
        return 1;
    }

    return 0;
}

int run_all_scenarios(const std::filesystem::path& scenarios_directory)
{
    if (!std::filesystem::is_directory(scenarios_directory)) {
        std::cerr << "Scenarios directory not found: " << scenarios_directory.string() << '\n';
        return 1;
    }

    int failed_count = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(scenarios_directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        const ScenarioDefinition scenario = load_scenario_definition(entry.path());
        if (run_scenario(scenario) != 0) {
            ++failed_count;
        }
    }

    if (failed_count > 0) {
        std::cerr << failed_count << " scenario(s) failed\n";
        return 1;
    }

    std::cout << "All scenarios passed\n";
    return 0;
}

std::filesystem::path default_scenarios_directory()
{
    return data::default_data_directory() / "scenarios";
}

} // namespace aoa::harness
