#pragma once

#include "app/application.hpp"

namespace aoa::app {

[[nodiscard]] AppShellSettings load_app_settings();

void save_app_settings(const AppShellSettings& settings);

} // namespace aoa::app
