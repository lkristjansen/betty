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
  , total_capacity_rows_(rows + k_scrollback_max)
  , cells_(static_cast<size_t>(cols) * total_capacity_rows_, grid_cell{}) {}

// ===========================================================================
// physical_index — logical → physical row mapping
// ===========================================================================

auto terminal_grid::physical_index(uint32_t logical_row) const -> uint32_t {
  assert(logical_row < scrollback_count_ + rows_);
  return (scrollback_head_ + logical_row) % total_capacity_rows_;
}

// ===========================================================================
// write_char
// ===========================================================================

void terminal_grid::write_char(char32_t cp) {
  if (cursor_col_ < cols_ && rows_ > 0) {
    uint32_t const logical = scrollback_count_ + cursor_row_;
    uint32_t const phys = physical_index(logical);
    auto& cell = cells_[static_cast<size_t>(phys) * cols_ + cursor_col_];
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
// scroll_up — shift visible rows, preserving top row in scrollback
// ===========================================================================

void terminal_grid::scroll_up() {
  if (rows_ == 0) return;

  // 1. Absorb the current top visible row into scrollback.
  //    The top visible row is at logical index scrollback_count_.
  //    After incrementing scrollback_count_, that same physical row becomes
  //    the newest scrollback row — no data copying needed.
  if (scrollback_count_ < k_scrollback_max) {
    scrollback_count_++;
  } else {
    // Buffer full: drop the oldest scrollback row by advancing the head.
    scrollback_head_ = (scrollback_head_ + 1) % total_capacity_rows_;
    // scrollback_count_ stays at k_scrollback_max.
  }

  // 2. Clear the new bottom visible row.
  //    The bottom visible row is at logical index scrollback_count_ + rows_ - 1.
  uint32_t const bottom_logical = scrollback_count_ + rows_ - 1;
  uint32_t const bottom_phys = physical_index(bottom_logical);
  size_t const offset = static_cast<size_t>(bottom_phys) * cols_;
  std::fill_n(cells_.data() + offset, cols_, grid_cell{});

  // 3. If the user is scrolled back, auto-advance viewport_scroll_ so
  //    the same content remains visible (the new row is added "below").
  if (viewport_scroll_ > 0) {
    viewport_scroll_ = std::min(viewport_scroll_ + 1, scrollback_count_);
  }
}

// ===========================================================================
// scroll_viewport — adjust the scrollback viewport offset
// ===========================================================================

auto terminal_grid::scroll_viewport(int32_t delta) -> uint32_t {
  if (delta > 0) {
    // Scroll back (up).
    uint32_t const increment = static_cast<uint32_t>(delta);
    viewport_scroll_ = std::min(viewport_scroll_ + increment, scrollback_count_);
  } else if (delta < 0) {
    // Scroll forward (down).
    uint32_t const decrement = static_cast<uint32_t>(-delta);
    viewport_scroll_ = (viewport_scroll_ > decrement) ? viewport_scroll_ - decrement : 0;
  }
  return viewport_scroll_;
}

// ===========================================================================
// erase_display — ED (CSI Ps J)
// ===========================================================================

void terminal_grid::erase_display(uint32_t mode) {
  if (cols_ == 0 || rows_ == 0) return;

  // Helper: erase a range of cells in the visible grid.
  // vis_start_row, vis_end_row are 0-based visible row indices.
  auto erase_visible_range = [this](uint32_t vis_start_row, uint32_t vis_start_col,
                                      uint32_t vis_end_row, uint32_t vis_end_col) {
    for (uint32_t r = vis_start_row; r <= vis_end_row; ++r) {
      uint32_t const logical = scrollback_count_ + r;
      uint32_t const phys = physical_index(logical);
      size_t const base = static_cast<size_t>(phys) * cols_;
      uint32_t const start_c = (r == vis_start_row) ? vis_start_col : 0;
      uint32_t const end_c   = (r == vis_end_row)   ? vis_end_col   : cols_ - 1;
      for (uint32_t c = start_c; c <= end_c; ++c) {
        cells_[base + c] = grid_cell{};
      }
    }
  };

  uint32_t const last_vis_row = rows_ - 1;
  uint32_t const last_vis_col = cols_ - 1;

  switch (mode) {
  case 0: // Erase from cursor to end of display.
    erase_visible_range(cursor_row_, cursor_col_, last_vis_row, last_vis_col);
    break;
  case 1: // Erase from beginning of display to cursor.
    erase_visible_range(0, 0, cursor_row_, cursor_col_);
    break;
  case 2: // Erase entire visible display.
    erase_visible_range(0, 0, last_vis_row, last_vis_col);
    break;
  case 3: // Erase entire visible display + scrollback.
    cells_.assign(cells_.size(), grid_cell{});
    scrollback_count_ = 0;
    scrollback_head_ = 0;
    viewport_scroll_ = 0;
    break;
  default:
    erase_visible_range(cursor_row_, cursor_col_, last_vis_row, last_vis_col);
    break;
  }
}

// ===========================================================================
// erase_line — EL (CSI Ps K)
// ===========================================================================

void terminal_grid::erase_line(uint32_t mode) {
  if (cols_ == 0 || rows_ == 0) return;

  uint32_t const logical = scrollback_count_ + cursor_row_;
  uint32_t const phys = physical_index(logical);
  size_t const base = static_cast<size_t>(phys) * cols_;

  switch (mode) {
  case 0: // Erase from cursor to end of line.
    for (uint32_t c = cursor_col_; c < cols_; ++c)
      cells_[base + c] = grid_cell{};
    break;
  case 1: // Erase from beginning of line to cursor.
    for (uint32_t c = 0; c <= cursor_col_ && c < cols_; ++c)
      cells_[base + c] = grid_cell{};
    break;
  case 2: // Erase entire line.
    for (uint32_t c = 0; c < cols_; ++c)
      cells_[base + c] = grid_cell{};
    break;
  default:
    for (uint32_t c = cursor_col_; c < cols_; ++c)
      cells_[base + c] = grid_cell{};
    break;
  }
}

// ===========================================================================
// access
// ===========================================================================

auto terminal_grid::cell(uint32_t row, uint32_t col) const -> grid_cell const& {
  assert(row < rows_ && col < cols_);
  // Account for viewport scrolling: visible row 0 may not be the
  // "real" visible row 0 when the user has scrolled back.
  uint32_t const logical = scrollback_count_ - viewport_scroll_ + row;
  uint32_t const phys = physical_index(logical);
  return cells_[static_cast<size_t>(phys) * cols_ + col];
}

auto terminal_grid::cells() const noexcept -> std::span<const grid_cell> {
  return cells_;
}

// ===========================================================================
// render_cells — produce resolved render_cell buffer for the platform renderer
// ===========================================================================

auto terminal_grid::render_cells() -> std::span<const platform::render_cell> {
  size_t const n = static_cast<size_t>(cols_) * rows_;
  render_cache_.resize(n);

  // Determine which logical rows to render.
  // total_logical = scrollback_count_ + rows_
  // viewport_start = total_logical - rows_ - viewport_scroll_
  //                = scrollback_count_ - viewport_scroll_
  uint32_t const viewport_start = scrollback_count_ - viewport_scroll_;

  for (uint32_t r = 0; r < rows_; ++r) {
    uint32_t const logical_row = viewport_start + r;
    uint32_t const phys = physical_index(logical_row);
    size_t const src_offset = static_cast<size_t>(phys) * cols_;
    size_t const dst_offset = static_cast<size_t>(r) * cols_;

    for (uint32_t c = 0; c < cols_; ++c) {
      auto const& src = cells_[src_offset + c];
      auto& dst = render_cache_[dst_offset + c];

      dst.codepoint = src.codepoint;

      if (src.fg.flags & 1) {
        dst.fg = {k_default_fg_color.r, k_default_fg_color.g, k_default_fg_color.b};
      } else {
        dst.fg = {src.fg.r, src.fg.g, src.fg.b};
      }

      if (src.bg.flags & 1) {
        dst.bg = {k_default_bg_color.r, k_default_bg_color.g, k_default_bg_color.b};
      } else {
        dst.bg = {src.bg.r, src.bg.g, src.bg.b};
      }
    }
  }

  return render_cache_;
}

// ===========================================================================
// resize — reflow content when dimensions change
// ===========================================================================

void terminal_grid::resize(uint32_t new_cols, uint32_t new_rows) {
  if (new_cols == cols_ && new_rows == rows_) return;

  // Zero-size: just reset.
  if (new_cols == 0 || new_rows == 0) {
    cols_ = new_cols;
    rows_ = new_rows;
    total_capacity_rows_ = new_rows + k_scrollback_max;
    cells_.assign(static_cast<size_t>(new_cols) * total_capacity_rows_, grid_cell{});
    scrollback_count_ = 0;
    scrollback_head_ = 0;
    viewport_scroll_ = 0;
    cursor_col_ = 0;
    cursor_row_ = 0;
    saved_cursor_col_ = 0;
    saved_cursor_row_ = 0;
    return;
  }

  uint32_t const new_capacity = new_rows + k_scrollback_max;
  std::vector<grid_cell> new_cells(
      static_cast<size_t>(new_cols) * new_capacity, grid_cell{});

  // Extract all existing logical rows (scrollback + visible).
  uint32_t const total_old_logical = scrollback_count_ + rows_;
  uint32_t new_logical_idx = 0;  // next write position in new buffer

  for (uint32_t log_old = 0; log_old < total_old_logical; ++log_old) {
    uint32_t const old_phys = physical_index(log_old);
    size_t const old_offset = static_cast<size_t>(old_phys) * cols_;

    // Reflow this old row into new_cols-width chunks.
    // When cols_ == new_cols, this loop runs exactly once (copy_count = cols_).
    for (uint32_t c = 0; c < cols_; c += new_cols) {
      uint32_t const copy_count = std::min(new_cols, cols_ - c);
      uint32_t const new_phys = new_logical_idx % new_capacity;
      size_t const new_offset = static_cast<size_t>(new_phys) * new_cols;
      std::copy_n(cells_.data() + old_offset + c, copy_count,
                  new_cells.data() + new_offset);
      // Remaining cells in the new row are already blank.
      new_logical_idx++;
    }
  }

  // Determine new scrollback: the last new_rows logical rows are visible,
  // the rest (if any) are scrollback.
  uint32_t new_scrollback_count = 0;
  uint32_t new_scrollback_head = 0;

  if (new_logical_idx > new_rows) {
    uint32_t const excess = new_logical_idx - new_rows;
    new_scrollback_count = std::min(excess, k_scrollback_max);
    if (excess > k_scrollback_max) {
      new_scrollback_head = (excess - k_scrollback_max) % new_capacity;
    }
  }

  cells_ = std::move(new_cells);
  scrollback_head_ = new_scrollback_head;
  scrollback_count_ = new_scrollback_count;
  cols_ = new_cols;
  rows_ = new_rows;
  total_capacity_rows_ = new_capacity;

  // Clamp cursor and viewport.
  if (cursor_col_ >= cols_) cursor_col_ = cols_ > 0 ? cols_ - 1 : 0;
  if (cursor_row_ >= rows_) cursor_row_ = rows_ > 0 ? rows_ - 1 : 0;
  if (viewport_scroll_ > scrollback_count_) viewport_scroll_ = scrollback_count_;

  // Reset saved cursor — it's stale after a resize.
  saved_cursor_row_ = 0;
  saved_cursor_col_ = 0;
}

// ===========================================================================
// set_observer
// ===========================================================================

void terminal_grid::set_observer(std::function<void(std::string_view)> on_title) {
  on_window_title_ = std::move(on_title);
}

} // namespace betty::terminal
