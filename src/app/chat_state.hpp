#pragma once

#include "core/constants.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace aoa::app {

struct ChatLine {
    std::uint8_t player_slot{0U};
    std::string text{};
    std::chrono::steady_clock::time_point created_at{};
    bool system{false};
};

class ChatState {
public:
    void set_message_hook(std::function<void(std::uint8_t, const std::string&)> hook)
    {
        std::lock_guard lock(mutex_);
        message_hook_ = std::move(hook);
    }

    void push_message(const std::uint8_t player_slot, std::string text)
    {
        push_line(player_slot, std::move(text), false, constants::CHAT_MESSAGE_TTL_MS);
    }

    void push_system_message(std::string text)
    {
        push_line(
            constants::CHAT_SYSTEM_PLAYER_SLOT,
            std::move(text),
            true,
            constants::CHAT_SYSTEM_MESSAGE_TTL_MS);
    }

    [[nodiscard]] std::deque<ChatLine> snapshot() const
    {
        std::lock_guard lock(mutex_);
        prune_expired_unlocked();
        return lines_;
    }

private:
    void push_line(
        const std::uint8_t player_slot,
        std::string text,
        const bool system,
        const int ttl_ms)
    {
        if (text.empty()) {
            return;
        }

        if (text.size() > static_cast<std::size_t>(aoa::constants::CHAT_MAX_MESSAGE_LENGTH)) {
            text.resize(static_cast<std::size_t>(aoa::constants::CHAT_MAX_MESSAGE_LENGTH));
        }

        std::function<void(std::uint8_t, const std::string&)> hook{};
        std::string hook_text{};
        {
            std::lock_guard lock(mutex_);
            prune_expired_unlocked();
            if (!system) {
                hook = message_hook_;
                hook_text = text;
            }
            lines_.push_back(ChatLine{
                player_slot,
                std::move(text),
                std::chrono::steady_clock::now(),
                system,
            });
            while (static_cast<int>(lines_.size()) > aoa::constants::CHAT_MAX_VISIBLE_LINES) {
                lines_.pop_front();
            }
            (void)ttl_ms;
        }

        if (hook) {
            hook(player_slot, hook_text);
        }
    }

    void prune_expired_unlocked() const
    {
        const auto now = std::chrono::steady_clock::now();
        while (!lines_.empty()) {
            const int ttl_ms = lines_.front().system
                ? aoa::constants::CHAT_SYSTEM_MESSAGE_TTL_MS
                : aoa::constants::CHAT_MESSAGE_TTL_MS;
            if (now - lines_.front().created_at < std::chrono::milliseconds(ttl_ms)) {
                break;
            }

            lines_.pop_front();
        }
    }

    mutable std::mutex mutex_{};
    mutable std::deque<ChatLine> lines_{};
    std::function<void(std::uint8_t, const std::string&)> message_hook_{};
};

} // namespace aoa::app
