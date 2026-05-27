#pragma once
#include <cstdint>
#include <expected>
#include <functional>
#include <utility>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include "types.hpp"

namespace betty::platform {

// --- Callback type aliases -------------------------------------------------

using on_key_callback    = std::move_only_function<void(vk_code, bool ctrl, bool shift, bool alt)>;
using on_char_callback   = std::move_only_function<void(uint32_t codepoint)>;
using on_resize_callback = std::move_only_function<void(uint32_t width, uint32_t height, bool completed)>;

namespace detail {
// Callback storage and constraints (heap-allocated; pointer stored via
// GWLP_USERDATA).  Internal implementation detail.
struct window_callbacks {
  on_key_callback on_key;
  on_char_callback on_char;
  // Resize callback — called on WM_SIZE (completed=false during drag,
  // completed=true for maximize/restore) and WM_EXITSIZEMOVE (completed=true).
  on_resize_callback on_resize;
};

struct window_state {
  window_callbacks callbacks;
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

  // --- Callback setters ----------------------------------------------------

  auto set_key_callback(on_key_callback cb) -> void;
  auto set_char_callback(on_char_callback cb) -> void;
  auto set_resize_callback(on_resize_callback cb) -> void;

  // --- Configuration -------------------------------------------------------

  auto set_window_title(std::string_view title) -> void;
  auto set_min_window_size(uint32_t client_width, uint32_t client_height) -> void;
  [[nodiscard]] auto get_client_size() const -> window_dimensions;

  // Resize the client area to the given pixel dimensions (preserves Z-order
  // and position).  Uses SetWindowPos internally.
  auto resize_client_area(uint32_t client_width, uint32_t client_height) -> void;

private:
  [[nodiscard]] auto as_hwnd() const noexcept -> void* { return handle_; }

  struct empty_tag {};
  explicit win32_window(empty_tag) noexcept : handle_(nullptr) {}

  void* handle_{ nullptr };

  // State storage (heap-allocated; pointer stored via GWLP_USERDATA).
  std::unique_ptr<detail::window_state> state_;

  friend auto make_window(window_settings const&)
    -> std::expected<win32_window, std::error_code>;
  friend auto make_swap_chain(struct d3d_device const&, win32_window const&, struct swap_chain_settings const&)
    -> std::expected<struct d3d_swap_chain, std::error_code>;
  friend auto make_renderer_context(win32_window const&, std::string_view, float)
    -> std::expected<class renderer_context, std::error_code>;
};

[[nodiscard]] auto make_window(window_settings const& settings)
  -> std::expected<win32_window, std::error_code>;

// Returns false when WM_QUIT is received (application should exit).
// Wraps PeekMessageW / TranslateMessage / DispatchMessageW internally.
[[nodiscard]] auto dispatch_pending_messages() -> bool;

// Show a modal error message box.  Used for fatal startup errors so the
// user sees a diagnostic even though the app runs with the WINDOWS subsystem.
auto show_error_message(std::string_view title, std::string_view message) -> void;

} // namespace betty::platform
