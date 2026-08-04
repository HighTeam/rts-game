#include "net/lockstep_runner.hpp"

#include "core/constants.hpp"
#include "net/enet_transport.hpp"
#include "net/lockstep_session.hpp"
#include "net/net_constants.hpp"
#include "sim/player/player_command.hpp"
#include "sim/scenario/test_scenario.hpp"
#include "sim/simulation.hpp"

#include <iostream>
#include <string>

namespace aoa::net {

namespace {

bool wait_for_lockstep_connection(LockstepSession& host, LockstepSession& client)
{
    for (int attempt = 0; attempt < constants::LOCKSTEP_CONNECT_ATTEMPTS; ++attempt) {
        host.poll();
        client.poll();

        if (host.is_connected() && client.is_connected()) {
            return true;
        }
    }

    return false;
}

bool advance_lockstep_session(LockstepSession& session, const std::uint64_t target_ticks)
{
    std::uint64_t completed_ticks = 0U;

    for (int attempt = 0; attempt < constants::LOCKSTEP_ADVANCE_ATTEMPTS; ++attempt) {
        session.poll();

        if (session.is_desynced()) {
            return false;
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

    constexpr std::uint64_t smoke_ticks = 30U;
    bool issued_gather = false;

    std::uint64_t host_ticks = 0U;
    std::uint64_t client_ticks = 0U;

    while (host_ticks < smoke_ticks || client_ticks < smoke_ticks) {
        host.poll();
        client.poll();

        if (!issued_gather && host_simulation.tick_count() == 4U) {
            issue_smoke_gather_command(host, host_simulation);
            issued_gather = true;
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
        if (session.is_connected()) {
            break;
        }
    }

    if (!session.is_connected()) {
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
        if (session.is_connected()) {
            break;
        }
    }

    if (!session.is_connected()) {
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

} // namespace aoa::net
