#pragma once

#include "sim/player/player_command.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace aoa::net {

struct TickInputBatch {
    std::uint64_t execute_tick{0U};
    std::uint8_t player_slot{0U};
    std::vector<sim::player::PlayerCommand> commands{};
};

struct TickStateHashMessage {
    std::uint64_t execute_tick{0U};
    std::uint8_t player_slot{0U};
    std::uint64_t state_hash{0U};
};

struct LatencyProbeMessage {
    std::uint64_t send_time_ns{0U};
    std::uint32_t sequence{0U};
};

[[nodiscard]] std::vector<std::byte> encode_latency_probe(const LatencyProbeMessage& message);

[[nodiscard]] std::optional<LatencyProbeMessage> decode_latency_probe(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_tick_input_batch(const TickInputBatch& batch);

[[nodiscard]] std::optional<TickInputBatch> decode_tick_input_batch(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_tick_state_hash(const TickStateHashMessage& message);

[[nodiscard]] std::optional<TickStateHashMessage> decode_tick_state_hash(std::span<const std::byte> bytes);

} // namespace aoa::net
