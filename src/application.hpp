#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <system_error>
#include "config/config.hpp"
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
                       terminal::terminal_session session,
                       betty_config config);

  // Callback handlers — wrapped in lambdas passed to the window.
  void on_key(platform::vk_code vk, bool ctrl, bool shift, bool alt);
  void on_char(uint32_t codepoint);
  void on_resize(uint32_t width, uint32_t height, bool completed);

  platform::win32_window window_;
  platform::renderer_context renderer_ctx_;
  terminal::terminal_session session_;
  betty_config config_;
  std::optional<std::error_code> fatal_error_;

  friend auto make_application(betty_config const&)
      -> std::expected<application, std::error_code>;
};

// ===========================================================================
// compute_window_size — utility for tests and callers
// ===========================================================================
// Compute the pixel dimensions for a client area that exactly fits `cols` ×
// `rows` grid cells with `padding` pixels on all sides.

[[nodiscard]] inline auto compute_window_size(uint32_t cols, uint32_t rows,
                                               uint32_t cell_w, uint32_t cell_h,
                                               uint32_t padding)
    -> platform::window_dimensions {
  return {
    cols * cell_w + 2 * padding,
    rows * cell_h + 2 * padding
  };
}

// ===========================================================================
// make_application — create the full application stack
// ===========================================================================
// Accepts a validated betty_config.  Creates the window, renderer context,
// shell, and terminal session, sizing the window to match configured
// columns × rows.

[[nodiscard]] auto make_application(betty_config const& cfg)
    -> std::expected<application, std::error_code>;

} // namespace betty
