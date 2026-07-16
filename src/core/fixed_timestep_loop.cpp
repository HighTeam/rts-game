#include "core/fixed_timestep_loop.hpp"

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
    const std::function<bool()>& should_continue)
{
    using clock = std::chrono::steady_clock;

    const double sim_delta_seconds =
        1.0 / static_cast<double>(constants::SIM_TICKS_PER_SECOND);

    auto previous_time = clock::now();
    double accumulator = 0.0;

    while (should_continue()) {
        const auto current_time = clock::now();
        const double frame_seconds =
            std::chrono::duration<double>(current_time - previous_time).count();
        previous_time = current_time;

        accumulator += frame_seconds;

        while (accumulator >= sim_delta_seconds) {
            on_tick();
            accumulator -= sim_delta_seconds;
        }

        const float interpolation_alpha =
            static_cast<float>(accumulator / sim_delta_seconds);
        on_render(interpolation_alpha);
    }
}

} // namespace aoa::core
