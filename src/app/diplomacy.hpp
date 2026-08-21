#pragma once

#include "app/game_menu.hpp"
#include "core/constants.hpp"

#include <SFML/System/Vector2.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace aoa::app {

enum class DiplomacyTab : std::uint8_t {
    Chat = 0,
    Trades = 1,
    Teams = 2,
};

enum class DiplomacyChatSubtab : std::uint8_t {
    All = 0,
    Allies = 1,
};

struct DiplomacyState {
    bool open{false};
    DiplomacyTab tab{DiplomacyTab::Chat};
    DiplomacyTab last_tab{DiplomacyTab::Chat};
    DiplomacyChatSubtab chat_subtab{DiplomacyChatSubtab::All};
    bool hud_send_allies{false};
    std::string chat_draft{};
    std::array<int, constants::MAX_PLAYER_SLOTS> trade_wood{};
    std::array<int, constants::MAX_PLAYER_SLOTS> trade_food{};
    std::array<int, constants::MAX_PLAYER_SLOTS> trade_gold{};
    std::array<int, constants::MAX_PLAYER_SLOTS> trade_mana{};
    std::array<std::uint8_t, constants::MAX_PLAYER_SLOTS> draft_ally{};
    bool chat_input_focused{false};
    bool draft_ally_victory{false};
    bool draft_initialized{false};
};

[[nodiscard]] inline GameMenuRect diplomacy_button_rect(const sf::Vector2u window_size)
{
    const GameMenuRect menu = menu_button_rect(window_size);
    const float width =
        static_cast<float>(window_size.x) * constants::HUD_DIPLOMACY_BUTTON_WIDTH_FRACTION;
    return GameMenuRect{
        menu.x - constants::HUD_DIPLOMACY_BUTTON_GAP_PX - width,
        menu.y,
        width,
        menu.height,
    };
}

[[nodiscard]] inline GameMenuRect diplomacy_panel_rect(const sf::Vector2u window_size)
{
    const float width =
        static_cast<float>(window_size.x) * constants::HUD_DIPLOMACY_WIDTH_FRACTION;
    const float height =
        static_cast<float>(window_size.y) * constants::HUD_DIPLOMACY_HEIGHT_FRACTION;
    return GameMenuRect{
        (static_cast<float>(window_size.x) - width) * 0.5F,
        (static_cast<float>(window_size.y) - height) * 0.5F,
        width,
        height,
    };
}

[[nodiscard]] inline GameMenuRect diplomacy_tab_rect(
    const sf::Vector2u window_size,
    const int tab_index)
{
    const GameMenuRect panel = diplomacy_panel_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    const float tab_width =
        (panel.width - padding * 2.0F - gap * 2.0F) / 3.0F;
    return GameMenuRect{
        panel.x + padding + static_cast<float>(tab_index) * (tab_width + gap),
        panel.y + padding,
        tab_width,
        static_cast<float>(constants::HUD_DIPLOMACY_TAB_HEIGHT_PX),
    };
}

[[nodiscard]] inline GameMenuRect diplomacy_close_rect(const sf::Vector2u window_size)
{
    const GameMenuRect panel = diplomacy_panel_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float width = static_cast<float>(constants::HUD_DIPLOMACY_CLOSE_WIDTH_PX);
    const float height = static_cast<float>(constants::HUD_DIPLOMACY_TAB_HEIGHT_PX);
    return GameMenuRect{
        panel.x + panel.width - padding - width,
        panel.y + panel.height - padding - height,
        width,
        height,
    };
}

[[nodiscard]] inline GameMenuRect diplomacy_action_rect(const sf::Vector2u window_size)
{
    const GameMenuRect panel = diplomacy_panel_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float width = static_cast<float>(constants::HUD_DIPLOMACY_ACTION_WIDTH_PX);
    const float height = static_cast<float>(constants::HUD_DIPLOMACY_TAB_HEIGHT_PX);
    return GameMenuRect{
        panel.x + padding,
        panel.y + panel.height - padding - height,
        width,
        height,
    };
}

[[nodiscard]] inline GameMenuRect diplomacy_content_rect(const sf::Vector2u window_size)
{
    const GameMenuRect panel = diplomacy_panel_rect(window_size);
    const GameMenuRect tab = diplomacy_tab_rect(window_size, 0);
    const GameMenuRect close = diplomacy_close_rect(window_size);
    const float padding = static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX);
    const float top = tab.y + tab.height + padding;
    return GameMenuRect{
        panel.x + padding,
        top,
        panel.width - padding * 2.0F,
        std::max(0.0F, close.y - padding - top),
    };
}

[[nodiscard]] inline GameMenuRect chat_channel_toggle_rect(const sf::Vector2u window_size)
{
    const float input_width =
        static_cast<float>(window_size.x) * constants::CHAT_INPUT_WIDTH_FRACTION;
    const float input_height = static_cast<float>(constants::CHAT_INPUT_HEIGHT_PX);
    const float input_x = (static_cast<float>(window_size.x) - input_width) * 0.5F;
    const float input_y = (static_cast<float>(window_size.y) - input_height) * 0.5F;
    const float width = static_cast<float>(constants::HUD_CHAT_CHANNEL_BUTTON_WIDTH_PX);
    return GameMenuRect{
        input_x - constants::HUD_DIPLOMACY_BUTTON_GAP_PX - width,
        input_y,
        width,
        input_height,
    };
}

