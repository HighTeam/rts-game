#include "net/lobby_session.hpp"

#include "core/constants.hpp"

#include <algorithm>

namespace aoa::net {

namespace {

[[nodiscard]] std::chrono::milliseconds elapsed_since(
    const std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
}

} // namespace

LobbySession::LobbySession(const LobbyRole role)
    : role_(role)
{
}

LobbySession::~LobbySession()
{
    transport_.disconnect();
}

bool LobbySession::start_host(
    const std::uint16_t port,
    const LobbySettings& settings,
    const std::string& host_name)
{
    const std::uint8_t max_clients = static_cast<std::uint8_t>(settings.player_count - 1U);
    if (!transport_.start_host(port, max_clients)) {
        status_ = LobbyStatus::ConnectFailed;
        return false;
    }

    local_name_ = host_name;
    local_slot_ = constants::LOCKSTEP_HOST_PLAYER_SLOT;
    view_ = LobbyStateMessage{};
    view_.host_slot = constants::LOCKSTEP_HOST_PLAYER_SLOT;
    view_.recipient_slot = local_slot_;
    view_.settings = settings;
    view_.slots[local_slot_].occupied = true;
    view_.slots[local_slot_].name = host_name;
    status_ = LobbyStatus::Joined;
    return true;
}

bool LobbySession::connect(
    const std::string& address,
    const std::uint16_t port,
    const std::string& name)
{
    if (!transport_.connect(address.c_str(), port)) {
        status_ = LobbyStatus::ConnectFailed;
        return false;
    }

    local_name_ = name;
    view_ = LobbyStateMessage{};
    join_sent_ = false;
    status_ = LobbyStatus::Connecting;
    connect_started_ = std::chrono::steady_clock::now();
    return true;
}

std::uint8_t LobbySession::occupied_slot_count() const
{
    std::uint8_t count = 0U;
    for (const LobbySlotInfo& slot : view_.slots) {
        if (slot.occupied) {
            ++count;
        }
    }

    return count;
}

bool LobbySession::local_ready() const
{
    return view_.slots[local_slot_].ready;
}

void LobbySession::set_local_ready(const bool ready)
{
    view_.slots[local_slot_].ready = ready;

    if (is_host()) {
        send_lobby_state_to_clients();
        return;
    }

    LobbyReadyMessage message{};
    message.player_slot = local_slot_;
    message.ready = ready;
    send_to_host(NetMessageKind::LobbyReady, encode_lobby_ready(message));
}

void LobbySession::toggle_local_ready()
{
    set_local_ready(!local_ready());
}

bool LobbySession::can_start_match() const
{
    if (!is_host() || status_ != LobbyStatus::Joined) {
        return false;
    }

    if (occupied_slot_count() != view_.settings.player_count) {
        return false;
    }

    for (const LobbySlotInfo& slot : view_.slots) {
        if (slot.occupied && !slot.ready) {
            return false;
        }
    }

    return true;
}

void LobbySession::request_match_start()
{
    if (!can_start_match()) {
        return;
    }

    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::LobbyMatchStart, encode_lobby_settings(view_.settings));
    (void)transport_.broadcast_reliable_except(
        wire_message,
        constants::CHANNEL_RELIABLE,
        std::nullopt);

    match_start_settings_ = view_.settings;
    match_start_pending_ = true;
}

std::optional<LobbySettings> LobbySession::consume_match_start()
{
    if (!match_start_pending_) {
        return std::nullopt;
    }

    match_start_pending_ = false;
    return match_start_settings_;
}

void LobbySession::send_to_host(const NetMessageKind kind, const std::vector<std::byte>& payload)
{
    const std::vector<std::byte> wire_message = encode_net_message(kind, payload);
    (void)transport_.send_reliable(wire_message, constants::CHANNEL_RELIABLE);
}

void LobbySession::leave()
{
    if (status_ == LobbyStatus::Idle) {
        return;
    }

    if (is_host()) {
        const std::vector<std::byte> wire_message =
            encode_net_message(NetMessageKind::LobbyLeave, {});
        (void)transport_.broadcast_reliable_except(
            wire_message,
            constants::CHANNEL_RELIABLE,
            std::nullopt);
        transport_.disconnect_peer();
    }
    else if (status_ == LobbyStatus::Joined) {
        send_to_host(NetMessageKind::LobbyLeave, {});
        transport_.disconnect_peer();
    }

    shutdown();
}

void LobbySession::shutdown()
{
    transport_.disconnect();
    status_ = LobbyStatus::Idle;
}

void LobbySession::release_slot(const std::uint8_t player_slot)
{
    if (player_slot == 0U || player_slot >= view_.slots.size()) {
        return;
    }

    view_.slots[player_slot] = LobbySlotInfo{};
}

void LobbySession::refresh_host_pings()
{
    for (std::uint8_t slot = 1U; slot < view_.slots.size(); ++slot) {
        if (!view_.slots[slot].occupied) {
            continue;
        }

        view_.slots[slot].ping_ms =
            static_cast<std::uint16_t>(std::min<std::uint32_t>(
                transport_.peer_round_trip_time_ms(slot),
                0xFFFFU));
    }
}

