#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "vt_parser.hpp"

namespace betty::terminal {

// ===========================================================================
// grid_cell — a single character cell in the terminal grid
// ===========================================================================

struct grid_cell {
  char32_t codepoint = U' ';         // default: space
  rgb_color fg = default_fg();       // foreground colour
  rgb_color bg = default_bg();       // background colour
  // Future (Task 12): bold, italic, underline, strikethrough flags
};

// ===========================================================================
// terminal_grid — 2D cell grid with cursor tracking and auto-scroll
// ===========================================================================

class terminal_grid {
public:
  // Create a grid of `cols` × `rows` cells, all initialised to space.
  // Cursor starts at (0, 0).
  terminal_grid(uint32_t cols, uint32_t rows);

  // --- Dimensions -----------------------------------------------------------

  [[nodiscard]] auto cols() const noexcept -> uint32_t { return cols_; }
  [[nodiscard]] auto rows() const noexcept -> uint32_t { return rows_; }

  // --- Cursor ---------------------------------------------------------------

  [[nodiscard]] auto cursor_col() const noexcept -> uint32_t { return cursor_col_; }
  [[nodiscard]] auto cursor_row() const noexcept -> uint32_t { return cursor_row_; }

  // --- Write operations -----------------------------------------------------

  // Write a single printable codepoint at the cursor, then advance.
  // Handles auto-wrap at column boundary and scroll at bottom row.
  void write_char(char32_t cp);

  // Process a sequence of raw bytes (VT-stripped shell output).
  // Internally feeds bytes through vt_parser and applies resulting actions.
  void write_bytes(std::string_view data);

  // Apply a single parsed action to the grid.
  void apply(action const& a);

  // --- Explicit cursor control ----------------------------------------------

  // Move to start of next line; scroll if already on last row.
  void newline();

  // Move cursor to column 0 (same row).
  void carriage_return();

  // Shift all rows up by one, clearing the bottom row.
  // Cursor row is unchanged (caller adjusts if needed).
  void scroll_up();

  // --- Access (for rendering) -----------------------------------------------

  [[nodiscard]] auto cell(uint32_t row, uint32_t col) const -> grid_cell const&;
  [[nodiscard]] auto cells() const noexcept -> std::span<const grid_cell>;

  // --- Resize (placeholder for Task 10) -------------------------------------

  void resize(uint32_t new_cols, uint32_t new_rows);

private:
  uint32_t cols_;
  uint32_t rows_;
  uint32_t cursor_col_ = 0;
  uint32_t cursor_row_ = 0;
  std::vector<grid_cell> cells_;  // size = cols_ * rows_, row-major
  vt_parser parser_;

  // Current SGR state — applied to each cell on write_char.
  rgb_color current_fg_ = default_fg();
  rgb_color current_bg_ = default_bg();
};

} // namespace betty::terminal
