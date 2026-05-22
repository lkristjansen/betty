#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <system_error>
#include "platform/window.hpp"
#include "platform/gfx.hpp"
#include "platform/text.hpp"
#include "platform/shell.hpp"
#include "terminal/grid.hpp"
#include "terminal/input_handler.hpp"

namespace betty {

class Application {
public:
  [[nodiscard]] int run();

private:
  explicit Application(platform::win32_window window,
                       platform::d3d_device device,
                       platform::d3d_swap_chain swap_chain,
                       platform::d3d_render_target_view rtv,
                       platform::glyph_renderer renderer,
                       std::optional<platform::shell> shell,
                       uint32_t cols, uint32_t rows,
                       bool shell_creation_failed);

  terminal::input_handler input_;
  bool shell_input_failed_ = false;
  terminal::terminal_grid grid_;
  std::optional<platform::shell> shell_;
  platform::glyph_renderer renderer_;
  platform::d3d_render_target_view rtv_;
  platform::d3d_swap_chain swap_chain_;
  platform::d3d_device device_;
  platform::win32_window window_;

  friend auto make_application() -> std::expected<Application, std::error_code>;
};

[[nodiscard]] auto make_application() -> std::expected<Application, std::error_code>;

} // namespace betty
