#pragma once

#include "core/constants.hpp"

#include <chrono>
#include <cstdint>

namespace aoa::app {

class TpsTracker {
public:
    void record_tick()
    {
        note_completed_ticks(1);
    }

    void observe_tick_count(const std::uint64_t tick_count)
    {
        if (!has_last_tick_count_) {
            last_tick_count_ = tick_count;
            has_last_tick_count_ = true;
            ensure_window_started();
            return;
        }

        if (tick_count <= last_tick_count_) {
            ensure_window_started();
            maybe_close_sample_window();
            return;
        }

        note_completed_ticks(tick_count - last_tick_count_);
        last_tick_count_ = tick_count;
    }

    [[nodiscard]] float tps() const
    {
        return tps_;
    }

private:
    void ensure_window_started()
    {
        if (window_started_) {
            return;
        }

        window_start_ = std::chrono::steady_clock::now();
        window_started_ = true;
    }

    void note_completed_ticks(const std::uint64_t completed_ticks)
    {
        ticks_in_window_ += completed_ticks;
        ensure_window_started();
        maybe_close_sample_window();
    }

    void maybe_close_sample_window()
    {
        const auto now = std::chrono::steady_clock::now();
        const float elapsed_seconds = std::chrono::duration<float>(now - window_start_).count();
        if (elapsed_seconds < constants::HUD_TPS_SAMPLE_WINDOW_SECONDS) {
            return;
        }

        const float measured = static_cast<float>(ticks_in_window_) / elapsed_seconds;
        if (!has_tps_sample_) {
            tps_ = measured;
            has_tps_sample_ = true;
        }
        else {
            const float alpha = constants::HUD_TPS_DISPLAY_SMOOTHING_ALPHA;
            tps_ = tps_ * (1.0F - alpha) + measured * alpha;
        }

        ticks_in_window_ = 0U;
        window_start_ = now;
    }

    std::chrono::steady_clock::time_point window_start_{};
    std::uint64_t ticks_in_window_{0U};
    std::uint64_t last_tick_count_{0U};
    float tps_{0.0F};
    bool window_started_{false};
    bool has_last_tick_count_{false};
    bool has_tps_sample_{false};
};

} // namespace aoa::app
