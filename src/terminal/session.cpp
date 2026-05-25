#include "session.hpp"
#include "util/log.hpp"

namespace betty::terminal {
namespace {

// Try to read output from a live shell.
// Uses std::optional::and_then to form a monadic pipeline:
//   shell_ → check running → read_output → optional<string>
[[nodiscard]] auto read_from_live_shell(
    std::optional<platform::shell>& shell) -> std::optional<std::string> {
  return shell.and_then([](platform::shell& s) -> std::optional<std::string> {
    if (!platform::is_shell_running(s)) return std::nullopt;
    return platform::read_shell_output_raw(s);
  });
}

} // anonymous namespace

// ===========================================================================
// construction
// ===========================================================================

terminal_session::terminal_session(uint32_t cols, uint32_t rows,
                                   std::optional<platform::shell> shell)
    : grid_(cols, rows)
    , shell_(std::move(shell))
    , input_() {
  if (!shell_) {
    grid_.write_bytes("Failed to create shell process.\r\n");
  }
}

// ===========================================================================
// write_keyboard
// ===========================================================================

void terminal_session::write_keyboard(platform::vk_code vk, bool ctrl,
                                       bool shift, bool alt) {
  // ── Scrollback navigation ────────────────────────────────────────────
  if (ctrl && shift && !alt) {
    using platform::vk_code;
    if (vk == vk_code::arrow_up) {
      (void)grid_.scroll_viewport(1);
      return;
    }
    if (vk == vk_code::arrow_down) {
      (void)grid_.scroll_viewport(-1);
      return;
    }
    if (vk == vk_code::page_up) {
      (void)grid_.scroll_viewport(
          static_cast<int32_t>(grid_.rows() > 0 ? grid_.rows() - 1 : 0));
      return;
    }
    if (vk == vk_code::page_down) {
      (void)grid_.scroll_viewport(
          -static_cast<int32_t>(grid_.rows() > 0 ? grid_.rows() - 1 : 0));
      return;
    }
  }

  // ── Normal shell input ───────────────────────────────────────────────
  if (!shell_ || !platform::is_shell_running(*shell_)) return;
  if (shell_input_failed_) return;

  std::string bytes = input_.on_keydown(vk, ctrl, shift, alt);
  if (!bytes.empty()) {
    if (auto res = platform::write_shell_input(*shell_, bytes); !res) {
      util::log_error(res.error(), "write shell input");
      shell_input_failed_ = true;
    }
  }
}

// ===========================================================================
// process_output
// ===========================================================================

auto terminal_session::process_output() -> session_status {
  // Shell is alive: read and process output.
  if (auto raw = read_from_live_shell(shell_)) {
    if (!raw->empty()) {
      grid_.write_bytes(*raw);
    }
    return session_status::ok;
  }

  // Shell exists but dead.
  if (shell_ && !platform::is_shell_running(*shell_)) {
    if (!exit_notified_) {
      grid_.write_bytes("[shell exited]\r\n");
      exit_notified_ = true;
      return session_status::dead;
    }
    // Drain remaining output.
    std::string raw = platform::read_shell_output_raw(*shell_);
    if (!raw.empty()) {
      grid_.write_bytes(raw);
    }
    return session_status::dead;
  }

  // No shell at all.
  return session_status::ok;
}

// ===========================================================================
// scroll_viewport
// ===========================================================================

void terminal_session::scroll_viewport(int32_t delta) {
  (void)grid_.scroll_viewport(delta);
}

// ===========================================================================
// resize
// ===========================================================================

void terminal_session::resize(uint32_t cols, uint32_t rows) {
  if (cols == grid_.cols() && rows == grid_.rows()) return;

  grid_.resize(cols, rows);

  if (shell_ && platform::is_shell_running(*shell_)) {
    (void)platform::resize_shell(*shell_, cols, rows)
        .or_else([](std::error_code const& ec) -> std::expected<void, std::error_code> {
          util::log_error(ec, "resize shell");
          return {};
        });
  }
}

// ===========================================================================
// queries for rendering
// ===========================================================================

auto terminal_session::render_cells() -> std::span<const platform::render_cell> {
  return grid_.render_cells();
}

auto terminal_session::cursor_row() const -> uint32_t { return grid_.cursor_row(); }
auto terminal_session::cursor_col() const -> uint32_t { return grid_.cursor_col(); }
auto terminal_session::cols() const -> uint32_t { return grid_.cols(); }
auto terminal_session::rows() const -> uint32_t { return grid_.rows(); }
auto terminal_session::is_following_output() const -> bool { return grid_.is_following_output(); }

// ===========================================================================
// observer
// ===========================================================================

void terminal_session::set_observer(std::function<void(std::string_view)> on_title) {
  grid_.set_observer(std::move(on_title));
}

} // namespace betty::terminal
