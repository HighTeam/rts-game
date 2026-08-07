#include "net/lockstep_runner.hpp"

#include "core/constants.hpp"
#include "net/enet_transport.hpp"
#include "net/lockstep_session.hpp"
#include "net/net_constants.hpp"
#include "sim/player/player_command.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/resources.hpp"
#include "sim/components/world_position.hpp"
#include "sim/components/movement.hpp"
#include "sim/components/world_position.hpp"
#include "sim/scenario/test_scenario.hpp"
#include "sim/simulation.hpp"
#include "sim/snapshot/entity_snapshot_key.hpp"
#include "sim/snapshot/sim_snapshot.hpp"
#include "sim/systems/disconnected_player_ai.hpp"
#include "sim/systems/gameplay_systems.hpp"

#include <iostream>
#include <string>

namespace aoa::net {

namespace {

namespace {

void log_worker_layout(std::ostream& out, sim::Simulation& simulation, const std::string_view label)
{
    out << label << " workers:";
    std::vector<entt::entity> workers{};
    entt::registry& registry = simulation.registry();
    const auto view = registry.view<
        sim::components::WorkerUnitTag,
        sim::components::PlayerOwnedTag,
        sim::components::GridPosition>();
    for (const entt::entity entity : view) {
        if (sim::components::entity_player_slot(registry, entity) != 0U) {
            continue;
        }

        workers.push_back(entity);
    }

    workers = sim::snapshot::sort_entities_by_snapshot_key(registry, std::move(workers));
    for (const entt::entity entity : workers) {
        const core::GridPos cell = registry.get<sim::components::GridPosition>(entity).cell;
        const auto& world = registry.get<sim::components::WorldPosition>(entity);
        const auto brain = registry.get<sim::components::WorkerBrain>(entity).state;
        const int carried = registry.get<sim::components::CarriedWood>(entity).amount;
        const bool has_path = registry.any_of<sim::components::MovePath>(entity);
        const bool has_segment = registry.any_of<sim::components::MoveSegment>(entity);
        out << " key_ord="
            << sim::snapshot::compute_entity_snapshot_key(registry, entity)->ordinal << " cell=(" << cell.x << ','
            << cell.y << ") world=(" << world.x.raw() << ',' << world.y.raw() << ") brain="
            << static_cast<int>(brain) << " wood=" << carried << " path=" << has_path
            << " segment=" << has_segment;
    }
    out << '\n';
}

int count_player_workers(const sim::Simulation& simulation, const std::uint8_t player_slot)
{
    int count = 0;
    const auto view = simulation.registry().view<
        sim::components::WorkerUnitTag,
        sim::components::PlayerOwnedTag,
        sim::components::Health>();
    for (const entt::entity entity : view) {
        if (sim::components::entity_player_slot(simulation.registry(), entity) != player_slot) {
            continue;
        }

        if (view.get<sim::components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        ++count;
    }

    return count;
}

} // namespace

bool wait_for_lockstep_connection(LockstepSession& host, LockstepSession& client)
{
    for (int attempt = 0; attempt < constants::LOCKSTEP_CONNECT_ATTEMPTS; ++attempt) {
        host.poll();
        client.poll();

        if (host.is_connected() && client.is_connected() && host.is_session_ready()
            && client.is_session_ready()) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool lockstep_sessions_desynced(
    const LockstepSession& host,
    const LockstepSession& client)
{
    return host.is_desynced() || client.is_desynced();
}

[[nodiscard]] bool lockstep_sim_hashes_match(
    const sim::Simulation& host_simulation,
    const sim::Simulation& client_simulation)
{
    return host_simulation.tick_count() == client_simulation.tick_count()
        && host_simulation.state_hash() == client_simulation.state_hash();
}

[[nodiscard]] bool advance_both_lockstep_sessions(
    LockstepSession& host,
    LockstepSession& client,
    sim::Simulation& host_simulation,
    sim::Simulation& client_simulation,
    const std::uint64_t target_ticks)
{
    std::uint64_t host_ticks = 0U;
    std::uint64_t client_ticks = 0U;

    for (int attempt = 0; attempt < constants::LOCKSTEP_ADVANCE_ATTEMPTS; ++attempt) {
        host.poll();
        client.poll();

        if (lockstep_sessions_desynced(host, client)) {
            return false;
        }

        if (host_ticks < target_ticks && host.try_advance_tick()) {
            ++host_ticks;
        }

        if (client_ticks < target_ticks && client.try_advance_tick()) {
            ++client_ticks;
        }

        if (host_ticks >= target_ticks && client_ticks >= target_ticks) {
            break;
        }
    }

    if (host_ticks < target_ticks || client_ticks < target_ticks) {
        return false;
    }

    return lockstep_sim_hashes_match(host_simulation, client_simulation);
}

[[nodiscard]] bool wait_for_host_ai_fallback(LockstepSession& host)
{
    for (int attempt = 0; attempt < constants::LOCKSTEP_CONNECT_ATTEMPTS; ++attempt) {
        host.poll();
        if (host.is_desynced()) {
            return false;
        }

        if (host.is_ai_fallback()) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool advance_host_ai_ticks(
    LockstepSession& host,
    LockstepSession& client,
    const std::uint64_t target_ticks)
{
    std::uint64_t advanced_ticks = 0U;

    for (int attempt = 0; attempt < constants::LOCKSTEP_ADVANCE_ATTEMPTS; ++attempt) {
        host.poll();
        client.poll();

        if (lockstep_sessions_desynced(host, client)) {
            return false;
        }

        if (advanced_ticks >= target_ticks) {
            return true;
        }

        if (host.try_advance_tick()) {
            ++advanced_ticks;
        }
    }

    return advanced_ticks >= target_ticks;
}

[[nodiscard]] bool wait_for_lockstep_reconnect_resync(
    LockstepSession& host,
    LockstepSession& client,
    sim::Simulation& host_simulation,
    sim::Simulation& client_simulation)
{
    for (int attempt = 0; attempt < constants::LOCKSTEP_CONNECT_ATTEMPTS; ++attempt) {
        host.poll();
        client.poll();

        if (lockstep_sessions_desynced(host, client)) {
            return false;
        }

        if (host.is_ai_fallback()) {
            continue;
        }

        if (!host.is_connected() || !client.is_connected()) {
            continue;
        }

        if (!host.is_session_ready() || !client.is_session_ready()) {
            continue;
        }

        if (host.is_awaiting_reconnect_handshake() || client.is_awaiting_reconnect_handshake()) {
            continue;
        }

        if (!lockstep_sim_hashes_match(host_simulation, client_simulation)) {
            continue;
        }

        return true;
    }

    return false;
}

[[nodiscard]] bool wait_for_lockstep_client_ready(
    LockstepSession& host,
    LockstepSession& client,
    const std::uint8_t expected_connected_clients)
{
    for (int attempt = 0; attempt < constants::LOCKSTEP_CONNECT_ATTEMPTS; ++attempt) {
        host.poll();
        client.poll();

        if (lockstep_sessions_desynced(host, client)) {
            return false;
        }

        if (host.connected_peer_count() < expected_connected_clients) {
            continue;
        }

        if (!client.is_connected() || !client.is_session_ready()) {
            continue;
        }

        return true;
    }

    return false;
}

[[nodiscard]] bool wait_for_lockstep_4_connection(
    LockstepSession& host,
    LockstepSession& client_one,
    LockstepSession& client_two,
    LockstepSession& client_three)
{
    for (int attempt = 0; attempt < constants::LOCKSTEP_CONNECT_ATTEMPTS; ++attempt) {
        host.poll();
        client_one.poll();
        client_two.poll();
        client_three.poll();

        if (lockstep_sessions_desynced(host, client_one)
            || lockstep_sessions_desynced(host, client_two)
            || lockstep_sessions_desynced(host, client_three)) {
            return false;
        }

        if (host.connected_peer_count() < constants::LOCKSTEP_4_MAX_CLIENTS) {
            continue;
        }

        if (!host.is_session_ready()) {
            continue;
        }

        if (!client_one.is_connected() || !client_one.is_session_ready()) {
            continue;
        }

        if (!client_two.is_connected() || !client_two.is_session_ready()) {
            continue;
        }

        if (!client_three.is_connected() || !client_three.is_session_ready()) {
            continue;
        }

        return true;
    }

    return false;
}

[[nodiscard]] bool advance_four_lockstep_sessions(
    LockstepSession& host,
    LockstepSession& client_one,
    LockstepSession& client_two,
    LockstepSession& client_three,
    sim::Simulation& host_simulation,
    sim::Simulation& client_one_simulation,
    sim::Simulation& client_two_simulation,
    sim::Simulation& client_three_simulation,
    const std::uint64_t target_ticks)
{
    std::uint64_t host_ticks = 0U;
    std::uint64_t client_one_ticks = 0U;
    std::uint64_t client_two_ticks = 0U;
    std::uint64_t client_three_ticks = 0U;

    for (int attempt = 0; attempt < constants::LOCKSTEP_ADVANCE_ATTEMPTS; ++attempt) {
        host.poll();
        client_one.poll();
        client_two.poll();
        client_three.poll();

        if (lockstep_sessions_desynced(host, client_one)
            || lockstep_sessions_desynced(host, client_two)
            || lockstep_sessions_desynced(host, client_three)) {
            return false;
        }

        if (host_ticks < target_ticks && host.try_advance_tick()) {
            ++host_ticks;
        }

        if (client_one_ticks < target_ticks && client_one.try_advance_tick()) {
            ++client_one_ticks;
        }

        if (client_two_ticks < target_ticks && client_two.try_advance_tick()) {
            ++client_two_ticks;
        }

        if (client_three_ticks < target_ticks && client_three.try_advance_tick()) {
            ++client_three_ticks;
        }

        if (host_ticks >= target_ticks && client_one_ticks >= target_ticks && client_two_ticks >= target_ticks
            && client_three_ticks >= target_ticks) {
            break;
        }
    }

    if (host_ticks < target_ticks || client_one_ticks < target_ticks || client_two_ticks < target_ticks
        || client_three_ticks < target_ticks) {
        return false;
    }

    const std::uint64_t host_hash = host_simulation.state_hash();
    return host_hash == client_one_simulation.state_hash() && host_hash == client_two_simulation.state_hash()
        && host_hash == client_three_simulation.state_hash();
}

bool advance_lockstep_session(LockstepSession& session, const std::uint64_t target_ticks)
{
    std::uint64_t completed_ticks = 0U;

    for (int attempt = 0; attempt < constants::LOCKSTEP_ADVANCE_ATTEMPTS; ++attempt) {
        session.poll();

        if (session.is_desynced()) {
            return false;
        }

        if (session.is_peer_disconnected()) {
            return false;
        }

        if (session.is_host_gone() || session.is_reconnecting()) {
            return false;
        }

        if (!session.is_session_ready() && !session.is_ai_fallback()) {
            continue;
        }

        if (completed_ticks >= target_ticks) {
            return true;
        }

        if (session.try_advance_tick()) {
            ++completed_ticks;
        }
    }

    return completed_ticks >= target_ticks;
}

void issue_smoke_gather_command(LockstepSession& host, sim::Simulation& simulation)
{
    const entt::entity worker =
        sim::scenario::find_scenario_entity(simulation.registry(), "player_worker");
    if (worker == entt::null) {
        return;
    }

    sim::player::PlayerCommand command{};
    command.type = sim::player::PlayerCommandType::Gather;
    command.unit_ids = {worker};
    command.cell = {20, 12};
    host.submit_local_command(std::move(command));
}

void issue_smoke_spawn_worker_command(LockstepSession& host, sim::Simulation& simulation)
{
    const entt::entity town_center =
        sim::scenario::find_scenario_entity(simulation.registry(), "town_center");
    if (town_center == entt::null) {
        return;
    }

    sim::player::PlayerCommand command{};
    command.type = sim::player::PlayerCommandType::SpawnWorker;
    command.target_entity = town_center;
    host.submit_local_command(std::move(command));
}

} // namespace

std::optional<std::pair<std::string, std::uint16_t>> parse_lockstep_join_address(
    const std::string& address_text)
{
    const std::size_t separator = address_text.find(':');
    if (separator == std::string::npos) {
        return std::nullopt;
    }

    const std::string host_name = address_text.substr(0U, separator);
    if (host_name.empty()) {
        return std::nullopt;
    }

    const std::string port_text = address_text.substr(separator + 1U);
    if (port_text.empty()) {
        return std::nullopt;
    }

    const int port_value = std::stoi(port_text);
    if (port_value <= 0 || port_value > 65535) {
        return std::nullopt;
    }

    return std::pair{host_name, static_cast<std::uint16_t>(port_value)};
}

int run_lockstep_smoke()
{
    if (!EnetTransport::global_initialize()) {
        std::cerr << "lockstep-smoke: enet_initialize failed\n";
        return 1;
    }

    sim::Simulation host_simulation{};
    sim::Simulation client_simulation{};

    LockstepSession host{
        LockstepRole::Host,
        constants::LOCKSTEP_HOST_PLAYER_SLOT,
        host_simulation};
    LockstepSession client{
        LockstepRole::Client,
        constants::LOCKSTEP_CLIENT_PLAYER_SLOT,
        client_simulation};

    if (!host.start_host(constants::LOCKSTEP_SMOKE_PORT)) {
        std::cerr << "lockstep-smoke: failed to start host\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!client.connect("127.0.0.1", constants::LOCKSTEP_SMOKE_PORT)) {
        std::cerr << "lockstep-smoke: failed to connect client\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!wait_for_lockstep_connection(host, client)) {
        std::cerr << "lockstep-smoke: timed out waiting for connection\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    constexpr std::uint64_t smoke_ticks = 40U;
    bool issued_gather = false;
    bool issued_spawn_worker = false;

    std::uint64_t host_ticks = 0U;
    std::uint64_t client_ticks = 0U;

    while (host_ticks < smoke_ticks || client_ticks < smoke_ticks) {
        host.poll();
        client.poll();

        if (!issued_gather && host_simulation.tick_count() == 3U) {
            issue_smoke_gather_command(host, host_simulation);
            issued_gather = true;
        }

        if (!issued_spawn_worker && host_simulation.tick_count() == 10U) {
            issue_smoke_spawn_worker_command(host, host_simulation);
            issued_spawn_worker = true;
        }

        if (host.is_desynced() || client.is_desynced()) {
            std::cerr << "lockstep-smoke: desync detected\n";
            EnetTransport::global_deinitialize();
            return 1;
        }

        if (host_ticks < smoke_ticks && host.try_advance_tick()) {
            ++host_ticks;
        }

        if (client_ticks < smoke_ticks && client.try_advance_tick()) {
            ++client_ticks;
        }
    }

    const std::uint64_t host_hash = host_simulation.state_hash();
    const std::uint64_t client_hash = client_simulation.state_hash();
    if (host_hash != client_hash) {
        std::cerr << "lockstep-smoke: hash mismatch host=0x" << std::hex << host_hash << " client=0x"
                  << client_hash << std::dec << '\n';
        EnetTransport::global_deinitialize();
        return 1;
    }

    EnetTransport::global_deinitialize();
    std::cout << "lockstep-smoke: ok ticks=" << smoke_ticks << " hash=0x" << std::hex << host_hash
              << std::dec << '\n';
    return 0;
}

int run_lockstep_disconnect_smoke()
{
    if (!EnetTransport::global_initialize()) {
        std::cerr << "lockstep-disconnect-smoke: enet_initialize failed\n";
        return 1;
    }

    sim::Simulation host_simulation{};
    sim::Simulation client_simulation{};

    LockstepSession host{
        LockstepRole::Host,
        constants::LOCKSTEP_HOST_PLAYER_SLOT,
        host_simulation};
    LockstepSession client{
        LockstepRole::Client,
        constants::LOCKSTEP_CLIENT_PLAYER_SLOT,
        client_simulation};

    if (!host.start_host(constants::LOCKSTEP_DISCONNECT_SMOKE_PORT)) {
        std::cerr << "lockstep-disconnect-smoke: failed to start host\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!client.connect("127.0.0.1", constants::LOCKSTEP_DISCONNECT_SMOKE_PORT)) {
        std::cerr << "lockstep-disconnect-smoke: failed to connect client\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!wait_for_lockstep_connection(host, client)) {
        std::cerr << "lockstep-disconnect-smoke: timed out waiting for connection\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    constexpr std::uint64_t warmup_ticks = 15U;
    std::uint64_t host_ticks = 0U;
    std::uint64_t client_ticks = 0U;

    while (host_ticks < warmup_ticks || client_ticks < warmup_ticks) {
        host.poll();
        client.poll();

        if (host.is_desynced() || client.is_desynced()) {
            std::cerr << "lockstep-disconnect-smoke: desync during warmup\n";
            EnetTransport::global_deinitialize();
            return 1;
        }

        if (host_ticks < warmup_ticks && host.try_advance_tick()) {
            ++host_ticks;
        }

        if (client_ticks < warmup_ticks && client.try_advance_tick()) {
            ++client_ticks;
        }
    }

    const std::uint64_t tick_before_disconnect = host_simulation.tick_count();
    client.disconnect_transport();

    bool ai_started = false;
    for (int attempt = 0; attempt < constants::LOCKSTEP_CONNECT_ATTEMPTS; ++attempt) {
        host.poll();
        if (host.is_ai_fallback()) {
            ai_started = true;
            break;
        }
    }

    if (!ai_started) {
        std::cerr << "lockstep-disconnect-smoke: host did not enter AI fallback immediately\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    std::uint64_t ticks_after_disconnect = 0U;
    for (int attempt = 0; attempt < constants::LOCKSTEP_ADVANCE_ATTEMPTS; ++attempt) {
        host.poll();
        if (host.try_advance_tick()) {
            ++ticks_after_disconnect;
            if (ticks_after_disconnect >= 10U) {
                break;
            }
        }
    }

    if (ticks_after_disconnect < 10U) {
        std::cerr << "lockstep-disconnect-smoke: host sim stalled after disconnect (advanced "
                  << ticks_after_disconnect << " ticks)\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (host_simulation.tick_count() <= tick_before_disconnect) {
        std::cerr << "lockstep-disconnect-smoke: host tick count did not advance\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    const std::vector<std::byte> snapshot_bytes = sim::encode_sim_snapshot(host_simulation);
    if (snapshot_bytes.empty()) {
        std::cerr << "lockstep-disconnect-smoke: snapshot encode failed\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    sim::Simulation restored{};
    if (!restored.apply_snapshot(snapshot_bytes)) {
        std::cerr << "lockstep-disconnect-smoke: snapshot roundtrip failed\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    EnetTransport::global_deinitialize();
    std::cout << "lockstep-disconnect-smoke: ok ai_at_tick=" << tick_before_disconnect
              << " continued_to=" << host_simulation.tick_count() << '\n';
    return 0;
}

int run_lockstep_reconnect_smoke()
{
    if (!EnetTransport::global_initialize()) {
        std::cerr << "lockstep-reconnect-smoke: enet_initialize failed\n";
        return 1;
    }

    sim::Simulation host_simulation{};
    sim::Simulation client_simulation{};

    LockstepSession host{
        LockstepRole::Host,
        constants::LOCKSTEP_HOST_PLAYER_SLOT,
        host_simulation};
    LockstepSession client{
        LockstepRole::Client,
        constants::LOCKSTEP_CLIENT_PLAYER_SLOT,
        client_simulation};

    if (!host.start_host(constants::LOCKSTEP_RECONNECT_SMOKE_PORT)) {
        std::cerr << "lockstep-reconnect-smoke: failed to start host\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!client.connect("127.0.0.1", constants::LOCKSTEP_RECONNECT_SMOKE_PORT)) {
        std::cerr << "lockstep-reconnect-smoke: failed to connect client\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!wait_for_lockstep_connection(host, client)) {
        std::cerr << "lockstep-reconnect-smoke: timed out waiting for connection\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    bool issued_gather = false;
    bool issued_spawn_worker = false;
    std::uint64_t host_ticks = 0U;
    std::uint64_t client_ticks = 0U;
    const std::uint64_t warmup_ticks = constants::LOCKSTEP_RECONNECT_SMOKE_WARMUP_TICKS;

    while (host_ticks < warmup_ticks || client_ticks < warmup_ticks) {
        host.poll();
        client.poll();

        if (!issued_gather && host_simulation.tick_count() == 3U) {
            issue_smoke_gather_command(host, host_simulation);
            issued_gather = true;
        }

        if (!issued_spawn_worker && host_simulation.tick_count() == 10U) {
            issue_smoke_spawn_worker_command(host, host_simulation);
            issued_spawn_worker = true;
        }

        if (lockstep_sessions_desynced(host, client)) {
            std::cerr << "lockstep-reconnect-smoke: desync during warmup at tick "
                      << host_simulation.tick_count() << '\n';
            EnetTransport::global_deinitialize();
            return 1;
        }

        if (host_ticks < warmup_ticks && host.try_advance_tick()) {
            ++host_ticks;
        }

        if (client_ticks < warmup_ticks && client.try_advance_tick()) {
            ++client_ticks;
        }
    }

    if (!lockstep_sim_hashes_match(host_simulation, client_simulation)) {
        std::cerr << "lockstep-reconnect-smoke: hash mismatch after warmup host=0x" << std::hex
                  << host_simulation.state_hash() << " client=0x" << client_simulation.state_hash()
                  << std::dec << '\n';
        EnetTransport::global_deinitialize();
        return 1;
    }

    for (int cycle = 1; cycle <= constants::LOCKSTEP_RECONNECT_SMOKE_CYCLES; ++cycle) {
        client.disconnect_transport();

        if (!wait_for_host_ai_fallback(host)) {
            std::cerr << "lockstep-reconnect-smoke: host did not enter AI fallback on cycle "
                      << cycle << '\n';
            EnetTransport::global_deinitialize();
            return 1;
        }

        if (!advance_host_ai_ticks(host, client, constants::LOCKSTEP_RECONNECT_SMOKE_AI_TICKS)) {
            std::cerr << "lockstep-reconnect-smoke: host stalled in AI on cycle " << cycle << '\n';
            EnetTransport::global_deinitialize();
            return 1;
        }

        if (!client.connect("127.0.0.1", constants::LOCKSTEP_RECONNECT_SMOKE_PORT)) {
            std::cerr << "lockstep-reconnect-smoke: client reconnect connect failed on cycle "
                      << cycle << '\n';
            EnetTransport::global_deinitialize();
            return 1;
        }

        if (!wait_for_lockstep_reconnect_resync(
                host, client, host_simulation, client_simulation)) {
            std::cerr << "lockstep-reconnect-smoke: reconnect resync timed out on cycle " << cycle
                      << " host_tick=" << host_simulation.tick_count() << " client_tick="
                      << client_simulation.tick_count() << " host_ai=" << host.is_ai_fallback()
                      << " host_connected=" << host.is_connected()
                      << " client_connected=" << client.is_connected() << '\n';
            EnetTransport::global_deinitialize();
            return 1;
        }

        if (!advance_both_lockstep_sessions(
                host,
                client,
                host_simulation,
                client_simulation,
                constants::LOCKSTEP_RECONNECT_SMOKE_LIVE_TICKS)) {
            std::cerr << "lockstep-reconnect-smoke: live advance failed after cycle " << cycle
                      << " host=0x" << std::hex << host_simulation.state_hash() << " client=0x"
                      << client_simulation.state_hash() << std::dec << '\n';
            EnetTransport::global_deinitialize();
            return 1;
        }
    }

    EnetTransport::global_deinitialize();
    std::cout << "lockstep-reconnect-smoke: ok cycles=" << constants::LOCKSTEP_RECONNECT_SMOKE_CYCLES
              << " ticks=" << host_simulation.tick_count() << " hash=0x" << std::hex
              << host_simulation.state_hash() << std::dec << '\n';
    return 0;
}

int run_lockstep_4_smoke()
{
    if (!EnetTransport::global_initialize()) {
        std::cerr << "lockstep-4-smoke: enet_initialize failed\n";
        return 1;
    }

    sim::Simulation host_simulation{};
    sim::Simulation client_one_simulation{};
    sim::Simulation client_two_simulation{};
    sim::Simulation client_three_simulation{};

    LockstepSession host{
        LockstepRole::Host,
        constants::LOCKSTEP_HOST_PLAYER_SLOT,
        host_simulation,
        constants::LOCKSTEP_4_PLAYER_COUNT};
    LockstepSession client_one{
        LockstepRole::Client,
        1U,
        client_one_simulation,
        constants::LOCKSTEP_4_PLAYER_COUNT};
    LockstepSession client_two{
        LockstepRole::Client,
        2U,
        client_two_simulation,
        constants::LOCKSTEP_4_PLAYER_COUNT};
    LockstepSession client_three{
        LockstepRole::Client,
        3U,
        client_three_simulation,
        constants::LOCKSTEP_4_PLAYER_COUNT};

    if (!host.start_host(constants::LOCKSTEP_4_SMOKE_PORT)) {
        std::cerr << "lockstep-4-smoke: failed to start host\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!client_one.connect("127.0.0.1", constants::LOCKSTEP_4_SMOKE_PORT)) {
        std::cerr << "lockstep-4-smoke: client 1 connect failed\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!wait_for_lockstep_client_ready(host, client_one, 1U)) {
        std::cerr << "lockstep-4-smoke: client 1 handshake timed out\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!client_two.connect("127.0.0.1", constants::LOCKSTEP_4_SMOKE_PORT)) {
        std::cerr << "lockstep-4-smoke: client 2 connect failed\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!wait_for_lockstep_client_ready(host, client_two, 2U)) {
        std::cerr << "lockstep-4-smoke: client 2 handshake timed out\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!client_three.connect("127.0.0.1", constants::LOCKSTEP_4_SMOKE_PORT)) {
        std::cerr << "lockstep-4-smoke: client 3 connect failed\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!wait_for_lockstep_client_ready(host, client_three, constants::LOCKSTEP_4_MAX_CLIENTS)) {
        std::cerr << "lockstep-4-smoke: client 3 handshake timed out\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!host.is_session_ready()) {
        std::cerr << "lockstep-4-smoke: host session not ready\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!advance_four_lockstep_sessions(
            host,
            client_one,
            client_two,
            client_three,
            host_simulation,
            client_one_simulation,
            client_two_simulation,
            client_three_simulation,
            constants::LOCKSTEP_4_SMOKE_TICKS)) {
        std::cerr << "lockstep-4-smoke: advance/hash failed host=0x" << std::hex
                  << host_simulation.state_hash() << " c1=0x" << client_one_simulation.state_hash()
                  << " c2=0x" << client_two_simulation.state_hash() << " c3=0x"
                  << client_three_simulation.state_hash() << std::dec << '\n';
        EnetTransport::global_deinitialize();
        return 1;
    }

    EnetTransport::global_deinitialize();
    std::cout << "lockstep-4-smoke: ok ticks=" << constants::LOCKSTEP_4_SMOKE_TICKS << " hash=0x"
              << std::hex << host_simulation.state_hash() << std::dec << '\n';
    return 0;
}

int run_lockstep_host(const LockstepRunOptions& options)
{
    if (!EnetTransport::global_initialize()) {
        std::cerr << "lockstep-host: enet_initialize failed\n";
        return 1;
    }

    sim::Simulation simulation{};
    LockstepSession session{
        LockstepRole::Host,
        constants::LOCKSTEP_HOST_PLAYER_SLOT,
        simulation};

    const std::uint16_t port =
        options.port == 0U ? constants::DEFAULT_PORT : options.port;

    if (!session.start_host(port)) {
        std::cerr << "lockstep-host: failed to start on port " << port << '\n';
        EnetTransport::global_deinitialize();
        return 1;
    }

    std::cout << "lockstep-host: listening on port " << port << '\n';

    for (int attempt = 0; attempt < constants::LOCKSTEP_CONNECT_ATTEMPTS; ++attempt) {
        session.poll();
        if (session.is_connected() && session.is_session_ready()) {
            break;
        }
    }

    if (!session.is_connected() || !session.is_session_ready()) {
        std::cerr << "lockstep-host: timed out waiting for client\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    const std::uint64_t target_ticks =
        options.tick_count == 0U ? constants::LOCKSTEP_DEFAULT_TICK_COUNT : options.tick_count;

    if (!advance_lockstep_session(session, target_ticks)) {
        if (session.is_desynced()) {
            std::cerr << "lockstep-host: desync at tick " << session.desync_tick() << '\n';
        }
        else if (session.is_ai_fallback()) {
            std::cout << "lockstep-host: completed with AI fallback at tick "
                      << simulation.tick_count() << '\n';
            EnetTransport::global_deinitialize();
            return 0;
        }
        else {
            std::cerr << "lockstep-host: failed to complete " << target_ticks << " ticks\n";
        }

        EnetTransport::global_deinitialize();
        return 1;
    }

    std::cout << "lockstep-host: ok ticks=" << target_ticks << " hash=0x" << std::hex
              << simulation.state_hash() << std::dec << '\n';

    EnetTransport::global_deinitialize();
    return 0;
}

int run_lockstep_join(const LockstepRunOptions& options)
{
    if (!options.join_address.has_value()) {
        std::cerr << "lockstep-join: missing host address\n";
        return 1;
    }

    const auto parsed_address = parse_lockstep_join_address(*options.join_address);
    if (!parsed_address.has_value()) {
        std::cerr << "lockstep-join: invalid address (expected HOST:PORT)\n";
        return 1;
    }

    if (!EnetTransport::global_initialize()) {
        std::cerr << "lockstep-join: enet_initialize failed\n";
        return 1;
    }

    sim::Simulation simulation{};
    LockstepSession session{
        LockstepRole::Client,
        constants::LOCKSTEP_CLIENT_PLAYER_SLOT,
        simulation};

    if (!session.connect(parsed_address->first.c_str(), parsed_address->second)) {
        std::cerr << "lockstep-join: failed to connect to " << *options.join_address << '\n';
        EnetTransport::global_deinitialize();
        return 1;
    }

    for (int attempt = 0; attempt < constants::LOCKSTEP_CONNECT_ATTEMPTS; ++attempt) {
        session.poll();
        if (session.is_connected() && session.is_session_ready()) {
            break;
        }
    }

    if (!session.is_connected() || !session.is_session_ready()) {
        std::cerr << "lockstep-join: timed out waiting for host\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    const std::uint64_t target_ticks =
        options.tick_count == 0U ? constants::LOCKSTEP_DEFAULT_TICK_COUNT : options.tick_count;

    if (!advance_lockstep_session(session, target_ticks)) {
        if (session.is_desynced()) {
            std::cerr << "lockstep-join: desync at tick " << session.desync_tick() << '\n';
        }
        else if (session.is_ai_fallback()) {
            std::cout << "lockstep-join: completed with AI fallback at tick "
                      << simulation.tick_count() << '\n';
            EnetTransport::global_deinitialize();
            return 0;
        }
        else if (session.is_host_gone()) {
            std::cout << "lockstep-join: host left the game at tick " << simulation.tick_count() << '\n';
            EnetTransport::global_deinitialize();
            return 0;
        }
        else {
            std::cerr << "lockstep-join: failed to complete " << target_ticks << " ticks\n";
        }

        EnetTransport::global_deinitialize();
        return 1;
    }

    std::cout << "lockstep-join: ok ticks=" << target_ticks << " hash=0x" << std::hex
              << simulation.state_hash() << std::dec << '\n';

    EnetTransport::global_deinitialize();
    return 0;
}

int run_snapshot_double_spawn_smoke()
{
    sim::Simulation host{};

    for (int tick = 0; tick < 40; ++tick) {
        host.tick();
    }

    const std::vector<std::byte> snapshot_bytes = sim::encode_sim_snapshot(host);
    if (snapshot_bytes.empty()) {
        std::cerr << "snapshot-double-spawn-smoke: encode failed\n";
        return 1;
    }

    sim::Simulation probe{};
    if (!probe.apply_snapshot(snapshot_bytes)) {
        std::cerr << "snapshot-double-spawn-smoke: roundtrip validation failed\n";
        return 1;
    }

    if (!host.apply_snapshot(snapshot_bytes)) {
        std::cerr << "snapshot-double-spawn-smoke: host self-apply failed\n";
        return 1;
    }

    sim::Simulation client{};
    if (!client.apply_snapshot(snapshot_bytes)) {
        std::cerr << "snapshot-double-spawn-smoke: restore failed\n";
        return 1;
    }

    if (host.tick_count() != client.tick_count() || host.state_hash() != client.state_hash()) {
        std::cerr << "snapshot-double-spawn-smoke: restore metadata mismatch\n";
        return 1;
    }

    auto issue_spawn_worker_over_wire = [](
        sim::Simulation& simulation,
        const std::uint64_t execute_tick,
        const sim::player::PlayerCommand& host_command) {
        const std::vector<std::byte> wire_bytes =
            sim::player::encode_player_command_with_keys(host_command);
        if (wire_bytes.empty()) {
            return false;
        }

        const auto decoded = sim::player::decode_player_command_with_keys(wire_bytes);
        if (!decoded.has_value()) {
            return false;
        }

        sim::player::PlayerCommand command = *decoded;
        command.execute_tick = execute_tick;
        sim::snapshot::resolve_command_entity_ids(simulation.registry(), command);
        simulation.enqueue_network_command(std::move(command));
        return true;
    };

    for (int spawn_index = 0; spawn_index < 2; ++spawn_index) {
        const std::uint64_t execute_tick = host.tick_count() + 1U;

        sim::player::PlayerCommand host_command{};
        host_command.player_slot = 0U;
        host_command.execute_tick = execute_tick;
        host_command.type = sim::player::PlayerCommandType::SpawnWorker;
        host_command.target_entity =
            sim::scenario::find_scenario_entity(host.registry(), "town_center");
        sim::snapshot::annotate_command_entity_keys(host.registry(), host_command);
        if (!host_command.target_entity_key.has_value()) {
            std::cerr << "snapshot-double-spawn-smoke: failed to annotate host spawn command\n";
            return 1;
        }

        if (!issue_spawn_worker_over_wire(host, execute_tick, host_command)) {
            std::cerr << "snapshot-double-spawn-smoke: failed to queue host spawn\n";
            return 1;
        }

        if (!issue_spawn_worker_over_wire(client, execute_tick, host_command)) {
            std::cerr << "snapshot-double-spawn-smoke: failed to queue client spawn\n";
            return 1;
        }

        host.tick();
        client.tick();

        if (host.state_hash() != client.state_hash()) {
            const entt::entity host_town_center =
                sim::scenario::find_scenario_entity(host.registry(), "town_center");
            const entt::entity client_town_center =
                sim::scenario::find_scenario_entity(client.registry(), "town_center");
            const int host_wood = host_town_center != entt::null
                ? host.registry().get<sim::components::Stockpile>(host_town_center).wood
                : -1;
            const int client_wood = client_town_center != entt::null
                ? client.registry().get<sim::components::Stockpile>(client_town_center).wood
                : -1;
            std::cerr << "snapshot-double-spawn-smoke: hash mismatch after spawn " << spawn_index + 1
                      << " at tick " << host.tick_count() << " host=0x" << std::hex
                      << host.state_hash() << " client=0x" << client.state_hash() << std::dec
                      << " host_workers=" << count_player_workers(host, 0U)
                      << " client_workers=" << count_player_workers(client, 0U)
                      << " host_wood=" << host_wood << " client_wood=" << client_wood << '\n';
            log_worker_layout(std::cerr, host, "host");
            log_worker_layout(std::cerr, client, "client");
            return 1;
        }
    }

    for (int tick = 0; tick < 20; ++tick) {
        host.tick();
        client.tick();
        if (host.state_hash() != client.state_hash()) {
            std::cerr << "snapshot-double-spawn-smoke: hash drift at tick " << host.tick_count()
                      << " host=0x" << std::hex << host.state_hash() << " client=0x"
                      << client.state_hash() << std::dec << '\n';
            return 1;
        }
    }

    std::cout << "snapshot-double-spawn-smoke: ok ticks=" << host.tick_count() << " hash=0x"
              << std::hex << host.state_hash() << std::dec << '\n';
    return 0;
}

int run_snapshot_smoke()
{
    sim::Simulation source{};

    const entt::entity player_worker =
        sim::scenario::find_scenario_entity(source.registry(), "player_worker");
    const entt::entity player2_worker =
        sim::scenario::find_scenario_entity(source.registry(), "player2_worker");
    if (player_worker == entt::null || player2_worker == entt::null) {
        std::cerr << "snapshot-smoke: scenario entities missing\n";
        return 1;
    }

    sim::player::PlayerCommand gather_command{};
    gather_command.execute_tick = 5U;
    gather_command.type = sim::player::PlayerCommandType::Gather;
    gather_command.unit_ids = {player_worker};
    gather_command.cell = {20, 12};
    source.enqueue_player_command(gather_command);

    sim::player::PlayerCommand deposit_command{};
    deposit_command.execute_tick = 100U;
    deposit_command.type = sim::player::PlayerCommandType::Deposit;
    deposit_command.unit_ids = {player_worker};
    source.enqueue_player_command(deposit_command);

    sim::player::PlayerCommand move_command{};
    move_command.execute_tick = 20U;
    move_command.player_slot = 1U;
    move_command.type = sim::player::PlayerCommandType::Move;
    move_command.unit_ids = {player2_worker};
    move_command.cell = {48, 48};
    source.enqueue_player_command(move_command);

    for (int tick = 0; tick < 120; ++tick) {
        source.tick();
    }

    source.set_player_ai_controlled(1U, true);
    std::uint64_t ai_sequence = 1U;
    for (int tick = 0; tick < 31; ++tick) {
        const std::uint64_t execute_tick = source.next_command_execute_tick();
        const std::vector<sim::player::PlayerCommand> ai_commands =
            sim::systems::generate_ai_commands_for_slot(
                source.registry(),
                1U,
                execute_tick,
                ai_sequence);
        for (sim::player::PlayerCommand command : ai_commands) {
            source.enqueue_network_command(std::move(command));
        }

        source.tick();
    }

    source.set_player_ai_controlled(1U, false);

    sim::player::PlayerCommand human_move{};
    human_move.player_slot = 1U;
    human_move.execute_tick = source.next_command_execute_tick();
    human_move.type = sim::player::PlayerCommandType::Move;
    human_move.unit_ids = {player2_worker};
    human_move.cell = {40, 40};
    source.enqueue_network_command(human_move);

    for (int tick = 0; tick < 20; ++tick) {
        source.tick();
    }

    source.set_player_ai_controlled(1U, true);
    for (int tick = 0; tick < 10; ++tick) {
        const std::uint64_t execute_tick = source.next_command_execute_tick();
        const std::vector<sim::player::PlayerCommand> ai_commands =
            sim::systems::generate_ai_commands_for_slot(
                source.registry(),
                1U,
                execute_tick,
                ai_sequence);
        for (sim::player::PlayerCommand command : ai_commands) {
            source.enqueue_network_command(std::move(command));
        }

        source.tick();
    }

    source.tick();

    const auto world_view = source.registry().view<sim::components::WorldTag, sim::components::MapGrid>();
    if (world_view.begin() != world_view.end()) {
        auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
        for (std::size_t index = 0U; index < map.forest_wood.size(); ++index) {
            if (map.forest_wood[index] <= 0) {
                continue;
            }

            map.forest_wood[index] = 0;
            map.tiles[index] = sim::components::TileType::Grass;
            break;
        }

        sim::systems::compute_state_hash(source.registry());
    }

    const std::vector<std::byte> snapshot_bytes = sim::encode_sim_snapshot(source);
    if (snapshot_bytes.empty()) {
        std::cerr << "snapshot-smoke: encode failed\n";
        return 1;
    }

    sim::Simulation restored{};
    if (!restored.apply_snapshot(snapshot_bytes)) {
        std::cerr << "snapshot-smoke: restore failed\n";
        return 1;
    }

    if (source.tick_count() != restored.tick_count() || source.state_hash() != restored.state_hash()) {
        std::cerr << "snapshot-smoke: metadata mismatch\n";
        return 1;
    }

    for (int tick = 0; tick < 10; ++tick) {
        source.tick();
        restored.tick();
    }

    if (source.state_hash() != restored.state_hash()) {
        std::cerr << "snapshot-smoke: hash mismatch after ticks\n";
        return 1;
    }

    std::cout << "snapshot-smoke: ok ticks=" << restored.tick_count() << " hash=0x" << std::hex
              << restored.state_hash() << std::dec << '\n';
    return 0;
}

int run_snapshot_reconnect_smoke()
{
    sim::Simulation source{};

    const entt::entity player1_town_center =
        sim::scenario::find_scenario_entity(source.registry(), "town_center");
    if (player1_town_center == entt::null) {
        std::cerr << "snapshot-reconnect-smoke: town center missing\n";
        return 1;
    }

    for (int tick = 0; tick < 949; ++tick) {
        if (tick == 10 || tick == 200) {
            sim::player::PlayerCommand spawn{};
            spawn.player_slot = 0U;
            spawn.execute_tick = source.next_command_execute_tick();
            spawn.type = sim::player::PlayerCommandType::SpawnWorker;
            spawn.target_entity = player1_town_center;
            sim::snapshot::annotate_command_entity_keys(source.registry(), spawn);
            source.enqueue_player_command(std::move(spawn));
        }

        source.tick();
    }

    source.set_player_ai_controlled(1U, true);
    std::uint64_t ai_sequence = 500U;
    for (int tick = 0; tick < 50; ++tick) {
        const std::uint64_t execute_tick = source.next_command_execute_tick();
        const std::vector<sim::player::PlayerCommand> ai_commands =
            sim::systems::generate_ai_commands_for_slot(
                source.registry(),
                1U,
                execute_tick,
                ai_sequence);
        for (sim::player::PlayerCommand command : ai_commands) {
            source.enqueue_network_command(std::move(command));
        }

        source.tick();
    }

    const std::vector<std::byte> snapshot_bytes = sim::encode_sim_snapshot(source);
    if (snapshot_bytes.empty()) {
        std::cerr << "snapshot-reconnect-smoke: encode failed at tick " << source.tick_count() << '\n';
        return 1;
    }

    sim::Simulation restored{};
    if (!restored.apply_snapshot(snapshot_bytes)) {
        std::cerr << "snapshot-reconnect-smoke: roundtrip failed at tick " << source.tick_count()
                  << " live_hash=0x" << std::hex << source.state_hash() << std::dec << '\n';
        return 1;
    }

    std::cout << "snapshot-reconnect-smoke: ok ticks=" << source.tick_count() << " hash=0x"
              << std::hex << source.state_hash() << std::dec << '\n';
    return 0;
}

int run_snapshot_heavy_smoke()
{
    sim::Simulation source{};

    const entt::entity player1_town_center =
        sim::scenario::find_scenario_entity(source.registry(), "town_center");
    const entt::entity player2_town_center =
        sim::scenario::find_scenario_entity(source.registry(), "player2_town_center");
    const entt::entity player1_worker =
        sim::scenario::find_scenario_entity(source.registry(), "player_worker");
    const entt::entity player2_worker =
        sim::scenario::find_scenario_entity(source.registry(), "player2_worker");

    if (player1_town_center == entt::null || player2_town_center == entt::null
        || player1_worker == entt::null || player2_worker == entt::null) {
        std::cerr << "snapshot-heavy-smoke: scenario entities missing\n";
        return 1;
    }

    auto enqueue_for_slot = [&source](sim::player::PlayerCommand command, const std::uint8_t slot) {
        command.player_slot = slot;
        sim::snapshot::annotate_command_entity_keys(source.registry(), command);
        source.enqueue_network_command(std::move(command));
    };

    for (int tick = 0; tick < 715; ++tick) {
        if (tick > 0 && tick % 40 == 0) {
            sim::player::PlayerCommand spawn{};
            spawn.execute_tick = source.next_command_execute_tick();
            spawn.type = sim::player::PlayerCommandType::SpawnWorker;
            spawn.target_entity = player1_town_center;
            enqueue_for_slot(std::move(spawn), 0U);
        }

        if (tick > 0 && tick % 45 == 0) {
            sim::player::PlayerCommand spawn{};
            spawn.execute_tick = source.next_command_execute_tick();
            spawn.type = sim::player::PlayerCommandType::SpawnWorker;
            spawn.target_entity = player2_town_center;
            enqueue_for_slot(std::move(spawn), 1U);
        }

        if (tick > 0 && tick % 22 == 0) {
            sim::player::PlayerCommand gather{};
            gather.execute_tick = source.next_command_execute_tick();
            gather.type = sim::player::PlayerCommandType::Gather;
            gather.unit_ids = {player1_worker};
            gather.cell = {20, 12};
            enqueue_for_slot(std::move(gather), 0U);
        }

        if (tick > 0 && tick % 27 == 0) {
            sim::player::PlayerCommand gather{};
            gather.execute_tick = source.next_command_execute_tick();
            gather.type = sim::player::PlayerCommandType::Gather;
            gather.unit_ids = {player2_worker};
            gather.cell = {44, 44};
            enqueue_for_slot(std::move(gather), 1U);
        }

        if (tick > 0 && tick % 55 == 0) {
            sim::player::PlayerCommand move{};
            move.execute_tick = source.next_command_execute_tick();
            move.type = sim::player::PlayerCommandType::Move;
            move.unit_ids = {player1_worker};
            move.cell = {12 + (tick % 5), 10 + (tick % 4)};
            enqueue_for_slot(std::move(move), 0U);
        }

        source.tick();
    }

    source.set_player_ai_controlled(1U, true);
    std::uint64_t ai_sequence = 900U;
    for (int tick = 0; tick < 45; ++tick) {
        const std::uint64_t execute_tick = source.next_command_execute_tick();
        const std::vector<sim::player::PlayerCommand> ai_commands =
            sim::systems::generate_ai_commands_for_slot(
                source.registry(),
                1U,
                execute_tick,
                ai_sequence);
        for (sim::player::PlayerCommand command : ai_commands) {
            source.enqueue_network_command(std::move(command));
        }

        source.tick();
    }

    const std::vector<std::byte> snapshot_bytes = sim::encode_sim_snapshot(source);
    if (snapshot_bytes.empty()) {
        std::cerr << "snapshot-heavy-smoke: encode failed at tick " << source.tick_count() << '\n';
        return 1;
    }

    sim::Simulation restored{};
    if (!restored.apply_snapshot(snapshot_bytes)) {
        sim::diagnose_snapshot_roundtrip_failure(source, restored, snapshot_bytes);
        std::cerr << "snapshot-heavy-smoke: checkpoint roundtrip failed at tick "
                  << source.tick_count() << " live_hash=0x" << std::hex << source.state_hash()
                  << std::dec << '\n';
        return 1;
    }

    std::cout << "snapshot-heavy-smoke: ok ticks=" << source.tick_count() << " hash=0x"
              << std::hex << source.state_hash() << std::dec << '\n';
    return 0;
}

} // namespace aoa::net
