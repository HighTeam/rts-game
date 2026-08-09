#pragma once

#include "core/constants.hpp"

#include <chrono>

namespace aoa::app {

class FpsTracker {
public:
    void record_frame()
    {
        ++frames_in_window_;

        const auto now = std::chrono::steady_clock::now();
        if (!window_started_) {
            window_start_ = now;
            window_started_ = true;
            return;
        }

        const float elapsed_seconds = std::chrono::duration<float>(now - window_start_).count();
        if (elapsed_seconds < constants::HUD_PERF_SAMPLE_WINDOW_SECONDS) {
            return;
        }

        fps_ = static_cast<float>(frames_in_window_) / elapsed_seconds;
        frames_in_window_ = 0;
        window_start_ = now;
    }

    [[nodiscard]] float fps() const { return fps_; }

private:
    std::chrono::steady_clock::time_point window_start_{};
    int frames_in_window_{0};
    float fps_{0.0F};
    bool window_started_{false};
};

} // namespace aoa::app
