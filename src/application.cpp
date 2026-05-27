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
                         terminal::terminal_session session,
                         betty_config config,
                         std::filesystem::path config_dir)
    : window_(std::move(window))
    , renderer_ctx_(std::move(renderer_ctx))
    , session_(std::move(session))
    , config_(std::move(config))
    , config_dir_(config_dir)
    , watcher_(config_dir_, config_changed_.get()) {}

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
  if (width == 0 || height == 0) return;

  auto resize_result = renderer_ctx_.handle_resize(width, height);
  if (!resize_result) {
    fatal_error_ = resize_result.error();
    return;
  }

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
// on_config_changed — hot-reload handler
// ===========================================================================

void application::on_config_changed() {
  auto result = parse_config(config_dir_);
  auto const& new_cfg = result.config;

  // Font changes.
  if (new_cfg.font_family != config_.font_family ||
      new_cfg.font_size != config_.font_size) {
    auto const family = *new_cfg.font_family;
    auto const size = *new_cfg.font_size;
    if (auto res = renderer_ctx_.recreate_font(family, size); !res) {
      util::log_error(res.error(), "hot-reload: recreate font");
      return;
    }
    config_.font_family = family;
    config_.font_size = size;
  }

  // cursor_style — update stored value (read every frame).
  if (new_cfg.cursor_style != config_.cursor_style) {
    config_.cursor_style = new_cfg.cursor_style;
  }

  // scrollback_lines.
  if (new_cfg.scrollback_lines != config_.scrollback_lines) {
    session_.resize_scrollback(*new_cfg.scrollback_lines);
    config_.scrollback_lines = new_cfg.scrollback_lines;
  }

  // columns / rows.
  bool const cols_changed = (new_cfg.columns != config_.columns);
  bool const rows_changed = (new_cfg.rows != config_.rows);
  if (cols_changed || rows_changed) {
    uint32_t const new_cols = cols_changed ? *new_cfg.columns : *config_.columns;
    uint32_t const new_rows = rows_changed ? *new_cfg.rows : *config_.rows;

    uint32_t const cell_w = renderer_ctx_.cell_width();
    uint32_t const cell_h = renderer_ctx_.cell_height();
    uint32_t const pad = platform::k_padding_px;
    auto const exact_size = compute_window_size(new_cols, new_rows, cell_w, cell_h, pad);

    window_.resize_client_area(exact_size.width, exact_size.height);
    if (auto res = renderer_ctx_.handle_resize(exact_size.width, exact_size.height); !res) {
      util::log_error(res.error(), "hot-reload: resize renderer");
      return;
    }
    session_.resize(new_cols, new_rows);

    config_.columns = new_cols;
    config_.rows = new_rows;
  }

  // shell: silently ignored (requires restart).
}

// ===========================================================================
// run
// ===========================================================================

int application::run() {
  window_.set_key_callback(
    [this](platform::vk_code vk, bool ctrl, bool shift, bool alt) {
      on_key(vk, ctrl, shift, alt);
    });

  window_.set_char_callback(
    [this](uint32_t codepoint) {
      on_char(codepoint);
    });

  window_.set_resize_callback(
    [this](uint32_t width, uint32_t height, bool completed) {
      on_resize(width, height, completed);
    });

  session_.set_observer([this](std::string_view title) {
    window_.set_window_title(title);
  });

  session_.on_exited([this] {
    window_.set_window_title("betty");
  });

  int exit_code = 0;
  while (platform::dispatch_pending_messages()) {
    if (fatal_error_) {
      util::show_fatal_error(*fatal_error_, "resize renderer");
      exit_code = 1;
      break;
    }

    renderer_ctx_.begin_frame(platform::mocha_base);

    (void)session_.process_output();

    // Check for config.toml changes (hot-reload handler in C9).
    if (config_changed_->exchange(false, std::memory_order_acquire)) {
      on_config_changed();
    }

    auto const cells = session_.render_cells();
    if (!cells.empty()) {
      platform::size2d const dims{session_.cols(), session_.rows()};
      // Cursor visibility: suppressed when cursor_style is "none",
      // or when scrolled back looking at history.
      auto const cursor = [&]() -> std::optional<platform::point2d> {
        if (config_.cursor_style == "none") return std::nullopt;
        if (!session_.is_following_output()) return std::nullopt;
        return platform::point2d{session_.cursor_row(), session_.cursor_col()};
      }();
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

  session_.shutdown();
  return exit_code;
}

// ===========================================================================
// make_application
// ===========================================================================

auto make_application(betty_config const& cfg,
                      std::filesystem::path const& config_dir)
    -> std::expected<application, std::error_code> {

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

  auto renderer_ctx_result = platform::make_renderer_context(
      window, *cfg.font_family, *cfg.font_size);
  if (!renderer_ctx_result) {
    util::show_fatal_error(renderer_ctx_result.error(), "create renderer context");
    return std::unexpected(renderer_ctx_result.error());
  }
  auto renderer_ctx = std::move(*renderer_ctx_result);

  uint32_t const cell_w = renderer_ctx.cell_width();
  uint32_t const cell_h = renderer_ctx.cell_height();
  uint32_t const pad = platform::k_padding_px;

  uint32_t const cfg_cols = *cfg.columns;
  uint32_t const cfg_rows = *cfg.rows;

  auto const exact_size = compute_window_size(cfg_cols, cfg_rows, cell_w, cell_h, pad);
  window.resize_client_area(exact_size.width, exact_size.height);

  if (auto res = renderer_ctx.handle_resize(exact_size.width, exact_size.height); !res) {
    return std::unexpected(res.error());
  }

  std::optional<platform::shell> shell;
  auto shell_result = platform::make_shell(platform::shell_settings{
    .cols = cfg_cols,
    .rows = cfg_rows,
    .command_line = *cfg.shell
  });
  if (shell_result) {
    shell = std::move(*shell_result);
  } else {
    util::log_error(shell_result.error(), "Failed to create shell process");
  }

  uint32_t const min_win_width  = k_min_columns * cell_w + 2 * pad;
  uint32_t const min_win_height = k_min_rows * cell_h + 2 * pad;
  window.set_min_window_size(min_win_width, min_win_height);

  terminal::terminal_session session(cfg_cols, cfg_rows,
                                      *cfg.scrollback_lines,
                                      std::move(shell));

  return application{std::move(window), std::move(renderer_ctx),
                     std::move(session), cfg, config_dir};
}

} // namespace betty
