#pragma once

#include "net/enet_transport.hpp"
#include "net/lobby_wire.hpp"
#include "net/net_message.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace aoa::net {

enum class LobbyRole : std::uint8_t {
    Host = 0,
    Client = 1,
};

enum class LobbyStatus : std::uint8_t {
    Idle = 0,
    Connecting,
    Joined,
    ConnectFailed,
    Closed,
};

// Pre-match lobby over its own ENet transport. The lockstep match transport is
// created only after the host broadcasts LobbyMatchStart.
class LobbySession {
public:
    explicit LobbySession(LobbyRole role);
    ~LobbySession();

    LobbySession(const LobbySession&) = delete;
    LobbySession& operator=(const LobbySession&) = delete;

    [[nodiscard]] bool start_host(
        std::uint16_t port,
        const LobbySettings& settings,
        const std::string& host_name);
    [[nodiscard]] bool connect(const std::string& address, std::uint16_t port, const std::string& name);

    void poll();
    // Announces the departure to the other side, then tears the transport down.
    void leave();
    // Tears the transport down silently, e.g. when handing the port to the match host.
    void shutdown();

    void set_local_ready(bool ready);
    void toggle_local_ready();
    [[nodiscard]] bool local_ready() const;

    [[nodiscard]] bool can_start_match() const;
    void request_match_start();

    [[nodiscard]] std::optional<LobbySettings> consume_match_start();

    [[nodiscard]] LobbyRole role() const { return role_; }
    [[nodiscard]] bool is_host() const { return role_ == LobbyRole::Host; }
    [[nodiscard]] LobbyStatus status() const { return status_; }
    [[nodiscard]] std::uint8_t local_slot() const { return local_slot_; }
    [[nodiscard]] const LobbyStateMessage& view() const { return view_; }
    [[nodiscard]] std::uint8_t occupied_slot_count() const;

private:
    void handle_packet(const ReceivedPacket& packet);
    void handle_host_packet(NetMessageKind kind, const std::vector<std::byte>& payload, std::uint8_t sender_slot);
    void handle_client_packet(NetMessageKind kind, const std::vector<std::byte>& payload);
    void refresh_host_pings();
    void send_lobby_state_to_clients();
    void release_slot(std::uint8_t player_slot);
    void send_to_host(NetMessageKind kind, const std::vector<std::byte>& payload);

    LobbyRole role_{LobbyRole::Host};
    LobbyStatus status_{LobbyStatus::Idle};
    EnetTransport transport_{};
    LobbyStateMessage view_{};
    std::string local_name_{};
    std::uint8_t local_slot_{0U};
    bool join_sent_{false};
    bool match_start_pending_{false};
    LobbySettings match_start_settings_{};
    std::chrono::steady_clock::time_point connect_started_{};
    std::chrono::steady_clock::time_point last_state_broadcast_{};
};

} // namespace aoa::net
