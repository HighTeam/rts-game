#include "net/net_smoke.hpp"

#include "net/enet_transport.hpp"
#include "net/net_constants.hpp"
#include "net/net_message.hpp"
#include "sim/player/player_command.hpp"

#include <iostream>

namespace aoa::net {

namespace {

bool wait_for_connection(EnetTransport& host, EnetTransport& client)
{
    for (int attempt = 0; attempt < constants::NET_SMOKE_CONNECT_ATTEMPTS; ++attempt) {
        host.poll(constants::NET_POLL_TIMEOUT_MS);
        client.poll(constants::NET_POLL_TIMEOUT_MS);

        if (host.has_peer() && client.is_connected()) {
            return true;
        }
    }

    return false;
}

} // namespace

int run_net_smoke()
{
    if (!EnetTransport::global_initialize()) {
        std::cerr << "net-smoke: enet_initialize failed\n";
        return 1;
    }

    EnetTransport host{};
    EnetTransport client{};

    if (!host.start_host(constants::DEFAULT_PORT)) {
        std::cerr << "net-smoke: failed to start host\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!client.connect("127.0.0.1", constants::DEFAULT_PORT)) {
        std::cerr << "net-smoke: failed to connect client\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    if (!wait_for_connection(host, client)) {
        std::cerr << "net-smoke: timed out waiting for connection\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    sim::player::PlayerCommand command{};
    command.sequence = 1U;
    command.execute_tick = 5U;
    command.player_slot = 0U;
    command.type = sim::player::PlayerCommandType::Move;
    command.cell = {12, 14};

    const std::vector<std::byte> command_bytes = sim::player::encode_player_command(command);
    if (command_bytes.empty()) {
        std::cerr << "net-smoke: failed to encode player command\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    const std::vector<std::byte> wire_message =
        encode_net_message(NetMessageKind::PlayerCommand, command_bytes);
    if (!client.send_reliable(wire_message, constants::CHANNEL_RELIABLE)) {
        std::cerr << "net-smoke: failed to send player command\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    bool received_valid_command = false;

    for (int attempt = 0; attempt < constants::NET_SMOKE_RECEIVE_ATTEMPTS; ++attempt) {
        host.poll(constants::NET_POLL_TIMEOUT_MS);
        client.poll(constants::NET_POLL_TIMEOUT_MS);

        for (const std::vector<std::byte>& packet : host.drain_received()) {
            const auto decoded_message = decode_net_message(packet);
            if (!decoded_message.has_value()) {
                continue;
            }

            if (decoded_message->first != NetMessageKind::PlayerCommand) {
                continue;
            }

            const auto decoded_command =
                sim::player::decode_player_command(decoded_message->second);
            if (!decoded_command.has_value()) {
                continue;
            }

            if (decoded_command->type != sim::player::PlayerCommandType::Move) {
                continue;
            }

            if (decoded_command->execute_tick != command.execute_tick) {
                continue;
            }

            if (decoded_command->cell.x != command.cell.x || decoded_command->cell.y != command.cell.y) {
                continue;
            }

            received_valid_command = true;
            break;
        }

        if (received_valid_command) {
            break;
        }
    }

    if (!received_valid_command) {
        std::cerr << "net-smoke: host did not receive a valid player command\n";
        EnetTransport::global_deinitialize();
        return 1;
    }

    EnetTransport::global_deinitialize();
    std::cout << "net-smoke: ok\n";
    return 0;
}

} // namespace aoa::net