[[nodiscard]] inline GameMenuRect tab_scoreboard_rect(const sf::Vector2u window_size)
{
    const float width = static_cast<float>(constants::HUD_TAB_SCOREBOARD_WIDTH_PX);
    const float row = static_cast<float>(constants::HUD_TAB_SCOREBOARD_ROW_HEIGHT_PX);
    const float height = row * static_cast<float>(constants::MAX_PLAYER_SLOTS)
        + static_cast<float>(constants::HUD_OPTIONS_FRAME_PADDING_PX) * 2.0F;
    return GameMenuRect{
        (static_cast<float>(window_size.x) - width) * 0.5F,
        static_cast<float>(constants::HUD_MENU_BUTTON_MARGIN_PX),
        width,
        height,
    };
}

[[nodiscard]] inline GameMenuRect diplomacy_subtab_rect(
    const sf::Vector2u window_size,
    const int subtab_index)
{
    const GameMenuRect content = diplomacy_content_rect(window_size);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    const float width = (content.width - gap) * 0.5F;
    return GameMenuRect{
        content.x + static_cast<float>(subtab_index) * (width + gap),
        content.y,
        width,
        static_cast<float>(constants::HUD_DIPLOMACY_TAB_HEIGHT_PX),
    };
}

[[nodiscard]] inline GameMenuRect diplomacy_player_row_rect(
    const sf::Vector2u window_size,
    const int row_index)
{
    const GameMenuRect content = diplomacy_content_rect(window_size);
    const float top = content.y + static_cast<float>(constants::HUD_DIPLOMACY_TAB_HEIGHT_PX)
        + static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    const float height = static_cast<float>(constants::HUD_DIPLOMACY_ROW_HEIGHT_PX);
    return GameMenuRect{
        content.x,
        top + static_cast<float>(row_index) * (height + static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX)),
        content.width,
        height,
    };
}

[[nodiscard]] inline GameMenuRect diplomacy_chat_send_rect(const sf::Vector2u window_size)
{
    const GameMenuRect content = diplomacy_content_rect(window_size);
    const float height = static_cast<float>(constants::CHAT_INPUT_HEIGHT_PX);
    const float width = static_cast<float>(constants::HUD_DIPLOMACY_SEND_WIDTH_PX);
    return GameMenuRect{
        content.x + content.width - width,
        content.y + content.height - height,
        width,
        height,
    };
}

[[nodiscard]] inline GameMenuRect diplomacy_chat_input_rect(const sf::Vector2u window_size)
{
    const GameMenuRect content = diplomacy_content_rect(window_size);
    const GameMenuRect send = diplomacy_chat_send_rect(window_size);
    const float height = static_cast<float>(constants::CHAT_INPUT_HEIGHT_PX);
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    return GameMenuRect{
        content.x,
        content.y + content.height - height,
        std::max(0.0F, send.x - gap - content.x),
        height,
    };
}

[[nodiscard]] inline GameMenuRect diplomacy_row_name_rect(const GameMenuRect& row)
{
    return GameMenuRect{row.x, row.y, row.width * constants::HUD_DIPLOMACY_NAME_WIDTH_FRACTION, row.height};
}

[[nodiscard]] inline GameMenuRect diplomacy_row_button_rect(
    const GameMenuRect& row,
    const int button_index,
    const int button_count)
{
    const float gap = static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX);
    const GameMenuRect name = diplomacy_row_name_rect(row);
    const float area_x = name.x + name.width + gap;
    const float area_width = std::max(0.0F, row.x + row.width - area_x);
    const float width =
        (area_width - gap * static_cast<float>(button_count - 1))
        / static_cast<float>(button_count);
    return GameMenuRect{
        area_x + static_cast<float>(button_index) * (width + gap),
        row.y,
        width,
        row.height,
    };
}

inline void trade_reserved_totals(
    const DiplomacyState& diplomacy,
    int& wood,
    int& food,
    int& gold,
    int& mana)
{
    wood = 0;
    food = 0;
    gold = 0;
    mana = 0;
    for (std::size_t slot = 0; slot < diplomacy.trade_wood.size(); ++slot) {
        wood += diplomacy.trade_wood[slot];
        food += diplomacy.trade_food[slot];
        gold += diplomacy.trade_gold[slot];
        mana += diplomacy.trade_mana[slot];
    }
}

[[nodiscard]] inline GameMenuRect diplomacy_ally_victory_rect(const sf::Vector2u window_size)
{
    const GameMenuRect close = diplomacy_close_rect(window_size);
    const GameMenuRect content = diplomacy_content_rect(window_size);
    return GameMenuRect{
        content.x + (content.width - static_cast<float>(constants::HUD_DIPLOMACY_ALLY_VICTORY_WIDTH_PX))
            * 0.5F,
        close.y - static_cast<float>(constants::HUD_DIPLOMACY_ROW_HEIGHT_PX)
            - static_cast<float>(constants::HUD_OPTIONS_BUTTON_GAP_PX),
        static_cast<float>(constants::HUD_DIPLOMACY_ALLY_VICTORY_WIDTH_PX),
        static_cast<float>(constants::HUD_DIPLOMACY_ROW_HEIGHT_PX),
    };
}

} // namespace aoa::app
