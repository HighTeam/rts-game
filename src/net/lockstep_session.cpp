#include "net/lockstep_session.hpp"

#include "core/constants.hpp"
#include "net/chat_wire.hpp"
#include "net/lobby_wire.hpp"
#include "net/lockstep_auto_input.hpp"
#include "net/lockstep_debug_log.hpp"
#include "net/lockstep_wire.hpp"
#include "net/net_constants.hpp"
#include "net/net_message.hpp"
#include "net/reconnect_wire.hpp"
#include "render/sim_render_snapshot.hpp"
#include "sim/components/match_session.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/tags.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"
#include "sim/snapshot/sim_snapshot.hpp"
#include "sim/systems/disconnected_player_ai.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace aoa::net {

namespace {

void debug_log_session_state(
    const LockstepSession& session,
    const sim::Simulation& simulation,
    const std::string_view label)
{
    if (!LockstepDebugLog::is_enabled()) {
        return;
    }

    std::ostringstream detail{};
    detail << "label=" << label << " tick=" << simulation.tick_count() << " connected="
           << session.is_connected() << " ready=" << session.is_session_ready()
           << " ai=" << session.is_ai_fallback()
           << " reconnecting=" << session.is_reconnecting()
           << " wait_opponent=" << session.is_waiting_for_opponent_reconnect()
           << " await_handshake=" << session.is_awaiting_reconnect_handshake()
           << " desync=" << session.is_desynced();
    LockstepDebugLog::log(detail.str());
}

bool validate_snapshot_roundtrip(sim::Simulation& source, const std::vector<std::byte>& snapshot_bytes)
{
    if (snapshot_bytes.empty()) {
        return false;
    }

    sim::Simulation probe{};
    if (!probe.apply_snapshot(snapshot_bytes)) {
        sim::diagnose_snapshot_roundtrip_failure(source, probe, snapshot_bytes);
        return false;
    }

    return probe.state_hash() == source.state_hash();
}

[[nodiscard]] bool ai_should_think_this_tick(
    const std::uint64_t tick_count,
    const std::uint8_t slot)
{
    return (tick_count + static_cast<std::uint64_t>(slot))
        % static_cast<std::uint64_t>(aoa::constants::AI_THINK_INTERVAL_TICKS)
        == 0U;
}

int smooth_latency_sample(const int previous_ms, const int sample_ms)
{
    if (sample_ms <= 0) {
        return previous_ms;
    }

    if (previous_ms <= 0) {
        return sample_ms;
    }

    const float blended = static_cast<float>(previous_ms)
        * (1.0F - constants::LOCKSTEP_RTT_SMOOTHING_ALPHA)
        + static_cast<float>(sample_ms) * constants::LOCKSTEP_RTT_SMOOTHING_ALPHA;
    return static_cast<int>(blended + 0.5F);
}

[[nodiscard]] std::uint64_t steady_clock_now_ns()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

[[nodiscard]] int round_trip_ms_from_send_time_ns(const std::uint64_t send_time_ns)
{
    if (send_time_ns == 0U) {
        return 0;
    }

    const std::uint64_t now_ns = steady_clock_now_ns();
    if (now_ns <= send_time_ns) {
        return 0;
    }

    const std::uint64_t delta_ns = now_ns - send_time_ns;
    const int sample_ms = static_cast<int>((delta_ns + 999999U) / 1000000U);
    return sample_ms <= 0 ? 1 : sample_ms;
}

[[nodiscard]] bool is_multi_peer_session(const std::uint8_t session_player_count)
{
    return session_player_count > 2U;
}

[[nodiscard]] bool awaiting_reconnect_handshake_blocks_tick_advance(
    const LockstepRole role,
    const bool awaiting_reconnect_handshake,
    const std::uint8_t session_player_count)
{
    if (!awaiting_reconnect_handshake) {
        return false;
    }

    if (role == LockstepRole::Client) {
        return true;
    }

    return !is_multi_peer_session(session_player_count);
}

} // namespace

void LockstepSession::sync_command_sequences_from_input_log()
{
    local_command_sequence_ = 1U;
    ai_command_sequence_ = 1U;
    for (const sim::player::PlayerCommand& command : simulation_.command_queue().input_log()) {
        local_command_sequence_ = std::max(local_command_sequence_, command.sequence + 1U);
        ai_command_sequence_ = std::max(ai_command_sequence_, command.sequence + 1U);
    }
}

bool LockstepSession::apply_reconnect_snapshot_locally(const std::span<const std::byte> snapshot_bytes)
{
    if (!simulation_.apply_snapshot(snapshot_bytes)) {
        return false;
    }

    restore_match_identity_after_snapshot();

    // Entity ids are remapped by snapshot restore; last-seen poses are invalid.
    building_sight_memory_.clear();
    simulation_.snapshot_world_positions_for_render();
    publish_render_snapshot_locked();
    sync_command_sequences_from_input_log();
    return true;
}

void LockstepSession::restore_match_identity_after_snapshot()
{
    const auto world_view =
        simulation_.registry().view<sim::components::WorldTag, sim::components::MatchSession>();
    if (world_view.begin() == world_view.end()) {
        return;
    }

    auto& match_session = world_view.get<sim::components::MatchSession>(*world_view.begin());
    match_session.player_color_indices = lobby_slot_colors_;
    if (session_player_count_ > 0U
        && session_player_count_ <= static_cast<std::uint8_t>(aoa::constants::MAX_PLAYER_SLOTS)) {
        match_session.playing_slots_mask =
            static_cast<std::uint8_t>((1U << session_player_count_) - 1U);
    }

    if (!lobby_settings_configured_) {
        return;
    }

    match_session.civil_population_map_cap =
        static_cast<int>(lobby_settings_.civil_population_map_cap);
    match_session.fog_of_war_mode =
        static_cast<sim::components::FogOfWarMode>(lobby_settings_.fog_mode);
    match_session.fog_of_war_enabled = lobby_settings_.fog_of_war_enabled;
    match_session.cheats_enabled = lobby_settings_.cheats_enabled;
    match_session.victory_condition =
        static_cast<sim::components::VictoryCondition>(lobby_settings_.victory_condition);
}

LockstepSession::LockstepSession(
    const LockstepRole role,
    const std::uint8_t player_slot,
    sim::Simulation& simulation,
    const std::uint8_t session_player_count)
    : role_(role)
    , player_slot_(player_slot)
    , session_player_count_(std::max<std::uint8_t>(
        2U,
        std::min(session_player_count, static_cast<std::uint8_t>(constants::LOCKSTEP_MAX_PLAYER_SLOTS))))
    , simulation_(simulation)
{
    last_peer_activity_time_ = std::chrono::steady_clock::now();

    for (const sim::player::PlayerCommand& command : simulation_.command_queue().input_log()) {
        local_command_sequence_ = std::max(local_command_sequence_, command.sequence + 1U);
        ai_command_sequence_ = std::max(ai_command_sequence_, command.sequence + 1U);
    }

    transport_.set_inbound_latency_handler([this](const std::span<const std::byte> packet) {
        return handle_inbound_latency_packet(packet);
    });
}

LockstepSession::~LockstepSession()
{
    stop_background_tick_loop();
}

void LockstepSession::start_background_tick_loop()
{
    if (tick_loop_running_.exchange(true)) {
        return;
    }

    tick_thread_ = std::thread([this]() { background_tick_loop(); });
}

void LockstepSession::stop_background_tick_loop()
{
    if (!tick_loop_running_.exchange(false)) {
        return;
    }

    if (tick_thread_.joinable()) {
        tick_thread_.join();
    }
}

void LockstepSession::background_tick_loop()
{
    using clock = std::chrono::steady_clock;

    const double sim_delta_seconds =
        1.0 / static_cast<double>(aoa::constants::SIM_TICKS_PER_SECOND);
    debug_last_accumulator_time_ = clock::now();
    debug_last_heartbeat_ = debug_last_accumulator_time_;
    debug_last_stall_log_ = debug_last_accumulator_time_;

    LockstepDebugLog::log_event(
        "tick_loop_start",
        "sim_tps=" + std::to_string(aoa::constants::SIM_TICKS_PER_SECOND));

    while (tick_loop_running_.load()) {
        const auto frame_start = clock::now();
        const double frame_seconds = std::chrono::duration<double>(
            frame_start - debug_last_accumulator_time_).count();
        debug_last_accumulator_time_ = frame_start;
        transport_.poll(constants::NET_POLL_TIMEOUT_MS);
        service_network_latency();

        int ticks_this_loop = 0;
        bool due_waiting_remote = false;

        {
            std::unique_lock lock(simulation_access_mutex_);

            process_polled_messages_locked();

            const bool live_lockstep_ready =
                session_ready_ && !desynced_ && !host_lost_ && !reconnecting_ && !host_gone_
                && can_run_live_lockstep();
            if (!live_lockstep_ready) {
                debug_tick_accumulator_ = 0.0;
            }
            else {
                // Always advance the sim clock while live. Pausing it during peer
                // waits made each tick cost (wait + 50ms) ≈ 10 TPS on LAN.
                debug_tick_accumulator_ += frame_seconds;

                const bool waiting_remote = has_local_sent(next_execute_tick())
                    && !has_remote_ready(next_execute_tick());
                if (waiting_remote) {
                    // Stay "one tick due" so we resume immediately when the batch
                    // arrives, but do not bank multi-tick catch-up debt (jumps).
                    if (debug_tick_accumulator_ > sim_delta_seconds) {
                        debug_tick_accumulator_ = sim_delta_seconds;
                    }
                }
                else {
                    const double max_accumulator_seconds = sim_delta_seconds
                        * static_cast<double>(constants::LOCKSTEP_MAX_ACCUMULATOR_TICKS);
                    if (debug_tick_accumulator_ > max_accumulator_seconds) {
                        debug_tick_accumulator_ = max_accumulator_seconds;
                    }
                }

                // Keep the peer fed even while we are pacing/waiting.
                ensure_local_batches_pipelined();
            }

            while (ticks_this_loop < constants::LOCKSTEP_MAX_TICKS_PER_LOOP) {
                if (debug_tick_accumulator_ < sim_delta_seconds) {
                    break;
                }

                if (run_sim_tick_attempt()) {
                    debug_tick_accumulator_ -= sim_delta_seconds;
                    ++ticks_this_loop;
                    continue;
                }

                const bool still_waiting_remote =
                    session_ready_ && !desynced_ && !host_lost_ && !reconnecting_ && !host_gone_
                    && has_local_sent(next_execute_tick())
                    && !has_remote_ready(next_execute_tick());
                if (!still_waiting_remote) {
                    break;
                }

                due_waiting_remote = true;

                // Peer batch missing: poll without holding the sim lock, then retry.
                bool advanced_after_wait = false;
                for (int attempt = 0; attempt < constants::LOCKSTEP_WAIT_POLL_ATTEMPTS; ++attempt) {
                    lock.unlock();
                    transport_.poll(constants::NET_POLL_TIMEOUT_MS);
                    service_network_latency();
                    std::this_thread::yield();
                    lock.lock();
                    process_polled_messages_locked();

                    if (!run_sim_tick_attempt()) {
                        continue;
                    }

                    debug_tick_accumulator_ -= sim_delta_seconds;
                    ++ticks_this_loop;
                    advanced_after_wait = true;
                    due_waiting_remote = false;
                    break;
                }

                if (!advanced_after_wait) {
                    if (debug_tick_accumulator_ > sim_delta_seconds) {
                        debug_tick_accumulator_ = sim_delta_seconds;
                    }
                    break;
                }
            }

            debug_maybe_heartbeat();
        }

        if (ticks_this_loop == 0) {
            if (due_waiting_remote) {
                // Do not Sleep(1) here — on Windows it often becomes ~15ms and
                // stacks with peer RTT into ~12 TPS. Spin-poll the socket instead.
                for (int spin = 0; spin < constants::LOCKSTEP_DUE_WAIT_SPIN_ITERS; ++spin) {
                    transport_.poll(constants::NET_POLL_TIMEOUT_MS);
                    service_network_latency();
                    std::this_thread::yield();
                }
            }
            else {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(constants::LOCKSTEP_TICK_IDLE_SLEEP_MS));
            }
        }
    }

    LockstepDebugLog::log_event("tick_loop_stop", "tick=" + std::to_string(simulation_.tick_count()));
}

void LockstepSession::note_opponent_transport_down()
{
    if (is_multi_peer_session(session_player_count_)) {
        return;
    }

    if (role_ != LockstepRole::Host || !match_started_ || ai_fallback_ || host_lost_
        || opponent_reconnect_pending_) {
        return;
    }

    opponent_reconnect_pending_ = true;
    awaiting_reconnect_handshake_ = false;
    mid_match_reconnect_authorized_ = false;
    sync_status_chat_announced_ = false;
    client_resync_ready_ = false;
    desynced_ = false;
    desync_tick_ = 0U;
    last_reconnect_snapshot_sent_ = {};
    begin_opponent_reconnect_grace();
    clear_state_hash_tracking();

    push_system_chat(player_display_name(opponent_player_slot()) + " has disconnected.");
    enter_ai_fallback();
    push_system_chat(
        "AI takes over disconnected player " + player_display_name(opponent_player_slot()) + ".");
    std::cout << "lockstep: player " << static_cast<int>(opponent_player_slot() + 1U)
              << " left at tick " << simulation_.tick_count()
              << " — AI playing, waiting for reconnect...\n";
    LockstepDebugLog::log_event(
        "opponent_transport_down",
        "tick=" + std::to_string(simulation_.tick_count()));
}

bool LockstepSession::can_run_live_lockstep() const
{
    if (awaiting_reconnect_handshake_blocks_tick_advance(
            role_, awaiting_reconnect_handshake_, session_player_count_)) {
        return false;
    }

    if (opponent_reconnect_pending_ && !is_connected() && !ai_fallback_) {
        return false;
    }

    return true;
}

