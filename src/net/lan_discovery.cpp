#include "net/lan_discovery.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace aoa::net {

namespace {

constexpr unsigned long long k_invalid_socket = static_cast<unsigned long long>(~0ULL);

#pragma pack(push, 1)
struct DiscoveryWire {
    std::uint32_t magic{0U};
    std::uint8_t version{0U};
    std::uint8_t kind{0U};
    std::uint16_t game_port{0U};
    std::uint8_t occupied{0U};
    std::uint8_t max_players{0U};
    std::uint8_t source{0U};
    std::uint8_t lobby_open{constants::LAN_DISCOVERY_LOBBY_OPEN};
    char name[constants::LAN_DISCOVERY_NAME_CHARS]{};
};
#pragma pack(pop)

[[nodiscard]] SOCKET as_socket(const unsigned long long value)
{
    return static_cast<SOCKET>(value);
}

[[nodiscard]] DiscoveryWire make_wire(
    const std::uint8_t kind,
    const DiscoveryAnnounce& announce)
{
    DiscoveryWire wire{};
    wire.magic = constants::LAN_DISCOVERY_MAGIC;
    wire.version = constants::LAN_DISCOVERY_VERSION;
    wire.kind = kind;
    wire.game_port = announce.game_port;
    wire.occupied = announce.occupied;
    wire.max_players = announce.max_players;
    wire.source = constants::LAN_DISCOVERY_SOURCE_LAN;
    wire.lobby_open = announce.lobby_open
        ? constants::LAN_DISCOVERY_LOBBY_OPEN
        : constants::LAN_DISCOVERY_LOBBY_CLOSED;
    const std::size_t copy_count = std::min(announce.host_name.size(), sizeof(wire.name) - 1U);
    if (copy_count > 0U) {
        std::memcpy(wire.name, announce.host_name.data(), copy_count);
    }
    return wire;
}

[[nodiscard]] bool decode_wire(const DiscoveryWire& wire, DiscoveredGame& out)
{
    if (wire.magic != constants::LAN_DISCOVERY_MAGIC
        || wire.version != constants::LAN_DISCOVERY_VERSION
        || wire.kind != constants::LAN_DISCOVERY_KIND_ANNOUNCE) {
        return false;
    }

    if (wire.lobby_open != constants::LAN_DISCOVERY_LOBBY_OPEN) {
        return false;
    }

    out.port = wire.game_port;
    out.occupied = wire.occupied;
    out.max_players = wire.max_players;
    out.source = wire.source == constants::LAN_DISCOVERY_SOURCE_PUBLIC
        ? DiscoverySource::Public
        : DiscoverySource::Lan;
    out.host_name = std::string(wire.name, strnlen(wire.name, sizeof(wire.name)));
    out.last_seen = std::chrono::steady_clock::now();
    return true;
}

void collect_broadcast_targets(std::vector<sockaddr_in>& targets)
{
    sockaddr_in global{};
    global.sin_family = AF_INET;
    global.sin_port = htons(constants::LAN_DISCOVERY_PORT);
    global.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    targets.push_back(global);

    sockaddr_in loopback{};
    loopback.sin_family = AF_INET;
    loopback.sin_port = htons(constants::LAN_DISCOVERY_PORT);
    loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    targets.push_back(loopback);

    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 0U;
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW
        || size == 0U) {
        return;
    }

    std::vector<std::uint8_t> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &size) != NO_ERROR) {
        return;
    }

    for (IP_ADAPTER_ADDRESSES* adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp) {
            continue;
        }

        for (IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast != nullptr;
             unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr == nullptr
                || unicast->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }

            const auto* local =
                reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
            UINT prefix = unicast->OnLinkPrefixLength;
            if (prefix == 0U || prefix > 32U) {
                prefix = 24U;
            }

            const std::uint32_t mask = prefix == 0U
                ? 0U
                : static_cast<std::uint32_t>(~0U) << (32U - prefix);
            const std::uint32_t host = ntohl(local->sin_addr.s_addr);
            const std::uint32_t broadcast = host | ~mask;

            sockaddr_in target{};
            target.sin_family = AF_INET;
            target.sin_port = htons(constants::LAN_DISCOVERY_PORT);
            target.sin_addr.s_addr = htonl(broadcast);
            targets.push_back(target);
        }
    }
}

