#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "grid.hpp"
#include "input_handler.hpp"
#include "platform/shell.hpp"
#include "platform/types.hpp"
#include "vt_parser.hpp"

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
                   uint32_t scrollback_max_lines,
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

  // Send an exit command to the shell to initiate graceful shutdown.
  // Must be called before the session is destroyed.
  void shutdown();

  // --- Scrollback -----------------------------------------------------------

  void scroll_viewport(int32_t delta);

  // --- Resize ---------------------------------------------------------------

  // Resize the grid and notify the shell.
  void resize(uint32_t cols, uint32_t rows);

  // Change scrollback capacity without changing grid dimensions.
  void resize_scrollback(uint32_t max_lines);

  // --- Queries for rendering ------------------------------------------------

  [[nodiscard]] auto render_cells() -> std::span<const platform::render_cell>;

  [[nodiscard]] auto cursor_row() const -> uint32_t;
  [[nodiscard]] auto cursor_col() const -> uint32_t;
  [[nodiscard]] auto cols() const -> uint32_t;
  [[nodiscard]] auto rows() const -> uint32_t;
  [[nodiscard]] auto is_following_output() const -> bool;

  // --- Observers ------------------------------------------------------------

  // Register a callback invoked when the shell emits an OSC window-title
  // sequence (OSC 0 / 1 / 2).
  using on_exited_callback = std::move_only_function<void()>;

  void set_observer(on_title_callback on_title);

  // Register a callback invoked exactly once when the shell process exits.
  void on_exited(on_exited_callback callback);

private:
  // Feed raw bytes through the VT parser and apply resulting actions to the grid.
  void feed_bytes(std::string_view data);
  terminal_grid grid_;
  std::optional<platform::shell> shell_;
  input_handler input_;
  vt_parser parser_;
  bool shell_input_failed_ = false;
  bool exit_notified_ = false;
  on_exited_callback on_exited_;
};

} // namespace betty::terminal