bool LockstepSession::should_verify_state_hashes() const
{
    if (!match_started_ || ai_fallback_ || reconnecting_ || host_lost_ || host_gone_) {
        return false;
    }

    if (!session_ready_ || !is_connected()) {
        return false;
    }

    if (opponent_reconnect_pending_ || awaiting_reconnect_handshake_) {
        return false;
    }

    if (hash_verify_warmup_ticks_remaining_ > 0) {
        return false;
    }

    return can_run_live_lockstep();
}

void LockstepSession::clear_state_hash_tracking()
{
    local_state_hashes_.clear();
    remote_state_hashes_by_slot_.clear();
}

bool LockstepSession::run_sim_tick_attempt()
{
    if (desynced_ || host_lost_ || reconnecting_ || host_gone_) {
        debug_log_advance_blocked("session_halted");
        return false;
    }

    if (awaiting_reconnect_handshake_blocks_tick_advance(
            role_, awaiting_reconnect_handshake_, session_player_count_)) {
        debug_log_advance_blocked("awaiting_reconnect_handshake");
        return false;
    }

    if (!can_run_live_lockstep()) {
        if (opponent_reconnect_pending_ && !is_connected()) {
            debug_log_advance_blocked("opponent_disconnected");
        }
        return false;
    }

    if (!session_ready_ && !ai_fallback_) {
        debug_log_advance_blocked("session_not_ready");
        return false;
    }

    maybe_inject_lockstep_auto_input(*this, simulation_);

    if (role_ == LockstepRole::Host && session_player_count_ > 2U && !match_started_
        && !is_lobby_full()) {
        debug_log_advance_blocked("waiting_for_lobby");
        return false;
    }

    if (!is_connected() && !ai_fallback_) {
        if (role_ == LockstepRole::Host && match_started_) {
            take_over_unconnected_human_slots();
        }
        else if (required_registered_clients_mask() != 0U) {
            debug_log_advance_blocked("not_connected");
            return false;
        }
    }

    const bool advanced = try_advance_tick();

    if (advanced) {
        if (LockstepDebugLog::is_enabled()) {
            std::ostringstream detail{};
            detail << "tick=" << simulation_.tick_count() << " hash=0x" << std::hex
                   << simulation_.state_hash() << std::dec;
            LockstepDebugLog::log_event("tick_advanced", detail.str());
        }
        debug_last_logged_tick_ = simulation_.tick_count();
        return true;
    }

    const std::uint64_t execute_tick = next_execute_tick();
    if (!has_remote_ready(execute_tick)) {
        debug_log_advance_blocked("waiting_remote_batch execute_tick=" + std::to_string(execute_tick));
        return false;
    }

    debug_log_advance_blocked("try_advance_returned_false");
    return false;
}

void LockstepSession::debug_log_advance_blocked(const std::string_view reason)
{
    if (!LockstepDebugLog::is_enabled()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - debug_last_stall_log_ < std::chrono::seconds(2)) {
        return;
    }

    debug_last_stall_log_ = now;
    LockstepDebugLog::log_event(
        "advance_blocked",
        std::string(reason) + " tick=" + std::to_string(simulation_.tick_count()));
}

void LockstepSession::debug_maybe_heartbeat()
{
    if (!LockstepDebugLog::is_enabled()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - debug_last_heartbeat_ < std::chrono::seconds(5)) {
        return;
    }

    debug_last_heartbeat_ = now;
    debug_log_session_state(*this, simulation_, "heartbeat");
}

void LockstepSession::run_tick_frame()
{
    transport_.poll(constants::NET_POLL_TIMEOUT_MS);

    std::lock_guard lock(simulation_access_mutex_);
    process_polled_messages_locked();
    (void)run_sim_tick_attempt();
    debug_maybe_heartbeat();
}

std::recursive_mutex& LockstepSession::simulation_access_mutex()
{
    return simulation_access_mutex_;
}

bool LockstepSession::has_render_timing() const
{
    return last_tick_time_ns_.load() != 0U;
}

bool LockstepSession::consume_snapshot_restored()
{
    const bool restored = snapshot_restored_pending_;
    snapshot_restored_pending_ = false;
    return restored;
}

float LockstepSession::render_interpolation_alpha() const
{
    const std::uint64_t anchor_ns = render_clock_anchor_ns_.load();
    if (anchor_ns == 0U) {
        return 0.0F;
    }

    const auto anchor_time = std::chrono::steady_clock::time_point{
        std::chrono::steady_clock::duration{anchor_ns}};
    const auto now = std::chrono::steady_clock::now();
    const double elapsed_seconds = std::chrono::duration<double>(now - anchor_time).count();
    const double sim_delta_seconds =
        1.0 / static_cast<double>(aoa::constants::SIM_TICKS_PER_SECOND);
    const double max_alpha =
        1.0 + static_cast<double>(constants::LOCKSTEP_MAX_RENDER_EXTRAPOLATION_ALPHA);
    return static_cast<float>(std::clamp(elapsed_seconds / sim_delta_seconds, 0.0, max_alpha));
}

std::shared_ptr<const render::SimRenderSnapshot> LockstepSession::render_snapshot() const
{
    return render_snapshot_.load();
}

LockstepNetworkHudStats LockstepSession::network_hud_stats() const
{
    LockstepNetworkHudStats stats{};

    if (!is_connected() || ai_fallback_) {
        return stats;
    }

    stats.active = true;
    stats.local_ping_ms = display_ping_ms_.load();

    if (stats.local_ping_ms <= 0) {
        stats.local_ping_ms = latest_ping_ms_.load();
    }

    if (stats.local_ping_ms <= 0) {
        stats.local_ping_ms = smoothed_rtt_ms_.load();
    }

    return stats;
}

void LockstepSession::reset_latency_stats()
{
    {
        std::lock_guard lock(latency_stats_mutex_);
        ping_sample_count_ = 0;
        ping_sample_index_ = 0;
        ping_samples_.fill(0);
    }

    latest_ping_ms_.store(0);
    display_ping_ms_.store(0);
    smoothed_rtt_ms_.store(0);
    last_latency_probe_sent_ = {};
    latency_probe_sequence_ = 0U;
}

void LockstepSession::update_display_ping_locked()
{
    if (ping_sample_count_ <= 0) {
        display_ping_ms_.store(0);
        return;
    }

    int min_sample = ping_samples_[0];
    for (int index = 1; index < ping_sample_count_; ++index) {
        min_sample = std::min(min_sample, ping_samples_[index]);
    }

    display_ping_ms_.store(min_sample);
}

void LockstepSession::record_latency_sample(const int sample_ms)
{
    if (sample_ms <= 0
        || sample_ms > static_cast<int>(constants::LOCKSTEP_RTT_SAMPLE_MAX_MS)) {
        return;
    }

    {
        std::lock_guard lock(latency_stats_mutex_);
        ping_samples_[static_cast<std::size_t>(ping_sample_index_)] = sample_ms;
        ping_sample_index_ =
            (ping_sample_index_ + 1) % constants::LOCKSTEP_PING_DISPLAY_WINDOW;
        ping_sample_count_ = std::min(
            ping_sample_count_ + 1,
            constants::LOCKSTEP_PING_DISPLAY_WINDOW);
        update_display_ping_locked();
    }

    latest_ping_ms_.store(sample_ms);
    smoothed_rtt_ms_.store(smooth_latency_sample(smoothed_rtt_ms_.load(), sample_ms));
}

void LockstepSession::service_network_latency()
{
    {
        std::lock_guard lock(network_service_mutex_);
        enqueue_inbound_packets(transport_.drain_received());
    }
    process_inbound_latency_packets();

    if (!is_connected() || ai_fallback_) {
        return;
    }

    maybe_send_latency_probe();
}

void LockstepSession::note_peer_activity()
{
    last_peer_activity_time_ = std::chrono::steady_clock::now();
}

bool LockstepSession::is_opponent_unresponsive() const
{
    if (session_player_count_ > 2U) {
        return false;
    }

    if (role_ != LockstepRole::Host || !match_started_ || ai_fallback_ || !remote_batch_received_ever_) {
        return false;
    }

    if (hash_verify_warmup_ticks_remaining_ > 0) {
        return false;
    }

    if (is_opponent_reconnect_grace_active()) {
        // During initial reconnect alignment, wait out the grace window. After the first
        // post-resync tick clears resync_strict_batch_gate_, batch silence still means stall.
        if (resync_strict_batch_gate_ || awaiting_reconnect_handshake_) {
            return false;
        }
    }

    if (!waiting_remote_active_) {
        return false;
    }

    if (!is_connected()) {
        return waiting_remote_active_;
    }

    const auto silence_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - waiting_remote_since_).count();
    return silence_ms
        >= static_cast<long long>(constants::LOCKSTEP_PEER_SILENCE_MS);
}

void LockstepSession::note_sim_tick_completed()
{
    const auto now = std::chrono::steady_clock::now();
    const std::uint64_t now_ns = static_cast<std::uint64_t>(now.time_since_epoch().count());
    last_tick_time_ns_.store(now_ns);
    render_clock_anchor_ns_.store(now_ns);
}

void LockstepSession::publish_render_snapshot_locked()
{
    auto snapshot = std::make_shared<render::SimRenderSnapshot>(
        render::capture_sim_render_snapshot(
            simulation_.registry(),
            player_slot_,
            &building_sight_memory_,
            simulation_.tick_count()));
    render_snapshot_.store(std::move(snapshot));
}

void LockstepSession::enqueue_inbound_packets(std::vector<ReceivedPacket> packets)
{
    if (packets.empty()) {
        return;
    }

    std::lock_guard lock(inbound_packet_mutex_);
    inbound_packet_queue_.reserve(inbound_packet_queue_.size() + packets.size());
    for (ReceivedPacket& packet : packets) {
        inbound_packet_queue_.push_back(std::move(packet));
    }
}

void LockstepSession::drain_inbound_packet_queue_locked()
{
    std::vector<ReceivedPacket> packets{};
    {
        std::lock_guard lock(inbound_packet_mutex_);
        packets = std::move(inbound_packet_queue_);
        inbound_packet_queue_.clear();
    }

    for (const ReceivedPacket& packet : packets) {
        process_received_packet(packet.data, packet.sender_slot);
    }
}

void LockstepSession::maybe_send_latency_probe()
{
    if (!is_connected() || ai_fallback_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_latency_probe_sent_).count();
    if (elapsed_ms
        < static_cast<long long>(constants::LOCKSTEP_LATENCY_PROBE_INTERVAL_MS)) {
        return;
    }

    last_latency_probe_sent_ = now;

    LatencyProbeMessage probe{};
    probe.send_time_ns = steady_clock_now_ns();
    probe.sequence = ++latency_probe_sequence_;

    const std::vector<std::byte> payload = encode_latency_probe(probe);
    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::LatencyProbe, payload);
    (void)transport_.send_unreliable(wire_message, constants::CHANNEL_UNRELIABLE);
}

void LockstepSession::handle_latency_pong(const LatencyProbeMessage& message)
{
    const int sample_ms = round_trip_ms_from_send_time_ns(message.send_time_ns);
    if (sample_ms <= 0) {
        return;
    }

    record_latency_sample(sample_ms);
}

InboundLatencyHandleResult LockstepSession::handle_inbound_latency_packet(
    const std::span<const std::byte> packet)
{
    const auto decoded_message = decode_net_message(packet);
    if (!decoded_message.has_value()) {
        return {};
    }

    if (decoded_message->first == NetMessageKind::LatencyProbe) {
        const auto message = decode_latency_probe(decoded_message->second);
        if (!message.has_value()) {
            return {};
        }

        note_peer_activity();

        InboundLatencyHandleResult result{};
        result.handled = true;
        result.immediate_reply =
            encode_net_message(NetMessageKind::LatencyPong, encode_latency_probe(*message));
        return result;
    }

    if (decoded_message->first == NetMessageKind::LatencyPong) {
        const auto message = decode_latency_probe(decoded_message->second);
        if (!message.has_value()) {
            return {};
        }

        note_peer_activity();
        handle_latency_pong(*message);

        InboundLatencyHandleResult result{};
        result.handled = true;
        return result;
    }

    return {};
}

bool LockstepSession::try_process_latency_packet(const std::vector<std::byte>& packet)
{
    const InboundLatencyHandleResult result = handle_inbound_latency_packet(packet);
    if (!result.handled) {
        return false;
    }

    if (!result.immediate_reply.empty()) {
        (void)transport_.send_unreliable(result.immediate_reply, constants::CHANNEL_UNRELIABLE);
    }

    return true;
}

void LockstepSession::process_inbound_latency_packets()
{
    std::vector<ReceivedPacket> packets{};
    {
        std::lock_guard lock(inbound_packet_mutex_);
        packets = std::move(inbound_packet_queue_);
        inbound_packet_queue_.clear();
    }

    if (packets.empty()) {
        return;
    }

    std::vector<ReceivedPacket> game_packets{};
    game_packets.reserve(packets.size());

    for (ReceivedPacket& packet : packets) {
        if (try_process_latency_packet(packet.data)) {
            continue;
        }

        game_packets.push_back(std::move(packet));
    }

    if (game_packets.empty()) {
        return;
    }

    std::lock_guard lock(inbound_packet_mutex_);
    inbound_packet_queue_.insert(
        inbound_packet_queue_.end(),
        std::make_move_iterator(game_packets.begin()),
        std::make_move_iterator(game_packets.end()));
}

bool LockstepSession::is_waiting_for_opponent_reconnect() const
{
    return opponent_reconnect_pending_;
}

bool LockstepSession::has_disconnected_human_slots() const
{
    return disconnected_slots_mask_ != 0U;
}

std::uint8_t LockstepSession::first_disconnected_human_slot() const
{
    for (std::uint8_t slot = 0U; slot < session_player_count_; ++slot) {
        if ((disconnected_slots_mask_ & (1U << slot)) != 0U) {
            return slot;
        }
    }

    return 0U;
}

void LockstepSession::take_over_unconnected_human_slots()
{
    if (role_ != LockstepRole::Host || !match_started_) {
        return;
    }

    for (std::uint8_t slot = 0U; slot < session_player_count_; ++slot) {
        if (slot == player_slot_) {
            continue;
        }

        if (slot < slot_is_ai_.size() && slot_is_ai_[slot]) {
            continue;
        }

        handle_host_player_slot_disconnected(slot);
    }
}

