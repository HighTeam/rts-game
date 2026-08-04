#include "net/lockstep_session.hpp"

#include "net/lockstep_wire.hpp"
#include "net/net_constants.hpp"
#include "net/net_message.hpp"

#include <iostream>
#include <utility>

namespace aoa::net {

LockstepSession::LockstepSession(
    const LockstepRole role,
    const std::uint8_t player_slot,
    sim::Simulation& simulation)
    : role_(role)
    , player_slot_(player_slot)
    , simulation_(simulation)
{
}

bool LockstepSession::start_host(const std::uint16_t port)
{
    return transport_.start_host(port);
}

bool LockstepSession::connect(const char* host_name, const std::uint16_t port)
{
    return transport_.connect(host_name, port);
}

void LockstepSession::submit_local_command(sim::player::PlayerCommand command)
{
    if (desynced_) {
        return;
    }

    command.player_slot = player_slot_;
    command.sequence = local_command_sequence_++;

    const std::uint64_t earliest_execute_tick =
        simulation_.tick_count() + static_cast<std::uint64_t>(constants::LOCKSTEP_COMMAND_DELAY_TICKS);
    if (command.execute_tick < earliest_execute_tick) {
        command.execute_tick = earliest_execute_tick;
    }

    while (has_local_sent(command.execute_tick)) {
        ++command.execute_tick;
    }

    local_outbox_[command.execute_tick].push_back(command);
}

void LockstepSession::poll()
{
    if (desynced_) {
        return;
    }

    transport_.poll(constants::NET_POLL_TIMEOUT_MS);

    for (const std::vector<std::byte>& packet : transport_.drain_received()) {
        process_received_packet(packet);
    }
}

bool LockstepSession::try_advance_tick()
{
    if (desynced_ || !is_connected()) {
        return false;
    }

    const std::uint64_t execute_tick = next_execute_tick();
    ensure_local_batch_sent(execute_tick);

    if (!has_remote_ready(execute_tick)) {
        return false;
    }

    flush_local_commands_for_tick(execute_tick);
    simulation_.tick();

    const std::uint64_t completed_tick = simulation_.tick_count();
    const std::uint64_t local_hash = simulation_.state_hash();
    send_state_hash(completed_tick, local_hash);
    verify_state_hash(completed_tick, local_hash);

    return true;
}

bool LockstepSession::is_connected() const
{
    if (role_ == LockstepRole::Host) {
        return transport_.has_peer();
    }

    return transport_.is_connected();
}

bool LockstepSession::is_desynced() const
{
    return desynced_;
}

std::uint64_t LockstepSession::desync_tick() const
{
    return desync_tick_;
}

std::uint64_t LockstepSession::next_execute_tick() const
{
    return simulation_.tick_count() + 1U;
}

bool LockstepSession::has_local_sent(const std::uint64_t execute_tick) const
{
    return local_sent_ticks_.contains(execute_tick);
}

bool LockstepSession::has_remote_ready(const std::uint64_t execute_tick) const
{
    return remote_ready_ticks_.contains(execute_tick);
}

void LockstepSession::send_input_batch(
    const std::uint64_t execute_tick,
    const std::vector<sim::player::PlayerCommand>& commands)
{
    TickInputBatch batch{};
    batch.execute_tick = execute_tick;
    batch.player_slot = player_slot_;
    batch.commands = commands;

    const std::vector<std::byte> payload = encode_tick_input_batch(batch);
    if (payload.empty()) {
        desynced_ = true;
        desync_tick_ = execute_tick;
        return;
    }

    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::TickInputBatch, payload);
    if (!transport_.send_reliable(wire_message, constants::CHANNEL_RELIABLE)) {
        desynced_ = true;
        desync_tick_ = execute_tick;
    }
}

void LockstepSession::send_state_hash(const std::uint64_t execute_tick, const std::uint64_t state_hash)
{
    TickStateHashMessage message{};
    message.execute_tick = execute_tick;
    message.player_slot = player_slot_;
    message.state_hash = state_hash;

    const std::vector<std::byte> payload = encode_tick_state_hash(message);
    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::TickStateHash, payload);
    (void)transport_.send_reliable(wire_message, constants::CHANNEL_RELIABLE);
}

void LockstepSession::ensure_local_batch_sent(const std::uint64_t execute_tick)
{
    if (has_local_sent(execute_tick)) {
        return;
    }

    const std::vector<sim::player::PlayerCommand> empty_batch{};
    const auto iterator = local_outbox_.find(execute_tick);
    const std::vector<sim::player::PlayerCommand>& commands =
        iterator != local_outbox_.end() ? iterator->second : empty_batch;

    send_input_batch(execute_tick, commands);
    local_sent_ticks_.insert(execute_tick);
}

void LockstepSession::flush_local_commands_for_tick(const std::uint64_t execute_tick)
{
    const auto iterator = local_outbox_.find(execute_tick);
    if (iterator == local_outbox_.end()) {
        return;
    }

    for (sim::player::PlayerCommand& command : iterator->second) {
        simulation_.enqueue_network_command(std::move(command));
    }

    local_outbox_.erase(iterator);
}

void LockstepSession::process_received_packet(const std::vector<std::byte>& packet)
{
    const auto decoded_message = decode_net_message(packet);
    if (!decoded_message.has_value()) {
        return;
    }

    if (decoded_message->first == NetMessageKind::TickInputBatch) {
        const auto decoded_batch = decode_tick_input_batch(decoded_message->second);
        if (!decoded_batch.has_value()) {
            return;
        }

        TickInputBatch batch = *decoded_batch;
        if (batch.player_slot == player_slot_) {
            return;
        }

        for (sim::player::PlayerCommand& command : batch.commands) {
            command.player_slot = batch.player_slot;
            command.execute_tick = batch.execute_tick;
            simulation_.enqueue_network_command(std::move(command));
        }

        remote_ready_ticks_.insert(batch.execute_tick);
        return;
    }

    if (decoded_message->first == NetMessageKind::TickStateHash) {
        const auto message = decode_tick_state_hash(decoded_message->second);
        if (!message.has_value()) {
            return;
        }

        if (message->player_slot == player_slot_) {
            return;
        }

        remote_state_hashes_[message->execute_tick] = message->state_hash;

        const auto local_iterator = local_state_hashes_.find(message->execute_tick);
        if (local_iterator != local_state_hashes_.end()) {
            verify_state_hash(message->execute_tick, local_iterator->second);
        }
    }
}

void LockstepSession::verify_state_hash(
    const std::uint64_t execute_tick,
    const std::uint64_t local_hash)
{
    local_state_hashes_[execute_tick] = local_hash;

    const auto remote_iterator = remote_state_hashes_.find(execute_tick);
    if (remote_iterator == remote_state_hashes_.end()) {
        return;
    }

    if (remote_iterator->second == local_hash) {
        return;
    }

    if (desynced_) {
        return;
    }

    desynced_ = true;
    desync_tick_ = execute_tick;
    std::cerr << "lockstep desync at tick " << execute_tick << ": local=0x" << std::hex << local_hash
              << " remote=0x" << remote_iterator->second << std::dec << '\n';
}

} // namespace aoa::net
