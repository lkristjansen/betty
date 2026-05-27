#include "session.hpp"
#include "util/log.hpp"
#include "util/utf8.hpp"

namespace betty::terminal {
namespace {

// Try to read output from a live shell.
// Uses std::optional::and_then to form a monadic pipeline:
//   shell_ → check running → read_output → optional<string>
[[nodiscard]] auto read_from_live_shell(
    std::optional<platform::shell>& shell) -> std::optional<std::string> {
  return shell.and_then([](platform::shell& s) -> std::optional<std::string> {
    if (!s.is_running()) return std::nullopt;
    return s.read_output();
  });
}

} // anonymous namespace

// ===========================================================================
// construction
// ===========================================================================

terminal_session::terminal_session(uint32_t cols, uint32_t rows,
                                   uint32_t scrollback_max_lines,
                                   std::optional<platform::shell> shell)
    : grid_(cols, rows, scrollback_max_lines)
    , shell_(std::move(shell))
    , input_() {
  if (!shell_) {
    feed_bytes("Failed to create shell process.\r\n");
  }
}

// ===========================================================================
// feed_bytes
// ===========================================================================

void terminal_session::feed_bytes(std::string_view data) {
  for (unsigned char const b : data) {
    for (auto const& a : parser_.parse(b)) {
      grid_.apply(a);
    }
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
  if (!shell_ || !shell_->is_running()) return;
  if (shell_input_failed_) return;

  std::string bytes = input_.on_keydown(vk, ctrl, shift, alt);
  if (!bytes.empty()) {
    if (auto res = shell_->write_input( bytes); !res) {
      util::log_error(res.error(), "write shell input");
      shell_input_failed_ = true;
    }
  }
}

// ===========================================================================
// write_char
// ===========================================================================

void terminal_session::write_char(uint32_t codepoint) {
  if (!shell_ || !shell_->is_running()) return;
  if (shell_input_failed_) return;

  std::string bytes = util::utf8_encode(codepoint);
  if (!bytes.empty()) {
    if (auto res = shell_->write_input( bytes); !res) {
      util::log_error(res.error(), "write shell char input");
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
      feed_bytes(*raw);
    }
    return session_status::ok;
  }

  // Shell exists but dead.
  if (shell_ && !shell_->is_running()) {
    if (!exit_notified_) {
      feed_bytes("[shell exited]\r\n");
      exit_notified_ = true;
      if (on_exited_) on_exited_();
      return session_status::dead;
    }
    // Drain remaining output.
    std::string raw = shell_->read_output();
    if (!raw.empty()) {
      feed_bytes(raw);
    }
    return session_status::dead;
  }

  // No shell at all.
  return session_status::ok;
}

// ===========================================================================
// shutdown
// ===========================================================================

void terminal_session::shutdown() {
  if (!shell_ || !shell_->is_running()) return;

  constexpr std::string_view exit_cmd = "exit\r\n";
  if (auto res = shell_->write_input( exit_cmd); !res) {
    util::log_error(res.error(), "send exit command to shell");
  }
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

  if (shell_ && shell_->is_running()) {
    (void)shell_->resize( cols, rows)
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

void terminal_session::set_observer(on_title_callback on_title) {
  grid_.set_observer(std::move(on_title));
}

void terminal_session::on_exited(on_exited_callback callback) {
  on_exited_ = std::move(callback);
}

} // namespace betty::terminal
