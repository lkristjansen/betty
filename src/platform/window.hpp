#pragma once
#include <memory>
#include <expected>
#include <system_error>
#include <string_view>
#include "types.hpp"

// Forward-declare HWND without pulling in <windows.h>.
using HWND = struct HWND__*;

namespace betty::platform {

struct window_settings {
    window_dimensions size{ default_window_size };
    std::wstring_view class_name{ L"betty_window_class" };
    std::wstring_view title{ L"betty" };
    int show_command{ 5 };  // SW_SHOW
    // Note: no HINSTANCE — make_window() calls GetModuleHandleW(nullptr) internally.
};

// Move-only window handle. Closes the window on destruction.
struct win32_window {
    win32_window();
    ~win32_window();
    win32_window(win32_window&& other) noexcept;
    win32_window& operator=(win32_window&& other) noexcept;

    win32_window(win32_window const&) = delete;
    win32_window& operator=(win32_window const&) = delete;

private:
    HWND handle_{ nullptr };

    friend auto make_window(window_settings const&)
        -> std::expected<win32_window, std::error_code>;
    friend auto make_swap_chain(struct d3d_device const&, win32_window const&, struct swap_chain_settings const&)
        -> std::expected<struct d3d_swap_chain, std::error_code>;
};

auto make_window(window_settings const& settings)
    -> std::expected<win32_window, std::error_code>;

// Returns false when WM_QUIT is received (application should exit).
// Wraps PeekMessageW / TranslateMessage / DispatchMessageW internally.
auto dispatch_pending_messages() -> bool;

} // namespace betty::platform
