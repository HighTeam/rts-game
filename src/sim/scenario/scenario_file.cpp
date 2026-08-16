#include "sim/scenario/scenario_file.hpp"

#include "core/constants.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace aoa::sim::scenario {

std::optional<ScenarioFile> load_scenario_file(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }

    nlohmann::json json{};
    try {
        input >> json;
    }
    catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }

    ScenarioFile scenario{};
    if (json.contains("name") && json["name"].is_string()) {
        scenario.name = json["name"].get<std::string>();
    }
    else {
        scenario.name = path.stem().string();
    }

    if (json.contains("required_player_count") && json["required_player_count"].is_number_unsigned()) {
        const auto count = json["required_player_count"].get<unsigned int>();
        if (count > 0U && count <= static_cast<unsigned int>(constants::MAX_PLAYER_SLOTS)) {
            scenario.required_player_count = static_cast<std::uint8_t>(count);
        }
    }

    return scenario;
}

} // namespace aoa::sim::scenario
