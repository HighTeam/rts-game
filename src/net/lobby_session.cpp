#include "net/lobby_session.hpp"

#include "core/constants.hpp"
#include "sim/components/match_session.hpp"

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

void LobbySession::init_default_slots(const std::string& host_name)
{
    for (std::uint8_t slot = 0U; slot < view_.slots.size(); ++slot) {
        view_.slots[slot] = LobbySlotInfo{};
        view_.slots[slot].color = slot;
        view_.slots[slot].kind = slot == 0U
            ? LobbySlotKind::Host
            : (slot == 1U ? LobbySlotKind::Enabled : LobbySlotKind::Disabled);
    }

    view_.slots[0].occupied = true;
    view_.slots[0].name = host_name;
    view_.slots[0].ready = false;
    refresh_playing_player_count();
}

void LobbySession::refresh_playing_player_count()
{
    std::uint8_t playing = 0U;
    for (const LobbySlotInfo& slot : view_.slots) {
        if (slot.kind == LobbySlotKind::Disabled || slot.kind == LobbySlotKind::Spectator) {
            continue;
        }

        if (slot.kind == LobbySlotKind::Ai || slot.occupied) {
            ++playing;
        }
    }

    view_.settings.player_count = std::max<std::uint8_t>(playing, 1U);
}

