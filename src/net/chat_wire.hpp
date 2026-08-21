#pragma once

#include "core/constants.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace aoa::net {

enum class ChatChannel : std::uint8_t {
    All = 0,
    Allies = 1,
};

struct ChatMessagePayload {
    std::uint8_t player_slot{0U};
    std::uint8_t channel{static_cast<std::uint8_t>(ChatChannel::All)};
    std::string text{};
};

[[nodiscard]] inline std::vector<std::byte> encode_chat_message(const ChatMessagePayload& message)
{
    std::string text = message.text;
    if (text.size() > static_cast<std::size_t>(aoa::constants::CHAT_MAX_MESSAGE_LENGTH)) {
        text.resize(static_cast<std::size_t>(aoa::constants::CHAT_MAX_MESSAGE_LENGTH));
    }

    std::vector<std::byte> out{};
    out.reserve(3U + text.size());
    out.push_back(static_cast<std::byte>(message.player_slot));
    out.push_back(static_cast<std::byte>(message.channel));
    out.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(text.size())));
    for (const char character : text) {
        out.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(character)));
    }

    return out;
}

[[nodiscard]] inline std::optional<ChatMessagePayload> decode_chat_message(
    const std::span<const std::byte> bytes)
{
    if (bytes.size() < 2U) {
        return std::nullopt;
    }

    ChatMessagePayload message{};
    message.player_slot = static_cast<std::uint8_t>(bytes[0]);
    if (bytes.size() >= 3U) {
        message.channel = static_cast<std::uint8_t>(bytes[1]);
        const auto text_length = static_cast<std::uint8_t>(bytes[2]);
        if (text_length > static_cast<std::uint8_t>(aoa::constants::CHAT_MAX_MESSAGE_LENGTH)) {
            return std::nullopt;
        }

        if (bytes.size() != 3U + static_cast<std::size_t>(text_length)) {
            return std::nullopt;
        }

        message.text.resize(text_length);
        for (std::uint8_t index = 0U; index < text_length; ++index) {
            message.text[index] = static_cast<char>(bytes[3U + index]);
        }

        return message;
    }

    const auto text_length = static_cast<std::uint8_t>(bytes[1]);
    if (text_length > static_cast<std::uint8_t>(aoa::constants::CHAT_MAX_MESSAGE_LENGTH)
        || bytes.size() != 2U + static_cast<std::size_t>(text_length)) {
        return std::nullopt;
    }

    message.text.resize(text_length);
    for (std::uint8_t index = 0U; index < text_length; ++index) {
        message.text[index] = static_cast<char>(bytes[2U + index]);
    }

    return message;
}

} // namespace aoa::net