bool LockstepSession::is_awaiting_reconnect_handshake() const
{
    return awaiting_reconnect_handshake_;
}

bool LockstepSession::start_host(const std::uint16_t port)
{
    const std::uint8_t human_clients = static_cast<std::uint8_t>(
        expected_human_player_count() > 1U ? expected_human_player_count() - 1U : 1U);
    if (required_registered_clients_mask() == 0U) {
        session_ready_ = true;
    }

    return transport_.start_host(port, human_clients);
}

bool LockstepSession::connect(const char* host_name, const std::uint16_t port)
{
    reconnect_host_ = host_name;
    reconnect_port_ = port;
    reconnect_request_sent_ = false;
    session_ready_ = false;
    reset_latency_stats();
    return transport_.connect(host_name, port);
}

void LockstepSession::ensure_initial_render_snapshot()
{
    std::lock_guard lock(simulation_access_mutex_);

    if (render_snapshot_.load()) {
        return;
    }

    simulation_.snapshot_world_positions_for_render();
    publish_render_snapshot_locked();

    if (render_clock_anchor_ns_.load() == 0U) {
        note_sim_tick_completed();
    }
}

void LockstepSession::submit_local_command(sim::player::PlayerCommand command)
{
    std::lock_guard lock(simulation_access_mutex_);

    if (desynced_ || host_lost_ || reconnecting_ || host_gone_) {
        return;
    }

    if (commands_blocked_during_resync()) {
        return;
    }

    if (ai_fallback_ && role_ != LockstepRole::Host) {
        return;
    }

    command.player_slot = player_slot_;
    command.sequence = local_command_sequence_++;

    const std::uint64_t earliest_execute_tick =
        simulation_.tick_count() + static_cast<std::uint64_t>(constants::LOCKSTEP_COMMAND_DELAY_TICKS);
    if (command.execute_tick < earliest_execute_tick) {
        command.execute_tick = earliest_execute_tick;
    }

    while (has_local_sent(command.execute_tick)) {
        ++command.execute_tick;
    }

    sim::snapshot::annotate_command_entity_keys(simulation_.registry(), command);
    if (command.unit_keys.size() != command.unit_ids.size()) {
        return;
    }

    if ((command.type == sim::player::PlayerCommandType::Attack
            || command.type == sim::player::PlayerCommandType::SpawnWorker
            || command.type == sim::player::PlayerCommandType::SpawnMilitia
            || command.type == sim::player::PlayerCommandType::SpawnMage
            || command.type == sim::player::PlayerCommandType::DestroyBuilding
            || command.type == sim::player::PlayerCommandType::Garrison
            || command.type == sim::player::PlayerCommandType::UnloadGarrison
            || command.type == sim::player::PlayerCommandType::AdvanceAge
            || command.type == sim::player::PlayerCommandType::ResumeBuild
            || command.type == sim::player::PlayerCommandType::RenewFarm)
        && command.target_entity != entt::null && !command.target_entity_key.has_value()) {
        return;
    }

    local_outbox_[command.execute_tick].push_back(std::move(command));
}

void LockstepSession::set_chat_state(app::ChatState* chat_state)
{
    chat_state_ = chat_state;
}

void LockstepSession::configure_match_identity(
    const std::array<std::string, constants::LOCKSTEP_MAX_PLAYER_SLOTS>& player_names,
    const std::array<std::uint64_t, constants::LOCKSTEP_MAX_PLAYER_SLOTS>& reconnect_tokens)
{
    player_names_ = player_names;
    reconnect_tokens_ = reconnect_tokens;
}

void LockstepSession::configure_ai_slots(
    const std::array<bool, aoa::constants::MAX_PLAYER_SLOTS>& slot_is_ai)
{
    slot_is_ai_ = slot_is_ai;
}

void LockstepSession::configure_lobby_settings(
    const LobbySettings& settings,
    const std::array<std::uint8_t, aoa::constants::MAX_PLAYER_SLOTS>& slot_teams,
    const std::array<std::uint8_t, aoa::constants::MAX_PLAYER_SLOTS>& slot_colors)
{
    lobby_settings_ = settings;
    lobby_slot_teams_ = slot_teams;
    lobby_slot_colors_ = slot_colors;
    lobby_settings_configured_ = true;
}

bool LockstepSession::commands_blocked_during_resync() const
{
    if (awaiting_reconnect_handshake_blocks_tick_advance(
            role_, awaiting_reconnect_handshake_, session_player_count_)) {
        return true;
    }

    return !session_ready_ && !ai_fallback_;
}

std::uint8_t LockstepSession::expected_human_player_count() const
{
    std::uint8_t count = 0U;
    for (std::uint8_t slot = 0U; slot < session_player_count_; ++slot) {
        if (slot < slot_is_ai_.size() && slot_is_ai_[slot]) {
            continue;
        }

        ++count;
    }

    return count < 1U ? 1U : count;
}

std::string LockstepSession::player_display_name(const std::uint8_t player_slot) const
{
    if (player_slot < player_names_.size() && !player_names_[player_slot].empty()) {
        return player_names_[player_slot];
    }

    return "Player " + std::to_string(static_cast<int>(player_slot) + 1);
}

void LockstepSession::push_system_chat(const std::string_view text)
{
    if (chat_state_ == nullptr || text.empty()) {
        return;
    }

    chat_state_->push_system_message(std::string{text});
}

bool LockstepSession::claim_token_accepted(
    const std::uint8_t player_slot,
    const std::uint64_t claim_token) const
{
    if (player_slot >= reconnect_tokens_.size()) {
        return false;
    }

    const std::uint64_t expected = reconnect_tokens_[player_slot];
    // CLI/smoke matches leave tokens at 0 and accept any claim.
    if (expected == 0U) {
        return true;
    }

    return claim_token == expected;
}

bool LockstepSession::is_slot_reclaimable_mid_match(const std::uint8_t player_slot) const
{
    if (player_slot == 0U || player_slot >= session_player_count_) {
        return false;
    }

    if (session_player_count_ <= 2U) {
        return ai_fallback_ || opponent_reconnect_pending_
            || simulation_.is_player_ai_controlled(player_slot);
    }

    const bool disconnected = (disconnected_slots_mask_ & (1U << player_slot)) != 0U;
    const bool pending_resync = (resync_pending_slots_mask_ & (1U << player_slot)) != 0U;
    return disconnected || pending_resync || simulation_.is_player_ai_controlled(player_slot);
}

bool LockstepSession::try_accept_mid_match_lobby_rejoin(
    const std::uint8_t enet_client_slot,
    const std::string_view join_name)
{
    if (role_ != LockstepRole::Host || !match_started_ || simulation_.tick_count() == 0U) {
        return false;
    }

    std::uint8_t claimed_slot = session_player_count_;
    for (std::uint8_t slot = 1U; slot < session_player_count_; ++slot) {
        if (!is_slot_reclaimable_mid_match(slot)) {
            continue;
        }

        if (!join_name.empty() && player_names_[slot] == join_name) {
            claimed_slot = slot;
            break;
        }
    }

    // 2P: only one client slot — accept even if the Connect name was edited.
    if (claimed_slot >= session_player_count_ && session_player_count_ <= 2U
        && is_slot_reclaimable_mid_match(opponent_player_slot())) {
        claimed_slot = opponent_player_slot();
    }

    if (claimed_slot >= session_player_count_) {
        return false;
    }

    LobbySettings settings = lobby_settings_;
    settings.player_count = session_player_count_;

    std::array<std::uint8_t, aoa::constants::MAX_PLAYER_SLOTS> slot_colors = lobby_slot_colors_;
    std::array<std::uint8_t, aoa::constants::MAX_PLAYER_SLOTS> slot_teams = lobby_slot_teams_;

    const auto world_view =
        simulation_.registry().view<sim::components::WorldTag, sim::components::MatchSession>();
    if (world_view.begin() != world_view.end()) {
        const auto& match_session = world_view.get<sim::components::MatchSession>(*world_view.begin());
        settings.civil_population_map_cap =
            static_cast<std::uint8_t>(std::clamp(match_session.civil_population_map_cap, 0, 255));
        settings.fog_of_war_enabled = match_session.fog_of_war_enabled;
        settings.fog_mode = static_cast<std::uint8_t>(match_session.fog_of_war_mode);
        settings.cheats_enabled = match_session.cheats_enabled;
        settings.victory_condition = static_cast<std::uint8_t>(match_session.victory_condition);
        slot_colors = match_session.player_color_indices;

        if (simulation_.registry().all_of<sim::components::MapGrid>(*world_view.begin())) {
            const auto& map =
                simulation_.registry().get<sim::components::MapGrid>(*world_view.begin());
            settings.map_width = static_cast<std::int16_t>(map.width);
            settings.map_height = static_cast<std::int16_t>(map.height);
        }
    }

    LobbyStateMessage state{};
    state.host_slot = 0U;
    state.recipient_slot = claimed_slot;
    state.settings = settings;
    for (std::uint8_t slot = 0U; slot < session_player_count_; ++slot) {
        state.slots[slot].occupied = true;
        state.slots[slot].ready = true;
        state.slots[slot].kind =
            slot == 0U ? LobbySlotKind::Host : LobbySlotKind::Enabled;
        state.slots[slot].color = slot_colors[slot];
        state.slots[slot].team = slot_teams[slot];
        state.slots[slot].name = player_names_[slot].empty()
            ? ("Player " + std::to_string(static_cast<int>(slot) + 1))
            : player_names_[slot];
    }

    const std::vector<std::byte> state_message =
        encode_net_message(NetMessageKind::LobbyState, encode_lobby_state(state));
    const std::vector<std::byte> start_message =
        encode_net_message(NetMessageKind::LobbyMatchStart, encode_lobby_settings(settings));

    const bool sent_state = enet_client_slot != 0U
        ? transport_.send_reliable_to_client(
              enet_client_slot, state_message, constants::CHANNEL_RELIABLE)
        : transport_.send_reliable(state_message, constants::CHANNEL_RELIABLE);
    const bool sent_start = enet_client_slot != 0U
        ? transport_.send_reliable_to_client(
              enet_client_slot, start_message, constants::CHANNEL_RELIABLE)
        : transport_.send_reliable(start_message, constants::CHANNEL_RELIABLE);

    if (!sent_state || !sent_start) {
        LockstepDebugLog::log_event(
            "lobby_rejoin_send_failed",
            "player_slot=" + std::to_string(static_cast<int>(claimed_slot) + 1));
        return false;
    }

    if (!sync_status_chat_announced_) {
        push_system_chat(player_display_name(claimed_slot) + " is connecting.");
    }

    LockstepDebugLog::log_event(
        "lobby_rejoin_accepted",
        "player_slot=" + std::to_string(static_cast<int>(claimed_slot) + 1) + " enet_slot="
            + std::to_string(static_cast<int>(enet_client_slot)) + " tick="
            + std::to_string(simulation_.tick_count()));
    return true;
}

void LockstepSession::send_chat_message(const std::string_view text)
{
    if (text.empty()) {
        return;
    }

    ChatMessagePayload payload{};
    payload.player_slot = player_slot_;
    payload.text = std::string(text);
    if (payload.text.size() > static_cast<std::size_t>(aoa::constants::CHAT_MAX_MESSAGE_LENGTH)) {
        payload.text.resize(static_cast<std::size_t>(aoa::constants::CHAT_MAX_MESSAGE_LENGTH));
    }

    if (chat_state_ != nullptr) {
        chat_state_->push_message(payload.player_slot, payload.text);
    }

    if (!is_connected()) {
        return;
    }

    const std::vector<std::byte> encoded_payload = encode_chat_message(payload);
    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::Chat, encoded_payload);

    if (role_ == LockstepRole::Host) {
        (void)transport_.broadcast_unreliable_except(
            wire_message,
            constants::CHANNEL_UNRELIABLE,
            std::nullopt);
        return;
    }

    (void)transport_.send_unreliable(wire_message, constants::CHANNEL_UNRELIABLE);
}

void LockstepSession::handle_chat_message(
    const std::vector<std::byte>& payload,
    const std::uint8_t sender_slot)
{
    const auto decoded = decode_chat_message(payload);
    if (!decoded.has_value()) {
        return;
    }

    ChatMessagePayload message = *decoded;
    if (role_ == LockstepRole::Host) {
        message.player_slot = sender_slot;
        const std::vector<std::byte> relay_payload = encode_chat_message(message);
        const std::vector<std::byte> wire_message =
            encode_net_message(NetMessageKind::Chat, relay_payload);
        (void)transport_.broadcast_unreliable_except(
            wire_message,
            constants::CHANNEL_UNRELIABLE,
            sender_slot);
    }

    if (message.player_slot == player_slot_) {
        return;
    }

    if (chat_state_ != nullptr) {
        chat_state_->push_message(message.player_slot, message.text);
    }
}

void LockstepSession::disconnect_transport()
{
    transport_.disconnect();
}

void LockstepSession::poll()
{
    transport_.poll(constants::NET_POLL_TIMEOUT_MS);
    service_network_latency();

    std::lock_guard lock(simulation_access_mutex_);
    process_polled_messages_locked();
}

