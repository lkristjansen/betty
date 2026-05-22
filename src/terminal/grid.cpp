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
    auto& cell = cells_[static_cast<size_t>(cursor_row_) * cols_ + cursor_col_];
    cell.codepoint = cp;
    cell.fg = current_fg_;
    cell.bg = current_bg_;
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
// write_bytes — feed bytes to VT parser, apply resulting actions
// ===========================================================================

void terminal_grid::write_bytes(std::string_view data) {
  for (unsigned char const b : data) {
    for (auto const& a : parser_.parse(b)) {
      apply(a);
    }
  }
}

// ===========================================================================
// apply — dispatch a single action to the grid
// ===========================================================================

void terminal_grid::apply(action const& a) {
  switch (a.type) {
  case action_type::write_char:
    write_char(a.codepoint);
    break;
  case action_type::carriage_return:
    carriage_return();
    break;
  case action_type::newline:
    newline();
    break;
  case action_type::move_cursor:
    cursor_row_ = std::min(a.row, rows_ > 0 ? rows_ - 1 : 0);
    cursor_col_ = std::min(a.col, cols_ > 0 ? cols_ - 1 : 0);
    break;
  case action_type::move_cursor_up:
    cursor_row_ = (cursor_row_ > a.count) ? cursor_row_ - a.count : 0;
    break;
  case action_type::move_cursor_down:
    cursor_row_ = std::min(cursor_row_ + a.count, rows_ > 0 ? rows_ - 1 : 0);
    break;
  case action_type::move_cursor_forward:
    cursor_col_ = std::min(cursor_col_ + a.count, cols_ > 0 ? cols_ - 1 : 0);
    break;
  case action_type::move_cursor_back:
    cursor_col_ = (cursor_col_ > a.count) ? cursor_col_ - a.count : 0;
    break;
  case action_type::save_cursor:
    saved_cursor_row_ = cursor_row_;
    saved_cursor_col_ = cursor_col_;
    break;
  case action_type::restore_cursor:
    cursor_row_ = std::min(saved_cursor_row_, rows_ > 0 ? rows_ - 1 : 0);
    cursor_col_ = std::min(saved_cursor_col_, cols_ > 0 ? cols_ - 1 : 0);
    break;
  case action_type::sgr_reset:
    current_fg_ = default_fg();
    current_bg_ = default_bg();
    break;
  case action_type::sgr_set_fg:
    current_fg_ = a.color;
    break;
  case action_type::sgr_set_bg:
    current_bg_ = a.color;
    break;
  case action_type::erase_display:
    erase_display(a.count);
    break;
  case action_type::erase_line:
    erase_line(a.count);
    break;
  case action_type::set_window_title:
    if (on_window_title_) {
      on_window_title_(a.title);
    }
    break;
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
// erase helpers (Task 8)
// ===========================================================================

void terminal_grid::erase_cell_range(size_t start_idx, size_t end_idx) {
  assert(end_idx >= start_idx);
  assert(end_idx < cells_.size());
  for (size_t i = start_idx; i <= end_idx; ++i) {
    cells_[i] = grid_cell{};  // space, default fg, default bg
  }
}

void terminal_grid::erase_display(uint32_t mode) {
  if (cols_ == 0 || rows_ == 0) return;

  size_t const total = static_cast<size_t>(cols_) * rows_;
  if (total == 0) return;

  size_t const cursor_idx =
      static_cast<size_t>(cursor_row_) * cols_ + cursor_col_;

  switch (mode) {
  case 0: // Erase from cursor to end of display (inclusive).
    erase_cell_range(cursor_idx, total - 1);
    break;
  case 1: // Erase from beginning of display to cursor (inclusive).
    erase_cell_range(0, cursor_idx);
    break;
  case 2: // Erase entire display.
  case 3: // Erase entire display + scrollback (Task 11 adds scrollback).
    erase_cell_range(0, total - 1);
    break;
  default:
    // Unknown mode — treat as 0 (safe default).
    erase_cell_range(cursor_idx, total - 1);
    break;
  }
}

void terminal_grid::erase_line(uint32_t mode) {
  if (cols_ == 0 || rows_ == 0) return;

  size_t const row_start = static_cast<size_t>(cursor_row_) * cols_;
  size_t const row_end   = row_start + cols_ - 1;
  size_t const cursor_idx = row_start + cursor_col_;

  switch (mode) {
  case 0: // Erase from cursor to end of line (inclusive).
    erase_cell_range(cursor_idx, row_end);
    break;
  case 1: // Erase from beginning of line to cursor (inclusive).
    erase_cell_range(row_start, cursor_idx);
    break;
  case 2: // Erase entire line.
    erase_cell_range(row_start, row_end);
    break;
  default:
    // Unknown mode — treat as 0 (safe default).
    erase_cell_range(cursor_idx, row_end);
    break;
  }
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

// ===========================================================================
// render_cells — produce resolved render_cell buffer for the platform renderer
// ===========================================================================

auto terminal_grid::render_cells() -> std::span<const platform::render_cell> {
  size_t const n = cells_.size();
  // Lazy cache: rebuild every call, reuse capacity.
  render_cache_.resize(n);

  for (size_t i = 0; i < n; ++i) {
    auto const& src = cells_[i];
    auto& dst = render_cache_[i];

    dst.codepoint = src.codepoint;

    // Resolve foreground: if flagged as default, substitute the actual default colour.
    if (src.fg.flags & 1) {
      dst.fg = {k_default_fg_color.r, k_default_fg_color.g, k_default_fg_color.b};
    } else {
      dst.fg = {src.fg.r, src.fg.g, src.fg.b};
    }

    // Resolve background: if flagged as default, substitute the actual default colour.
    if (src.bg.flags & 1) {
      dst.bg = {k_default_bg_color.r, k_default_bg_color.g, k_default_bg_color.b};
    } else {
      dst.bg = {src.bg.r, src.bg.g, src.bg.b};
    }
  }

  return render_cache_;
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

  // Reset saved cursor — it's stale after a resize.
  saved_cursor_row_ = 0;
  saved_cursor_col_ = 0;

  cols_ = new_cols;
  rows_ = new_rows;
  cells_ = std::move(new_cells);
}

// ===========================================================================
// set_observer
// ===========================================================================

void terminal_grid::set_observer(std::function<void(std::string_view)> on_title) {
  on_window_title_ = std::move(on_title);
}

} // namespace betty::terminal
