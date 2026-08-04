#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace aoa::net {

struct LockstepRunOptions {
    std::uint64_t tick_count{0U};
    std::uint16_t port{0U};
    std::optional<std::string> join_address{};
};

[[nodiscard]] int run_lockstep_smoke();

[[nodiscard]] int run_lockstep_host(const LockstepRunOptions& options);

[[nodiscard]] int run_lockstep_join(const LockstepRunOptions& options);

[[nodiscard]] std::optional<std::pair<std::string, std::uint16_t>> parse_lockstep_join_address(
    const std::string& address_text);

} // namespace aoa::net
