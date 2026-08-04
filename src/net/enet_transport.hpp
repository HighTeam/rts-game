#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

struct _ENetHost;
struct _ENetPeer;

namespace aoa::net {

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
    void poll(std::uint32_t timeout_ms);

    [[nodiscard]] bool send_reliable(std::span<const std::byte> data, std::uint8_t channel);
    [[nodiscard]] std::vector<std::vector<std::byte>> drain_received();

    [[nodiscard]] bool has_peer() const;
    [[nodiscard]] bool is_connected() const;

private:
    _ENetHost* host_{nullptr};
    _ENetPeer* peer_{nullptr};
    bool is_server_{false};
    std::vector<std::vector<std::byte>> inbox_{};
};

} // namespace aoa::net