void LockstepSession::process_polled_messages_locked()
{
    if (role_ == LockstepRole::Host) {
        while (transport_.consume_peer_connected()) {
            handle_peer_connected();
            LockstepDebugLog::log_event(
                "peer_connected",
                "tick=" + std::to_string(simulation_.tick_count()));
        }

        if (!match_started_) {
            std::uint8_t lost_client_slot = 0U;
            while (transport_.consume_peer_lost_slot(lost_client_slot)) {
                note_registered_client_lost(lost_client_slot);
                LockstepDebugLog::log_event(
                    "peer_lost_lobby",
                    "player_slot=" + std::to_string(static_cast<int>(lost_client_slot) + 1));
            }
        }
    }

    if (role_ == LockstepRole::Client && is_connected() && !reconnect_request_sent_) {
        send_reconnect_request();
        LockstepDebugLog::log_event("reconnect_request_sent", "tick=" + std::to_string(simulation_.tick_count()));
    }

    drain_inbound_packet_queue_locked();
    maybe_retry_reconnect_handshake();
    maybe_retry_resync_ready();
    maybe_retry_host_resync_handshake();
    maybe_timeout_resync_handshake();
    maybe_send_pending_reconnect_snapshot();

    if (role_ == LockstepRole::Host && match_started_ && session_player_count_ <= 2U && !ai_fallback_
        && !host_lost_ && !is_connected() && !opponent_reconnect_pending_) {
        note_opponent_transport_down();
    }

    if (role_ == LockstepRole::Host && match_started_ && !host_lost_) {
        if (session_player_count_ > 2U) {
            std::uint8_t lost_client_slot = 0U;
            while (transport_.consume_peer_lost_slot(lost_client_slot)) {
                handle_host_player_slot_disconnected(lost_client_slot);
                LockstepDebugLog::log_event(
                    "peer_lost",
                    "player_slot=" + std::to_string(static_cast<int>(lost_client_slot) + 1) + " tick="
                        + std::to_string(simulation_.tick_count()));
            }
        }
        else if (transport_.consume_peer_lost()) {
            if (awaiting_reconnect_handshake_) {
                awaiting_reconnect_handshake_ = false;
                mid_match_reconnect_authorized_ = false;
                sync_status_chat_announced_ = false;
                client_resync_ready_ = false;
                opponent_needs_snapshot_ = true;
                if (!ai_fallback_) {
                    note_opponent_transport_down();
                }

                LockstepDebugLog::log_event(
                    "peer_lost_during_handshake",
                    "tick=" + std::to_string(simulation_.tick_count()));
                return;
            }

            if (ai_fallback_) {
                return;
            }

            if (!opponent_reconnect_pending_) {
                note_opponent_transport_down();
            }

            LockstepDebugLog::log_event(
                "peer_lost",
                "tick=" + std::to_string(simulation_.tick_count()) + " grace_ms="
                    + std::to_string(constants::LOCKSTEP_RECONNECT_GRACE_MS));
            return;
        }
    }
    else if (transport_.consume_peer_lost()) {
        if (match_started_ && !host_lost_ && !ai_fallback_) {
            if (role_ == LockstepRole::Client && is_host_reconnect_grace_active()) {
                // Do not drop the lost event — finish host-lost once grace ends.
                pending_host_lost_during_grace_ = true;
                return;
            }

            handle_opponent_lost();
            return;
        }

        // During reconnect attempts, consuming peer_lost clears the flag so the
        // next try_reconnect can start a fresh connect.
    }

    if (pending_host_lost_during_grace_ && !host_lost_ && !is_host_reconnect_grace_active()) {
        pending_host_lost_during_grace_ = false;
        handle_opponent_lost();
    }

    if (!host_lost_ && !reconnecting_ && !host_gone_ && !ai_fallback_
        && should_declare_opponent_disconnected()) {
        handle_opponent_lost();
    }

    if (desynced_) {
        return;
    }

    check_opponent_reconnect_timeout();

    if (reconnecting_ && !host_gone_) {
        try_reconnect();
    }
}

bool LockstepSession::try_advance_tick()
{
    if (awaiting_reconnect_handshake_blocks_tick_advance(
            role_, awaiting_reconnect_handshake_, session_player_count_)) {
        return false;
    }

    if (desynced_ || host_lost_ || reconnecting_ || host_gone_) {
        return false;
    }

    if (ai_fallback_) {
        return try_advance_ai_fallback_tick();
    }

    if (!session_ready_) {
        return false;
    }

    if (!is_connected()) {
        if (role_ == LockstepRole::Host && match_started_) {
            take_over_unconnected_human_slots();
            if (is_multi_peer_session(session_player_count_)) {
                return try_advance_live_tick();
            }

            if (!opponent_reconnect_pending_) {
                note_opponent_transport_down();
            }

            return try_advance_ai_fallback_tick();
        }

        return false;
    }

    return try_advance_live_tick();
}

bool LockstepSession::is_connected() const
{
    return transport_.is_connected();
}

bool LockstepSession::is_peer_disconnected() const
{
    return role_ == LockstepRole::Host && match_started_ && !is_connected() && !ai_fallback_
        && !opponent_reconnect_pending_;
}

bool LockstepSession::is_reconnecting() const
{
    return reconnecting_ && !host_gone_;
}

bool LockstepSession::is_host_gone() const
{
    return host_gone_;
}

bool LockstepSession::is_ai_fallback() const
{
    return ai_fallback_;
}

bool LockstepSession::is_session_ready() const
{
    return session_ready_;
}

bool LockstepSession::is_match_started() const
{
    return match_started_;
}

bool LockstepSession::is_desynced() const
{
    return desynced_;
}

std::uint64_t LockstepSession::desync_tick() const
{
    return desync_tick_;
}

std::uint8_t LockstepSession::ai_controlled_slot() const
{
    return ai_controlled_slot_;
}

std::uint8_t LockstepSession::opponent_player_slot() const
{
    return player_slot_ == constants::LOCKSTEP_HOST_PLAYER_SLOT
        ? constants::LOCKSTEP_CLIENT_PLAYER_SLOT
        : constants::LOCKSTEP_HOST_PLAYER_SLOT;
}

std::uint64_t LockstepSession::next_execute_tick() const
{
    return simulation_.tick_count() + 1U;
}

bool LockstepSession::has_local_sent(const std::uint64_t execute_tick) const
{
    return local_sent_ticks_.contains(execute_tick);
}

std::uint8_t LockstepSession::player_slot() const
{
    return player_slot_;
}

std::uint8_t LockstepSession::session_player_count() const
{
    return session_player_count_;
}

std::uint8_t LockstepSession::connected_peer_count() const
{
    if (role_ == LockstepRole::Host) {
        return transport_.connected_client_count();
    }

    return is_connected() ? 1U : 0U;
}

std::uint8_t LockstepSession::required_registered_clients_mask() const
{
    std::uint8_t mask = 0U;
    for (std::uint8_t slot = 1U; slot < session_player_count_; ++slot) {
        if (slot < slot_is_ai_.size() && slot_is_ai_[slot]) {
            continue;
        }

        mask = static_cast<std::uint8_t>(mask | (1U << slot));
    }

    return mask;
}

std::uint8_t LockstepSession::lobby_registered_client_count() const
{
    if (role_ != LockstepRole::Host || session_player_count_ <= 2U) {
        return connected_peer_count();
    }

    std::uint8_t count = 0U;
    for (std::uint8_t slot = 1U; slot < session_player_count_; ++slot) {
        if ((registered_clients_mask_ & (1U << slot)) != 0U) {
            ++count;
        }
    }

    return count;
}

void LockstepSession::note_registered_client_lost(const std::uint8_t player_slot)
{
    if (role_ != LockstepRole::Host || session_player_count_ <= 2U || player_slot == 0U
        || player_slot >= session_player_count_) {
        return;
    }

    registered_clients_mask_ =
        static_cast<std::uint8_t>(registered_clients_mask_ & ~(1U << player_slot));
    if (!match_started_) {
        session_ready_ = false;
    }
}

bool LockstepSession::is_lobby_full() const
{
    if (role_ != LockstepRole::Host) {
        return is_connected() && session_ready_;
    }

    const std::uint8_t required_mask = required_registered_clients_mask();
    if (required_mask == 0U) {
        return true;
    }

    if (session_player_count_ <= 2U) {
        return connected_peer_count() >= 1U;
    }

    return (registered_clients_mask_ & required_mask) == required_mask;
}

std::uint8_t LockstepSession::required_remote_slots_mask() const
{
    std::uint8_t mask = 0U;
    for (std::uint8_t slot = 0U; slot < session_player_count_; ++slot) {
        if (slot == player_slot_) {
            continue;
        }

        if (slot < slot_is_ai_.size() && slot_is_ai_[slot]) {
            continue;
        }

        mask = static_cast<std::uint8_t>(mask | (1U << slot));
    }

    const std::uint8_t ai_playing_mask =
        static_cast<std::uint8_t>(disconnected_slots_mask_ | resync_pending_slots_mask_);
    mask = static_cast<std::uint8_t>(mask & ~ai_playing_mask);

    return mask;
}

bool LockstepSession::is_remote_slot_ready(
    const std::uint64_t execute_tick,
    const std::uint8_t remote_slot) const
{
    const auto iterator = remote_ready_slots_by_tick_.find(execute_tick);
    if (iterator == remote_ready_slots_by_tick_.end()) {
        return false;
    }

    return (iterator->second & (1U << remote_slot)) != 0U;
}

void LockstepSession::note_remote_slot_ready(
    const std::uint64_t execute_tick,
    const std::uint8_t remote_slot)
{
    remote_ready_slots_by_tick_[execute_tick] =
        static_cast<std::uint8_t>(remote_ready_slots_by_tick_[execute_tick] | (1U << remote_slot));
}

bool LockstepSession::has_remote_ready(const std::uint64_t execute_tick) const
{
    const std::uint8_t required_mask = required_remote_slots_mask();
    if (required_mask == 0U) {
        return true;
    }

    const auto iterator = remote_ready_slots_by_tick_.find(execute_tick);
    if (iterator == remote_ready_slots_by_tick_.end()) {
        return false;
    }

    return (iterator->second & required_mask) == required_mask;
}

void LockstepSession::send_reconnect_request()
{
    ReconnectRequestMessage message{};
    message.player_slot = player_slot_;
    message.claim_token =
        player_slot_ < reconnect_tokens_.size() ? reconnect_tokens_[player_slot_] : 0U;

    const std::vector<std::byte> payload = encode_reconnect_request(message);
    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::ReconnectRequest, payload);
    if (transport_.send_reliable(wire_message, constants::CHANNEL_RELIABLE)) {
        reconnect_request_sent_ = true;
        last_reconnect_request_sent_ = std::chrono::steady_clock::now();
    }
}

void LockstepSession::send_resync_ready()
{
    if (role_ == LockstepRole::Client) {
        simulation_.set_player_ai_controlled(player_slot_, false);
    }

    ReconnectRequestMessage message{};
    message.player_slot = player_slot_;
    message.claim_token =
        player_slot_ < reconnect_tokens_.size() ? reconnect_tokens_[player_slot_] : 0U;

    const std::vector<std::byte> payload = encode_reconnect_request(message);
    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::ResyncReady, payload);
    if (transport_.send_reliable(wire_message, constants::CHANNEL_RELIABLE)) {
        resync_ready_sent_ = true;
        last_resync_ready_sent_ = std::chrono::steady_clock::now();
        LockstepDebugLog::log_event(
            "resync_ready_sent",
            "tick=" + std::to_string(simulation_.tick_count()));
    }
}

void LockstepSession::maybe_retry_resync_ready()
{
    if (role_ != LockstepRole::Client || !is_connected() || !session_ready_ || !resync_ready_sent_
        || !awaiting_reconnect_handshake_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto since_restore_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - snapshot_restored_at_);
    if (since_restore_ms.count()
        > static_cast<long long>(constants::LOCKSTEP_RESYNC_READY_RETRY_WINDOW_MS)) {
        return;
    }

    const auto since_last_sent_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_resync_ready_sent_);
    if (since_last_sent_ms.count()
        < static_cast<long long>(constants::LOCKSTEP_RESYNC_READY_RETRY_MS)) {
        return;
    }

    send_resync_ready();
    LockstepDebugLog::log_event("resync_ready_retry", "tick=" + std::to_string(simulation_.tick_count()));
}

void LockstepSession::maybe_retry_host_resync_handshake()
{
    if (role_ != LockstepRole::Host || !is_connected()) {
        return;
    }

    if (session_player_count_ > 2U) {
        if (resync_pending_slots_mask_ == 0U || !should_send_reconnect_snapshot_now()) {
            return;
        }

        for (std::uint8_t slot = 1U; slot < session_player_count_; ++slot) {
            if ((resync_pending_slots_mask_ & (1U << slot)) == 0U) {
                continue;
            }

            send_reconnect_snapshot_to_client(slot, false);
        }

        LockstepDebugLog::log_event(
            "snapshot_retry",
            "tick=" + std::to_string(simulation_.tick_count()));
        return;
    }

    if (!mid_match_reconnect_authorized_ || !awaiting_reconnect_handshake_
        || client_resync_ready_) {
        return;
    }

    if (!should_send_reconnect_snapshot_now()) {
        return;
    }

    send_reconnect_snapshot();
    LockstepDebugLog::log_event(
        "snapshot_retry",
        "tick=" + std::to_string(simulation_.tick_count()));
}

void LockstepSession::maybe_timeout_resync_handshake()
{
    const auto now = std::chrono::steady_clock::now();

    if (role_ == LockstepRole::Client) {
        if (!awaiting_reconnect_handshake_ || !session_ready_) {
            return;
        }

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - snapshot_restored_at_);
        if (elapsed_ms.count()
            < static_cast<long long>(constants::LOCKSTEP_RESYNC_HANDSHAKE_TIMEOUT_MS)) {
            return;
        }

        awaiting_reconnect_handshake_ = false;
        reconnect_request_sent_ = false;
        send_reconnect_request();
        LockstepDebugLog::log_event(
            "resync_handshake_client_timeout",
            "tick=" + std::to_string(simulation_.tick_count()));
        return;
    }

    if (role_ != LockstepRole::Host) {
        return;
    }

    if (session_player_count_ <= 2U) {
        if (!awaiting_reconnect_handshake_ || client_resync_ready_) {
            return;
        }

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - resync_handshake_started_);
        if (elapsed_ms.count()
            < static_cast<long long>(constants::LOCKSTEP_RESYNC_HANDSHAKE_TIMEOUT_MS)) {
            return;
        }

        awaiting_reconnect_handshake_ = false;
        mid_match_reconnect_authorized_ = false;
        sync_status_chat_announced_ = false;
        client_resync_ready_ = false;
        opponent_needs_snapshot_ = false;
        if (!ai_fallback_) {
            note_opponent_transport_down();
        }

        LockstepDebugLog::log_event(
            "resync_handshake_host_timeout",
            "tick=" + std::to_string(simulation_.tick_count()));
        return;
    }

    if (resync_pending_slots_mask_ == 0U) {
        return;
    }

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - resync_handshake_started_);
    if (elapsed_ms.count()
        < static_cast<long long>(constants::LOCKSTEP_RESYNC_HANDSHAKE_TIMEOUT_MS)) {
        return;
    }

    for (std::uint8_t slot = 1U; slot < session_player_count_; ++slot) {
        if ((resync_pending_slots_mask_ & (1U << slot)) == 0U) {
            continue;
        }

        resync_pending_slots_mask_ =
            static_cast<std::uint8_t>(resync_pending_slots_mask_ & ~(1U << slot));
        if ((disconnected_slots_mask_ & (1U << slot)) == 0U) {
            handle_slot_ai_takeover(slot);
            broadcast_slot_ai_takeover(slot);
        }
    }

    reset_tick_sync_state();
    ensure_local_batch_sent(next_execute_tick());
    LockstepDebugLog::log_event(
        "resync_pending_host_timeout",
        "tick=" + std::to_string(simulation_.tick_count()));
}

