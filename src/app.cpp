#include "app.hpp"
#include "util/log.hpp"
#include <algorithm>

namespace betty {

Application::Application(platform::win32_window window,
                         platform::d3d_device device,
                         platform::d3d_swap_chain swap_chain,
                         platform::d3d_render_target_view rtv,
                         platform::glyph_renderer renderer,
                         std::optional<platform::shell> shell,
                         uint32_t cols, uint32_t rows,
                         bool shell_creation_failed)
    : input_()
    , shell_input_failed_(false)
    , grid_(cols, rows)
    , shell_(std::move(shell))
    , renderer_(std::move(renderer))
    , rtv_(std::move(rtv))
    , swap_chain_(std::move(swap_chain))
    , device_(std::move(device))
    , window_(std::move(window)) {
  // Forward OSC window-title changes to the title bar.
  grid_.set_observer([this](std::string_view title) {
    platform::set_window_title(window_, title);
  });

  if (shell_creation_failed) {
    grid_.write_bytes("Failed to create shell process.\r\n");
  }
}

int Application::run() {
  // Keyboard callback — forward all input to the ConPTY shell.
  // WM_KEYDOWN exclusively: on_keydown handles everything (printable,
  // control keys, arrows, Ctrl combos).  No WM_CHAR needed.
  platform::set_key_callback(window_,
    [this](platform::vk_code vk, bool ctrl, bool shift, bool alt) {
      // ── Scrollback navigation ──────────────────────────────────────
      if (ctrl && shift && !alt) {
        if (vk == platform::vk_code::arrow_up) {
          grid_.scroll_viewport(1);
          return;
        }
        if (vk == platform::vk_code::arrow_down) {
          grid_.scroll_viewport(-1);
          return;
        }
        if (vk == platform::vk_code::page_up) {
          grid_.scroll_viewport(static_cast<int32_t>(grid_.rows() > 0 ? grid_.rows() - 1 : 0));
          return;
        }
        if (vk == platform::vk_code::page_down) {
          grid_.scroll_viewport(-static_cast<int32_t>(grid_.rows() > 0 ? grid_.rows() - 1 : 0));
          return;
        }
      }

      // ── Normal shell input ─────────────────────────────────────────
      if (!shell_ || !platform::is_shell_running(*shell_)) return;
      if (shell_input_failed_) return;

      std::string bytes = input_.on_keydown(vk, ctrl, shift, alt);
      if (!bytes.empty()) {
        if (auto res = platform::write_shell_input(*shell_, bytes); !res) {
          util::log_error(res.error(), "write shell input");
          shell_input_failed_ = true;
        }
      }
    });

  // Resize callback — swap chain + renderer on every WM_SIZE;
  // grid + ConPTY only on completed resize (drag release / maximize / restore).
  platform::set_resize_callback(window_,
    [this](uint32_t width, uint32_t height, bool completed) {
      // Ignore zero-area resize (minimized window).
      if (width == 0 || height == 0) return;

      platform::window_dimensions const new_dims{width, height};

      // Always: resize swap chain + update renderer constant buffer.
      auto new_rtv = platform::resize_swap_chain(
          device_, swap_chain_, std::move(rtv_), new_dims);
      if (new_rtv) {
        rtv_ = std::move(*new_rtv);
      } else {
        util::log_error(new_rtv.error(), "resize swap chain");
        return;  // rtv_ is now empty; the RTV guard below will exit cleanly.
      }

      if (auto result = renderer_.update_dimensions(device_, new_dims);
          !result) {
        util::log_error(result.error(), "update renderer dimensions");
      }

      // Only on completed resize: recompute terminal dimensions, resize
      // the grid buffer, and notify the ConPTY shell.
      if (completed) {
        uint32_t const cell_w = renderer_.cell_width();
        uint32_t const cell_h = renderer_.cell_height();

        uint32_t const new_cols = std::max(80u, width / cell_w);
        uint32_t const new_rows = std::max(1u,  height / cell_h);

        if (new_cols != grid_.cols() || new_rows != grid_.rows()) {
          grid_.resize(new_cols, new_rows);

          if (shell_ && platform::is_shell_running(*shell_)) {
            if (auto result =
                    platform::resize_shell(*shell_, new_cols, new_rows);
                !result) {
              util::log_error(result.error(), "resize shell");
            }
          }
        }
      }
    });

  // Message loop
  int exit_code = 0;
  bool exit_notified = false;
  while (platform::dispatch_pending_messages()) {
    // Guard: if a failed resize left the RTV empty, exit cleanly.
    if (!rtv_) {
      util::log_error(
          std::make_error_code(std::errc::io_error),
          "RTV was invalidated by a failed resize, exiting");
      exit_code = 1;
      break;
    }

    device_.clear(rtv_, platform::mocha_base);

    // Read shell output
    if (shell_ && platform::is_shell_running(*shell_)) {
      std::string raw = platform::read_shell_output_raw(*shell_);
      if (!raw.empty()) {
        grid_.write_bytes(raw);
      }
    } else if (shell_ && !platform::is_shell_running(*shell_)) {
      if (!exit_notified) {
        // Restore default window title on shell exit.
        platform::set_window_title(window_, "betty");
        grid_.write_bytes("[shell exited]\r\n");
        exit_notified = true;
      }
      // Drain remaining output
      std::string raw = platform::read_shell_output_raw(*shell_);
      if (!raw.empty()) {
        grid_.write_bytes(raw);
      }
    }

    // Render the grid
    auto const cells = grid_.render_cells();
    if (!cells.empty()) {
      renderer_.prepare_unicode_glyphs(device_, cells);

      platform::size2d const dims{grid_.cols(), grid_.rows()};
      // Suppress cursor when scrolled back (pass out-of-bounds position).
      platform::point2d const cursor{
        grid_.is_following_output() ? grid_.cursor_row() : grid_.rows(),
        grid_.is_following_output() ? grid_.cursor_col() : grid_.cols()
      };
      if (auto draw_result = renderer_.draw_grid(device_, rtv_, cells, dims, cursor);
          !draw_result) {
        util::log_error(draw_result.error(), "draw grid");
        exit_code = 1;
        break;
      }
    }

    if (auto present_result = swap_chain_.present(); !present_result) {
      util::log_error(present_result.error(), "present");
      exit_code = 1;
      break;
    }
  }

  // shell_ goes out of scope here — its destructor handles cleanup.
  return exit_code;
}

auto make_application() -> std::expected<Application, std::error_code> {
  // 1. Create window
  auto window_result = platform::make_window(
    platform::window_settings{
      .size = platform::default_window_size,
      .title = "betty"
    }
  );
  if (!window_result) {
    util::log_error(window_result.error(), "create window");
    return std::unexpected(window_result.error());
  }
  auto window = std::move(*window_result);

  // 2. Create D3D11 device
  auto device_result = platform::make_device();
  if (!device_result) {
    util::log_error(device_result.error(), "create device");
    return std::unexpected(device_result.error());
  }
  auto device = std::move(*device_result);

  // 3. Create swap chain
  auto swap_chain_result = platform::make_swap_chain(
    device, window,
    platform::swap_chain_settings{
      .size = platform::default_window_size
    }
  );
  if (!swap_chain_result) {
    util::log_error(swap_chain_result.error(), "create swap chain");
    return std::unexpected(swap_chain_result.error());
  }
  auto swap_chain = std::move(*swap_chain_result);

  // 4. Create render target view
  auto rtv_result = platform::make_render_target_view(device, swap_chain);
  if (!rtv_result) {
    util::log_error(rtv_result.error(), "create render target view");
    return std::unexpected(rtv_result.error());
  }
  auto rtv = std::move(*rtv_result);

  // 5. Create glyph renderer
  auto renderer_result = platform::make_glyph_renderer(
    device, platform::default_window_size
  );
  if (!renderer_result) {
    util::log_error(renderer_result.error(), "create glyph renderer");
    return std::unexpected(renderer_result.error());
  }
  auto renderer = std::move(*renderer_result);

  // 6. Compute terminal dimensions
  uint32_t const cell_w = renderer.cell_width();
  uint32_t const cell_h = renderer.cell_height();
  uint32_t const cols = platform::default_window_size.width / cell_w;
  uint32_t const rows = platform::default_window_size.height / cell_h;

  // 7. Create shell (non-fatal: app runs without shell if it fails)
  std::optional<platform::shell> shell;
  bool shell_creation_failed = false;

  auto shell_result = platform::make_shell(platform::shell_settings{
    .cols = cols,
    .rows = rows
  });
  if (shell_result) {
    shell = std::move(*shell_result);
  } else {
    util::log_error(shell_result.error(), "Failed to create shell process");
    shell_creation_failed = true;
  }

  // Set minimum window size: 80 columns × 1 row.
  uint32_t const min_win_width  = 80 * cell_w;
  uint32_t const min_win_height = 1 * cell_h;
  platform::set_min_window_size(window, min_win_width, min_win_height);

  return Application{std::move(window), std::move(device),
                     std::move(swap_chain), std::move(rtv),
                     std::move(renderer), std::move(shell),
                     cols, rows, shell_creation_failed};
}

} // namespace betty
