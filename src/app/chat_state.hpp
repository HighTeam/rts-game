#pragma once

#include "core/constants.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace aoa::app {

struct ChatTextSpan {
    std::string text{};
    bool use_player_color{false};
    std::uint8_t color_player_slot{0U};
};

struct ChatLine {
    std::uint8_t player_slot{0U};
    std::string text{};
    std::vector<ChatTextSpan> spans{};
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

    void push_system_spans(std::vector<ChatTextSpan> spans)
    {
        if (spans.empty()) {
            return;
        }

        std::string combined{};
        for (const ChatTextSpan& span : spans) {
            combined += span.text;
        }

        ChatLine line{};
        line.player_slot = constants::CHAT_SYSTEM_PLAYER_SLOT;
        line.text = std::move(combined);
        line.spans = std::move(spans);
        line.created_at = std::chrono::steady_clock::now();
        line.system = true;
        if (line.text.size() > static_cast<std::size_t>(aoa::constants::CHAT_MAX_MESSAGE_LENGTH)) {
            line.text.resize(static_cast<std::size_t>(aoa::constants::CHAT_MAX_MESSAGE_LENGTH));
        }

        std::lock_guard lock(mutex_);
        prune_expired_unlocked();
        lines_.push_back(std::move(line));
        while (static_cast<int>(lines_.size()) > aoa::constants::CHAT_MAX_VISIBLE_LINES) {
            lines_.pop_front();
        }
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
                .player_slot = player_slot,
                .text = std::move(text),
                .created_at = std::chrono::steady_clock::now(),
                .system = system,
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
