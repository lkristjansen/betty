#pragma once
#include <atomic>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <system_error>
#include "config/config.hpp"
#include "config/config_watcher.hpp"
#include "platform/window.hpp"
#include "platform/renderer_context.hpp"
#include "terminal/session.hpp"

namespace betty {

// ===========================================================================
// application — owns the window, renderer context, and terminal session
// ===========================================================================

class application {
public:
  [[nodiscard]] int run();

private:
  explicit application(platform::win32_window window,
                       platform::renderer_context renderer_ctx,
                       terminal::terminal_session session,
                       betty_config config,
                       std::filesystem::path config_dir);

  void on_key(platform::vk_code vk, bool ctrl, bool shift, bool alt);
  void on_char(uint32_t codepoint);
  void on_resize(uint32_t width, uint32_t height, bool completed);
  void on_config_changed();

  platform::win32_window window_;
  platform::renderer_context renderer_ctx_;
  terminal::terminal_session session_;
  betty_config config_;
  std::filesystem::path config_dir_;
  std::unique_ptr<std::atomic<bool>> config_changed_ =
      std::make_unique<std::atomic<bool>>(false);
  config_watcher watcher_;
  std::optional<std::error_code> fatal_error_;

  friend auto make_application(betty_config const&, std::filesystem::path const&)
      -> std::expected<application, std::error_code>;
};

// ===========================================================================
// compute_window_size
// ===========================================================================

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
// make_application
// ===========================================================================

[[nodiscard]] auto make_application(betty_config const& cfg,
                                     std::filesystem::path const& config_dir)
    -> std::expected<application, std::error_code>;

} // namespace betty
