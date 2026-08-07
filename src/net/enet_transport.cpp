#include "net/enet_transport.hpp"

#include <enet/enet.h>

#include <chrono>
#include <cstring>
#include <thread>

namespace aoa::net {

namespace {

int global_init_count = 0;

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

void EnetTransport::apply_peer_timeouts_locked()
{
    if (peer_ == nullptr) {
        return;
    }

    peer_->timeoutMinimum = constants::ENET_PEER_TIMEOUT_MINIMUM_MS;
    peer_->timeoutMaximum = constants::ENET_PEER_TIMEOUT_MAXIMUM_MS;
    peer_->timeoutLimit = constants::ENET_PEER_TIMEOUT_LIMIT;
}

void EnetTransport::flush_outbound_locked()
{
    if (host_ == nullptr || peer_ == nullptr) {
        outbound_queue_.clear();
        return;
    }

    while (!outbound_queue_.empty()) {
        OutboundPacket outbound_packet = std::move(outbound_queue_.front());
        outbound_queue_.pop_front();

        ENetPacket* enet_packet = enet_packet_create(
            outbound_packet.data.data(),
            outbound_packet.data.size(),
            outbound_packet.reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED);
        if (enet_packet == nullptr) {
            continue;
        }

        if (enet_peer_send(peer_, outbound_packet.channel, enet_packet) != 0) {
            enet_packet_destroy(enet_packet);
        }
    }

    enet_host_flush(host_);
}

bool EnetTransport::send_unreliable_immediate_locked(
    const std::span<const std::byte> data,
    const std::uint8_t channel)
{
    if (host_ == nullptr || peer_ == nullptr || data.empty()) {
        return false;
    }

    ENetPacket* enet_packet = enet_packet_create(
        data.data(),
        data.size(),
        ENET_PACKET_FLAG_UNSEQUENCED);
    if (enet_packet == nullptr) {
        return false;
    }

    if (enet_peer_send(peer_, channel, enet_packet) != 0) {
        enet_packet_destroy(enet_packet);
        return false;
    }

    enet_host_flush(host_);
    return true;
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
                if (peer_ != nullptr && peer_ != event.peer) {
                    enet_peer_disconnect(peer_, 0);
                }

                peer_ = event.peer;
            }

            apply_peer_timeouts_locked();
            peer_lost_ = false;
            peer_connected_ = true;
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
                            latency_result.immediate_reply,
                            constants::CHANNEL_UNRELIABLE);
                    }

                    enet_packet_destroy(event.packet);
                    break;
                }
            }

            inbound_queue_.push_back(std::move(packet_bytes));
            enet_packet_destroy(event.packet);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
            if (event.peer == peer_) {
                peer_lost_ = true;
                peer_ = nullptr;
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
    if (host_ != nullptr && peer_ != nullptr) {
        enet_peer_disconnect(peer_, 0);
        ENetEvent event{};
        while (enet_host_service(host_, &event, 1000) > 0) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                enet_packet_destroy(event.packet);
            }
        }
    }

    if (host_ != nullptr) {
        enet_host_destroy(host_);
    }

    host_ = nullptr;
    peer_ = nullptr;
    is_server_ = false;
    peer_lost_ = false;
    peer_connected_ = false;
    outbound_queue_.clear();
    inbound_queue_.clear();
}

bool EnetTransport::start_host(const std::uint16_t port)
{
    disconnect();

    {
        std::lock_guard lock(mutex_);

        ENetAddress address{};
        address.host = ENET_HOST_ANY;
        address.port = port;

        host_ = enet_host_create(
            &address,
            constants::MAX_PEERS,
            constants::CHANNEL_COUNT,
            0,
            0);
        if (host_ == nullptr) {
            return false;
        }

        is_server_ = true;
        peer_lost_ = false;
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
        peer_ = enet_host_connect(host_, &address, constants::CHANNEL_COUNT, 0);
        if (peer_ == nullptr) {
            destroy_host_locked();
            return false;
        }

        apply_peer_timeouts_locked();
        is_server_ = false;
        peer_lost_ = false;
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

    if (host_ == nullptr || peer_ == nullptr) {
        return;
    }

    enet_peer_disconnect(peer_, 0);
    ENetEvent event{};
    while (enet_host_service(host_, &event, 1000) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(event.packet);
        }
    }

    peer_ = nullptr;
    peer_lost_ = false;
}

void EnetTransport::poll(const std::uint32_t timeout_ms)
{
    if (timeout_ms == 0U) {
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
}

bool EnetTransport::send_reliable(const std::span<const std::byte> data, const std::uint8_t channel)
{
    return enqueue_outbound(data, channel, true);
}

bool EnetTransport::send_unreliable(const std::span<const std::byte> data, const std::uint8_t channel)
{
    return enqueue_outbound(data, channel, false);
}

bool EnetTransport::enqueue_outbound(
    const std::span<const std::byte> data,
    const std::uint8_t channel,
    const bool reliable)
{
    if (!network_thread_running_.load()) {
        return false;
    }

    OutboundPacket outbound_packet{};
    outbound_packet.data.assign(data.begin(), data.end());
    outbound_packet.channel = channel;
    outbound_packet.reliable = reliable;

    {
        std::lock_guard lock(mutex_);

        if (peer_ == nullptr) {
            return false;
        }

        outbound_queue_.push_back(std::move(outbound_packet));
    }

    outbound_cv_.notify_one();
    return true;
}

std::vector<std::vector<std::byte>> EnetTransport::drain_received()
{
    std::lock_guard lock(mutex_);

    std::vector<std::vector<std::byte>> received = std::move(inbound_queue_);
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
    return peer_ != nullptr;
}

bool EnetTransport::is_connected() const
{
    std::lock_guard lock(mutex_);

    if (peer_ == nullptr) {
        return false;
    }

    return peer_->state == ENET_PEER_STATE_CONNECTED;
}

bool EnetTransport::consume_peer_lost()
{
    std::lock_guard lock(mutex_);

    if (!peer_lost_) {
        return false;
    }

    peer_lost_ = false;
    return true;
}

bool EnetTransport::consume_peer_connected()
{
    std::lock_guard lock(mutex_);

    if (!peer_connected_) {
        return false;
    }

    peer_connected_ = false;
    return true;
}

std::uint32_t EnetTransport::peer_round_trip_time_ms() const
{
    std::lock_guard lock(mutex_);

    if (peer_ == nullptr) {
        return 0U;
    }

    if (peer_->lastRoundTripTime > 0U) {
        return peer_->lastRoundTripTime;
    }

    return peer_->roundTripTime;
}

void EnetTransport::set_inbound_latency_handler(const InboundLatencyHandler handler)
{
    std::lock_guard lock(mutex_);
    inbound_latency_handler_ = std::move(handler);
}

} // namespace aoa::net
