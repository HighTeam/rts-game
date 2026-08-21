#include "net/enet_transport.hpp"

#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace aoa::net {

namespace {

int global_init_count = 0;

[[nodiscard]] std::uint8_t peer_slot_from_data(_ENetPeer* peer)
{
    if (peer == nullptr) {
        return 0U;
    }

    return static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(peer->data));
}

void set_peer_slot(_ENetPeer* peer, const std::uint8_t client_slot)
{
    if (peer != nullptr) {
        peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(client_slot));
    }
}

} // namespace

EnetTransport::EnetTransport() = default;

EnetTransport::~EnetTransport()
{
    disconnect();
}

bool EnetTransport::global_initialize()
{
    if (global_init_count > 0) {
        ++global_init_count;
        return true;
    }

    if (enet_initialize() != 0) {
        return false;
    }

    global_init_count = 1;
    return true;
}

void EnetTransport::global_deinitialize()
{
    if (global_init_count <= 0) {
        return;
    }

    --global_init_count;
    if (global_init_count == 0) {
        enet_deinitialize();
    }
}

void EnetTransport::start_network_thread()
{
    if (network_thread_running_.exchange(true)) {
        return;
    }

    network_thread_ = std::thread([this]() { network_thread_loop(); });
}

void EnetTransport::stop_network_thread()
{
    if (!network_thread_running_.exchange(false)) {
        return;
    }

    outbound_cv_.notify_all();

    if (network_thread_.joinable()) {
        network_thread_.join();
    }
}

void EnetTransport::apply_peer_timeouts_locked(_ENetPeer* peer)
{
    if (peer == nullptr) {
        return;
    }

    peer->timeoutMinimum = constants::ENET_PEER_TIMEOUT_MINIMUM_MS;
    peer->timeoutMaximum = constants::ENET_PEER_TIMEOUT_MAXIMUM_MS;
    peer->timeoutLimit = constants::ENET_PEER_TIMEOUT_LIMIT;
}

_ENetPeer* EnetTransport::client_peer_locked(const std::uint8_t client_slot) const
{
    if (client_slot >= client_peers_.size()) {
        return nullptr;
    }

    return client_peers_[client_slot];
}

std::uint8_t EnetTransport::peer_client_slot_locked(_ENetPeer* peer) const
{
    return peer_slot_from_data(peer);
}

bool EnetTransport::send_unreliable_immediate_locked(
    _ENetPeer* peer,
    const std::span<const std::byte> data,
    const std::uint8_t channel)
{
    if (host_ == nullptr || peer == nullptr || data.empty()) {
        return false;
    }

    ENetPacket* enet_packet = enet_packet_create(
        data.data(),
        data.size(),
        ENET_PACKET_FLAG_UNSEQUENCED);
    if (enet_packet == nullptr) {
        return false;
    }

    if (enet_peer_send(peer, channel, enet_packet) != 0) {
        enet_packet_destroy(enet_packet);
        return false;
    }

    enet_host_flush(host_);
    return true;
}

