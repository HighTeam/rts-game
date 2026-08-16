#pragma once

#include <filesystem>

namespace aoa::core {

[[nodiscard]] std::filesystem::path executable_directory();

[[nodiscard]] std::filesystem::path default_data_directory();

[[nodiscard]] std::filesystem::path default_scenarios_directory();

[[nodiscard]] std::filesystem::path default_game_root_directory();

[[nodiscard]] std::filesystem::path default_playable_scenarios_directory();

[[nodiscard]] std::filesystem::path default_patterns_directory();

[[nodiscard]] std::filesystem::path default_assets_directory();

[[nodiscard]] std::filesystem::path default_logs_directory();

} // namespace aoa::core
