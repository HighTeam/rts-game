#pragma once

#include "render/camera.hpp"

namespace aoa::render {

[[nodiscard]] CameraView active_camera_view();
[[nodiscard]] bool is_camera_view_available(const CameraView view);
[[nodiscard]] const char* camera_view_label(const CameraView view);

} // namespace aoa::render
