#pragma once
#include <expected>
#include <memory>
#include <system_error>
#include <string>
#include "types.hpp"

// Forward-declare HWND without pulling in <windows.h>.
using HWND = struct HWND__*;

namespace betty::platform {

struct window_settings {
  window_dimensions size{ default_window_size };
  std::wstring class_name{ L"betty_window_class" };
  std::wstring title{ L"betty" };
  window_show_command show_command{ default_show_command };
  // Note: no HINSTANCE — make_window() calls GetModuleHandleW(nullptr) internally.
};

// Move-only window handle. Closes the window on destruction.
struct win32_window {
  ~win32_window();
  win32_window(win32_window&& other) noexcept;
  win32_window& operator=(win32_window&& other) noexcept;

  win32_window(win32_window const&) = delete;
  win32_window& operator=(win32_window const&) = delete;

  [[nodiscard]] auto native_handle() const noexcept -> HWND { return handle_; }

private:
  struct empty_tag {};
  explicit win32_window(empty_tag) noexcept : handle_(nullptr) {}

  HWND handle_{ nullptr };

  friend auto make_window(window_settings const&)
    -> std::expected<win32_window, std::error_code>;
  friend auto make_swap_chain(struct d3d_device const&, win32_window const&, struct swap_chain_settings const&)
    -> std::expected<struct d3d_swap_chain, std::error_code>;
};

[[nodiscard]] auto make_window(window_settings const& settings)
  -> std::expected<win32_window, std::error_code>;

// Returns false when WM_QUIT is received (application should exit).
// Wraps PeekMessageW / TranslateMessage / DispatchMessageW internally.
auto dispatch_pending_messages() -> bool;

// Show a modal error message box.  Used for fatal startup errors so the
// user sees a diagnostic even though the app runs with the WINDOWS subsystem.
auto show_error_message(std::string_view title, std::string_view message) -> void;

} // namespace betty::platform
