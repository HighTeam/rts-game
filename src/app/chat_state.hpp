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
        if (text.empty()) {
            return;
        }

        if (text.size() > static_cast<std::size_t>(aoa::constants::CHAT_MAX_MESSAGE_LENGTH)) {
            text.resize(static_cast<std::size_t>(aoa::constants::CHAT_MAX_MESSAGE_LENGTH));
        }

        std::function<void(std::uint8_t, const std::string&)> hook{};
        {
            std::lock_guard lock(mutex_);
            prune_expired_unlocked();
            lines_.push_back(ChatLine{
                player_slot,
                text,
                std::chrono::steady_clock::now(),
            });
            while (static_cast<int>(lines_.size()) > aoa::constants::CHAT_MAX_VISIBLE_LINES) {
                lines_.pop_front();
            }
            hook = message_hook_;
        }

        if (hook) {
            hook(player_slot, text);
        }
    }

    [[nodiscard]] std::deque<ChatLine> snapshot() const
    {
        std::lock_guard lock(mutex_);
        prune_expired_unlocked();
        return lines_;
    }

private:
    void prune_expired_unlocked() const
    {
        const auto now = std::chrono::steady_clock::now();
        const auto ttl = std::chrono::milliseconds(aoa::constants::CHAT_MESSAGE_TTL_MS);
        while (!lines_.empty() && now - lines_.front().created_at >= ttl) {
            lines_.pop_front();
        }
    }

    mutable std::mutex mutex_{};
    mutable std::deque<ChatLine> lines_{};
    std::function<void(std::uint8_t, const std::string&)> message_hook_{};
};

} // namespace aoa::app
