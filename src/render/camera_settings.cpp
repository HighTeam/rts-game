#include "render/camera_settings.hpp"

namespace aoa::render {

CameraView active_camera_view()
{
    return CameraView::Classic;
}

bool is_camera_view_available(const CameraView view)
{
    if (view == CameraView::Classic) {
        return true;
    }

    return false;
}

const char* camera_view_label(const CameraView view)
{
    switch (view) {
    case CameraView::Classic:
        return "Classic";
    case CameraView::Full3D:
        return "Full 3D";
    }

    return "Unknown";
}

} // namespace aoa::render
