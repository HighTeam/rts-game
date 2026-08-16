#pragma once

#include "net/net_constants.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace aoa::net {

enum class DiscoverySource : std::uint8_t {
    Lan = constants::LAN_DISCOVERY_SOURCE_LAN,
    Public = constants::LAN_DISCOVERY_SOURCE_PUBLIC,
};

struct DiscoveredGame {
    std::string address{};
    std::uint16_t port{constants::DEFAULT_PORT};
    std::string host_name{};
    std::uint8_t occupied{0U};
    std::uint8_t max_players{0U};
    DiscoverySource source{DiscoverySource::Lan};
    std::chrono::steady_clock::time_point last_seen{};
};

struct DiscoveryAnnounce {
    std::uint16_t game_port{constants::DEFAULT_PORT};
    std::uint8_t occupied{1U};
    std::uint8_t max_players{2U};
    bool lobby_open{true};
    std::string host_name{};
};

class LanDiscovery {
public:
    LanDiscovery() = default;
    ~LanDiscovery();

    LanDiscovery(const LanDiscovery&) = delete;
    LanDiscovery& operator=(const LanDiscovery&) = delete;

    [[nodiscard]] bool start_host_announcer(const DiscoveryAnnounce& announce);
    [[nodiscard]] bool start_browser();
    void set_announce(const DiscoveryAnnounce& announce);
    void refresh();
    void poll();
    void stop();

    [[nodiscard]] const std::vector<DiscoveredGame>& games() const { return games_; }

private:
    void close_socket();
    void send_query();
    void send_announce();
    void receive_packets();
    void remember_game(const DiscoveredGame& game);
    void forget_game(const std::string& address, std::uint16_t port);
    void prune_stale_games();
    void send_goodbye();

    unsigned long long socket_{static_cast<unsigned long long>(~0ULL)};
    bool announcing_{false};
    DiscoveryAnnounce announce_{};
    std::vector<DiscoveredGame> games_{};
    std::chrono::steady_clock::time_point last_announce_{};
};

} // namespace aoa::net
