#pragma once

#include "net/net_constants.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

struct _ENetHost;
struct _ENetPeer;

namespace aoa::net {

struct InboundLatencyHandleResult {
    bool handled{false};
    std::vector<std::byte> immediate_reply{};
};

struct ReceivedPacket {
    std::uint8_t sender_slot{0U};
    std::vector<std::byte> data{};
};

class EnetTransport {
public:
    EnetTransport();
    ~EnetTransport();

    EnetTransport(const EnetTransport&) = delete;
    EnetTransport& operator=(const EnetTransport&) = delete;

    [[nodiscard]] static bool global_initialize();
    static void global_deinitialize();

    [[nodiscard]] bool start_host(
        std::uint16_t port,
        std::uint8_t max_clients = constants::LOCKSTEP_DEFAULT_MAX_CLIENTS);
    [[nodiscard]] bool connect(const char* host_name, std::uint16_t port);

    void disconnect();
    void disconnect_peer();
    void disconnect_peer_slot(std::uint8_t client_slot);
    void poll(std::uint32_t timeout_ms);

    [[nodiscard]] bool send_reliable(std::span<const std::byte> data, std::uint8_t channel);
    [[nodiscard]] bool send_reliable_to_client(
        std::uint8_t client_slot,
        std::span<const std::byte> data,
        std::uint8_t channel);
    [[nodiscard]] bool broadcast_reliable_except(
        std::span<const std::byte> data,
        std::uint8_t channel,
        std::optional<std::uint8_t> except_client_slot = std::nullopt);
    [[nodiscard]] bool broadcast_unreliable_except(
        std::span<const std::byte> data,
        std::uint8_t channel,
        std::optional<std::uint8_t> except_client_slot = std::nullopt);
    [[nodiscard]] bool send_unreliable(std::span<const std::byte> data, std::uint8_t channel);
    [[nodiscard]] std::vector<ReceivedPacket> drain_received();
    void discard_pending_received();

    [[nodiscard]] bool has_peer() const;
    [[nodiscard]] bool is_connected() const;
    [[nodiscard]] bool consume_peer_lost();
    [[nodiscard]] bool consume_peer_lost_slot(std::uint8_t& lost_client_slot);
    [[nodiscard]] bool consume_peer_connected();
    [[nodiscard]] bool consume_peer_connected_slot(std::uint8_t& connected_client_slot);
    [[nodiscard]] std::uint8_t connected_client_count() const;
    [[nodiscard]] std::uint32_t peer_round_trip_time_ms() const;
    [[nodiscard]] std::uint32_t peer_round_trip_time_ms(std::uint8_t client_slot) const;

    using InboundLatencyHandler =
        std::function<InboundLatencyHandleResult(std::span<const std::byte> packet)>;
    void set_inbound_latency_handler(InboundLatencyHandler handler);

private:
    struct OutboundPacket {
        std::vector<std::byte> data;
        std::uint8_t channel;
        bool reliable{true};
        bool broadcast{false};
        std::optional<std::uint8_t> except_client_slot{};
        std::optional<std::uint8_t> target_client_slot{};
    };

    void start_network_thread();
    void stop_network_thread();
    void network_thread_loop();
    void service_events_locked(std::uint32_t timeout_ms);
    void flush_outbound_locked();
    void apply_peer_timeouts_locked(_ENetPeer* peer);
    void destroy_host_locked();
    [[nodiscard]] _ENetPeer* client_peer_locked(std::uint8_t client_slot) const;
    [[nodiscard]] std::uint8_t peer_client_slot_locked(_ENetPeer* peer) const;
    [[nodiscard]] bool send_unreliable_immediate_locked(
        _ENetPeer* peer,
        std::span<const std::byte> data,
        std::uint8_t channel);
    [[nodiscard]] bool enqueue_outbound(OutboundPacket packet);
    [[nodiscard]] bool enqueue_broadcast_except(
        std::span<const std::byte> data,
        std::uint8_t channel,
        bool reliable,
        std::optional<std::uint8_t> except_client_slot);

    mutable std::mutex mutex_{};
    std::condition_variable outbound_cv_{};
    _ENetHost* host_{nullptr};
    _ENetPeer* client_peer_{nullptr};
    std::array<_ENetPeer*, constants::LOCKSTEP_MAX_PLAYER_SLOTS> client_peers_{};
    std::uint8_t max_clients_{constants::LOCKSTEP_DEFAULT_MAX_CLIENTS};
    bool is_server_{false};
    bool client_peer_lost_{false};
    std::deque<std::uint8_t> pending_client_connected_slots_{};
    std::deque<std::uint8_t> pending_client_lost_slots_{};
    std::deque<OutboundPacket> outbound_queue_{};
    std::vector<ReceivedPacket> inbound_queue_{};
    InboundLatencyHandler inbound_latency_handler_{};

    std::atomic<bool> network_thread_running_{false};
    std::thread network_thread_{};
};

} // namespace aoa::net