bool LockstepSession::should_send_reconnect_snapshot_now() const
{
    if (!is_multi_peer_session(session_player_count_) && client_resync_ready_
        && !awaiting_reconnect_handshake_ && !opponent_needs_snapshot_) {
        return false;
    }

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - last_reconnect_snapshot_sent_);
    return elapsed_ms.count()
        >= static_cast<long long>(constants::LOCKSTEP_RECONNECT_SNAPSHOT_DEBOUNCE_MS);
}

void LockstepSession::maybe_retry_reconnect_handshake()
{
    if (role_ != LockstepRole::Client || !is_connected()) {
        return;
    }

    if (session_ready_ && !host_lost_ && !reconnecting_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_reconnect_request_sent_);
    if (elapsed_ms.count()
        < static_cast<long long>(constants::LOCKSTEP_RECONNECT_REQUEST_RETRY_MS)) {
        return;
    }

    reconnect_request_sent_ = false;
    send_reconnect_request();
    LockstepDebugLog::log_event("reconnect_request_retry", "tick=" + std::to_string(simulation_.tick_count()));
}

void LockstepSession::maybe_send_pending_reconnect_snapshot()
{
    if (is_multi_peer_session(session_player_count_)) {
        return;
    }

    if (role_ != LockstepRole::Host || !is_connected() || !opponent_needs_snapshot_) {
        return;
    }

    if (!mid_match_reconnect_authorized_) {
        return;
    }

    if (client_resync_ready_ && !awaiting_reconnect_handshake_ && !opponent_needs_snapshot_) {
        opponent_needs_snapshot_ = false;
        return;
    }

    if (!should_send_reconnect_snapshot_now()) {
        return;
    }

    send_reconnect_snapshot();
    LockstepDebugLog::log_event(
        "snapshot_pending_send",
        "tick=" + std::to_string(simulation_.tick_count()));
}

void LockstepSession::apply_post_join_accepted_host_state()
{
    if (role_ == LockstepRole::Host && session_player_count_ > 2U) {
        session_ready_ = is_lobby_full();
    }
    else {
        session_ready_ = true;
    }

    awaiting_reconnect_handshake_ = false;
    opponent_reconnect_pending_ = false;
    client_resync_ready_ = true;
    hash_verify_warmup_ticks_remaining_ = constants::LOCKSTEP_HASH_VERIFY_WARMUP_TICKS;
    reset_latency_stats();
    simulation_.snapshot_world_positions_for_render();
    publish_render_snapshot_locked();
}

void LockstepSession::send_join_accepted(
    const std::uint8_t player_slot,
    const std::uint8_t enet_client_slot)
{
    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::JoinAccepted, {});

    if (role_ == LockstepRole::Host && session_player_count_ > 2U) {
        (void)transport_.send_reliable_to_client(
            enet_client_slot,
            wire_message,
            constants::CHANNEL_RELIABLE);
    }
    else {
        (void)transport_.send_reliable(wire_message, constants::CHANNEL_RELIABLE);
    }

    (void)player_slot;
    apply_post_join_accepted_host_state();
}

void LockstepSession::send_join_accepted_to_all_connected_clients()
{
    reset_tick_sync_state();

    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::JoinAccepted, {});
    (void)transport_.broadcast_reliable_except(wire_message, constants::CHANNEL_RELIABLE, std::nullopt);

    apply_post_join_accepted_host_state();

    LockstepDebugLog::log_event(
        "lobby_ready",
        "players=" + std::to_string(session_player_count_));
}

void LockstepSession::send_reconnect_snapshot()
{
    if (client_resync_ready_ && !awaiting_reconnect_handshake_ && !opponent_needs_snapshot_) {
        opponent_needs_snapshot_ = false;
        LockstepDebugLog::log_event(
            "snapshot_send_skipped",
            "handshake_complete tick=" + std::to_string(simulation_.tick_count()));
        return;
    }

    if (!should_send_reconnect_snapshot_now()) {
        LockstepDebugLog::log_event(
            "snapshot_send_debounced",
            "tick=" + std::to_string(simulation_.tick_count()));
        return;
    }

    flush_pending_local_commands_to_input_log();

    const std::vector<std::byte> snapshot_bytes = sim::encode_sim_snapshot(simulation_);
    if (snapshot_bytes.empty()) {
        std::cerr << "lockstep: snapshot encode failed at tick " << simulation_.tick_count()
                  << " — client reconnect blocked until retry succeeds\n";
        return;
    }

    if (!validate_snapshot_roundtrip(simulation_, snapshot_bytes)) {
        std::cerr << "lockstep: snapshot roundtrip validation failed at tick "
                  << simulation_.tick_count() << " — not sending to client\n";
        return;
    }

    if (!apply_reconnect_snapshot_locally(snapshot_bytes)) {
        std::cerr << "lockstep: host failed to apply reconnect snapshot locally at tick "
                  << simulation_.tick_count() << " — not sending to client\n";
        return;
    }

    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::ReconnectSnapshot, snapshot_bytes);
    if (!transport_.send_reliable(wire_message, constants::CHANNEL_RELIABLE)) {
        std::cerr << "lockstep: snapshot send failed at tick " << simulation_.tick_count()
                  << " — will retry on next reconnect request\n";
        return;
    }

    session_ready_ = true;
    opponent_reconnect_pending_ = false;
    opponent_needs_snapshot_ = false;
    client_resync_ready_ = false;
    const bool first_handshake_snapshot = !awaiting_reconnect_handshake_;
    awaiting_reconnect_handshake_ = true;
    resync_handshake_started_ = std::chrono::steady_clock::now();
    resync_strict_batch_gate_ = true;
    desynced_ = false;
    desync_tick_ = 0U;
    last_reconnect_snapshot_sent_ = std::chrono::steady_clock::now();

    if (first_handshake_snapshot || !sync_status_chat_announced_) {
        push_system_chat(
            "Synchronizing data with " + player_display_name(opponent_player_slot()) + ".");
        sync_status_chat_announced_ = true;
    }

    LockstepDebugLog::log_event(
        "snapshot_sent",
        "tick=" + std::to_string(simulation_.tick_count()));
}

void LockstepSession::broadcast_slot_ai_takeover(const std::uint8_t player_slot)
{
    ReconnectRequestMessage message{};
    message.player_slot = player_slot;

    const std::vector<std::byte> payload = encode_reconnect_request(message);
    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::SlotAiTakeover, payload);

    for (std::uint8_t client_slot = 1U; client_slot < session_player_count_; ++client_slot) {
        if (client_slot == player_slot) {
            continue;
        }

        (void)transport_.send_reliable_to_client(
            client_slot,
            wire_message,
            constants::CHANNEL_RELIABLE);
    }
}

void LockstepSession::broadcast_slot_ai_resume(const std::uint8_t player_slot)
{
    ReconnectRequestMessage message{};
    message.player_slot = player_slot;

    const std::vector<std::byte> payload = encode_reconnect_request(message);
    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::SlotAiResume, payload);

    // One reliable send per client (avoid duplicate broadcast+directed delivery).
    for (std::uint8_t client_slot = 1U; client_slot < session_player_count_; ++client_slot) {
        (void)transport_.send_reliable_to_client(
            client_slot,
            wire_message,
            constants::CHANNEL_RELIABLE);
    }
}

void LockstepSession::handle_host_player_slot_disconnected(const std::uint8_t player_slot)
{
    if (role_ != LockstepRole::Host || !is_multi_peer_session(session_player_count_) || player_slot == 0U
        || player_slot >= session_player_count_) {
        return;
    }

    // Always clear a half-finished resync so a second disconnect cannot leave the
    // host stuck waiting for ResyncReady that will never arrive.
    const bool was_resync_pending =
        (resync_pending_slots_mask_ & (1U << player_slot)) != 0U;
    resync_pending_slots_mask_ =
        static_cast<std::uint8_t>(resync_pending_slots_mask_ & ~(1U << player_slot));

    if ((disconnected_slots_mask_ & (1U << player_slot)) != 0U) {
        if (was_resync_pending) {
            LockstepDebugLog::log_event(
                "resync_pending_cleared_on_disconnect",
                "player_slot=" + std::to_string(static_cast<int>(player_slot) + 1));
        }
        return;
    }

    handle_slot_ai_takeover(player_slot);
    broadcast_slot_ai_takeover(player_slot);
    // Re-barrier at the current execute tick so peers that were waiting on the
    // disconnected slot can complete without the host running a tick ahead.
    reset_tick_sync_state();
    ensure_local_batch_sent(next_execute_tick());

    push_system_chat(player_display_name(player_slot) + " has disconnected.");
    push_system_chat("AI takes over disconnected player " + player_display_name(player_slot) + ".");

    std::cout << "lockstep: player " << static_cast<int>(player_slot + 1U) << " left at tick "
              << simulation_.tick_count() << " — AI playing, waiting for reconnect...\n";
    LockstepDebugLog::log_event(
        "slot_disconnected",
        "player_slot=" + std::to_string(static_cast<int>(player_slot) + 1) + " tick="
            + std::to_string(simulation_.tick_count()));
}

void LockstepSession::handle_slot_ai_takeover(const std::uint8_t player_slot)
{
    if (player_slot >= session_player_count_) {
        return;
    }

    disconnected_slots_mask_ =
        static_cast<std::uint8_t>(disconnected_slots_mask_ | (1U << player_slot));
    if (ai_command_sequence_by_slot_[player_slot] == 0U) {
        ai_command_sequence_by_slot_[player_slot] = 1U;
    }

    simulation_.set_player_ai_controlled(player_slot, true);
    clear_remote_wait();
    // Drop stale ready-bits for the lost peer and re-advertise our batch for
    // the tick we are still collecting.
    if (const auto iterator = remote_ready_slots_by_tick_.find(next_execute_tick());
        iterator != remote_ready_slots_by_tick_.end()) {
        iterator->second = static_cast<std::uint8_t>(
            iterator->second & ~(1U << player_slot));
    }
    force_resend_local_batch(next_execute_tick());
    LockstepDebugLog::log_event(
        "slot_ai_takeover",
        "player_slot=" + std::to_string(static_cast<int>(player_slot) + 1) + " tick="
            + std::to_string(simulation_.tick_count()));
}

void LockstepSession::force_resend_local_batch(const std::uint64_t execute_tick)
{
    local_sent_ticks_.erase(execute_tick);
    ensure_local_batch_sent(execute_tick);
}

void LockstepSession::handle_slot_ai_resume(const std::uint8_t player_slot)
{
    if (player_slot >= session_player_count_) {
        return;
    }

    disconnected_slots_mask_ =
        static_cast<std::uint8_t>(disconnected_slots_mask_ & ~(1U << player_slot));
    resync_pending_slots_mask_ =
        static_cast<std::uint8_t>(resync_pending_slots_mask_ & ~(1U << player_slot));
    simulation_.set_player_ai_controlled(player_slot, false);

    if (player_slot == player_slot_) {
        // Reconnecting local player: full barrier reset after snapshot apply.
        awaiting_reconnect_handshake_ = false;
        resync_strict_batch_gate_ = true;
        hash_verify_warmup_ticks_remaining_ = constants::LOCKSTEP_HASH_VERIFY_WARMUP_TICKS;
        reset_tick_sync_state();
        ensure_local_batch_sent(next_execute_tick());
    }
    else {
        // Bystanders must NOT wipe remote_ready — that desyncs the live barrier.
        // Only re-send our current batch so the catching-up peer can receive it.
        force_resend_local_batch(next_execute_tick());
    }

    LockstepDebugLog::log_event(
        "slot_ai_resume",
        "player_slot=" + std::to_string(static_cast<int>(player_slot) + 1) + " tick="
            + std::to_string(simulation_.tick_count()));
}

void LockstepSession::send_reconnect_snapshot_to_client(
    const std::uint8_t player_slot,
    const bool force_immediate)
{
    if (role_ != LockstepRole::Host || player_slot == 0U || player_slot >= session_player_count_) {
        return;
    }

    if (!force_immediate && !should_send_reconnect_snapshot_now()) {
        LockstepDebugLog::log_event(
            "snapshot_send_debounced",
            "player_slot=" + std::to_string(static_cast<int>(player_slot) + 1) + " tick="
                + std::to_string(simulation_.tick_count()));
        return;
    }

    flush_pending_local_commands_to_input_log();

    const std::vector<std::byte> snapshot_bytes = sim::encode_sim_snapshot(simulation_);
    if (snapshot_bytes.empty()) {
        std::cerr << "lockstep: snapshot encode failed at tick " << simulation_.tick_count()
                  << " — client reconnect blocked until retry succeeds\n";
        return;
    }

    if (!validate_snapshot_roundtrip(simulation_, snapshot_bytes)) {
        std::cerr << "lockstep: snapshot roundtrip validation failed at tick "
                  << simulation_.tick_count() << " — not sending to client\n";
        return;
    }

    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::ReconnectSnapshot, snapshot_bytes);
    if (!transport_.send_reliable_to_client(player_slot, wire_message, constants::CHANNEL_RELIABLE)) {
        std::cerr << "lockstep: snapshot send failed for player "
                  << static_cast<int>(player_slot + 1U) << " at tick " << simulation_.tick_count()
                  << " — will retry on next reconnect request\n";
        return;
    }

    resync_pending_slots_mask_ =
        static_cast<std::uint8_t>(resync_pending_slots_mask_ | (1U << player_slot));
    last_reconnect_snapshot_sent_ = std::chrono::steady_clock::now();
    if (resync_handshake_started_.time_since_epoch().count() == 0
        || (resync_pending_slots_mask_ & (1U << player_slot)) != 0U) {
        resync_handshake_started_ = last_reconnect_snapshot_sent_;
    }

    LockstepDebugLog::log_event(
        "snapshot_sent",
        "player_slot=" + std::to_string(static_cast<int>(player_slot) + 1) + " tick="
            + std::to_string(simulation_.tick_count()));
}

