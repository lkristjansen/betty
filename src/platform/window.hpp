#pragma once
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <system_error>
#include <string>
#include "types.hpp"

namespace betty::platform {

namespace detail {
// Callback storage (heap-allocated; pointer stored via GWLP_USERDATA).
// Internal implementation detail — not part of the public API.
struct window_callbacks {
  std::function<void(vk_code, bool ctrl, bool shift, bool alt)> on_key;
  std::function<void(uint32_t)> on_char;
  // Resize callback — called on WM_SIZE (completed=false during drag,
  // completed=true for maximize/restore) and WM_EXITSIZEMOVE (completed=true).
  std::function<void(uint32_t width, uint32_t height, bool completed)> on_resize;
  // Minimum client area dimensions for WM_GETMINMAXINFO.
  uint32_t min_client_width  = 0;
  uint32_t min_client_height = 0;
};
} // namespace detail

// Opaque window handle — callers can only check validity.
struct window_handle {
  [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
private:
  explicit window_handle(void* h) noexcept : handle_(h) {}
  void* handle_;
  friend struct win32_window;
  friend auto make_swap_chain(struct d3d_device const&, win32_window const&, struct swap_chain_settings const&)
    -> std::expected<struct d3d_swap_chain, std::error_code>;
};

struct window_settings {
  window_dimensions size{ default_window_size };
  std::string class_name{ "betty_window_class" };
  std::string title{ "betty" };
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

  [[nodiscard]] auto native_handle() const noexcept -> window_handle;

private:
  [[nodiscard]] auto as_hwnd() const noexcept -> void* { return handle_; }

  struct empty_tag {};
  explicit win32_window(empty_tag) noexcept : handle_(nullptr) {}

  void* handle_{ nullptr };

  // Callback storage (heap-allocated; pointer stored via GWLP_USERDATA).
  std::unique_ptr<detail::window_callbacks> callbacks_;

  friend auto make_window(window_settings const&)
    -> std::expected<win32_window, std::error_code>;
  friend auto make_swap_chain(struct d3d_device const&, win32_window const&, struct swap_chain_settings const&)
    -> std::expected<struct d3d_swap_chain, std::error_code>;
  friend auto set_key_callback(win32_window&, std::function<void(vk_code, bool ctrl, bool shift, bool alt)>) -> void;
  friend auto set_char_callback(win32_window&, std::function<void(uint32_t)>) -> void;
  friend auto set_window_title(win32_window&, std::string_view) -> void;
  friend auto set_resize_callback(win32_window&, std::function<void(uint32_t, uint32_t, bool)>) -> void;
  friend auto set_min_window_size(win32_window&, uint32_t, uint32_t) -> void;
  friend auto get_client_size(win32_window const&) -> window_dimensions;
};

[[nodiscard]] auto make_window(window_settings const& settings)
  -> std::expected<win32_window, std::error_code>;

// Set key / char callbacks on an existing window.
auto set_key_callback(win32_window& window, std::function<void(vk_code, bool ctrl, bool shift, bool alt)> cb) -> void;
auto set_char_callback(win32_window& window, std::function<void(uint32_t codepoint)> cb) -> void;

// Returns false when WM_QUIT is received (application should exit).
// Wraps PeekMessageW / TranslateMessage / DispatchMessageW internally.
[[nodiscard]] auto dispatch_pending_messages() -> bool;

// Show a modal error message box.  Used for fatal startup errors so the
// user sees a diagnostic even though the app runs with the WINDOWS subsystem.
auto show_error_message(std::string_view title, std::string_view message) -> void;

// Set the window title bar text.  Converts UTF-8 to UTF-16 internally.
auto set_window_title(win32_window& window, std::string_view title) -> void;

// Set a callback for window resize events.
// Called on WM_SIZE (completed=false during drag; completed=true for maximize/restore)
// and on WM_EXITSIZEMOVE (completed=true after drag finishes).
auto set_resize_callback(win32_window& window,
    std::function<void(uint32_t width, uint32_t height, bool completed)> cb) -> void;

// Set the minimum client area size enforced via WM_GETMINMAXINFO.
auto set_min_window_size(win32_window& window,
    uint32_t client_width, uint32_t client_height) -> void;

// Query the current client-area dimensions of the window.
[[nodiscard]] auto get_client_size(win32_window const& window) -> window_dimensions;

} // namespace betty::platform
