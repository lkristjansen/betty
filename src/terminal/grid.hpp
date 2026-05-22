#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "platform/types.hpp"
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

  // Produce a flat row-major buffer of resolved render_cell structs.
  // Default fg/bg colours are resolved to actual RGB values internally.
  // The returned span references a stable internal cache that is rebuilt on
  // each call; capacity is reused across calls to avoid repeated allocations.
  [[nodiscard]] auto render_cells() -> std::span<const platform::render_cell>;

  // --- Resize (placeholder for Task 10) -------------------------------------

  void resize(uint32_t new_cols, uint32_t new_rows);

private:
  uint32_t cols_;
  uint32_t rows_;
  uint32_t cursor_col_ = 0;
  uint32_t cursor_row_ = 0;
  std::vector<grid_cell> cells_;  // size = cols_ * rows_, row-major
  vt_parser parser_;

  // Cache of resolved render_cell values for the platform renderer.
  mutable std::vector<platform::render_cell> render_cache_;

  // Current SGR state — applied to each cell on write_char.
  rgb_color current_fg_ = default_fg();
  rgb_color current_bg_ = default_bg();

  // --- Erase helpers (Task 8) ----------------------------------------------

  // Erase a contiguous range of cells [start_idx, end_idx] inclusive.
  void erase_cell_range(size_t start_idx, size_t end_idx);

  // ED — Erase in Display (CSI Ps J).
  void erase_display(uint32_t mode);

  // EL — Erase in Line (CSI Ps K).
  void erase_line(uint32_t mode);
};

} // namespace betty::terminal
