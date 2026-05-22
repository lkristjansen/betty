#include "grid.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace betty::terminal {

// ===========================================================================
// construction
// ===========================================================================

terminal_grid::terminal_grid(uint32_t cols, uint32_t rows)
  : cols_(cols)
  , rows_(rows)
  , cells_(static_cast<size_t>(cols) * rows, grid_cell{}) {}

// ===========================================================================
// write_char
// ===========================================================================

void terminal_grid::write_char(char32_t cp) {
  if (cursor_col_ < cols_) {
    cells_[static_cast<size_t>(cursor_row_) * cols_ + cursor_col_].codepoint = cp;
  }

  cursor_col_++;

  // Auto-wrap: if cursor is past the last column, move to next row.
  if (cursor_col_ >= cols_) {
    cursor_col_ = 0;
    cursor_row_++;

    // Scroll if past the last row.
    if (cursor_row_ >= rows_) {
      scroll_up();
      cursor_row_ = rows_ - 1;
    }
  }
}

// ===========================================================================
// write_bytes
// ===========================================================================

void terminal_grid::write_bytes(std::string_view data) {
  for (unsigned char const b : data) {
    switch (b) {
    case '\r':
      carriage_return();
      break;
    case '\n':
      newline();
      break;
    default:
      if (b >= 0x20) {
        write_char(static_cast<char32_t>(b));
      }
      // Other C0 controls and high bytes (>= 0x80) are silently ignored.
      break;
    }
  }
}

// ===========================================================================
// newline
// ===========================================================================

void terminal_grid::newline() {
  cursor_col_ = 0;
  cursor_row_++;

  if (cursor_row_ >= rows_) {
    scroll_up();
    cursor_row_ = rows_ - 1;
  }
}

// ===========================================================================
// carriage_return
// ===========================================================================

void terminal_grid::carriage_return() {
  cursor_col_ = 0;
}

// ===========================================================================
// scroll_up
// ===========================================================================

void terminal_grid::scroll_up() {
  if (rows_ <= 1) {
    // Single-row grid: just clear it.
    cells_.assign(cells_.size(), grid_cell{});
    return;
  }

  size_t const row_size = static_cast<size_t>(cols_);
  size_t const total = static_cast<size_t>(cols_) * rows_;

  // Shift rows [1..rows_-1] up to [0..rows_-2].
  std::memmove(
    cells_.data(),
    cells_.data() + row_size,
    (total - row_size) * sizeof(grid_cell));

  // Clear the last row.
  std::fill_n(cells_.data() + (total - row_size), row_size, grid_cell{});
}

// ===========================================================================
// access
// ===========================================================================

auto terminal_grid::cell(uint32_t row, uint32_t col) const -> grid_cell const& {
  assert(row < rows_ && col < cols_);
  return cells_[static_cast<size_t>(row) * cols_ + col];
}

auto terminal_grid::cells() const noexcept -> std::span<const grid_cell> {
  return cells_;
}

auto terminal_grid::codepoints() const noexcept -> std::span<const char32_t> {
  // grid_cell is a single char32_t at offset 0, so the memory layout is
  // identical to an array of char32_t.  Accessing through the first member
  // of a standard-layout struct is well-defined.
  return { reinterpret_cast<const char32_t*>(cells_.data()), cells_.size() };
}

// ===========================================================================
// resize (placeholder for Task 10)
// ===========================================================================

void terminal_grid::resize(uint32_t new_cols, uint32_t new_rows) {
  if (new_cols == cols_ && new_rows == rows_) return;

  std::vector<grid_cell> new_cells(static_cast<size_t>(new_cols) * new_rows, grid_cell{});

  // Copy as much of the old content as fits, row by row.
  uint32_t const copy_rows = std::min(rows_, new_rows);
  uint32_t const copy_cols = std::min(cols_, new_cols);

  for (uint32_t r = 0; r < copy_rows; ++r) {
    std::copy(
      cells_.data() + static_cast<size_t>(r) * cols_,
      cells_.data() + static_cast<size_t>(r) * cols_ + copy_cols,
      new_cells.data() + static_cast<size_t>(r) * new_cols);
  }

  // Clamp cursor to new dimensions.
  if (cursor_col_ >= new_cols) cursor_col_ = new_cols > 0 ? new_cols - 1 : 0;
  if (cursor_row_ >= new_rows) cursor_row_ = new_rows > 0 ? new_rows - 1 : 0;

  cols_ = new_cols;
  rows_ = new_rows;
  cells_ = std::move(new_cells);
}

} // namespace betty::terminal