void EnetTransport::flush_outbound_locked()
{
    if (host_ == nullptr) {
        outbound_queue_.clear();
        return;
    }

    while (!outbound_queue_.empty()) {
        OutboundPacket outbound_packet = std::move(outbound_queue_.front());
        outbound_queue_.pop_front();

        const int packet_flags =
            outbound_packet.reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED;

        if (is_server_) {
            if (outbound_packet.broadcast) {
                for (std::uint8_t client_slot = 1U; client_slot <= max_clients_; ++client_slot) {
                    if (outbound_packet.except_client_slot.has_value()
                        && *outbound_packet.except_client_slot == client_slot) {
                        continue;
                    }

                    _ENetPeer* peer = client_peers_[client_slot];
                    if (peer == nullptr) {
                        continue;
                    }

                    ENetPacket* packet_copy = enet_packet_create(
                        outbound_packet.data.data(),
                        outbound_packet.data.size(),
                        packet_flags);
                    if (packet_copy == nullptr) {
                        continue;
                    }

                    if (enet_peer_send(peer, outbound_packet.channel, packet_copy) != 0) {
                        enet_packet_destroy(packet_copy);
                    }
                }
            }
            else {
                _ENetPeer* target_peer = nullptr;
                if (outbound_packet.target_client_slot.has_value()) {
                    target_peer = client_peer_locked(*outbound_packet.target_client_slot);
                }
                else {
                    for (std::uint8_t client_slot = 1U; client_slot <= max_clients_; ++client_slot) {
                        if (client_peers_[client_slot] != nullptr) {
                            target_peer = client_peers_[client_slot];
                            break;
                        }
                    }
                }

                if (target_peer == nullptr) {
                    continue;
                }

                ENetPacket* enet_packet = enet_packet_create(
                    outbound_packet.data.data(),
                    outbound_packet.data.size(),
                    packet_flags);
                if (enet_packet == nullptr) {
                    continue;
                }

                if (enet_peer_send(target_peer, outbound_packet.channel, enet_packet) != 0) {
                    enet_packet_destroy(enet_packet);
                }
            }
        }
        else {
            if (client_peer_ == nullptr) {
                continue;
            }

            ENetPacket* enet_packet = enet_packet_create(
                outbound_packet.data.data(),
                outbound_packet.data.size(),
                packet_flags);
            if (enet_packet == nullptr) {
                continue;
            }

            if (enet_peer_send(client_peer_, outbound_packet.channel, enet_packet) != 0) {
                enet_packet_destroy(enet_packet);
            }
        }
    }

    enet_host_flush(host_);
}

void EnetTransport::service_events_locked(const std::uint32_t timeout_ms)
{
    if (host_ == nullptr) {
        return;
    }

    ENetEvent event{};
    std::uint32_t remaining_timeout = timeout_ms;

    while (enet_host_service(host_, &event, remaining_timeout) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            if (is_server_) {
                std::uint8_t assigned_slot = 0U;
                bool assigned = false;
                for (std::uint8_t client_slot = 1U; client_slot <= max_clients_; ++client_slot) {
                    if (client_peers_[client_slot] == nullptr) {
                        assigned_slot = client_slot;
                        assigned = true;
                        break;
                    }
                }

                if (!assigned) {
                    enet_peer_disconnect(event.peer, 0);
                    break;
                }

                client_peers_[assigned_slot] = event.peer;
                set_peer_slot(event.peer, assigned_slot);
                pending_client_connected_slots_.push_back(assigned_slot);
            }
            else {
                client_peer_ = event.peer;
                set_peer_slot(client_peer_, constants::LOCKSTEP_HOST_PLAYER_SLOT);
            }

            apply_peer_timeouts_locked(event.peer);
            client_peer_lost_ = false;
            break;
        case ENET_EVENT_TYPE_RECEIVE: {
            std::vector<std::byte> packet_bytes(event.packet->dataLength);
            if (!packet_bytes.empty()) {
                std::memcpy(
                    packet_bytes.data(),
                    event.packet->data,
                    event.packet->dataLength);
            }

            if (inbound_latency_handler_) {
                const InboundLatencyHandleResult latency_result =
                    inbound_latency_handler_(packet_bytes);
                if (latency_result.handled) {
                    if (!latency_result.immediate_reply.empty()) {
                        (void)send_unreliable_immediate_locked(
                            event.peer,
                            latency_result.immediate_reply,
                            constants::CHANNEL_UNRELIABLE);
                    }

                    enet_packet_destroy(event.packet);
                    break;
                }
            }

            ReceivedPacket received{};
            received.sender_slot = is_server_ ? peer_client_slot_locked(event.peer)
                                             : constants::LOCKSTEP_HOST_PLAYER_SLOT;
            received.data = std::move(packet_bytes);
            inbound_queue_.push_back(std::move(received));
            enet_packet_destroy(event.packet);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
            if (is_server_) {
                const std::uint8_t lost_slot = peer_client_slot_locked(event.peer);
                if (lost_slot > 0U && lost_slot < client_peers_.size()) {
                    client_peers_[lost_slot] = nullptr;
                    pending_client_lost_slots_.push_back(lost_slot);
                }
            }
            else if (event.peer == client_peer_) {
                client_peer_lost_ = true;
                client_peer_ = nullptr;
            }

            break;
        default:
            break;
        }

        remaining_timeout = 0U;
    }
}

