#pragma once

#include "sim/simulation.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aoa::sim::persistence {

inline constexpr std::string_view SAVE_EXTENSION = ".aoa";
inline constexpr std::string_view AUTOSAVE_FILENAME = "autosave.aoa";
inline constexpr std::string_view AUTOSAVE_MP_FILENAME = "autosave.mp.aoa";

[[nodiscard]] std::filesystem::path default_saves_directory();

[[nodiscard]] std::filesystem::path save_path_for_stem(std::string_view stem);

[[nodiscard]] std::filesystem::path default_autosave_path();

[[nodiscard]] std::filesystem::path default_autosave_mp_path();

[[nodiscard]] bool save_simulation_to_file(
    const Simulation& simulation,
    const std::filesystem::path& path);

[[nodiscard]] bool load_simulation_from_file(
    Simulation& simulation,
    const std::filesystem::path& path);

[[nodiscard]] bool save_file_exists(std::string_view stem);

[[nodiscard]] std::vector<std::string> list_save_stems();

/// Strips a trailing `.aoa` if present and rejects empty / path-like names.
[[nodiscard]] std::optional<std::string> normalize_save_stem(std::string_view input);

} // namespace aoa::sim::persistence
