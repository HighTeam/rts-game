#pragma once

#include "net/enet_transport.hpp"
#include "sim/player/player_command.hpp"
#include "sim/simulation.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aoa::net {

enum class LockstepRole {
    Host,
    Client,
};

class LockstepSession {
public:
    LockstepSession(LockstepRole role, std::uint8_t player_slot, sim::Simulation& simulation);

    [[nodiscard]] bool start_host(std::uint16_t port);
    [[nodiscard]] bool connect(const char* host_name, std::uint16_t port);

    void submit_local_command(sim::player::PlayerCommand command);
    void poll();

    [[nodiscard]] bool try_advance_tick();

    [[nodiscard]] bool is_connected() const;
    [[nodiscard]] bool is_desynced() const;
    [[nodiscard]] std::uint64_t desync_tick() const;

private:
    [[nodiscard]] std::uint64_t next_execute_tick() const;
    [[nodiscard]] bool has_local_sent(std::uint64_t execute_tick) const;
    [[nodiscard]] bool has_remote_ready(std::uint64_t execute_tick) const;

    void send_input_batch(std::uint64_t execute_tick, const std::vector<sim::player::PlayerCommand>& commands);
    void send_state_hash(std::uint64_t execute_tick, std::uint64_t state_hash);
    void ensure_local_batch_sent(std::uint64_t execute_tick);
    void flush_local_commands_for_tick(std::uint64_t execute_tick);
    void process_received_packet(const std::vector<std::byte>& packet);
    void verify_state_hash(std::uint64_t execute_tick, std::uint64_t remote_hash);

    LockstepRole role_{LockstepRole::Host};
    std::uint8_t player_slot_{0U};
    sim::Simulation& simulation_;
    EnetTransport transport_{};

    std::unordered_set<std::uint64_t> local_sent_ticks_{};
    std::unordered_set<std::uint64_t> remote_ready_ticks_{};
    std::unordered_map<std::uint64_t, std::vector<sim::player::PlayerCommand>> local_outbox_{};
    std::uint64_t local_command_sequence_{1U};

    bool desynced_{false};
    std::uint64_t desync_tick_{0U};
    std::unordered_map<std::uint64_t, std::uint64_t> local_state_hashes_{};
    std::unordered_map<std::uint64_t, std::uint64_t> remote_state_hashes_{};
};

} // namespace aoa::net
