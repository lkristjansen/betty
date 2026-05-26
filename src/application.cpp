#include "application.hpp"
#include "util/log.hpp"
#include <algorithm>
#include <optional>

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
  // NOTE: the OSC window-title observer is installed in run(), not here.
  // `application` may be moved (e.g. through the std::expected returned by
  // make_application) between construction and run(); capturing `this` here
  // would leave the observer pointing at a moved-from object, so the title
  // updates would silently fire against a dangling window_ reference.
}

// ===========================================================================
// callback handlers
// ===========================================================================

void application::on_key(platform::vk_code vk, bool ctrl, bool shift, bool alt) {
  session_.write_keyboard(vk, ctrl, shift, alt);
}

void application::on_char(uint32_t codepoint) {
  session_.write_char(codepoint);
}

void application::on_resize(uint32_t width, uint32_t height, bool completed) {
  // Ignore zero-area resize (minimized window).
  if (width == 0 || height == 0) return;

  auto resize_result = renderer_ctx_.handle_resize(width, height);
  if (!resize_result) {
    fatal_error_ = resize_result.error();
    return;
  }

  // Only on completed resize: recompute terminal dimensions and resize
  // the session (grid + shell).
  if (completed) {
    uint32_t const cell_w = renderer_ctx_.cell_width();
    uint32_t const cell_h = renderer_ctx_.cell_height();
    uint32_t const pad = platform::k_padding_px;

    uint32_t const new_cols = std::max(k_min_columns, (width - 2 * pad) / cell_w);
    uint32_t const new_rows = std::max(k_min_rows, (height - 2 * pad) / cell_h);

    if (new_cols != session_.cols() || new_rows != session_.rows()) {
      session_.resize(new_cols, new_rows);
    }
  }
}

// ===========================================================================
// run
// ===========================================================================

int application::run() {
  // Keyboard callback (WM_KEYDOWN — non-printable keys, Ctrl+letter).
  platform::set_key_callback(window_,
    [this](platform::vk_code vk, bool ctrl, bool shift, bool alt) {
      on_key(vk, ctrl, shift, alt);
    });

  // Character callback (WM_CHAR — printable Unicode, layout-translated).
  platform::set_char_callback(window_,
    [this](uint32_t codepoint) {
      on_char(codepoint);
    });

  // Resize callback.
  platform::set_resize_callback(window_,
    [this](uint32_t width, uint32_t height, bool completed) {
      on_resize(width, height, completed);
    });

  // Forward OSC window-title changes to the title bar.  Installed here
  // (not in the constructor) because `this` must refer to the final,
  // post-move address of the application.
  session_.set_observer([this](std::string_view title) {
    platform::set_window_title(window_, title);
  });

  // Message loop.
  int exit_code = 0;
  while (platform::dispatch_pending_messages()) {
    // Guard: if a resize failure was detected, exit with the real error.
    if (fatal_error_) {
      util::show_fatal_error(*fatal_error_, "resize renderer");
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
      // Suppress cursor when scrolled back.
      auto const cursor = session_.is_following_output()
          ? std::optional<platform::point2d>{}
          : std::optional<platform::point2d>{{session_.cursor_row(), session_.cursor_col()}};
      if (auto draw_result = renderer_ctx_.draw_grid(cells, dims, cursor, platform::k_padding_px);
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

  // 3. Compute initial terminal dimensions (accounting for padding).
  uint32_t const cell_w = renderer_ctx.cell_width();
  uint32_t const cell_h = renderer_ctx.cell_height();
  uint32_t const pad = platform::k_padding_px;
  uint32_t const cols = (platform::default_window_size.width - 2 * pad) / cell_w;
  uint32_t const rows = (platform::default_window_size.height - 2 * pad) / cell_h;

  // 4. Create shell (non-fatal: app runs without shell if it fails).
  std::optional<platform::shell> shell;
  auto shell_result = platform::make_shell(platform::shell_settings{
    .cols = cols,
    .rows = rows
  });
  if (shell_result) {
    shell = std::move(*shell_result);
  } else {
    util::log_error(shell_result.error(), "Failed to create shell process");
  }

  // 5. Set minimum window size with padding accounted for.
  uint32_t const min_win_width  = k_min_columns * cell_w + 2 * pad;
  uint32_t const min_win_height = k_min_rows * cell_h + 2 * pad;
  platform::set_min_window_size(window, min_win_width, min_win_height);

  // 6. Create terminal session.
  terminal::terminal_session session(cols, rows, std::move(shell));

  return application{std::move(window), std::move(renderer_ctx), std::move(session)};
}

} // namespace betty
