#pragma once

#include "net/enet_transport.hpp"
#include "net/lockstep_network_hud.hpp"
#include "net/lockstep_wire.hpp"
#include "render/sim_render_snapshot.hpp"
#include "sim/player/player_command.hpp"
#include "sim/simulation.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aoa::net {

enum class LockstepRole {
    Host,
    Client,
};

class LockstepSession {
public:
    LockstepSession(
        LockstepRole role,
        std::uint8_t player_slot,
        sim::Simulation& simulation,
        std::uint8_t session_player_count = static_cast<std::uint8_t>(constants::LOCKSTEP_PLAYER_COUNT));
    ~LockstepSession();

    LockstepSession(const LockstepSession&) = delete;
    LockstepSession& operator=(const LockstepSession&) = delete;

    [[nodiscard]] bool start_host(std::uint16_t port);
    [[nodiscard]] bool connect(const char* host_name, std::uint16_t port);

    void ensure_initial_render_snapshot();

    void service_network_latency();
    void start_background_tick_loop();
    void stop_background_tick_loop();

    void submit_local_command(sim::player::PlayerCommand command);
    void poll();
    void run_tick_frame();
    void disconnect_transport();

    [[nodiscard]] bool try_advance_tick();

    [[nodiscard]] std::recursive_mutex& simulation_access_mutex();

    [[nodiscard]] bool is_connected() const;
    [[nodiscard]] bool is_peer_disconnected() const;
    [[nodiscard]] bool is_ai_fallback() const;
    [[nodiscard]] bool is_reconnecting() const;
    [[nodiscard]] bool is_host_gone() const;
    [[nodiscard]] bool is_session_ready() const;
    [[nodiscard]] std::uint8_t session_player_count() const;
    [[nodiscard]] std::uint8_t connected_peer_count() const;
    [[nodiscard]] bool is_waiting_for_opponent_reconnect() const;
    [[nodiscard]] bool is_awaiting_reconnect_handshake() const;
    [[nodiscard]] bool is_desynced() const;
    [[nodiscard]] std::uint64_t desync_tick() const;
    [[nodiscard]] std::uint8_t ai_controlled_slot() const;
    [[nodiscard]] float render_interpolation_alpha() const;
    [[nodiscard]] LockstepNetworkHudStats network_hud_stats() const;
    [[nodiscard]] std::shared_ptr<const render::SimRenderSnapshot> render_snapshot() const;
    [[nodiscard]] bool has_render_timing() const;
    [[nodiscard]] bool consume_snapshot_restored();

private:
    [[nodiscard]] std::uint8_t opponent_player_slot() const;
    [[nodiscard]] std::uint64_t next_execute_tick() const;
    [[nodiscard]] bool has_local_sent(std::uint64_t execute_tick) const;
    [[nodiscard]] bool has_remote_ready(std::uint64_t execute_tick) const;
    [[nodiscard]] bool is_remote_slot_ready(std::uint64_t execute_tick, std::uint8_t remote_slot) const;
    [[nodiscard]] std::uint8_t required_remote_slots_mask() const;
    void note_remote_slot_ready(std::uint64_t execute_tick, std::uint8_t remote_slot);

    void send_reconnect_request();
    void send_resync_ready();
    void send_join_accepted(std::uint8_t target_client_slot);
    void send_reconnect_snapshot();
    void maybe_retry_reconnect_handshake();
    void maybe_retry_resync_ready();
    void maybe_retry_host_resync_handshake();
    void maybe_send_pending_reconnect_snapshot();
    [[nodiscard]] bool should_send_reconnect_snapshot_now() const;
    void handle_resync_ready(std::uint8_t player_slot);
    void handle_reconnect_request(std::uint8_t player_slot);
    void handle_reconnect_snapshot(const std::vector<std::byte>& payload);
    void handle_join_accepted();
    void handle_opponent_lost();
    void enter_ai_fallback();
    void enter_host_lost();
    void try_reconnect();
    void resume_player_control(std::uint8_t player_slot);
    void reset_tick_sync_state();
    void begin_opponent_reconnect_grace();
    [[nodiscard]] bool is_opponent_reconnect_grace_active() const;
    void begin_host_reconnect_grace();
    [[nodiscard]] bool is_host_reconnect_grace_active() const;
    [[nodiscard]] bool should_declare_opponent_disconnected() const;
    void check_opponent_reconnect_timeout();
    void background_tick_loop();
    void process_polled_messages_locked();
    void enqueue_inbound_packets(std::vector<ReceivedPacket> packets);
    void process_inbound_latency_packets();
    [[nodiscard]] InboundLatencyHandleResult handle_inbound_latency_packet(std::span<const std::byte> packet);
    [[nodiscard]] bool try_process_latency_packet(const std::vector<std::byte>& packet);
    void drain_inbound_packet_queue_locked();
    void publish_render_snapshot_locked();
    void maybe_send_latency_probe();
    void reset_latency_stats();
    void handle_latency_pong(const LatencyProbeMessage& message);
    void record_latency_sample(int sample_ms);
    void update_display_ping_locked();
    [[nodiscard]] bool run_sim_tick_attempt();
    void debug_maybe_heartbeat();
    void clear_remote_wait();

