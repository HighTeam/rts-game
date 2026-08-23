#pragma once

#include "core/constants.hpp"

#include <SFML/System/String.hpp>
#include <SFML/Window/Clipboard.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace aoa::app {

enum class TextFieldFilter : std::uint8_t {
    Printable = 0,
    Digits = 1,
};

struct TextFieldEdit {
    std::string& text;
    std::size_t max_length{0U};
    bool& all_selected;
    TextFieldFilter filter{TextFieldFilter::Printable};
};

[[nodiscard]] inline bool text_field_char_allowed(
    const char character,
    const TextFieldFilter filter)
{
    if (filter == TextFieldFilter::Digits) {
        return std::isdigit(static_cast<unsigned char>(character)) != 0;
    }

    const auto code = static_cast<unsigned char>(character);
    return code >= static_cast<unsigned char>(constants::MAIN_MENU_MIN_PRINTABLE_CHAR)
        && code <= static_cast<unsigned char>(constants::MAIN_MENU_MAX_PRINTABLE_CHAR);
}

inline void copy_text_field_to_clipboard(const std::string& text)
{
    sf::Clipboard::setString(sf::String(text));
}

[[nodiscard]] inline std::string clipboard_filtered_text(const TextFieldFilter filter)
{
    const sf::String raw = sf::Clipboard::getString();
    std::string out{};
    out.reserve(raw.getSize());
    for (std::size_t index = 0U; index < raw.getSize(); ++index) {
        const char32_t code = raw[index];
        if (code < 32U || code > 126U) {
            continue;
        }

        const char character = static_cast<char>(code);
        if (!text_field_char_allowed(character, filter)) {
            continue;
        }

        out.push_back(character);
    }

    return out;
}

inline void append_text_field_chars(
    TextFieldEdit& field,
    const std::string_view incoming)
{
    if (field.all_selected) {
        field.text.clear();
        field.all_selected = false;
    }

    for (const char character : incoming) {
        if (field.text.size() >= field.max_length) {
            break;
        }

        if (!text_field_char_allowed(character, field.filter)) {
            continue;
        }

        field.text.push_back(character);
    }

    if (field.text.empty()) {
        field.all_selected = false;
    }
}

[[nodiscard]] inline bool apply_text_field_hotkey(
    const sf::Keyboard::Key key,
    const bool control,
    TextFieldEdit& field)
{
    if (!control) {
        return false;
    }

    if (key == sf::Keyboard::Key::A) {
        field.all_selected = !field.text.empty();
        return true;
    }

    if (key == sf::Keyboard::Key::C) {
        copy_text_field_to_clipboard(field.text);
        return true;
    }

    if (key == sf::Keyboard::Key::V) {
        append_text_field_chars(field, clipboard_filtered_text(field.filter));
        return true;
    }

    return false;
}

inline bool apply_text_field_backspace(TextFieldEdit& field)
{
    if (field.all_selected) {
        field.text.clear();
        field.all_selected = false;
        return true;
    }

    if (field.text.empty()) {
        field.all_selected = false;
        return false;
    }

    field.text.pop_back();
    if (field.text.empty()) {
        field.all_selected = false;
    }
    return true;
}

} // namespace aoa::app
