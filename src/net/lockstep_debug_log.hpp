#pragma once

#include "net/lockstep_session.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace aoa::net {

class LockstepDebugLog {
public:
    static void enable(std::uint8_t player_slot, LockstepRole role);
    static void disable();
    [[nodiscard]] static bool is_enabled();

    static void log(std::string_view message);
    static void log_event(std::string_view event, std::string_view detail = {});

private:
    [[nodiscard]] static std::string timestamp_now();
};

} // namespace aoa::net
