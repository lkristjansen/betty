#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

#include "grid.hpp"
#include "input_handler.hpp"
#include "platform/shell.hpp"
#include "platform/types.hpp"

namespace betty::terminal {

// ===========================================================================
// session_status — result of process_output()
// ===========================================================================

enum class session_status : uint8_t {
  ok,    // shell is running normally
  dead,  // shell has exited or input pipe failed
};

// ===========================================================================
// terminal_session — owns grid + shell + input_handler
// ===========================================================================
// Pure-logic terminal core: transforms shell I/O bytes into renderable cell
// data, independently of any window or GPU.  Testable with mock I/O.

class terminal_session {
public:
  // Create a session with the given dimensions.
  // If `shell` is nullopt, a "Failed to create shell process" message is
  // written to the grid.
  terminal_session(uint32_t cols, uint32_t rows,
                   std::optional<platform::shell> shell);

  // --- Input/output ---------------------------------------------------------

  // Translate a keyboard event (WM_KEYDOWN) into shell input bytes and write them.
  // Handles only non-printable keys + Ctrl+letter combos.
  // Printable characters arrive via write_char() (WM_CHAR path).
  // Scrollback navigation (Ctrl+Shift+arrows/pgup/pgdn) intercepted internally.
  void write_keyboard(platform::vk_code vk, bool ctrl, bool shift, bool alt);

  // Write a single Unicode codepoint (from WM_CHAR) to the shell as UTF-8.
  void write_char(uint32_t codepoint);

  // Drain shell output into the grid.
  // Returns `dead` on first detection that the shell has exited, then `dead`
  // on subsequent calls (still draining remaining output).
  [[nodiscard]] auto process_output() -> session_status;

  // --- Scrollback -----------------------------------------------------------

  void scroll_viewport(int32_t delta);

  // --- Resize ---------------------------------------------------------------

  // Resize the grid and notify the shell.
  void resize(uint32_t cols, uint32_t rows);

  // --- Queries for rendering ------------------------------------------------

  [[nodiscard]] auto render_cells() -> std::span<const platform::render_cell>;

  [[nodiscard]] auto cursor_row() const -> uint32_t;
  [[nodiscard]] auto cursor_col() const -> uint32_t;
  [[nodiscard]] auto cols() const -> uint32_t;
  [[nodiscard]] auto rows() const -> uint32_t;
  [[nodiscard]] auto is_following_output() const -> bool;

  // --- Observer (window title changes) --------------------------------------

  void set_observer(std::function<void(std::string_view)> on_title);

private:
  terminal_grid grid_;
  std::optional<platform::shell> shell_;
  input_handler input_;
  bool shell_input_failed_ = false;
  bool exit_notified_ = false;
};

} // namespace betty::terminal
