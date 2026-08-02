#pragma once

#include "data/content_types.hpp"

#include <filesystem>
#include <string>

namespace aoa::data {

CivDefinition load_civ_definition(const std::filesystem::path& civ_json_path);

std::filesystem::path default_data_directory();

} // namespace aoa::data
