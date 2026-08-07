#pragma once

#include "net/net_constants.hpp"

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

class EnetTransport {
public:
    EnetTransport();
    ~EnetTransport();

    EnetTransport(const EnetTransport&) = delete;
    EnetTransport& operator=(const EnetTransport&) = delete;

    [[nodiscard]] static bool global_initialize();
    static void global_deinitialize();

    [[nodiscard]] bool start_host(std::uint16_t port);
    [[nodiscard]] bool connect(const char* host_name, std::uint16_t port);

    void disconnect();
    void disconnect_peer();
    void poll(std::uint32_t timeout_ms);

    [[nodiscard]] bool send_reliable(std::span<const std::byte> data, std::uint8_t channel);
    [[nodiscard]] bool send_unreliable(std::span<const std::byte> data, std::uint8_t channel);
    [[nodiscard]] std::vector<std::vector<std::byte>> drain_received();
    void discard_pending_received();

    [[nodiscard]] bool has_peer() const;
    [[nodiscard]] bool is_connected() const;
    [[nodiscard]] bool consume_peer_lost();
    [[nodiscard]] bool consume_peer_connected();
    [[nodiscard]] std::uint32_t peer_round_trip_time_ms() const;

    using InboundLatencyHandler =
        std::function<InboundLatencyHandleResult(std::span<const std::byte> packet)>;
    void set_inbound_latency_handler(InboundLatencyHandler handler);

private:
    struct OutboundPacket {
        std::vector<std::byte> data;
        std::uint8_t channel;
        bool reliable{true};
    };

    void start_network_thread();
    void stop_network_thread();
    void network_thread_loop();
    void service_events_locked(std::uint32_t timeout_ms);
    void flush_outbound_locked();
    void apply_peer_timeouts_locked();
    void destroy_host_locked();
    [[nodiscard]] bool send_unreliable_immediate_locked(
        std::span<const std::byte> data,
        std::uint8_t channel);
    [[nodiscard]] bool enqueue_outbound(
        std::span<const std::byte> data,
        std::uint8_t channel,
        bool reliable);

    mutable std::mutex mutex_{};
    std::condition_variable outbound_cv_{};
    _ENetHost* host_{nullptr};
    _ENetPeer* peer_{nullptr};
    bool is_server_{false};
    bool peer_lost_{false};
    bool peer_connected_{false};
    std::deque<OutboundPacket> outbound_queue_{};
    std::vector<std::vector<std::byte>> inbound_queue_{};
    InboundLatencyHandler inbound_latency_handler_{};

    std::atomic<bool> network_thread_running_{false};
    std::thread network_thread_{};
};

} // namespace aoa::net
