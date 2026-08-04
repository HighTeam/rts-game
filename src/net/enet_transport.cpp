#include "net/enet_transport.hpp"

#include "net/net_constants.hpp"

#include <enet/enet.h>

#include <cstring>

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

bool EnetTransport::start_host(const std::uint16_t port)
{
    disconnect();

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
    return true;
}

bool EnetTransport::connect(const char* host_name, const std::uint16_t port)
{
    disconnect();

    host_ = enet_host_create(nullptr, 1, constants::CHANNEL_COUNT, 0, 0);
    if (host_ == nullptr) {
        return false;
    }

    ENetAddress address{};
    if (enet_address_set_host(&address, host_name) != 0) {
        disconnect();
        return false;
    }

    address.port = port;
    peer_ = enet_host_connect(host_, &address, constants::CHANNEL_COUNT, 0);
    if (peer_ == nullptr) {
        disconnect();
        return false;
    }

    is_server_ = false;
    return true;
}

void EnetTransport::disconnect()
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
    inbox_.clear();
}

void EnetTransport::poll(const std::uint32_t timeout_ms)
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
                peer_ = event.peer;
            }
            break;
        case ENET_EVENT_TYPE_RECEIVE: {
            std::vector<std::byte> packet_bytes(event.packet->dataLength);
            if (!packet_bytes.empty()) {
                std::memcpy(
                    packet_bytes.data(),
                    event.packet->data,
                    event.packet->dataLength);
            }

            inbox_.push_back(std::move(packet_bytes));
            enet_packet_destroy(event.packet);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
            peer_ = nullptr;
            break;
        default:
            break;
        }

        remaining_timeout = 0U;
    }
}

bool EnetTransport::send_reliable(const std::span<const std::byte> data, const std::uint8_t channel)
{
    if (peer_ == nullptr) {
        return false;
    }

    ENetPacket* packet = enet_packet_create(
        data.data(),
        data.size(),
        ENET_PACKET_FLAG_RELIABLE);
    if (packet == nullptr) {
        return false;
    }

    if (enet_peer_send(peer_, channel, packet) != 0) {
        enet_packet_destroy(packet);
        return false;
    }

    if (host_ != nullptr) {
        enet_host_flush(host_);
    }

    return true;
}

std::vector<std::vector<std::byte>> EnetTransport::drain_received()
{
    std::vector<std::vector<std::byte>> received = std::move(inbox_);
    inbox_.clear();
    return received;
}

bool EnetTransport::has_peer() const
{
    return peer_ != nullptr;
}

bool EnetTransport::is_connected() const
{
    if (peer_ == nullptr) {
        return false;
    }

    return peer_->state == ENET_PEER_STATE_CONNECTED;
}

} // namespace aoa::net
