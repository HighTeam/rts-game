#include "core/fixed_timestep_loop.hpp"

#include <chrono>

namespace aoa::core {

void FixedTimestepLoop::run_headless(TickCallback on_tick, const std::uint64_t tick_count)
{
    for (std::uint64_t tick = 0U; tick < tick_count; ++tick) {
        on_tick();
    }
}

void FixedTimestepLoop::run_realtime(
    TickCallback on_tick,
    RenderCallback on_render,
    const std::function<bool()>& should_continue,
    TickCallback on_before_sim_ticks)
{
    using clock = std::chrono::steady_clock;

    const double sim_delta_seconds =
        1.0 / static_cast<double>(constants::SIM_TICKS_PER_SECOND);

    auto previous_time = clock::now();
    double accumulator = 0.0;

    while (should_continue()) {
        const auto frame_start = clock::now();
        const auto current_time = frame_start;
        const double frame_seconds =
            std::chrono::duration<double>(current_time - previous_time).count();
        previous_time = current_time;

        double clamped_frame_seconds = frame_seconds;
        if (clamped_frame_seconds > static_cast<double>(constants::SIM_MAX_FRAME_DELTA_SECONDS)) {
            clamped_frame_seconds = static_cast<double>(constants::SIM_MAX_FRAME_DELTA_SECONDS);
        }
        accumulator += clamped_frame_seconds;

        const double max_accumulator_seconds =
            sim_delta_seconds * static_cast<double>(constants::SIM_MAX_TICKS_PER_FRAME);
        if (accumulator > max_accumulator_seconds) {
            accumulator = max_accumulator_seconds;
        }

        bool snapshotted = false;
        int ticks_this_frame = 0;
        while (accumulator >= sim_delta_seconds
            && ticks_this_frame < constants::SIM_MAX_TICKS_PER_FRAME) {
            if (!snapshotted && on_before_sim_ticks) {
                on_before_sim_ticks();
                snapshotted = true;
            }

            on_tick();
            accumulator -= sim_delta_seconds;
            ++ticks_this_frame;
        }

        const float interpolation_alpha =
            static_cast<float>(accumulator / sim_delta_seconds);
        on_render(interpolation_alpha);
    }
}

} // namespace aoa::core
