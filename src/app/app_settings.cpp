#include "app/app_settings.hpp"

#include "core/constants.hpp"
#include "core/runtime_paths.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace aoa::app {
namespace {

[[nodiscard]] std::filesystem::path settings_file_path()
{
    const std::filesystem::path executable_dir = core::executable_directory();
    if (!executable_dir.empty()) {
        return executable_dir / constants::SETTINGS_FILE_NAME;
    }

    return std::filesystem::path(constants::SETTINGS_FILE_NAME);
}

[[nodiscard]] float clamp_volume(const float value)
{
    return std::clamp(value, constants::AUDIO_VOLUME_MIN, constants::AUDIO_VOLUME_MAX);
}

[[nodiscard]] int sanitize_fps_limit(const int fps_limit)
{
    for (const int preset : constants::VIDEO_FPS_PRESETS) {
        if (preset == fps_limit) {
            return fps_limit;
        }
    }

    return constants::TARGET_DISPLAY_FPS;
}

} // namespace

AppShellSettings load_app_settings()
{
    AppShellSettings settings{};
    settings.master_volume = constants::AUDIO_VOLUME_MAX;
    settings.music_volume = constants::AUDIO_VOLUME_MAX;
    settings.sfx_volume = constants::AUDIO_VOLUME_MAX;

    std::ifstream in(settings_file_path());
    if (!in) {
        return settings;
    }

    nlohmann::json json{};
    try {
        in >> json;
    }
    catch (const nlohmann::json::exception&) {
        return settings;
    }

    if (!json.is_object()) {
        return settings;
    }

    settings.fullscreen = json.value("fullscreen", settings.fullscreen);
    settings.mouse_capture = json.value("mouse_capture", settings.mouse_capture);
    settings.vsync = json.value("vsync", settings.vsync);
    settings.fps_limit = sanitize_fps_limit(json.value("fps_limit", settings.fps_limit));
    settings.show_perf_hud = json.value("show_perf_hud", settings.show_perf_hud);
    settings.master_volume = clamp_volume(json.value("master_volume", settings.master_volume));
    settings.music_volume = clamp_volume(json.value("music_volume", settings.music_volume));
    settings.sfx_volume = clamp_volume(json.value("sfx_volume", settings.sfx_volume));
    settings.scroll_speed = std::clamp(
        json.value("scroll_speed", settings.scroll_speed),
        constants::CAMERA_SCROLL_SPEED_MIN,
        constants::CAMERA_SCROLL_SPEED_MAX);
    return settings;
}

void save_app_settings(const AppShellSettings& settings)
{
    const std::filesystem::path path = settings_file_path();
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code error{};
        std::filesystem::create_directories(parent, error);
        if (error) {
            return;
        }
    }

    const nlohmann::json json = {
        {"version", constants::SETTINGS_FILE_VERSION},
        {"fullscreen", settings.fullscreen},
        {"mouse_capture", settings.mouse_capture},
        {"vsync", settings.vsync},
        {"fps_limit", settings.fps_limit},
        {"show_perf_hud", settings.show_perf_hud},
        {"master_volume", settings.master_volume},
        {"music_volume", settings.music_volume},
        {"sfx_volume", settings.sfx_volume},
        {"scroll_speed", settings.scroll_speed},
    };

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return;
    }

    out << json.dump(2);
}

} // namespace aoa::app