void EnetTransport::network_thread_loop()
{
    while (network_thread_running_.load()) {
        {
            std::unique_lock lock(mutex_);

            flush_outbound_locked();

            if (host_ != nullptr) {
                service_events_locked(constants::ENET_NETWORK_THREAD_SERVICE_TIMEOUT_MS);
            }

            if (!network_thread_running_.load()) {
                break;
            }

            if (outbound_queue_.empty()) {
                outbound_cv_.wait_for(
                    lock,
                    std::chrono::milliseconds(constants::ENET_NETWORK_THREAD_IDLE_WAIT_MS),
                    [this]() {
                        return !network_thread_running_.load() || !outbound_queue_.empty();
                    });
            }
        }
    }
}

void EnetTransport::destroy_host_locked()
{
    // Force-drop without waiting for a remote ACK. A blocking enet_host_service
    // wait here freezes the UI thread (same transport mutex) during client reconnect.
    if (host_ != nullptr && !is_server_ && client_peer_ != nullptr) {
        enet_peer_disconnect_now(client_peer_, 0);
        ENetEvent event{};
        while (enet_host_service(host_, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                enet_packet_destroy(event.packet);
            }
        }
    }

    if (host_ != nullptr) {
        enet_host_destroy(host_);
    }

    host_ = nullptr;
    client_peer_ = nullptr;
    client_peers_.fill(nullptr);
    is_server_ = false;
    client_peer_lost_ = false;
    pending_client_connected_slots_.clear();
    pending_client_lost_slots_.clear();
    outbound_queue_.clear();
    inbound_queue_.clear();
}

bool EnetTransport::start_host(const std::uint16_t port, const std::uint8_t max_clients)
{
    disconnect();

    {
        std::lock_guard lock(mutex_);

        max_clients_ = std::max<std::uint8_t>(1U, max_clients);
        if (max_clients_ >= constants::LOCKSTEP_MAX_PLAYER_SLOTS) {
            max_clients_ = static_cast<std::uint8_t>(constants::LOCKSTEP_MAX_PLAYER_SLOTS - 1);
        }

        ENetAddress address{};
        address.host = ENET_HOST_ANY;
        address.port = port;

        host_ = enet_host_create(
            &address,
            max_clients_,
            constants::CHANNEL_COUNT,
            0,
            0);
        if (host_ == nullptr) {
            return false;
        }

        is_server_ = true;
        client_peer_lost_ = false;
        client_peers_.fill(nullptr);
    }

    start_network_thread();
    return true;
}

bool EnetTransport::connect(const char* host_name, const std::uint16_t port)
{
    disconnect();

    {
        std::lock_guard lock(mutex_);

        host_ = enet_host_create(nullptr, 1, constants::CHANNEL_COUNT, 0, 0);
        if (host_ == nullptr) {
            return false;
        }

        ENetAddress address{};
        if (enet_address_set_host(&address, host_name) != 0) {
            destroy_host_locked();
            return false;
        }

        address.port = port;
        client_peer_ = enet_host_connect(host_, &address, constants::CHANNEL_COUNT, 0);
        if (client_peer_ == nullptr) {
            destroy_host_locked();
            return false;
        }

        apply_peer_timeouts_locked(client_peer_);
        set_peer_slot(client_peer_, constants::LOCKSTEP_HOST_PLAYER_SLOT);
        is_server_ = false;
        client_peer_lost_ = false;
        max_clients_ = constants::LOCKSTEP_DEFAULT_MAX_CLIENTS;
    }

    start_network_thread();
    return true;
}

void EnetTransport::disconnect()
{
    stop_network_thread();

    std::lock_guard lock(mutex_);
    destroy_host_locked();
}

void EnetTransport::disconnect_peer()
{
    std::lock_guard lock(mutex_);

    if (is_server_) {
        for (std::uint8_t client_slot = 1U; client_slot <= max_clients_; ++client_slot) {
            if (client_peers_[client_slot] != nullptr) {
                enet_peer_disconnect(client_peers_[client_slot], 0);
                client_peers_[client_slot] = nullptr;
            }
        }

        ENetEvent event{};
        while (host_ != nullptr && enet_host_service(host_, &event, 1000) > 0) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                enet_packet_destroy(event.packet);
            }
        }

        return;
    }

    if (host_ == nullptr || client_peer_ == nullptr) {
        return;
    }

    enet_peer_disconnect(client_peer_, 0);
    ENetEvent event{};
    while (enet_host_service(host_, &event, 1000) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(event.packet);
        }
    }

    client_peer_ = nullptr;
    client_peer_lost_ = false;
}