void send_to_targets(const SOCKET socket, const DiscoveryWire& wire)
{
    std::vector<sockaddr_in> targets{};
    collect_broadcast_targets(targets);
    for (const sockaddr_in& target : targets) {
        (void)sendto(
            socket,
            reinterpret_cast<const char*>(&wire),
            static_cast<int>(sizeof(wire)),
            0,
            reinterpret_cast<const sockaddr*>(&target),
            static_cast<int>(sizeof(target)));
    }
}

} // namespace

LanDiscovery::~LanDiscovery()
{
    stop();
}

void LanDiscovery::close_socket()
{
    if (socket_ == k_invalid_socket) {
        return;
    }

    closesocket(as_socket(socket_));
    socket_ = k_invalid_socket;
}

bool LanDiscovery::start_host_announcer(const DiscoveryAnnounce& announce)
{
    stop();
    announce_ = announce;
    announcing_ = true;

    const SOCKET created = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (created == INVALID_SOCKET) {
        return false;
    }

    BOOL reuse = TRUE;
    (void)setsockopt(
        created,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse),
        sizeof(reuse));
    BOOL broadcast = TRUE;
    (void)setsockopt(
        created,
        SOL_SOCKET,
        SO_BROADCAST,
        reinterpret_cast<const char*>(&broadcast),
        sizeof(broadcast));

    u_long non_blocking = 1U;
    (void)ioctlsocket(created, FIONBIO, &non_blocking);

    sockaddr_in bind_address{};
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons(constants::LAN_DISCOVERY_PORT);
    bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(created, reinterpret_cast<sockaddr*>(&bind_address), sizeof(bind_address)) != 0) {
        closesocket(created);
        announcing_ = false;
        return false;
    }

    socket_ = static_cast<unsigned long long>(created);
    last_announce_ = {};
    send_announce();
    return true;
}

bool LanDiscovery::start_browser()
{
    stop();
    announcing_ = false;

    const SOCKET created = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (created == INVALID_SOCKET) {
        return false;
    }

    BOOL reuse = TRUE;
    (void)setsockopt(
        created,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse),
        sizeof(reuse));
    BOOL broadcast = TRUE;
    (void)setsockopt(
        created,
        SOL_SOCKET,
        SO_BROADCAST,
        reinterpret_cast<const char*>(&broadcast),
        sizeof(broadcast));

    u_long non_blocking = 1U;
    (void)ioctlsocket(created, FIONBIO, &non_blocking);

    sockaddr_in bind_address{};
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = 0;
    bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(created, reinterpret_cast<sockaddr*>(&bind_address), sizeof(bind_address)) != 0) {
        closesocket(created);
        return false;
    }

    socket_ = static_cast<unsigned long long>(created);
    refresh();
    return true;
}

void LanDiscovery::set_announce(const DiscoveryAnnounce& announce)
{
    announce_ = announce;
}

void LanDiscovery::refresh()
{
    games_.clear();
    send_query();
}

void LanDiscovery::poll()
{
    if (socket_ == k_invalid_socket) {
        return;
    }

    receive_packets();
    prune_stale_games();

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - last_announce_);
    if (announcing_) {
        if (last_announce_.time_since_epoch().count() == 0
            || elapsed.count()
                >= static_cast<long long>(constants::LAN_DISCOVERY_ANNOUNCE_INTERVAL_MS)) {
            send_announce();
        }
        return;
    }

    if (last_announce_.time_since_epoch().count() == 0
        || elapsed.count()
            >= static_cast<long long>(constants::LAN_DISCOVERY_ANNOUNCE_INTERVAL_MS)) {
        send_query();
        last_announce_ = std::chrono::steady_clock::now();
    }
}

void LanDiscovery::send_goodbye()
{
    if (socket_ == k_invalid_socket || !announcing_) {
        return;
    }

    DiscoveryAnnounce closed = announce_;
    closed.lobby_open = false;
    send_to_targets(
        as_socket(socket_),
        make_wire(constants::LAN_DISCOVERY_KIND_ANNOUNCE, closed));
}

