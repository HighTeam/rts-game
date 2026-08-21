#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace aoa::sim::scenario {

struct ScenarioFile {
    std::string name{};
    std::uint8_t required_player_count{0U};
};

[[nodiscard]] std::optional<ScenarioFile> load_scenario_file(const std::filesystem::path& path);

} // namespace aoa::sim::scenario