void LockstepSession::broadcast_reconnect_snapshot_to_all_clients()
{
    if (role_ != LockstepRole::Host || !is_multi_peer_session(session_player_count_)) {
        return;
    }

    flush_pending_local_commands_to_input_log();

    const std::vector<std::byte> snapshot_bytes = sim::encode_sim_snapshot(simulation_);
    if (snapshot_bytes.empty()) {
        std::cerr << "lockstep: catch-up snapshot encode failed at tick "
                  << simulation_.tick_count() << '\n';
        return;
    }

    if (!validate_snapshot_roundtrip(simulation_, snapshot_bytes)) {
        std::cerr << "lockstep: catch-up snapshot roundtrip validation failed at tick "
                  << simulation_.tick_count() << '\n';
        return;
    }

    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::ReconnectSnapshot, snapshot_bytes);

    for (std::uint8_t client_slot = 1U; client_slot < session_player_count_; ++client_slot) {
        if ((registered_clients_mask_ & (1U << client_slot)) == 0U) {
            continue;
        }

        (void)transport_.send_reliable_to_client(
            client_slot,
            wire_message,
            constants::CHANNEL_RELIABLE);
    }

    // Host must share the exact restored pending/cooldown state clients get.
    // Live host pending + unsnapshotted fields otherwise diverge after warmup.
    if (!apply_reconnect_snapshot_locally(snapshot_bytes)) {
        std::cerr << "lockstep: host failed to self-apply catch-up snapshot at tick "
                  << simulation_.tick_count() << '\n';
        return;
    }

    last_reconnect_snapshot_sent_ = std::chrono::steady_clock::now();
    {
        std::ostringstream detail{};
        detail << "tick=" << simulation_.tick_count() << " hash=0x" << std::hex
               << simulation_.state_hash() << std::dec;
        LockstepDebugLog::log_event("catchup_snapshot_broadcast", detail.str());
    }
}

void LockstepSession::handle_peer_connected()
{
    if (role_ != LockstepRole::Host) {
        return;
    }

    note_peer_activity();
    if (match_started_ && simulation_.tick_count() > 0U && session_player_count_ <= 2U) {
        reset_tick_sync_state();
    }

    desynced_ = false;
    desync_tick_ = 0U;
    reset_latency_stats();

    if (match_started_ && simulation_.tick_count() > 0U) {
        if (session_player_count_ <= 2U) {
            // Wait for ReconnectRequest + claim token before snapshotting.
            // Bare ENet connects (lobby Join to a live match) must not arm the handshake.
            opponent_needs_snapshot_ = true;
            client_resync_ready_ = false;
            awaiting_reconnect_handshake_ = false;
            mid_match_reconnect_authorized_ = false;
            LockstepDebugLog::log_event(
                "peer_connected_awaiting_reconnect_request",
                "tick=" + std::to_string(simulation_.tick_count()));
        }
    }
    else {
        awaiting_reconnect_handshake_ = false;
        resync_strict_batch_gate_ = false;
    }

    if (opponent_reconnect_pending_ || ai_fallback_) {
        begin_opponent_reconnect_grace();
    }

    simulation_.snapshot_world_positions_for_render();
    publish_render_snapshot_locked();

    LockstepDebugLog::log_event(
        "peer_connected_reset",
        "tick=" + std::to_string(simulation_.tick_count()));
}

void LockstepSession::handle_resync_ready(const std::uint8_t player_slot)
{
    if (role_ != LockstepRole::Host) {
        return;
    }

    if (player_slot >= session_player_count_ || player_slot == player_slot_) {
        return;
    }

    if (session_player_count_ == 2U && player_slot != opponent_player_slot()) {
        return;
    }

    if (session_player_count_ > 2U) {
        if ((resync_pending_slots_mask_ & (1U << player_slot)) == 0U) {
            LockstepDebugLog::log_event(
                "resync_ready_unexpected",
                "player_slot=" + std::to_string(static_cast<int>(player_slot) + 1) + " tick="
                    + std::to_string(simulation_.tick_count()));
            return;
        }

        // Keep resync_pending set until broadcasts are queued so try_advance
        // stays paused (avoids host AI ticks vs reconnecting human ticks).
        resume_player_control(player_slot);
        disconnected_slots_mask_ =
            static_cast<std::uint8_t>(disconnected_slots_mask_ & ~(1U << player_slot));

        // Resume AI flags on peers BEFORE catch-up snapshot so nobody simulates
        // the reconnecting slot as AI while the host treats them as human.
        broadcast_slot_ai_resume(player_slot);
        broadcast_reconnect_snapshot_to_all_clients();

        resync_pending_slots_mask_ =
            static_cast<std::uint8_t>(resync_pending_slots_mask_ & ~(1U << player_slot));

        hash_verify_warmup_ticks_remaining_ = constants::LOCKSTEP_HASH_VERIFY_WARMUP_TICKS;
        reset_tick_sync_state();
        ensure_local_batch_sent(next_execute_tick());

        LockstepDebugLog::log_event(
            "resync_ready_received",
            "player_slot=" + std::to_string(static_cast<int>(player_slot) + 1) + " tick="
                + std::to_string(simulation_.tick_count()));
        return;
    }

    if (client_resync_ready_ && !awaiting_reconnect_handshake_) {
        LockstepDebugLog::log_event(
            "resync_ready_duplicate_ignored",
            "tick=" + std::to_string(simulation_.tick_count()));
        return;
    }

    client_resync_ready_ = true;
    awaiting_reconnect_handshake_ = false;
    mid_match_reconnect_authorized_ = false;
    sync_status_chat_announced_ = false;
    opponent_reconnect_pending_ = false;
    opponent_needs_snapshot_ = false;
    desynced_ = false;
    desync_tick_ = 0U;
    resync_strict_batch_gate_ = true;
    hash_verify_warmup_ticks_remaining_ = constants::LOCKSTEP_HASH_VERIFY_WARMUP_TICKS;
    reset_tick_sync_state();
    resume_player_control(player_slot);
    begin_opponent_reconnect_grace();
    ensure_local_batch_sent(next_execute_tick());

    LockstepDebugLog::log_event(
        "resync_ready_received",
        "tick=" + std::to_string(simulation_.tick_count()));
}

bool LockstepSession::should_send_reconnect_snapshot() const
{
    return ai_fallback_ || opponent_needs_snapshot_;
}

void LockstepSession::handle_reconnect_request(
    const std::uint8_t player_slot,
    const std::uint8_t enet_client_slot,
    const std::uint64_t claim_token)
{
    if (role_ != LockstepRole::Host) {
        return;
    }

    if (player_slot >= session_player_count_ || player_slot == player_slot_) {
        return;
    }

    if (session_player_count_ == 2U && player_slot != opponent_player_slot()) {
        return;
    }

    const bool mid_match_reconnect =
        match_started_ && simulation_.tick_count() > 0U;

    if (mid_match_reconnect && !claim_token_accepted(player_slot, claim_token)) {
        LockstepDebugLog::log_event(
            "reconnect_request_rejected",
            "player_slot=" + std::to_string(static_cast<int>(player_slot) + 1)
                + " reason=claim_token");
        return;
    }

    if (is_multi_peer_session(session_player_count_) && mid_match_reconnect) {
        if (!transport_.rebind_client_to_player_slot(enet_client_slot, player_slot)) {
            LockstepDebugLog::log_event(
                "reconnect_request_rejected",
                "player_slot=" + std::to_string(static_cast<int>(player_slot) + 1) + " enet_slot="
                    + std::to_string(enet_client_slot) + " rebind_failed");
            return;
        }

        registered_clients_mask_ =
            static_cast<std::uint8_t>(registered_clients_mask_ | (1U << player_slot));

        const bool slot_disconnected =
            (disconnected_slots_mask_ & (1U << player_slot)) != 0U;
        const bool slot_pending_resync =
            (resync_pending_slots_mask_ & (1U << player_slot)) != 0U;
        const bool slot_ai_controlled = simulation_.is_player_ai_controlled(player_slot);

        if (!slot_disconnected && !slot_ai_controlled && !slot_pending_resync) {
            // Player already live after a completed resync — ignore duplicates.
            LockstepDebugLog::log_event(
                "reconnect_request_ignored_already_live",
                "player_slot=" + std::to_string(static_cast<int>(player_slot) + 1));
            return;
        }

        if (!slot_pending_resync) {
            push_system_chat(player_display_name(player_slot) + " is connecting.");
            push_system_chat("Synchronizing data with " + player_display_name(player_slot) + ".");
        }
        send_reconnect_snapshot_to_client(player_slot, true);
        return;
    }

    if (is_multi_peer_session(session_player_count_)) {
        if ((registered_clients_mask_ & (1U << player_slot)) != 0U) {
            if (is_lobby_full() && !match_started_) {
                send_join_accepted(player_slot, player_slot);
            }

            return;
        }

        if (!transport_.rebind_client_to_player_slot(enet_client_slot, player_slot)) {
            LockstepDebugLog::log_event(
                "reconnect_request_rejected",
                "player_slot=" + std::to_string(player_slot) + " enet_slot="
                    + std::to_string(enet_client_slot) + " rebind_failed");
            return;
        }

        registered_clients_mask_ =
            static_cast<std::uint8_t>(registered_clients_mask_ | (1U << player_slot));

        if (!is_lobby_full()) {
            LockstepDebugLog::log_event(
                "join_waiting_for_lobby",
                "registered=" + std::to_string(lobby_registered_client_count()) + "/"
                    + std::to_string(expected_human_player_count() > 1U
                        ? expected_human_player_count() - 1U
                        : 0U));
            return;
        }

        send_join_accepted_to_all_connected_clients();
        return;
    }

    if (mid_match_reconnect) {
        mid_match_reconnect_authorized_ = true;
    }

    const bool reconnect_in_progress =
        ai_fallback_ || opponent_needs_snapshot_ || awaiting_reconnect_handshake_
        || !client_resync_ready_;

    if (mid_match_reconnect && reconnect_in_progress) {
        opponent_needs_snapshot_ = true;
        if (client_resync_ready_ && !awaiting_reconnect_handshake_) {
            client_resync_ready_ = false;
        }
    }
    else if (mid_match_reconnect && client_resync_ready_ && !awaiting_reconnect_handshake_
        && !opponent_needs_snapshot_ && !ai_fallback_) {
        return;
    }

    if (awaiting_reconnect_handshake_ && !client_resync_ready_) {
        if (mid_match_reconnect_authorized_ && should_send_reconnect_snapshot_now()) {
            send_reconnect_snapshot();
        }
        return;
    }

    if (mid_match_reconnect || should_send_reconnect_snapshot()) {
        send_reconnect_snapshot();
        return;
    }

    opponent_reconnect_pending_ = false;
    send_join_accepted(player_slot, enet_client_slot);
}

void LockstepSession::handle_reconnect_snapshot(const std::vector<std::byte>& payload)
{
    if (role_ != LockstepRole::Client) {
        return;
    }

    const std::uint64_t tick_before = simulation_.tick_count();
    const std::uint64_t hash_before = simulation_.state_hash();
    // After the first restore we set awaiting_handshake but clear reconnecting_.
    // A follow-up host catch-up broadcast must NOT look like a fresh reconnect
    // (that would spam ResyncReady and wipe the barrier again).
    // connect() clears session_ready_ even when host_lost_/reconnecting_ were never
    // set (e.g. local disconnect_transport in smoke) — treat that as reconnect too.
    const bool actively_reconnecting =
        reconnecting_ || host_lost_ || !match_started_ || !session_ready_;

    if (!apply_reconnect_snapshot_locally(payload)) {
        session_ready_ = false;
        reconnect_request_sent_ = false;
        std::cerr << "lockstep reconnect: failed to apply snapshot — will retry on next request\n";
        push_system_chat("Failed to apply match snapshot. Retrying...");
        return;
    }

    const bool state_changed = tick_before != simulation_.tick_count()
        || hash_before != simulation_.state_hash();

    // Exact duplicate of an already-applied live snapshot — ignore.
    if (!actively_reconnecting && !state_changed && session_ready_ && match_started_
        && resync_ready_sent_) {
        LockstepDebugLog::log_event(
            "snapshot_ignored_duplicate",
            "tick=" + std::to_string(simulation_.tick_count()));
        force_resend_local_batch(next_execute_tick());
        return;
    }

    ai_fallback_ = false;
    host_lost_ = false;
    reconnecting_ = false;
    host_gone_ = false;
    reconnect_request_sent_ = true;
    match_started_ = true;
    session_ready_ = true;
    opponent_reconnect_pending_ = false;
    reset_tick_sync_state();
    transport_.discard_pending_received();
    resync_strict_batch_gate_ = true;
    begin_host_reconnect_grace();

    desynced_ = false;
    desync_tick_ = 0U;
    hash_verify_warmup_ticks_remaining_ = constants::LOCKSTEP_HASH_VERIFY_WARMUP_TICKS;

    snapshot_restored_pending_ = true;
    snapshot_restored_at_ = std::chrono::steady_clock::now();

    {
        std::ostringstream detail{};
        detail << "tick=" << simulation_.tick_count() << " hash=0x" << std::hex
               << simulation_.state_hash() << std::dec
               << (actively_reconnecting ? " role=reconnect" : " role=catchup");
        LockstepDebugLog::log_event("snapshot_restored", detail.str());
    }

    // Fresh reconnect OR host resent a snapshot before seeing ResyncReady —
    // keep acknowledging so the host is not stuck retrying forever.
    if (actively_reconnecting || awaiting_reconnect_handshake_) {
        awaiting_reconnect_handshake_ = true;
        resync_ready_sent_ = false;
        if (actively_reconnecting && !sync_status_chat_announced_) {
            push_system_chat("Synchronizing data with " + player_display_name(0U) + ".");
            sync_status_chat_announced_ = true;
        }
        send_resync_ready();
        std::cout << "lockstep reconnect: restored tick " << simulation_.tick_count() << " hash=0x"
                  << std::hex << simulation_.state_hash() << std::dec << '\n';
        return;
    }

    // Bystander catch-up: align to host tick without starting a new handshake.
    // SlotAiResume is broadcast before this snapshot so AI flags should already
    // match the host for the reconnecting slot.
    awaiting_reconnect_handshake_ = false;
    ensure_local_batch_sent(next_execute_tick());
    LockstepDebugLog::log_event(
        "catchup_snapshot_applied",
        "tick=" + std::to_string(simulation_.tick_count()));
}