    [[nodiscard]] bool send_input_batch(
        std::uint64_t execute_tick,
        const std::vector<sim::player::PlayerCommand>& commands);
    void send_state_hash(std::uint64_t execute_tick, std::uint64_t state_hash);
    void ensure_local_batch_sent(std::uint64_t execute_tick);
    void maybe_resend_local_batch(std::uint64_t execute_tick);
    void flush_local_commands_for_tick(std::uint64_t execute_tick);
    void flush_pending_local_commands_to_input_log();
    void sync_command_sequences_from_input_log();
    [[nodiscard]] bool apply_reconnect_snapshot_locally(const std::span<const std::byte> snapshot_bytes);
    void process_received_packet(const std::vector<std::byte>& packet);
    void verify_state_hash(std::uint64_t execute_tick, std::uint64_t remote_hash);
    [[nodiscard]] bool try_advance_live_tick();
    [[nodiscard]] bool try_advance_ai_fallback_tick();
    void handle_peer_connected();
    void note_opponent_transport_down();
    [[nodiscard]] bool can_run_live_lockstep() const;
    [[nodiscard]] bool should_verify_state_hashes() const;
    void clear_state_hash_tracking();
    [[nodiscard]] bool should_send_reconnect_snapshot() const;
    [[nodiscard]] bool is_remote_input_batch_current(const TickInputBatch& batch) const;
    [[nodiscard]] bool is_valid_remote_player_slot(std::uint8_t player_slot) const;
    [[nodiscard]] bool should_drop_batch_for_reconnect_handshake(std::uint8_t batch_player_slot) const;
    void inject_ai_commands(std::uint64_t execute_tick);
    void note_sim_tick_completed();
    void note_peer_activity();
    [[nodiscard]] bool is_opponent_unresponsive() const;

    void debug_log_advance_blocked(std::string_view reason);

    std::mutex inbound_packet_mutex_{};
    std::mutex network_service_mutex_{};
    std::vector<std::vector<std::byte>> inbound_packet_queue_{};
    std::atomic<std::shared_ptr<const render::SimRenderSnapshot>> render_snapshot_{};
    std::atomic<std::uint64_t> render_clock_anchor_ns_{0U};
    std::chrono::steady_clock::time_point last_latency_probe_sent_{};
    std::uint32_t latency_probe_sequence_{0U};

    LockstepRole role_{LockstepRole::Host};
    std::uint8_t player_slot_{0U};
    std::uint8_t session_player_count_{static_cast<std::uint8_t>(constants::LOCKSTEP_PLAYER_COUNT)};
    sim::Simulation& simulation_;
    EnetTransport transport_{};

    std::recursive_mutex simulation_access_mutex_{};
    std::thread tick_thread_{};
    std::atomic<bool> tick_loop_running_{false};

    std::string reconnect_host_{};
    std::uint16_t reconnect_port_{0U};

    std::unordered_set<std::uint64_t> local_sent_ticks_{};
    std::unordered_map<std::uint64_t, std::uint8_t> remote_ready_slots_by_tick_{};
    std::unordered_map<std::uint64_t, std::vector<sim::player::PlayerCommand>> local_outbox_{};
    std::uint64_t local_command_sequence_{1U};
    std::uint64_t ai_command_sequence_{1U};

    bool session_ready_{false};
    bool match_started_{false};
    bool ai_fallback_{false};
    bool host_lost_{false};
    bool reconnecting_{false};
    bool host_gone_{false};
    bool reconnect_request_sent_{false};
    bool opponent_needs_snapshot_{false};
    bool opponent_reconnect_pending_{false};
    bool awaiting_reconnect_handshake_{false};
    bool resync_strict_batch_gate_{false};
    bool client_resync_ready_{false};
    std::uint8_t pending_reconnect_player_slot_{constants::LOCKSTEP_INVALID_PLAYER_SLOT};
    bool resync_ready_sent_{false};
    bool snapshot_restored_pending_{false};
    std::chrono::steady_clock::time_point last_reconnect_request_sent_{};
    std::chrono::steady_clock::time_point last_reconnect_snapshot_sent_{};
    std::chrono::steady_clock::time_point last_resync_ready_sent_{};
    std::chrono::steady_clock::time_point snapshot_restored_at_{};
    std::uint8_t ai_controlled_slot_{0U};
    int reconnect_attempts_{0};
    std::chrono::steady_clock::time_point last_reconnect_try_{};

    bool desynced_{false};
    std::uint64_t desync_tick_{0U};
    std::unordered_map<std::uint64_t, std::uint64_t> local_state_hashes_{};
    std::unordered_map<
        std::uint64_t,
        std::array<std::optional<std::uint64_t>, constants::LOCKSTEP_MAX_PLAYER_SLOTS>>
        remote_state_hashes_by_slot_{};

    std::uint64_t waiting_remote_execute_tick_{0U};
    bool waiting_remote_active_{false};
    std::chrono::steady_clock::time_point waiting_remote_since_{};
    std::chrono::steady_clock::time_point last_batch_resend_time_{};
    bool remote_batch_received_ever_{false};
    std::chrono::steady_clock::time_point opponent_reconnect_grace_until_{};
    std::chrono::steady_clock::time_point host_reconnect_grace_until_{};
    std::chrono::steady_clock::time_point last_peer_activity_time_{};
    std::chrono::steady_clock::time_point debug_last_heartbeat_{};
    std::chrono::steady_clock::time_point debug_last_stall_log_{};
    double debug_tick_accumulator_{0.0};
    std::chrono::steady_clock::time_point debug_last_accumulator_time_{};
    std::uint64_t debug_last_logged_tick_{0U};
    int hash_verify_warmup_ticks_remaining_{0};
    std::atomic<int> latest_ping_ms_{0};
    std::atomic<int> display_ping_ms_{0};
    std::atomic<int> smoothed_rtt_ms_{0};
    std::mutex latency_stats_mutex_{};
    std::array<int, constants::LOCKSTEP_PING_DISPLAY_WINDOW> ping_samples_{};
    int ping_sample_count_{0};
    int ping_sample_index_{0};
    std::atomic<std::uint64_t> last_tick_time_ns_{0U};
};

} // namespace aoa::net