void LobbySession::send_lobby_state_to_clients()
{
    if (!is_host()) {
        return;
    }

    refresh_host_pings();

    for (std::uint8_t slot = 1U; slot < view_.slots.size(); ++slot) {
        if (!view_.slots[slot].occupied) {
            continue;
        }

        LobbyStateMessage message = view_;
        message.recipient_slot = slot;
        const std::vector<std::byte> wire_message =
            encode_net_message(NetMessageKind::LobbyState, encode_lobby_state(message));
        (void)transport_.send_reliable_to_client(slot, wire_message, constants::CHANNEL_RELIABLE);
    }

    last_state_broadcast_ = std::chrono::steady_clock::now();
}

void LobbySession::handle_host_packet(
    const NetMessageKind kind,
    const std::vector<std::byte>& payload,
    const std::uint8_t sender_slot)
{
    if (sender_slot == 0U || sender_slot >= view_.slots.size()) {
        return;
    }

    if (kind == NetMessageKind::LobbyJoin) {
        const std::optional<LobbyJoinMessage> message = decode_lobby_join(payload);
        if (!message.has_value()) {
            return;
        }

        view_.slots[sender_slot].occupied = true;
        view_.slots[sender_slot].ready = false;
        view_.slots[sender_slot].name = message->name;
        send_lobby_state_to_clients();
        return;
    }

    if (kind == NetMessageKind::LobbyReady) {
        const std::optional<LobbyReadyMessage> message = decode_lobby_ready(payload);
        if (!message.has_value()) {
            return;
        }

        view_.slots[sender_slot].ready = message->ready;
        send_lobby_state_to_clients();
        return;
    }

    if (kind == NetMessageKind::LobbyLeave) {
        release_slot(sender_slot);
        transport_.disconnect_peer_slot(sender_slot);
        send_lobby_state_to_clients();
    }
}

void LobbySession::handle_client_packet(
    const NetMessageKind kind,
    const std::vector<std::byte>& payload)
{
    if (kind == NetMessageKind::LobbyState) {
        const std::optional<LobbyStateMessage> message = decode_lobby_state(payload);
        if (!message.has_value()) {
            return;
        }

        view_ = *message;
        local_slot_ = message->recipient_slot;
        status_ = LobbyStatus::Joined;
        return;
    }

    if (kind == NetMessageKind::LobbyMatchStart) {
        const std::optional<LobbySettings> settings = decode_lobby_settings(payload);
        if (!settings.has_value()) {
            return;
        }

        match_start_settings_ = *settings;
        match_start_pending_ = true;
        return;
    }

    if (kind == NetMessageKind::LobbyLeave) {
        status_ = LobbyStatus::Closed;
    }
}

void LobbySession::handle_packet(const ReceivedPacket& packet)
{
    const auto decoded = decode_net_message(packet.data);
    if (!decoded.has_value()) {
        return;
    }

    if (is_host()) {
        handle_host_packet(decoded->first, decoded->second, packet.sender_slot);
        return;
    }

    handle_client_packet(decoded->first, decoded->second);
}

void LobbySession::poll()
{
    if (status_ == LobbyStatus::Idle || status_ == LobbyStatus::ConnectFailed
        || status_ == LobbyStatus::Closed) {
        return;
    }

    if (is_host()) {
        std::uint8_t connected_slot = 0U;
        while (transport_.consume_peer_connected_slot(connected_slot)) {
            // The join name arrives with LobbyJoin; the slot is reserved on connect.
            view_.slots[connected_slot].occupied = true;
        }

        std::uint8_t lost_slot = 0U;
        bool lost_any = false;
        while (transport_.consume_peer_lost_slot(lost_slot)) {
            release_slot(lost_slot);
            lost_any = true;
        }

        for (const ReceivedPacket& packet : transport_.drain_received()) {
            handle_packet(packet);
        }

        if (lost_any
            || elapsed_since(last_state_broadcast_).count()
                >= static_cast<long long>(constants::LOBBY_STATE_BROADCAST_INTERVAL_MS)) {
            send_lobby_state_to_clients();
        }

        return;
    }

    if (status_ == LobbyStatus::Connecting) {
        if (transport_.is_connected() && !join_sent_) {
            LobbyJoinMessage message{};
            message.name = local_name_;
            send_to_host(NetMessageKind::LobbyJoin, encode_lobby_join(message));
            join_sent_ = true;
        }
        else if (!join_sent_
            && elapsed_since(connect_started_).count()
                >= aoa::constants::MULTIPLAYER_LOBBY_CONNECT_TIMEOUT_MS) {
            status_ = LobbyStatus::ConnectFailed;
            transport_.disconnect();
            return;
        }
    }

    if (transport_.consume_peer_lost()) {
        status_ = LobbyStatus::Closed;
        transport_.disconnect();
        return;
    }

    for (const ReceivedPacket& packet : transport_.drain_received()) {
        handle_packet(packet);
    }
}

} // namespace aoa::net
