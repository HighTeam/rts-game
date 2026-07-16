#pragma once

#include "core/constants.hpp"

#include <chrono>
#include <cstdint>
#include <functional>

namespace aoa::core {

class FixedTimestepLoop {
public:
    using TickCallback = std::function<void()>;
    using RenderCallback = std::function<void(float interpolation_alpha)>;

    void run_headless(TickCallback on_tick, std::uint64_t tick_count);

    void run_realtime(
        TickCallback on_tick,
        RenderCallback on_render,
        const std::function<bool()>& should_continue);
};

} // namespace aoa::core
