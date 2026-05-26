#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <system_error>
#include "platform/window.hpp"
#include "platform/renderer_context.hpp"
#include "terminal/session.hpp"

namespace betty {

// ===========================================================================
// application — owns the window, renderer context, and terminal session
// ===========================================================================
// Wires the window callbacks to the session (keyboard input, resize) and
// runs the main message + render loop.

class application {
public:
  [[nodiscard]] int run();

private:
  explicit application(platform::win32_window window,
                       platform::renderer_context renderer_ctx,
                       terminal::terminal_session session);

  // Callback handlers — wrapped in lambdas passed to the window.
  void on_key(platform::vk_code vk, bool ctrl, bool shift, bool alt);
  void on_char(uint32_t codepoint);
  void on_resize(uint32_t width, uint32_t height, bool completed);

  platform::win32_window window_;
  platform::renderer_context renderer_ctx_;
  terminal::terminal_session session_;
  bool session_dead_ = false;
  std::optional<std::error_code> fatal_error_;

  friend auto make_application() -> std::expected<application, std::error_code>;
};

// Create the application (window, GPU resources, terminal session, shell).
[[nodiscard]] auto make_application() -> std::expected<application, std::error_code>;

} // namespace betty
