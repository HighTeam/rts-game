#include "harness/regression_harness.hpp"

#include "core/grid.hpp"
#include "data/content_loader.hpp"
#include "sim/player/player_command.hpp"
#include "sim/scenario/test_scenario.hpp"
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

sim::player::PlayerCommandType parse_command_type(const std::string& type_text)
{
    if (type_text == "move") {
        return sim::player::PlayerCommandType::Move;
    }

    if (type_text == "attack") {
        return sim::player::PlayerCommandType::Attack;
    }

    if (type_text == "gather") {
        return sim::player::PlayerCommandType::Gather;
    }

    if (type_text == "deposit") {
        return sim::player::PlayerCommandType::Deposit;
    }

    if (type_text == "spawn_worker") {
        return sim::player::PlayerCommandType::SpawnWorker;
    }

    throw std::invalid_argument("Unknown command type: " + type_text);
}

void print_hash_line(const ScenarioDefinition& scenario, const std::uint64_t actual_hash)
{
    std::cout << scenario.scenario_id << ": ticks=" << scenario.ticks << " hash=0x" << std::hex
              << actual_hash << std::dec << '\n';
}

sim::player::PlayerCommand build_player_command(
    entt::registry& registry,
    const ScenarioCommandDefinition& command_definition)
{
    sim::player::PlayerCommand command{};
    command.execute_tick = command_definition.execute_tick;
    command.type = command_definition.type;

    for (const std::string& unit_role : command_definition.unit_roles) {
        const entt::entity unit = sim::scenario::find_scenario_entity(registry, unit_role);
        if (unit == entt::null) {
            throw std::runtime_error("Unknown scenario unit role: " + unit_role);
        }

        command.unit_ids.push_back(unit);
    }

    command.cell = command_definition.cell;

    if (!command_definition.target_role.empty()) {
        const entt::entity target =
            sim::scenario::find_scenario_entity(registry, command_definition.target_role);
        if (target == entt::null) {
            throw std::runtime_error("Unknown scenario target role: " + command_definition.target_role);
        }

        command.target_entity = target;
    }

    return command;
}

void enqueue_due_commands(sim::Simulation& simulation, const ScenarioDefinition& scenario)
{
    const std::uint64_t execute_tick = simulation.next_command_execute_tick();
    entt::registry& registry = simulation.registry();

    for (const ScenarioCommandDefinition& command_definition : scenario.commands) {
        if (command_definition.execute_tick != execute_tick) {
            continue;
        }

        simulation.enqueue_player_command(build_player_command(registry, command_definition));
    }
}

void run_ticks(sim::Simulation& simulation, const ScenarioDefinition& scenario)
{
    for (std::uint64_t tick_index = 0U; tick_index < scenario.ticks; ++tick_index) {
        enqueue_due_commands(simulation, scenario);
        simulation.tick();
    }
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

    if (!json.contains("commands")) {
        return scenario;
    }

    for (const nlohmann::json& command_json : json.at("commands")) {
        ScenarioCommandDefinition command{};
        command.execute_tick = command_json.at("execute_tick").get<std::uint64_t>();
        command.type = parse_command_type(command_json.at("type").get<std::string>());
        command.unit_roles = command_json.at("units").get<std::vector<std::string>>();

        if (command_json.contains("cell")) {
            command.cell.x = command_json.at("cell").at("x").get<int>();
            command.cell.y = command_json.at("cell").at("y").get<int>();
        }

        if (command_json.contains("target")) {
            command.target_role = command_json.at("target").get<std::string>();
        }

        scenario.commands.push_back(command);
    }

    return scenario;
}

int run_scenario(const ScenarioDefinition& scenario)
{
    if (scenario.scenario_id != "earth_default" && scenario.scenario_id != "earth_player_commands") {
        std::cerr << "Unsupported scenario_id: " << scenario.scenario_id << '\n';
        return 1;
    }

    sim::Simulation simulation{};
    run_ticks(simulation, scenario);

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
