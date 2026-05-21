#pragma once
#include <expected>
#include <functional>
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

// Callback storage (heap-allocated; pointer stored via GWLP_USERDATA).
struct window_callbacks {
  std::function<void(vk_code, bool ctrl, bool shift, bool alt)> on_key;
  std::function<void(uint32_t)> on_char;
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

  // Callback storage (heap-allocated; pointer stored via GWLP_USERDATA).
  std::unique_ptr<window_callbacks> callbacks_;

  friend auto make_window(window_settings const&)
    -> std::expected<win32_window, std::error_code>;
  friend auto make_swap_chain(struct d3d_device const&, win32_window const&, struct swap_chain_settings const&)
    -> std::expected<struct d3d_swap_chain, std::error_code>;
  friend auto set_key_callback(win32_window&, std::function<void(vk_code, bool ctrl, bool shift, bool alt)>) -> void;
  friend auto set_char_callback(win32_window&, std::function<void(uint32_t)>) -> void;
};

[[nodiscard]] auto make_window(window_settings const& settings)
  -> std::expected<win32_window, std::error_code>;

// Set key / char callbacks on an existing window.
auto set_key_callback(win32_window& window, std::function<void(vk_code, bool ctrl, bool shift, bool alt)> cb) -> void;
auto set_char_callback(win32_window& window, std::function<void(uint32_t codepoint)> cb) -> void;

// Returns false when WM_QUIT is received (application should exit).
// Wraps PeekMessageW / TranslateMessage / DispatchMessageW internally.
auto dispatch_pending_messages() -> bool;

// Show a modal error message box.  Used for fatal startup errors so the
// user sees a diagnostic even though the app runs with the WINDOWS subsystem.
auto show_error_message(std::string_view title, std::string_view message) -> void;

} // namespace betty::platform