void EnetTransport::disconnect_peer_slot(const std::uint8_t client_slot)
{
    std::lock_guard lock(mutex_);

    if (!is_server_) {
        disconnect_peer();
        return;
    }

    _ENetPeer* peer = client_peer_locked(client_slot);
    if (peer == nullptr) {
        return;
    }

    enet_peer_disconnect(peer, 0);
    client_peers_[client_slot] = nullptr;
}

void EnetTransport::poll(const std::uint32_t timeout_ms)
{
    if (timeout_ms == 0U) {
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
}

void EnetTransport::flush_outbound()
{
    std::lock_guard lock(mutex_);
    flush_outbound_locked();
}

bool EnetTransport::enqueue_outbound(OutboundPacket packet)
{
    if (!network_thread_running_.load()) {
        return false;
    }

    {
        std::lock_guard lock(mutex_);

        if (is_server_) {
            bool has_any_client = false;
            for (std::uint8_t client_slot = 1U; client_slot <= max_clients_; ++client_slot) {
                if (client_peers_[client_slot] != nullptr) {
                    has_any_client = true;
                    break;
                }
            }

            if (!has_any_client && !packet.broadcast) {
                return false;
            }
        }
        else if (client_peer_ == nullptr) {
            return false;
        }

        outbound_queue_.push_back(std::move(packet));
    }

    outbound_cv_.notify_one();
    return true;
}

bool EnetTransport::enqueue_broadcast_except(
    const std::span<const std::byte> data,
    const std::uint8_t channel,
    const bool reliable,
    const std::optional<std::uint8_t> except_client_slot)
{
    OutboundPacket packet{};
    packet.data.assign(data.begin(), data.end());
    packet.channel = channel;
    packet.reliable = reliable;
    packet.broadcast = true;
    packet.except_client_slot = except_client_slot;
    return enqueue_outbound(std::move(packet));
}

bool EnetTransport::send_reliable(const std::span<const std::byte> data, const std::uint8_t channel)
{
    OutboundPacket packet{};
    packet.data.assign(data.begin(), data.end());
    packet.channel = channel;
    packet.reliable = true;
    return enqueue_outbound(std::move(packet));
}

bool EnetTransport::send_reliable_to_client(
    const std::uint8_t client_slot,
    const std::span<const std::byte> data,
    const std::uint8_t channel)
{
    OutboundPacket packet{};
    packet.data.assign(data.begin(), data.end());
    packet.channel = channel;
    packet.reliable = true;
    packet.target_client_slot = client_slot;
    return enqueue_outbound(std::move(packet));
}

bool EnetTransport::broadcast_reliable_except(
    const std::span<const std::byte> data,
    const std::uint8_t channel,
    const std::optional<std::uint8_t> except_client_slot)
{
    return enqueue_broadcast_except(data, channel, true, except_client_slot);
}

bool EnetTransport::broadcast_unreliable_except(
    const std::span<const std::byte> data,
    const std::uint8_t channel,
    const std::optional<std::uint8_t> except_client_slot)
{
    return enqueue_broadcast_except(data, channel, false, except_client_slot);
}

bool EnetTransport::send_unreliable(const std::span<const std::byte> data, const std::uint8_t channel)
{
    OutboundPacket packet{};
    packet.data.assign(data.begin(), data.end());
    packet.channel = channel;
    packet.reliable = false;
    return enqueue_outbound(std::move(packet));
}

std::vector<ReceivedPacket> EnetTransport::drain_received()
{
    std::lock_guard lock(mutex_);

    std::vector<ReceivedPacket> received = std::move(inbound_queue_);
    inbound_queue_.clear();
    return received;
}

void EnetTransport::discard_pending_received()
{
    std::lock_guard lock(mutex_);
    inbound_queue_.clear();
}

bool EnetTransport::has_peer() const
{
    std::lock_guard lock(mutex_);

    if (is_server_) {
        for (std::uint8_t client_slot = 1U; client_slot <= max_clients_; ++client_slot) {
            if (client_peers_[client_slot] != nullptr) {
                return true;
            }
        }

        return false;
    }

    return client_peer_ != nullptr;
}

bool EnetTransport::is_connected() const
{
    std::lock_guard lock(mutex_);

    if (is_server_) {
        for (std::uint8_t client_slot = 1U; client_slot <= max_clients_; ++client_slot) {
            _ENetPeer* peer = client_peers_[client_slot];
            if (peer != nullptr && peer->state == ENET_PEER_STATE_CONNECTED) {
                return true;
            }
        }

        return false;
    }

    if (client_peer_ == nullptr) {
        return false;
    }

    return client_peer_->state == ENET_PEER_STATE_CONNECTED;
}

bool EnetTransport::consume_peer_lost()
{
    std::uint8_t lost_slot = 0U;
    return consume_peer_lost_slot(lost_slot);
}

bool EnetTransport::consume_peer_lost_slot(std::uint8_t& lost_client_slot)
{
    std::lock_guard lock(mutex_);

    if (!pending_client_lost_slots_.empty()) {
        lost_client_slot = pending_client_lost_slots_.front();
        pending_client_lost_slots_.pop_front();
        return true;
    }

    if (client_peer_lost_) {
        client_peer_lost_ = false;
        lost_client_slot = constants::LOCKSTEP_CLIENT_PLAYER_SLOT;
        return true;
    }

    return false;
}

bool EnetTransport::consume_peer_connected()
{
    std::uint8_t connected_slot = 0U;
    return consume_peer_connected_slot(connected_slot);
}

bool EnetTransport::consume_peer_connected_slot(std::uint8_t& connected_client_slot)
{
    std::lock_guard lock(mutex_);

    if (!pending_client_connected_slots_.empty()) {
        connected_client_slot = pending_client_connected_slots_.front();
        pending_client_connected_slots_.pop_front();
        return true;
    }

    return false;
}

bool EnetTransport::rebind_client_to_player_slot(
    const std::uint8_t connected_client_slot,
    const std::uint8_t player_slot)
{
    std::lock_guard lock(mutex_);

    if (!is_server_ || player_slot == 0U || player_slot > max_clients_) {
        return false;
    }

    if (connected_client_slot == 0U || connected_client_slot > max_clients_) {
        return false;
    }

    _ENetPeer* peer = client_peers_[connected_client_slot];
    if (peer == nullptr) {
        return false;
    }

    if (client_peers_[player_slot] != nullptr && client_peers_[player_slot] != peer) {
        return false;
    }

    if (connected_client_slot != player_slot) {
        client_peers_[connected_client_slot] = nullptr;
        client_peers_[player_slot] = peer;
    }

    set_peer_slot(peer, player_slot);
    return true;
}

std::uint8_t EnetTransport::connected_client_count() const
{
    std::lock_guard lock(mutex_);

    if (!is_server_) {
        return client_peer_ != nullptr ? 1U : 0U;
    }

    std::uint8_t count = 0U;
    for (std::uint8_t client_slot = 1U; client_slot <= max_clients_; ++client_slot) {
        if (client_peers_[client_slot] != nullptr) {
            ++count;
        }
    }

    return count;
}

std::uint32_t EnetTransport::peer_round_trip_time_ms() const
{
    return peer_round_trip_time_ms(constants::LOCKSTEP_CLIENT_PLAYER_SLOT);
}

std::uint32_t EnetTransport::peer_round_trip_time_ms(const std::uint8_t client_slot) const
{
    std::lock_guard lock(mutex_);

    _ENetPeer* peer = nullptr;
    if (is_server_) {
        peer = client_peer_locked(client_slot);
    }
    else {
        peer = client_peer_;
    }

    if (peer == nullptr) {
        return 0U;
    }

    if (peer->lastRoundTripTime > 0U) {
        return peer->lastRoundTripTime;
    }

    return peer->roundTripTime;
}

void EnetTransport::set_inbound_latency_handler(const InboundLatencyHandler handler)
{
    std::lock_guard lock(mutex_);
    inbound_latency_handler_ = std::move(handler);
}

} // namespace aoa::net