bool LobbySession::start_host(
    const std::uint16_t port,
    const LobbySettings& settings,
    const std::string& host_name)
{
    const std::uint8_t max_clients =
        static_cast<std::uint8_t>(constants::LOCKSTEP_MAX_PLAYER_SLOTS - 1);
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
    init_default_slots(host_name);
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

std::uint8_t LobbySession::connected_human_count() const
{
    return lobby_connected_human_count(view_);
}

std::uint8_t LobbySession::playing_slot_count() const
{
    return lobby_playing_slot_count(view_);
}

std::uint8_t LobbySession::configured_slot_count() const
{
    return lobby_configured_slot_count(view_);
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

void LobbySession::set_settings(const LobbySettings& settings)
{
    if (!is_host()) {
        return;
    }

    view_.settings = settings;
    refresh_playing_player_count();
    send_lobby_state_to_clients();
}

void LobbySession::apply_color(const std::uint8_t slot, const std::uint8_t preferred)
{
    if (slot >= view_.slots.size()) {
        return;
    }

    view_.slots[slot].color = next_free_lobby_color(view_, preferred, slot);
}

void LobbySession::cycle_slot_kind(const std::uint8_t slot)
{
    if (!is_host() || slot >= view_.slots.size()) {
        return;
    }

    if (view_.settings.required_player_count > 0U && slot != 0U) {
        return;
    }

    const std::uint8_t playing = playing_slot_count();
    const LobbySlotKind current_kind = view_.slots[slot].kind;
    if (current_kind == LobbySlotKind::Disabled
        && playing >= view_.settings.pattern_max_players) {
        return;
    }

    if (current_kind == LobbySlotKind::Ai
        && playing <= view_.settings.pattern_min_players) {
        return;
    }

    LobbySlotInfo& info = view_.slots[slot];
    if (slot == 0U) {
        info.kind = info.kind == LobbySlotKind::Spectator ? LobbySlotKind::Host
                                                         : LobbySlotKind::Spectator;
        refresh_playing_player_count();
        send_lobby_state_to_clients();
        return;
    }

    const bool had_human = info.occupied && info.kind == LobbySlotKind::Enabled;
    const std::uint8_t previous_color = info.color;
    if (info.kind == LobbySlotKind::Enabled) {
        if (had_human) {
            transport_.disconnect_peer_slot(slot);
        }

        info = LobbySlotInfo{};
        info.kind = LobbySlotKind::Ai;
        info.occupied = true;
        info.ready = true;
        info.name = std::string(aoa::constants::SINGLEPLAYER_SLOT_AI_LABEL);
        info.color = next_free_lobby_color(view_, previous_color, slot);
    }
    else if (info.kind == LobbySlotKind::Ai) {
        info = LobbySlotInfo{};
        info.kind = LobbySlotKind::Disabled;
        info.color = previous_color;
    }
    else {
        info.kind = LobbySlotKind::Enabled;
        info.occupied = false;
        info.ready = false;
        info.name.clear();
        apply_color(slot, previous_color);
    }

    refresh_playing_player_count();
    send_lobby_state_to_clients();
}

void LobbySession::cycle_slot_color(const std::uint8_t slot)
{
    if (!is_host() || slot >= view_.slots.size()) {
        return;
    }

    if (view_.slots[slot].kind == LobbySlotKind::Disabled) {
        return;
    }

    if (slot != local_slot_ && view_.slots[slot].kind == LobbySlotKind::Enabled
        && view_.slots[slot].occupied) {
        return;
    }

    apply_color(slot, static_cast<std::uint8_t>((view_.slots[slot].color + 1U) % view_.slots.size()));
    send_lobby_state_to_clients();
}

void LobbySession::request_local_color_cycle()
{
    if (local_slot_ >= view_.slots.size()
        || view_.slots[local_slot_].kind == LobbySlotKind::Disabled) {
        return;
    }

    const std::uint8_t next = static_cast<std::uint8_t>(
        (view_.slots[local_slot_].color + 1U) % view_.slots.size());
    if (is_host()) {
        apply_color(local_slot_, next);
        send_lobby_state_to_clients();
        return;
    }

    LobbyColorMessage message{};
    message.player_slot = local_slot_;
    message.color = next;
    send_to_host(NetMessageKind::LobbyColor, encode_lobby_color(message));
}

void LobbySession::cycle_slot_team(const std::uint8_t slot)
{
    if (!is_host() || slot >= view_.slots.size()) {
        return;
    }

    if (view_.slots[slot].kind == LobbySlotKind::Disabled) {
        return;
    }

    if (slot != local_slot_ && view_.slots[slot].kind == LobbySlotKind::Enabled
        && view_.slots[slot].occupied) {
        return;
    }

    view_.slots[slot].team = sim::components::next_lobby_team(view_.slots[slot].team);
    send_lobby_state_to_clients();
}

void LobbySession::request_local_team_cycle()
{
    if (local_slot_ >= view_.slots.size()
        || view_.slots[local_slot_].kind == LobbySlotKind::Disabled) {
        return;
    }

    const std::uint8_t next = sim::components::next_lobby_team(view_.slots[local_slot_].team);
    if (is_host()) {
        view_.slots[local_slot_].team = next;
        send_lobby_state_to_clients();
        return;
    }

    LobbyTeamMessage message{};
    message.player_slot = local_slot_;
    message.team = next;
    send_to_host(NetMessageKind::LobbyTeam, encode_lobby_team(message));
}

bool LobbySession::try_apply_scenario(
    const std::uint8_t required_player_count,
    const std::string& name)
{
    if (!is_host() || required_player_count == 0U) {
        return false;
    }

    const std::uint8_t humans = connected_human_count();
    if (humans > required_player_count) {
        return false;
    }

    view_.settings.required_player_count = required_player_count;
    view_.settings.scenario_name = name;
    view_.settings.game_style = 1U;

    for (std::uint8_t slot = 0U; slot < view_.slots.size(); ++slot) {
        LobbySlotInfo& info = view_.slots[slot];
        if (slot == 0U) {
            if (info.kind == LobbySlotKind::Spectator) {
                info.kind = LobbySlotKind::Host;
            }
            continue;
        }

        if (slot < required_player_count) {
            if (info.occupied && info.kind == LobbySlotKind::Enabled) {
                continue;
            }

            info.kind = LobbySlotKind::Ai;
            info.occupied = true;
            info.ready = true;
            info.name = std::string(aoa::constants::SINGLEPLAYER_SLOT_AI_LABEL);
            apply_color(slot, info.color);
            continue;
        }

        if (info.occupied && info.kind == LobbySlotKind::Enabled) {
            transport_.disconnect_peer_slot(slot);
        }

        info = LobbySlotInfo{};
        info.kind = LobbySlotKind::Disabled;
        info.color = slot;
    }

    refresh_playing_player_count();
    send_lobby_state_to_clients();
    return true;
}

bool LobbySession::try_lock_player_count(const std::uint8_t required_player_count)
{
    if (!is_host() || required_player_count == 0U) {
        return false;
    }

    if (connected_human_count() > required_player_count) {
        return false;
    }

    view_.settings.required_player_count = required_player_count;
    for (std::uint8_t slot = 0U; slot < view_.slots.size(); ++slot) {
        LobbySlotInfo& info = view_.slots[slot];
        if (slot == 0U) {
            if (info.kind == LobbySlotKind::Spectator) {
                info.kind = LobbySlotKind::Host;
            }
            continue;
        }

        if (slot < required_player_count) {
            if (info.occupied && info.kind == LobbySlotKind::Enabled) {
                continue;
            }

            info.kind = LobbySlotKind::Ai;
            info.occupied = true;
            info.ready = true;
            info.name = std::string(aoa::constants::SINGLEPLAYER_SLOT_AI_LABEL);
            apply_color(slot, info.color);
            continue;
        }

        if (info.occupied && info.kind == LobbySlotKind::Enabled) {
            transport_.disconnect_peer_slot(slot);
        }

        info = LobbySlotInfo{};
        info.kind = LobbySlotKind::Disabled;
        info.color = slot;
    }

    refresh_playing_player_count();
    send_lobby_state_to_clients();
    return true;
}

void LobbySession::clear_player_count_lock()
{
    if (!is_host()) {
        return;
    }

    view_.settings.required_player_count = 0U;
    send_lobby_state_to_clients();
}

void LobbySession::clear_scenario_requirement()
{
    if (!is_host()) {
        return;
    }

    view_.settings.required_player_count = 0U;
    view_.settings.scenario_name.clear();
    view_.settings.game_style = 0U;
    send_lobby_state_to_clients();
}

bool LobbySession::can_start_match() const
{
    if (!is_host() || status_ != LobbyStatus::Joined) {
        return false;
    }

    if (playing_slot_count() < aoa::constants::MULTIPLAYER_MIN_PLAYER_COUNT) {
        return false;
    }

    if (view_.settings.required_player_count > 0U
        && playing_slot_count() != view_.settings.required_player_count) {
        return false;
    }

    for (const LobbySlotInfo& slot : view_.slots) {
        if (slot.occupied && slot.kind != LobbySlotKind::Ai && !slot.ready) {
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

    refresh_playing_player_count();
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

void LobbySession::send_join_reject(const std::uint8_t player_slot, const std::string& reason)
{
    LobbyRejectMessage message{};
    message.reason = reason;
    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::LobbyReject, encode_lobby_reject(message));
    (void)transport_.send_reliable_to_client(player_slot, wire_message, constants::CHANNEL_RELIABLE);
    release_slot(player_slot);
    transport_.disconnect_peer_slot(player_slot);
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

    const std::uint8_t color = view_.slots[player_slot].color;
    const bool keep_enabled = view_.slots[player_slot].kind == LobbySlotKind::Enabled
        || view_.slots[player_slot].kind == LobbySlotKind::Ai;
    view_.slots[player_slot] = LobbySlotInfo{};
    view_.slots[player_slot].color = color;
    view_.slots[player_slot].kind =
        keep_enabled ? LobbySlotKind::Enabled : LobbySlotKind::Disabled;
    refresh_playing_player_count();
}

void LobbySession::refresh_host_pings()
{
    for (std::uint8_t slot = 1U; slot < view_.slots.size(); ++slot) {
        if (!view_.slots[slot].occupied || view_.slots[slot].kind != LobbySlotKind::Enabled) {
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
        if (!view_.slots[slot].occupied || view_.slots[slot].kind != LobbySlotKind::Enabled) {
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
        if (!message.has_value() || message->version != aoa::constants::GAME_VERSION) {
            send_join_reject(
                sender_slot,
                std::string(aoa::constants::LOBBY_VERSION_MISMATCH_MESSAGE));
            return;
        }

        view_.slots[sender_slot].occupied = true;
        view_.slots[sender_slot].ready = false;
        view_.slots[sender_slot].kind = LobbySlotKind::Enabled;
        view_.slots[sender_slot].name = message->name;
        refresh_playing_player_count();
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

    if (kind == NetMessageKind::LobbyColor) {
        const std::optional<LobbyColorMessage> message = decode_lobby_color(payload);
        if (!message.has_value() || message->player_slot != sender_slot) {
            return;
        }

        apply_color(sender_slot, message->color);
        send_lobby_state_to_clients();
        return;
    }

    if (kind == NetMessageKind::LobbyTeam) {
        const std::optional<LobbyTeamMessage> message = decode_lobby_team(payload);
        if (!message.has_value() || message->player_slot != sender_slot) {
            return;
        }

        if (sender_slot < view_.slots.size()
            && message->team < aoa::constants::LOBBY_TEAM_OPTION_COUNT) {
            view_.slots[sender_slot].team = message->team;
        }
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

    if (kind == NetMessageKind::LobbyReject) {
        const std::optional<LobbyRejectMessage> message = decode_lobby_reject(payload);
        reject_reason_ = message.has_value() && !message->reason.empty()
            ? message->reason
            : std::string(aoa::constants::LOBBY_VERSION_MISMATCH_MESSAGE);
        status_ = LobbyStatus::Rejected;
        transport_.disconnect();
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
        || status_ == LobbyStatus::Closed || status_ == LobbyStatus::Rejected) {
        return;
    }

    if (is_host()) {
        std::uint8_t connected_slot = 0U;
        while (transport_.consume_peer_connected_slot(connected_slot)) {
            const std::optional<std::uint8_t> join_slot = first_joinable_lobby_slot(view_);
            if (!join_slot.has_value()) {
                transport_.disconnect_peer_slot(connected_slot);
                continue;
            }

            if (*join_slot != connected_slot) {
                if (!transport_.rebind_client_to_player_slot(connected_slot, *join_slot)) {
                    transport_.disconnect_peer_slot(connected_slot);
                    continue;
                }
            }

            view_.slots[*join_slot].occupied = true;
            view_.slots[*join_slot].kind = LobbySlotKind::Enabled;
            view_.slots[*join_slot].ready = false;
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
            message.version = std::string(aoa::constants::GAME_VERSION);
            send_to_host(NetMessageKind::LobbyJoin, encode_lobby_join(message));
            join_sent_ = true;
        }

        const int timeout_ms = join_sent_
            ? aoa::constants::MULTIPLAYER_LOBBY_JOIN_RESPONSE_TIMEOUT_MS
            : aoa::constants::MULTIPLAYER_LOBBY_CONNECT_TIMEOUT_MS;
        if (elapsed_since(connect_started_).count() >= timeout_ms) {
            status_ = LobbyStatus::ConnectFailed;
            transport_.disconnect();
            return;
        }
    }

    for (const ReceivedPacket& packet : transport_.drain_received()) {
        handle_packet(packet);
    }

    if (status_ == LobbyStatus::Rejected) {
        return;
    }

    if (transport_.consume_peer_lost()) {
        status_ = LobbyStatus::Closed;
        transport_.disconnect();
    }
}

} // namespace aoa::net
