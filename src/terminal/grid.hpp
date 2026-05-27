#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include "cursor_state.hpp"
#include "platform/types.hpp"
#include "scrollback_buffer.hpp"
#include "sgr_state.hpp"
#include "types.hpp"
#include "vt_parser.hpp"

namespace betty::terminal {

// --- Callback type aliases -------------------------------------------------

using on_title_callback = std::move_only_function<void(std::string_view)>;

// ===========================================================================
// terminal_grid — 2D cell grid with cursor tracking and auto-scroll
// ===========================================================================

class terminal_grid {
public:
  // Create a grid of `cols` × `rows` cells, all initialised to space.
  // Cursor starts at (0, 0).
  terminal_grid(uint32_t cols, uint32_t rows, uint32_t scrollback_max_lines);

  // --- Dimensions -----------------------------------------------------------

  [[nodiscard]] auto cols() const noexcept -> uint32_t { return cols_; }
  [[nodiscard]] auto rows() const noexcept -> uint32_t { return rows_; }

  // --- Cursor ---------------------------------------------------------------

  [[nodiscard]] auto cursor_col() const noexcept -> uint32_t { return cursor_.col(); }
  [[nodiscard]] auto cursor_row() const noexcept -> uint32_t { return cursor_.row(); }

  // --- Write operations -----------------------------------------------------

  // Write a single printable codepoint at the cursor, then advance.
  // Handles wide characters (2 cells), combining characters (NFC pre-composition),
  // auto-wrap at column boundary, and scroll at bottom row.
  void write_char(char32_t cp);

  // Apply a single parsed action to the grid.
  void apply(action const& a);

  // --- Explicit cursor control ---------------------------------------------

  // Move to start of next line; scroll if already on last row.
  void newline();

  // Move cursor to column 0 (same row).
  void carriage_return();

  // Shift all rows up by one, clearing the bottom row.
  // Cursor row is unchanged (caller adjusts if needed).
  // Respects scroll region: only rows [scroll_top_, scroll_bottom_] shift.
  void scroll_up();

  // --- Scroll region (DECSTBM) ---------------------------------------------

  // Set the scrolling region (top and bottom margins, 1-based).
  // CSI Ps ; Ps r.  If top >= bottom the call is ignored.
  // A bottom of 0 resets to full screen.
  void set_scroll_region(uint32_t top, uint32_t bottom);

  // --- Line operations ------------------------------------------------------

  // IL — insert n blank lines at cursor within the scroll region.
  // Ignored if cursor is outside the scroll region.
  // Cursor column is reset to 0.
  void insert_lines(uint32_t n);

  // DL — delete n lines at cursor within the scroll region.
  // Ignored if cursor is outside the scroll region.
  // Cursor column is reset to 0.
  void delete_lines(uint32_t n);

  // --- Character operations -------------------------------------------------

  // ICH — insert n blank cells at cursor, shifting the row right.
  // Cells shifted past the right edge are lost. Cursor is unchanged.
  void insert_chars(uint32_t n);

  // DCH — delete n cells at cursor, shifting the row left.
  // Blank cells fill the vacated positions on the right. Cursor is unchanged.
  void delete_chars(uint32_t n);

  // ECH — overwrite n cells at cursor with blank cells (no shifting).
  // Cursor is unchanged.
  void erase_chars(uint32_t n);

  // SU — scroll the scroll region up by n lines.
  // If scroll_top_ == 0 (full screen), scrolled-off rows go into scrollback.
  void scroll_page_up(uint32_t n);

  // SD — scroll the scroll region down by n lines.
  // Always inserts blank rows at top; no scrollback interaction.
  void scroll_page_down(uint32_t n);

  // --- Scrollback -----------------------------------------------------------

  // Scroll the viewport up/down by `delta` rows.
  // Positive delta = scroll back (up), negative = scroll forward (down).
  // Returns the new viewport scroll offset.
  [[nodiscard]] auto scroll_viewport(int32_t delta) -> uint32_t;

  // Whether the viewport is following output (at the bottom).
  [[nodiscard]] auto is_following_output() const noexcept -> bool { return buffer_.is_following_output(); }

  // --- Access (for rendering) -----------------------------------------------

  [[nodiscard]] auto cell(uint32_t row, uint32_t col) const -> grid_cell const&;

  // Produce a flat row-major buffer of resolved render_cell structs.
  // Default fg/bg colours are resolved to actual RGB values internally.
  // The returned span references a stable internal cache that is rebuilt on
  // each call; capacity is reused across calls to avoid repeated allocations.
  [[nodiscard]] auto render_cells() -> std::span<const platform::render_cell>;

  // --- Resize (placeholder for Task 10) -------------------------------------

  void resize(uint32_t new_cols, uint32_t new_rows);

  // Change scrollback capacity without changing grid dimensions.
  void resize_scrollback(uint32_t new_max_lines);

  // --- Observer for out-of-band terminal events (e.g. OSC window title) -----

  void set_observer(on_title_callback on_title);

private:
  uint32_t cols_;
  uint32_t rows_;
  uint32_t scrollback_max_lines_;

  scrollback_buffer buffer_;
  cursor_state cursor_;
  sgr_state sgr_;

  // Cache of resolved render_cell values for the platform renderer.
  mutable std::vector<platform::render_cell> render_cache_;

  // Observer for out-of-band terminal events (OSC window title, etc.).
  on_title_callback on_window_title_;

  // Pending auto-wrap flag. Set when write_char wraps to the next row;
  // consumed by the next \n to prevent a double row advance.
  bool pending_wrap_ = false;

  // --- Private helpers -----------------------------------------------------

  void write_cell(uint32_t col, char32_t cp, terminal_color fg, terminal_color bg, cell_attr attr);
  void write_combining_char(char32_t cp);

  void erase_display(uint32_t mode);
  void erase_line(uint32_t mode);
  void erase_visible_range(uint32_t vis_start_row, uint32_t vis_start_col,
                           uint32_t vis_end_row, uint32_t vis_end_col);
};

} // namespace betty::terminal
