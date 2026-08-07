#pragma once

#include "net/net_constants.hpp"

#include <array>
#include <cstdint>

namespace aoa::net {

struct LockstepNetworkHudStats {
    bool active{false};
    int local_ping_ms{-1};
    std::array<int, constants::LOCKSTEP_MAX_PLAYER_SLOTS> peer_latency_ms{};
};

} // namespace aoa::net
