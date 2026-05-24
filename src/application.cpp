#include "application.hpp"
#include "util/log.hpp"
#include <algorithm>

namespace {

constexpr uint32_t k_min_columns = 80;
constexpr uint32_t k_min_rows    = 1;

} // anonymous namespace

namespace betty {

// ===========================================================================
// construction
// ===========================================================================

application::application(platform::win32_window window,
                         platform::renderer_context renderer_ctx,
                         terminal::terminal_session session)
    : window_(std::move(window))
    , renderer_ctx_(std::move(renderer_ctx))
    , session_(std::move(session)) {
  // Forward OSC window-title changes to the title bar.
  session_.set_observer([this](std::string_view title) {
    platform::set_window_title(window_, title);
  });
}

// ===========================================================================
// callback handlers
// ===========================================================================

void application::on_key(platform::vk_code vk, bool ctrl, bool shift, bool alt) {
  session_.write_keyboard(vk, ctrl, shift, alt);
}

void application::on_resize(uint32_t width, uint32_t height, bool completed) {
  // Ignore zero-area resize (minimized window).
  if (width == 0 || height == 0) return;

  renderer_ctx_.handle_resize(width, height);

  // Only on completed resize: recompute terminal dimensions and resize
  // the session (grid + shell).
  if (completed && renderer_ctx_.is_valid()) {
    uint32_t const cell_w = renderer_ctx_.cell_width();
    uint32_t const cell_h = renderer_ctx_.cell_height();

    uint32_t const new_cols = std::max(k_min_columns, width / cell_w);
    uint32_t const new_rows = std::max(k_min_rows, height / cell_h);

    if (new_cols != session_.cols() || new_rows != session_.rows()) {
      session_.resize(new_cols, new_rows);
    }
  }
}

// ===========================================================================
// run
// ===========================================================================

int application::run() {
  // Keyboard callback.
  platform::set_key_callback(window_,
    [this](platform::vk_code vk, bool ctrl, bool shift, bool alt) {
      on_key(vk, ctrl, shift, alt);
    });

  // Resize callback.
  platform::set_resize_callback(window_,
    [this](uint32_t width, uint32_t height, bool completed) {
      on_resize(width, height, completed);
    });

  // Message loop.
  int exit_code = 0;
  while (platform::dispatch_pending_messages()) {
    // Guard: if a failed resize left the RTV empty, exit cleanly.
    if (!renderer_ctx_.is_valid()) {
      util::log_error(
          std::make_error_code(std::errc::io_error),
          "RTV was invalidated by a failed resize, exiting");
      exit_code = 1;
      break;
    }

    renderer_ctx_.begin_frame(platform::mocha_base);

    // Read shell output.
    auto const status = session_.process_output();
    if (status == terminal::session_status::dead && !session_dead_) {
      session_dead_ = true;
      // Restore default window title on shell exit.
      platform::set_window_title(window_, "betty");
    }

    // Render the grid.
    auto const cells = session_.render_cells();
    if (!cells.empty()) {
      platform::size2d const dims{session_.cols(), session_.rows()};
      // Suppress cursor when scrolled back (pass out-of-bounds position).
      platform::point2d const cursor{
        session_.is_following_output() ? session_.cursor_row() : session_.rows(),
        session_.is_following_output() ? session_.cursor_col() : session_.cols()
      };
      if (auto draw_result = renderer_ctx_.draw_grid(cells, dims, cursor);
          !draw_result) {
        util::log_error(draw_result.error(), "draw grid");
        exit_code = 1;
        break;
      }
    }

    if (auto present_result = renderer_ctx_.end_frame(); !present_result) {
      util::log_error(present_result.error(), "present");
      exit_code = 1;
      break;
    }
  }

  // shell_ goes out of scope via terminal_session destruction.
  return exit_code;
}

// ===========================================================================
// make_application
// ===========================================================================

auto make_application() -> std::expected<application, std::error_code> {
  // 1. Create window.
  auto window_result = platform::make_window(
    platform::window_settings{
      .size = platform::default_window_size,
      .title = "betty"
    }
  );
  if (!window_result) {
    util::show_fatal_error(window_result.error(), "create window");
    return std::unexpected(window_result.error());
  }
  auto window = std::move(*window_result);

  // 2. Create renderer context (device, swap chain, RTV, glyph renderer).
  auto renderer_ctx_result = platform::make_renderer_context(window);
  if (!renderer_ctx_result) {
    util::show_fatal_error(renderer_ctx_result.error(), "create renderer context");
    return std::unexpected(renderer_ctx_result.error());
  }
  auto renderer_ctx = std::move(*renderer_ctx_result);

  // 3. Compute initial terminal dimensions.
  uint32_t const cell_w = renderer_ctx.cell_width();
  uint32_t const cell_h = renderer_ctx.cell_height();
  uint32_t const cols = platform::default_window_size.width / cell_w;
  uint32_t const rows = platform::default_window_size.height / cell_h;

  // 4. Create shell (non-fatal: app runs without shell if it fails).
  std::optional<platform::shell> shell;
  auto shell_result = platform::make_shell(platform::shell_settings{
    .cols = cols,
    .rows = rows
  });
  if (shell_result) {
    shell = std::move(*shell_result);
  } else {
    util::show_fatal_error(shell_result.error(), "Failed to create shell process");
  }

  // 5. Set minimum window size.
  uint32_t const min_win_width  = k_min_columns * cell_w;
  uint32_t const min_win_height = k_min_rows * cell_h;
  platform::set_min_window_size(window, min_win_width, min_win_height);

  // 6. Create terminal session.
  terminal::terminal_session session(cols, rows, std::move(shell));

  return application{std::move(window), std::move(renderer_ctx), std::move(session)};
}

} // namespace betty
