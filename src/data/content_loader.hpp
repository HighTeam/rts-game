#pragma once

#include "data/content_types.hpp"

#include <filesystem>

namespace aoa::data {

ContentDatabase load_content_database(const std::filesystem::path& data_directory);

std::filesystem::path default_data_directory();

} // namespace aoa::data