void LockstepSession::handle_join_accepted()
{
    host_lost_ = false;
    reconnecting_ = false;
    host_gone_ = false;
    reconnect_request_sent_ = true;
    session_ready_ = true;
    reset_latency_stats();
    hash_verify_warmup_ticks_remaining_ = constants::LOCKSTEP_HASH_VERIFY_WARMUP_TICKS;
    simulation_.snapshot_world_positions_for_render();
    publish_render_snapshot_locked();
    begin_host_reconnect_grace();

    std::cout << "lockstep-join: joined at tick " << simulation_.tick_count() << '\n';
    LockstepDebugLog::log_event("join_accepted", "tick=" + std::to_string(simulation_.tick_count()));
}

void LockstepSession::handle_opponent_lost()
{
    if (!match_started_ || host_lost_ || ai_fallback_) {
        return;
    }

    if (role_ == LockstepRole::Host) {
        if (!opponent_reconnect_pending_ && !ai_fallback_) {
            note_opponent_transport_down();
        }

        return;
    }

    enter_host_lost();
}

void LockstepSession::enter_host_lost()
{
    if (host_lost_) {
        return;
    }

    host_lost_ = true;
    reconnecting_ = true;
    pending_host_lost_during_grace_ = false;
    resync_ready_sent_ = false;
    reconnect_request_sent_ = false;
    reconnect_attempts_ = 0;
    last_reconnect_try_ = std::chrono::steady_clock::now()
        - std::chrono::milliseconds(constants::LOCKSTEP_RECONNECT_INTERVAL_MS);
    reset_tick_sync_state();

    std::cout << "lockstep: disconnected from host at tick " << simulation_.tick_count()
              << ", reconnecting...\n";
}

void LockstepSession::try_reconnect()
{
    if (reconnect_host_.empty() || reconnect_port_ == 0U) {
        host_gone_ = true;
        reconnecting_ = false;
        return;
    }

    if (is_connected()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_reconnect_try_);

    // An in-flight connect still owns a peer — wait for CONNECTED, peer_lost, or timeout.
    if (transport_.has_peer()) {
        if (elapsed_ms.count()
            < static_cast<long long>(constants::LOCKSTEP_RECONNECT_CONNECT_TIMEOUT_MS)) {
            return;
        }

        transport_.disconnect();
    }

    if (elapsed_ms.count()
        < static_cast<long long>(constants::LOCKSTEP_RECONNECT_INTERVAL_MS)) {
        return;
    }

    last_reconnect_try_ = now;
    ++reconnect_attempts_;

    if (reconnect_attempts_ > constants::LOCKSTEP_RECONNECT_MAX_ATTEMPTS) {
        host_gone_ = true;
        reconnecting_ = false;
        push_system_chat("Host has left the game. Continuation is impossible.");
        std::cout << "lockstep: host left the game (reconnect failed)\n";
        return;
    }

    transport_.disconnect();
    reconnect_request_sent_ = false;
    session_ready_ = false;

    if (!transport_.connect(reconnect_host_.c_str(), reconnect_port_)) {
        return;
    }
}

void LockstepSession::enter_ai_fallback()
{
    if (ai_fallback_) {
        return;
    }

    ai_controlled_slot_ = opponent_player_slot();
    ai_fallback_ = true;
    simulation_.set_player_ai_controlled(ai_controlled_slot_, true);
    reset_tick_sync_state();
    session_ready_ = true;
    debug_tick_accumulator_ = 0.0;
    hash_verify_warmup_ticks_remaining_ = 0;

    std::cout << "lockstep: AI takeover for player " << static_cast<int>(ai_controlled_slot_ + 1U)
              << " at tick " << simulation_.tick_count() << '\n';
    LockstepDebugLog::log_event(
        "ai_takeover",
        "slot=" + std::to_string(ai_controlled_slot_) + " tick=" + std::to_string(simulation_.tick_count()));

    if (role_ == LockstepRole::Host && transport_.is_connected()
        && !is_opponent_reconnect_grace_active() && !opponent_reconnect_pending_
        && session_player_count_ == 2U) {
        transport_.disconnect_peer();
    }
}

void LockstepSession::resume_player_control(const std::uint8_t player_slot)
{
    simulation_.set_player_ai_controlled(player_slot, false);

    if (ai_fallback_ && ai_controlled_slot_ == player_slot) {
        ai_fallback_ = false;
    }

    opponent_reconnect_pending_ = false;

    if (role_ == LockstepRole::Host) {
        begin_opponent_reconnect_grace();
    }
}

void LockstepSession::check_opponent_reconnect_timeout()
{
    if (role_ != LockstepRole::Host || !opponent_reconnect_pending_ || ai_fallback_) {
        return;
    }

    if (is_connected()) {
        return;
    }

    if (is_opponent_reconnect_grace_active()) {
        return;
    }

    opponent_reconnect_pending_ = false;
    enter_ai_fallback();
}

void LockstepSession::begin_opponent_reconnect_grace()
{
    opponent_reconnect_grace_until_ = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(constants::LOCKSTEP_RECONNECT_GRACE_MS);
    clear_remote_wait();
}

bool LockstepSession::is_opponent_reconnect_grace_active() const
{
    if (role_ != LockstepRole::Host) {
        return false;
    }

    return std::chrono::steady_clock::now() < opponent_reconnect_grace_until_;
}

void LockstepSession::begin_host_reconnect_grace()
{
    if (role_ != LockstepRole::Client) {
        return;
    }

    host_reconnect_grace_until_ = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(constants::LOCKSTEP_JOIN_HANDSHAKE_GRACE_MS);
}

bool LockstepSession::is_host_reconnect_grace_active() const
{
    if (role_ != LockstepRole::Client) {
        return false;
    }

    return std::chrono::steady_clock::now() < host_reconnect_grace_until_;
}

bool LockstepSession::should_declare_opponent_disconnected() const
{
    if (role_ == LockstepRole::Client) {
        if (is_connected()) {
            return false;
        }

        if (is_host_reconnect_grace_active()) {
            return false;
        }

        return remote_batch_received_ever_;
    }

    return is_opponent_unresponsive();
}

void LockstepSession::reset_tick_sync_state()
{
    local_sent_ticks_.clear();
    remote_ready_slots_by_tick_.clear();
    local_outbox_.clear();
    clear_state_hash_tracking();
    remote_batch_received_ever_ = false;
    clear_remote_wait();
}

bool LockstepSession::is_remote_input_batch_current(const TickInputBatch& batch) const
{
    const std::uint64_t expected_execute_tick = next_execute_tick();

    if (batch.execute_tick < expected_execute_tick) {
        return false;
    }

    const std::uint64_t max_buffered_execute_tick = expected_execute_tick
        + static_cast<std::uint64_t>(constants::LOCKSTEP_BATCH_BUFFER_TICKS);

    if (resync_strict_batch_gate_ && hash_verify_warmup_ticks_remaining_ <= 0) {
        return batch.execute_tick <= max_buffered_execute_tick;
    }

    return batch.execute_tick <= max_buffered_execute_tick;
}

void LockstepSession::clear_remote_wait()
{
    waiting_remote_execute_tick_ = 0U;
    waiting_remote_active_ = false;
}

void LockstepSession::inject_ai_commands(const std::uint64_t execute_tick)
{
    if (!simulation_.is_player_ai_controlled(ai_controlled_slot_)) {
        return;
    }

    if (!ai_should_think_this_tick(simulation_.tick_count(), ai_controlled_slot_)) {
        return;
    }

    const std::vector<sim::player::PlayerCommand> ai_commands = sim::systems::generate_ai_commands_for_slot(
        simulation_.registry(),
        ai_controlled_slot_,
        execute_tick,
        ai_command_sequence_);

    for (sim::player::PlayerCommand command : ai_commands) {
        sim::snapshot::annotate_command_entity_keys(simulation_.registry(), command);
        simulation_.enqueue_network_command(std::move(command));
    }
}

void LockstepSession::inject_ai_commands_for_disconnected_slots(const std::uint64_t execute_tick)
{
    const std::uint8_t ai_playing_mask =
        static_cast<std::uint8_t>(disconnected_slots_mask_ | resync_pending_slots_mask_);

    for (std::uint8_t slot = 0U; slot < session_player_count_; ++slot) {
        if (slot == player_slot_) {
            continue;
        }

        const bool lobby_ai = slot < slot_is_ai_.size() && slot_is_ai_[slot];
        const bool disconnect_ai = (ai_playing_mask & (1U << slot)) != 0U;
        if (!lobby_ai && !disconnect_ai) {
            continue;
        }

        if (disconnect_ai && !simulation_.is_player_ai_controlled(slot)) {
            simulation_.set_player_ai_controlled(slot, true);
        }

        if (!ai_should_think_this_tick(simulation_.tick_count(), slot)) {
            continue;
        }

        if (ai_command_sequence_by_slot_[slot] == 0U) {
            ai_command_sequence_by_slot_[slot] = 1U;
        }

        const std::vector<sim::player::PlayerCommand> ai_commands =
            sim::systems::generate_ai_commands_for_slot(
                simulation_.registry(),
                slot,
                execute_tick,
                ai_command_sequence_by_slot_[slot]);

        for (sim::player::PlayerCommand command : ai_commands) {
            sim::snapshot::annotate_command_entity_keys(simulation_.registry(), command);
            simulation_.enqueue_network_command(std::move(command));
        }
    }
}

bool LockstepSession::send_input_batch(
    const std::uint64_t execute_tick,
    const std::vector<sim::player::PlayerCommand>& commands)
{
    TickInputBatch batch{};
    batch.execute_tick = execute_tick;
    batch.player_slot = player_slot_;
    batch.commands = commands;

    const std::vector<std::byte> payload = encode_tick_input_batch(batch);
    if (payload.empty()) {
        desynced_ = true;
        desync_tick_ = execute_tick;
        return false;
    }

    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::TickInputBatch, payload);
    if (role_ == LockstepRole::Host && session_player_count_ > 2U) {
        if (!transport_.broadcast_reliable_except(wire_message, constants::CHANNEL_RELIABLE, std::nullopt)) {
            LockstepDebugLog::log_event(
                "batch_send_failed",
                "execute_tick=" + std::to_string(execute_tick));
            if (match_started_ && is_opponent_unresponsive()) {
                note_opponent_transport_down();
                return false;
            }

            if (match_started_) {
                return false;
            }

            desynced_ = true;
            desync_tick_ = execute_tick;
            return false;
        }

        return true;
    }

    if (!transport_.send_reliable(wire_message, constants::CHANNEL_RELIABLE)) {
        LockstepDebugLog::log_event(
            "batch_send_failed",
            "execute_tick=" + std::to_string(execute_tick));
        if (match_started_ && is_opponent_unresponsive()) {
            note_opponent_transport_down();
            return false;
        }

        if (match_started_) {
            return false;
        }

        desynced_ = true;
        desync_tick_ = execute_tick;
        return false;
    }

    return true;
}

void LockstepSession::send_state_hash(const std::uint64_t execute_tick, const std::uint64_t state_hash)
{
    TickStateHashMessage message{};
    message.execute_tick = execute_tick;
    message.player_slot = player_slot_;
    message.state_hash = state_hash;

    const std::vector<std::byte> payload = encode_tick_state_hash(message);
    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::TickStateHash, payload);
    if (role_ == LockstepRole::Host && session_player_count_ > 2U) {
        (void)transport_.broadcast_unreliable_except(wire_message, constants::CHANNEL_UNRELIABLE, std::nullopt);
        return;
    }

    (void)transport_.send_unreliable(wire_message, constants::CHANNEL_UNRELIABLE);
}

void LockstepSession::ensure_local_batch_sent(const std::uint64_t execute_tick)
{
    if (has_local_sent(execute_tick)) {
        return;
    }

    const std::vector<sim::player::PlayerCommand> empty_batch{};
    const auto iterator = local_outbox_.find(execute_tick);
    const std::vector<sim::player::PlayerCommand>& commands =
        iterator != local_outbox_.end() ? iterator->second : empty_batch;

    if (!send_input_batch(execute_tick, commands)) {
        return;
    }

    local_sent_ticks_.insert(execute_tick);
    LockstepDebugLog::log_event(
        "batch_sent",
        "execute_tick=" + std::to_string(execute_tick) + " commands=" + std::to_string(commands.size()));

    // Only the current barrier tick arms resend timing — pipeline-ahead sends must not.
    if ((match_started_ || (session_ready_ && session_player_count_ > 1U))
        && execute_tick == next_execute_tick()) {
        waiting_remote_execute_tick_ = execute_tick;
        waiting_remote_active_ = true;
        waiting_remote_since_ = std::chrono::steady_clock::now();
        last_batch_resend_time_ = waiting_remote_since_;
    }
}

void LockstepSession::ensure_local_batches_pipelined()
{
    const std::uint64_t base_execute_tick = next_execute_tick();
    for (int offset = 0; offset < constants::LOCKSTEP_BATCH_PIPELINE_TICKS; ++offset) {
        ensure_local_batch_sent(base_execute_tick + static_cast<std::uint64_t>(offset));
    }
}

