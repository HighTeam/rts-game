#include "app/native_file_dialog.hpp"

#include "core/constants.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

namespace aoa::app {

namespace {

#ifdef _WIN32

[[nodiscard]] std::wstring utf8_to_wide(const std::string& text)
{
    if (text.empty()) {
        return {};
    }

    const int wide_count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wide_count <= 0) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(wide_count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), wide_count) <= 0) {
        return {};
    }

    if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
    }

    return wide;
}

[[nodiscard]] std::string wide_to_utf8(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }

    const int utf8_count =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8_count <= 0) {
        return {};
    }

    std::string utf8(static_cast<std::size_t>(utf8_count), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), utf8_count, nullptr, nullptr)
        <= 0) {
        return {};
    }

    if (!utf8.empty() && utf8.back() == '\0') {
        utf8.pop_back();
    }

    return utf8;
}

[[nodiscard]] std::wstring make_file_filter(
    const std::string& filter_description,
    const std::string& filter_pattern)
{
    const std::wstring description = utf8_to_wide(filter_description);
    const std::wstring pattern = utf8_to_wide(filter_pattern);
    std::wstring filter;
    filter.append(description);
    filter.push_back(L'\0');
    filter.append(pattern);
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    return filter;
}

#endif

} // namespace

std::optional<std::filesystem::path> pick_existing_file(
    sf::Window& window,
    const std::filesystem::path& initial_directory,
    const std::string& filter_description,
    const std::string& filter_pattern)
{
#ifdef _WIN32
    std::error_code error{};
    std::filesystem::create_directories(initial_directory, error);

    std::wstring filter = make_file_filter(filter_description, filter_pattern);
    const std::wstring initial_dir = initial_directory.wstring();
    std::vector<wchar_t> file_buffer(
        static_cast<std::size_t>(constants::NATIVE_FILE_DIALOG_PATH_CHARS),
        L'\0');

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window.getNativeHandle();
    dialog.lpstrFilter = filter.c_str();
    dialog.nFilterIndex = static_cast<DWORD>(constants::NATIVE_FILE_DIALOG_FIRST_FILTER_INDEX);
    dialog.lpstrFile = file_buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(file_buffer.size());
    dialog.lpstrInitialDir = initial_dir.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR
        | OFN_HIDEREADONLY;

    const BOOL accepted = GetOpenFileNameW(&dialog);
    (void)window.setActive(true);
    if (accepted == FALSE) {
        return std::nullopt;
    }

    const std::string selected = wide_to_utf8(std::wstring(file_buffer.data()));
    if (selected.empty()) {
        return std::nullopt;
    }

    return std::filesystem::path(selected);
#else
    (void)window;
    (void)initial_directory;
    (void)filter_description;
    (void)filter_pattern;
    return std::nullopt;
#endif
}

std::optional<std::filesystem::path> pick_save_file(
    sf::Window& window,
    const std::filesystem::path& initial_directory,
    const std::string& filter_description,
    const std::string& filter_pattern,
    const std::string& default_name)
{
#ifdef _WIN32
    std::error_code error{};
    std::filesystem::create_directories(initial_directory, error);

    std::wstring filter = make_file_filter(filter_description, filter_pattern);
    const std::wstring initial_dir = initial_directory.wstring();
    std::vector<wchar_t> file_buffer(
        static_cast<std::size_t>(constants::NATIVE_FILE_DIALOG_PATH_CHARS),
        L'\0');
    const std::wstring default_wide = utf8_to_wide(default_name);
    if (!default_wide.empty() && default_wide.size() < file_buffer.size()) {
        std::copy(default_wide.begin(), default_wide.end(), file_buffer.begin());
    }

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window.getNativeHandle();
    dialog.lpstrFilter = filter.c_str();
    dialog.nFilterIndex = static_cast<DWORD>(constants::NATIVE_FILE_DIALOG_FIRST_FILTER_INDEX);
    dialog.lpstrFile = file_buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(file_buffer.size());
    dialog.lpstrInitialDir = initial_dir.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT
        | OFN_HIDEREADONLY;

    const BOOL accepted = GetSaveFileNameW(&dialog);
    (void)window.setActive(true);
    if (accepted == FALSE) {
        return std::nullopt;
    }

    const std::string selected = wide_to_utf8(std::wstring(file_buffer.data()));
    if (selected.empty()) {
        return std::nullopt;
    }

    return std::filesystem::path(selected);
#else
    (void)window;
    (void)initial_directory;
    (void)filter_description;
    (void)filter_pattern;
    (void)default_name;
    return std::nullopt;
#endif
}

void open_url_in_browser(const std::string& url)
{
#ifdef _WIN32
    const std::wstring wide_url = utf8_to_wide(url);
    if (wide_url.empty()) {
        return;
    }

    (void)ShellExecuteW(nullptr, L"open", wide_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)url;
#endif
}

} // namespace aoa::app