void LanDiscovery::stop()
{
    send_goodbye();
    close_socket();
    announcing_ = false;
    games_.clear();
}

void LanDiscovery::send_query()
{
    if (socket_ == k_invalid_socket) {
        return;
    }

    DiscoveryAnnounce empty{};
    send_to_targets(as_socket(socket_), make_wire(constants::LAN_DISCOVERY_KIND_QUERY, empty));
}

void LanDiscovery::send_announce()
{
    if (socket_ == k_invalid_socket) {
        return;
    }

    send_to_targets(
        as_socket(socket_),
        make_wire(constants::LAN_DISCOVERY_KIND_ANNOUNCE, announce_));
    last_announce_ = std::chrono::steady_clock::now();
}

void LanDiscovery::receive_packets()
{
    if (socket_ == k_invalid_socket) {
        return;
    }

    DiscoveryWire wire{};
    sockaddr_in from{};
    int from_size = sizeof(from);
    while (true) {
        const int received = recvfrom(
            as_socket(socket_),
            reinterpret_cast<char*>(&wire),
            static_cast<int>(sizeof(wire)),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &from_size);
        if (received < static_cast<int>(sizeof(DiscoveryWire))) {
            break;
        }

        if (wire.magic != constants::LAN_DISCOVERY_MAGIC
            || wire.version != constants::LAN_DISCOVERY_VERSION) {
            continue;
        }

        char address[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &from.sin_addr, address, sizeof(address)) == nullptr) {
            continue;
        }

        if (wire.kind == constants::LAN_DISCOVERY_KIND_QUERY && announcing_) {
            sockaddr_in reply = from;
            const DiscoveryWire announce_wire =
                make_wire(constants::LAN_DISCOVERY_KIND_ANNOUNCE, announce_);
            (void)sendto(
                as_socket(socket_),
                reinterpret_cast<const char*>(&announce_wire),
                static_cast<int>(sizeof(announce_wire)),
                0,
                reinterpret_cast<const sockaddr*>(&reply),
                sizeof(reply));
            continue;
        }

        if (wire.kind == constants::LAN_DISCOVERY_KIND_ANNOUNCE
            && wire.lobby_open != constants::LAN_DISCOVERY_LOBBY_OPEN) {
            forget_game(address, wire.game_port);
            continue;
        }

        DiscoveredGame game{};
        if (!decode_wire(wire, game)) {
            continue;
        }

        game.address = address;
        remember_game(game);
    }
}

void LanDiscovery::forget_game(const std::string& address, const std::uint16_t port)
{
    games_.erase(
        std::remove_if(
            games_.begin(),
            games_.end(),
            [&address, port](const DiscoveredGame& game) {
                return game.address == address && game.port == port;
            }),
        games_.end());
}

void LanDiscovery::remember_game(const DiscoveredGame& game)
{
    for (DiscoveredGame& existing : games_) {
        if (existing.address == game.address && existing.port == game.port) {
            existing = game;
            return;
        }

        if (existing.port == game.port && existing.host_name == game.host_name) {
            if (game.address == "127.0.0.1" || existing.address != "127.0.0.1") {
                existing = game;
            }
            return;
        }
    }

    games_.push_back(game);
    std::stable_sort(
        games_.begin(),
        games_.end(),
        [](const DiscoveredGame& left, const DiscoveredGame& right) {
            if (left.source != right.source) {
                return left.source == DiscoverySource::Lan;
            }

            return left.host_name < right.host_name;
        });
}

void LanDiscovery::prune_stale_games()
{
    const auto now = std::chrono::steady_clock::now();
    games_.erase(
        std::remove_if(
            games_.begin(),
            games_.end(),
            [&now](const DiscoveredGame& game) {
                return std::chrono::duration_cast<std::chrono::milliseconds>(now - game.last_seen)
                    .count()
                    >= static_cast<long long>(constants::LAN_DISCOVERY_STALE_MS);
            }),
        games_.end());
}

} // namespace aoa::net
