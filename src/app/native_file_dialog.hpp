#pragma once

#include <SFML/Window/Window.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace aoa::app {

[[nodiscard]] std::optional<std::filesystem::path> pick_existing_file(
    sf::Window& window,
    const std::filesystem::path& initial_directory,
    const std::string& filter_description,
    const std::string& filter_pattern);

[[nodiscard]] std::optional<std::filesystem::path> pick_save_file(
    sf::Window& window,
    const std::filesystem::path& initial_directory,
    const std::string& filter_description,
    const std::string& filter_pattern,
    const std::string& default_name);

void open_url_in_browser(const std::string& url);

} // namespace aoa::app
