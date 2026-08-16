#include "core/runtime_paths.hpp"

#include "core/constants.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <system_error>
#include <string_view>
#include <vector>

namespace aoa::core {

namespace {

[[nodiscard]] std::filesystem::path read_executable_directory()
{
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0U || length >= MAX_PATH) {
        return {};
    }

    return std::filesystem::path(buffer).parent_path();
#else
    return {};
#endif
}

[[nodiscard]] bool is_valid_directory(
    const std::filesystem::path& directory,
    const std::filesystem::path& validation_relative)
{
    std::error_code error{};
    if (validation_relative.empty()) {
        return std::filesystem::is_directory(directory, error) && !error;
    }

    return std::filesystem::is_directory(directory / validation_relative, error) && !error;
}

[[nodiscard]] std::filesystem::path resolve_runtime_directory(
    const std::string_view relative_name,
    const std::filesystem::path& validation_relative)
{
    std::vector<std::filesystem::path> candidates{};

    const std::filesystem::path executable_dir = read_executable_directory();
    if (!executable_dir.empty()) {
        candidates.push_back(executable_dir / relative_name);
    }

#ifdef AOA_RUNTIME_ROOT
    // Dev-tree fallback only. Never throw if the compile-time drive is missing on other PCs.
    candidates.push_back(std::filesystem::path(AOA_RUNTIME_ROOT) / relative_name);
#endif

    candidates.emplace_back(relative_name);

    for (const std::filesystem::path& candidate : candidates) {
        if (is_valid_directory(candidate, validation_relative)) {
            return candidate;
        }
    }

    // Prefer the executable directory for portable installs (zip next to aoa.exe).
    if (!executable_dir.empty()) {
        return executable_dir / relative_name;
    }

    return std::filesystem::path(relative_name);
}

} // namespace

std::filesystem::path executable_directory()
{
    return read_executable_directory();
}

std::filesystem::path default_data_directory()
{
    return resolve_runtime_directory("data", "archetypes");
}

std::filesystem::path default_scenarios_directory()
{
    return default_data_directory() / constants::SCENARIOS_DIRECTORY_NAME;
}

std::filesystem::path default_game_root_directory()
{
    const std::filesystem::path executable_dir = read_executable_directory();
    if (!executable_dir.empty()) {
        return executable_dir;
    }

#ifdef AOA_RUNTIME_ROOT
    return std::filesystem::path(AOA_RUNTIME_ROOT);
#else
    return {};
#endif
}

std::filesystem::path default_playable_scenarios_directory()
{
    return default_game_root_directory() / constants::SCENARIOS_DIRECTORY_NAME;
}

std::filesystem::path default_patterns_directory()
{
    return default_game_root_directory() / constants::PATTERNS_DIRECTORY_NAME;
}

std::filesystem::path default_assets_directory()
{
    return resolve_runtime_directory("assets", std::filesystem::path{});
}

std::filesystem::path default_logs_directory()
{
    const std::filesystem::path executable_dir = read_executable_directory();
    if (!executable_dir.empty()) {
        return executable_dir / "logs";
    }

    return std::filesystem::path("logs");
}

} // namespace aoa::core