void LockstepSession::maybe_resend_local_batch(const std::uint64_t execute_tick)
{
    const auto now = std::chrono::steady_clock::now();

    // Always treat a blocked barrier tick as waiting, even if pipeline/send order
    // failed to arm waiting_remote_* (that previously prevented resends forever).
    if (!waiting_remote_active_ || waiting_remote_execute_tick_ != execute_tick) {
        waiting_remote_execute_tick_ = execute_tick;
        waiting_remote_active_ = true;
        waiting_remote_since_ = now;
        last_batch_resend_time_ = now;
        return;
    }

    const auto waiting_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - waiting_remote_since_).count();
    if (waiting_ms < static_cast<long long>(constants::LOCKSTEP_BATCH_RESEND_MS)) {
        return;
    }

    const auto since_resend_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_batch_resend_time_).count();
    if (since_resend_ms < static_cast<long long>(constants::LOCKSTEP_BATCH_RESEND_MS)) {
        return;
    }

    // Resend the whole pipeline window. Ahead ticks can be dropped when a peer is
    // briefly behind; without this they stay "sent" locally and never recover.
    for (int offset = 0; offset < constants::LOCKSTEP_BATCH_PIPELINE_TICKS; ++offset) {
        const std::uint64_t pipeline_tick =
            execute_tick + static_cast<std::uint64_t>(offset);
        local_sent_ticks_.erase(pipeline_tick);
        ensure_local_batch_sent(pipeline_tick);
    }
    last_batch_resend_time_ = now;

    LockstepDebugLog::log_event(
        "batch_resent",
        "execute_tick=" + std::to_string(execute_tick)
            + " pipeline=" + std::to_string(constants::LOCKSTEP_BATCH_PIPELINE_TICKS));
}

void LockstepSession::flush_local_commands_for_tick(const std::uint64_t execute_tick)
{
    const auto iterator = local_outbox_.find(execute_tick);
    if (iterator == local_outbox_.end()) {
        return;
    }

    for (sim::player::PlayerCommand& command : iterator->second) {
        sim::snapshot::resolve_command_entity_ids(simulation_.registry(), command);
        simulation_.enqueue_network_command(std::move(command));
    }

    local_outbox_.erase(iterator);
}

void LockstepSession::flush_pending_local_commands_to_input_log()
{
    for (auto& [execute_tick, commands] : local_outbox_) {
        (void)execute_tick;
        for (sim::player::PlayerCommand& command : commands) {
            sim::snapshot::resolve_command_entity_ids(simulation_.registry(), command);
            simulation_.enqueue_network_command(std::move(command));
        }
    }

    local_outbox_.clear();
}

void LockstepSession::process_received_packet(
    const std::vector<std::byte>& packet,
    const std::uint8_t sender_slot)
{
    const auto decoded_message = decode_net_message(packet);
    if (!decoded_message.has_value()) {
        return;
    }

    note_peer_activity();

    // Menu Connect while a match is live: returning players send LobbyJoin.
    // Accept known reclaimable slots by handing them LobbyState + MatchStart so they
    // re-enter lockstep and send ReconnectRequest with their claim token.
    if (role_ == LockstepRole::Host && match_started_
        && decoded_message->first == NetMessageKind::LobbyJoin) {
        const auto join = decode_lobby_join(decoded_message->second);
        if (join.has_value()
            && join->version == aoa::constants::GAME_VERSION
            && try_accept_mid_match_lobby_rejoin(sender_slot, join->name)) {
            return;
        }

        LockstepDebugLog::log_event(
            "lobby_message_rejected_mid_match",
            "kind=" + std::to_string(static_cast<int>(decoded_message->first)) + " sender_slot="
                + std::to_string(static_cast<int>(sender_slot) + 1));
        if (sender_slot != 0U) {
            transport_.disconnect_peer_slot(sender_slot);
        }
        else {
            transport_.disconnect_peer();
        }
        return;
    }

    if (role_ == LockstepRole::Host && match_started_
        && (decoded_message->first == NetMessageKind::LobbyReady
            || decoded_message->first == NetMessageKind::LobbyLeave)) {
        // Ignore leftover lobby chatter from a rejoin hand-off; do not kick.
        return;
    }

    if (decoded_message->first == NetMessageKind::ReconnectRequest) {
        const auto message = decode_reconnect_request(decoded_message->second);
        if (!message.has_value()) {
            return;
        }

        handle_reconnect_request(message->player_slot, sender_slot, message->claim_token);
        return;
    }

    if (decoded_message->first == NetMessageKind::ReconnectSnapshot) {
        handle_reconnect_snapshot(decoded_message->second);
        return;
    }

    if (decoded_message->first == NetMessageKind::JoinAccepted) {
        handle_join_accepted();
        return;
    }

    if (decoded_message->first == NetMessageKind::ResyncReady) {
        const auto message = decode_reconnect_request(decoded_message->second);
        if (!message.has_value()) {
            return;
        }

        handle_resync_ready(message->player_slot);
        return;
    }

    if (decoded_message->first == NetMessageKind::SlotAiTakeover) {
        const auto message = decode_reconnect_request(decoded_message->second);
        if (!message.has_value()) {
            return;
        }

        handle_slot_ai_takeover(message->player_slot);
        return;
    }

    if (decoded_message->first == NetMessageKind::SlotAiResume) {
        const auto message = decode_reconnect_request(decoded_message->second);
        if (!message.has_value()) {
            return;
        }

        handle_slot_ai_resume(message->player_slot);
        return;
    }

    if (decoded_message->first == NetMessageKind::Chat) {
        handle_chat_message(decoded_message->second, sender_slot);
        return;
    }

    if (desynced_) {
        return;
    }

    if (decoded_message->first == NetMessageKind::TickInputBatch) {
        if (role_ == LockstepRole::Host && match_started_ && simulation_.tick_count() > 0U
            && session_player_count_ <= 2U && !client_resync_ready_) {
            LockstepDebugLog::log_event("batch_ignored", "waiting_resync_ready");
            return;
        }

        if (awaiting_reconnect_handshake_ && role_ == LockstepRole::Host
            && session_player_count_ <= 2U) {
            LockstepDebugLog::log_event("batch_ignored", "awaiting_handshake");
            return;
        }

        if (!session_ready_ && role_ == LockstepRole::Client) {
            LockstepDebugLog::log_event("batch_ignored", "session_not_ready");
            return;
        }

        const auto decoded_batch = decode_tick_input_batch(decoded_message->second);
        if (!decoded_batch.has_value()) {
            return;
        }

        TickInputBatch batch = *decoded_batch;
        if (batch.player_slot == player_slot_) {
            return;
        }

        if (is_remote_slot_ready(batch.execute_tick, batch.player_slot)) {
            return;
        }

        if (batch.execute_tick < next_execute_tick()) {
            return;
        }

        if (!is_remote_input_batch_current(batch)) {
            LockstepDebugLog::log_event(
                "batch_rejected",
                "execute_tick=" + std::to_string(batch.execute_tick) + " expected="
                + std::to_string(next_execute_tick()));
            return;
        }

        if (batch.execute_tick > next_execute_tick()) {
            LockstepDebugLog::log_event(
                "batch_buffered",
                "execute_tick=" + std::to_string(batch.execute_tick) + " expected="
                + std::to_string(next_execute_tick()));
        }

        for (sim::player::PlayerCommand& command : batch.commands) {
            command.player_slot = batch.player_slot;
            command.execute_tick = batch.execute_tick;
            sim::snapshot::resolve_command_entity_ids(simulation_.registry(), command);
            simulation_.enqueue_network_command(std::move(command));
        }

        remote_batch_received_ever_ = true;
        note_remote_slot_ready(batch.execute_tick, batch.player_slot);

        if (role_ == LockstepRole::Host && session_player_count_ > 2U
            && batch.player_slot != player_slot_) {
            (void)transport_.broadcast_reliable_except(
                packet,
                constants::CHANNEL_RELIABLE,
                sender_slot);
        }

        LockstepDebugLog::log_event(
            "batch_received",
            "execute_tick=" + std::to_string(batch.execute_tick) + " commands="
            + std::to_string(batch.commands.size()));
        if (role_ == LockstepRole::Client && awaiting_reconnect_handshake_
            && batch.execute_tick == next_execute_tick()) {
            awaiting_reconnect_handshake_ = false;
            sync_status_chat_announced_ = false;
            LockstepDebugLog::log_event(
                "reconnect_bootstrap_complete",
                "execute_tick=" + std::to_string(batch.execute_tick));
        }
        if (batch.execute_tick == waiting_remote_execute_tick_ && has_remote_ready(batch.execute_tick)) {
            clear_remote_wait();
        }
        return;
    }

    if (decoded_message->first == NetMessageKind::TickStateHash) {
        const auto message = decode_tick_state_hash(decoded_message->second);
        if (!message.has_value()) {
            return;
        }

        if (message->player_slot == player_slot_) {
            return;
        }

        if (message->execute_tick > simulation_.tick_count()) {
            return;
        }

        remote_state_hashes_by_slot_[message->execute_tick][message->player_slot] = message->state_hash;

        if (role_ == LockstepRole::Host && session_player_count_ > 2U) {
            (void)transport_.broadcast_unreliable_except(
                packet,
                constants::CHANNEL_UNRELIABLE,
                sender_slot);
        }

        if (!should_verify_state_hashes()) {
            return;
        }

        const auto local_iterator = local_state_hashes_.find(message->execute_tick);
        if (local_iterator != local_state_hashes_.end()) {
            verify_state_hash(message->execute_tick, local_iterator->second);
        }
    }
}

void LockstepSession::verify_state_hash(
    const std::uint64_t execute_tick,
    const std::uint64_t local_hash)
{
    if (!should_verify_state_hashes()) {
        return;
    }

    local_state_hashes_[execute_tick] = local_hash;

    const std::array<std::optional<std::uint64_t>, constants::LOCKSTEP_MAX_PLAYER_SLOTS>& remote_hashes =
        remote_state_hashes_by_slot_[execute_tick];
    const std::uint8_t required_mask = required_remote_slots_mask();
    for (std::uint8_t slot = 0U; slot < session_player_count_; ++slot) {
        if (slot == player_slot_ || (required_mask & (1U << slot)) == 0U) {
            continue;
        }

        if (!remote_hashes[slot].has_value()) {
            return;
        }

        if (*remote_hashes[slot] == local_hash) {
            continue;
        }

        if (desynced_) {
            return;
        }

        desynced_ = true;
        desync_tick_ = execute_tick;
        std::cerr << "lockstep desync at tick " << execute_tick << ": local=0x" << std::hex << local_hash
                  << " remote_slot=" << static_cast<int>(slot) << "=0x" << *remote_hashes[slot] << std::dec
                  << '\n';
        {
            std::ostringstream detail{};
            detail << "tick=" << execute_tick << " local=0x" << std::hex << local_hash
                   << " remote_slot=" << static_cast<int>(slot) << "=0x" << *remote_hashes[slot]
                   << std::dec;
            LockstepDebugLog::log_event("desync", detail.str());
        }
        return;
    }
}

bool LockstepSession::try_advance_live_tick()
{
    // While a client is restoring from snapshot, the host must not keep
    // simulating that slot with AI — the restored client is already live and
    // will diverge within the hash-warmup window.
    const std::uint8_t live_resync_mask =
        static_cast<std::uint8_t>(resync_pending_slots_mask_ & ~disconnected_slots_mask_);
    if (role_ == LockstepRole::Host && live_resync_mask != 0U) {
        debug_log_advance_blocked(
            "resync_pending mask=" + std::to_string(live_resync_mask));
        return false;
    }

    const std::uint64_t execute_tick = next_execute_tick();
    ensure_local_batches_pipelined();
    inject_ai_commands_for_disconnected_slots(execute_tick);

    if (!has_remote_ready(execute_tick)) {
        maybe_resend_local_batch(execute_tick);

        if (!is_connected() && !is_multi_peer_session(session_player_count_)) {
            note_opponent_transport_down();
            return try_advance_ai_fallback_tick();
        }

        if (is_opponent_unresponsive() && !is_multi_peer_session(session_player_count_)) {
            note_opponent_transport_down();
            return try_advance_ai_fallback_tick();
        }

        return false;
    }

    clear_remote_wait();

    flush_local_commands_for_tick(execute_tick);
    simulation_.snapshot_world_positions_for_render();
    simulation_.tick();
    publish_render_snapshot_locked();

    const std::uint64_t completed_tick = simulation_.tick_count();
    const std::uint64_t local_hash = simulation_.state_hash();
    send_state_hash(completed_tick, local_hash);
    verify_state_hash(completed_tick, local_hash);

    match_started_ = true;

    if (hash_verify_warmup_ticks_remaining_ > 0) {
        --hash_verify_warmup_ticks_remaining_;
    }

    if (resync_strict_batch_gate_) {
        resync_strict_batch_gate_ = false;
        LockstepDebugLog::log_event(
            "resync_gate_cleared",
            "tick=" + std::to_string(completed_tick));
    }

    note_sim_tick_completed();
    ensure_local_batches_pipelined();
    return true;
}

bool LockstepSession::try_advance_ai_fallback_tick()
{
    const std::uint64_t execute_tick = next_execute_tick();

    flush_local_commands_for_tick(execute_tick);
    inject_ai_commands(execute_tick);
    simulation_.snapshot_world_positions_for_render();
    simulation_.tick();
    publish_render_snapshot_locked();

    match_started_ = true;
    note_sim_tick_completed();
    return true;
}

void LockstepSession::set_auto_input_enabled(const bool enabled)
{
    auto_input_enabled_ = enabled;
}

bool LockstepSession::is_auto_input_enabled() const
{
    return auto_input_enabled_;
}

std::uint64_t LockstepSession::last_auto_input_tick() const
{
    return last_auto_input_at_tick_;
}

void LockstepSession::set_last_auto_input_tick(const std::uint64_t tick_count)
{
    last_auto_input_at_tick_ = tick_count;
}

} // namespace aoa::net
