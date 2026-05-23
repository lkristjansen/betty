#include "grid.hpp"
#include "wcwidth.hpp"
#include "platform/unicode.hpp"
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
  , scroll_bottom_(rows > 0 ? rows - 1 : 0)
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
  int const w = wcwidth(cp);

  if (w == 0) {
    write_combining_char(cp);
    return;
  }

  if (w < 0) {
    // Control character — silently ignore.
    return;
  }

  if (w == 2) {
    // Wide character — needs 2 cells.
    // If at or past the second-to-last column, wrap to next line first.
    if (cursor_col_ >= cols_ - 1 && cols_ > 0) {
      newline();
    }

    // Write the actual glyph to the first cell.
    if (cursor_col_ < cols_ && rows_ > 0) {
      write_cell(cursor_col_, cp, current_fg_, current_bg_,
                 current_attr_ | cell_attr::wide);
    }
    cursor_col_++;

    // Mark the continuation cell.
    if (cursor_col_ < cols_ && rows_ > 0) {
      write_cell(cursor_col_, U' ', current_fg_, current_bg_,
                 current_attr_ | cell_attr::wide_tail);
    }
    cursor_col_++;
  } else {
    // Normal width-1 character.
    if (cursor_col_ < cols_ && rows_ > 0) {
      write_cell(cursor_col_, cp, current_fg_, current_bg_, current_attr_);
    }
    cursor_col_++;
  }

  // Auto-wrap: if cursor is past the last column, move to next row.
  if (cursor_col_ >= cols_) {
    cursor_col_ = 0;

    if (cursor_row_ >= scroll_bottom_) {
      scroll_up();
    } else {
      cursor_row_++;
    }
  }
}

// ===========================================================================
// write_cell — set a single cell at (cursor_row_, col)
// ===========================================================================

void terminal_grid::write_cell(uint32_t col, char32_t cp,
                                rgb_color fg, rgb_color bg, cell_attr attr) {
  uint32_t const logical = scrollback_count_ + cursor_row_;
  uint32_t const phys = physical_index(logical);
  auto& cell = cells_[static_cast<size_t>(phys) * cols_ + col];
  cell.codepoint = cp;
  cell.fg = fg;
  cell.bg = bg;
  cell.attr = attr;
}

// ===========================================================================
// write_combining_char — NFC pre-composition for zero-width marks
// ===========================================================================

void terminal_grid::write_combining_char(char32_t cp) {
  // If at column 0, can't compose with anything.
  if (cursor_col_ == 0) {
    if (rows_ > 0) {
      write_cell(cursor_col_, cp, current_fg_, current_bg_, current_attr_);
    }
    cursor_col_++;
    return;
  }

  // Look at the previous cell.
  uint32_t const logical = scrollback_count_ + cursor_row_;
  uint32_t const phys = physical_index(logical);
  auto& prev_cell = cells_[static_cast<size_t>(phys) * cols_ + cursor_col_ - 1];

  // Don't compose onto wide_tail cells.
  if ((static_cast<uint8_t>(prev_cell.attr) & static_cast<uint8_t>(cell_attr::wide_tail)) != 0) {
    if (rows_ > 0) {
      write_cell(cursor_col_, cp, current_fg_, current_bg_, current_attr_);
    }
    cursor_col_++;
    return;
  }

  char32_t const base = prev_cell.codepoint;
  if (base == U' ' || base == 0) {
    // Empty cell — write combining char as width 1.
    if (rows_ > 0) {
      write_cell(cursor_col_, cp, current_fg_, current_bg_, current_attr_);
    }
    cursor_col_++;
    return;
  }

  // Attempt NFC composition via the platform helper.
  char32_t const composed = platform::nfc_compose(base, cp);
  if (composed != 0) {
    prev_cell.codepoint = composed;
    // Cursor does not advance (combining char is zero-width).
  } else {
    // Uncomposable — fallback to width 1.
    if (rows_ > 0) {
      write_cell(cursor_col_, cp, current_fg_, current_bg_, current_attr_);
    }
    cursor_col_++;
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
    current_attr_ = cell_attr::none;
    break;
  case action_type::sgr_set_fg:
    current_fg_ = a.color;
    break;
  case action_type::sgr_set_bg:
    current_bg_ = a.color;
    break;
  case action_type::sgr_set_attr:
    current_attr_ = current_attr_ | static_cast<cell_attr>(a.count);
    break;
  case action_type::sgr_clear_attr:
    current_attr_ = current_attr_ & ~static_cast<cell_attr>(a.count);
    break;
  case action_type::erase_display:
    erase_display(a.count);
    break;
  case action_type::erase_line:
    erase_line(a.count);
    break;
  case action_type::set_scroll_region:
    set_scroll_region(a.row, a.col);
    break;
  case action_type::insert_lines:
    insert_lines(a.count);
    break;
  case action_type::delete_lines:
    delete_lines(a.count);
    break;
  case action_type::insert_chars:
    insert_chars(a.count);
    break;
  case action_type::delete_chars:
    delete_chars(a.count);
    break;
  case action_type::erase_chars:
    erase_chars(a.count);
    break;
  case action_type::scroll_up_page:
    scroll_page_up(a.count);
    break;
  case action_type::scroll_down_page:
    scroll_page_down(a.count);
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

  if (cursor_row_ >= scroll_bottom_) {
    scroll_up();  // region-aware scroll
    // cursor stays at scroll_bottom_
  } else {
    cursor_row_++;
  }
}

// ===========================================================================
// carriage_return
// ===========================================================================

void terminal_grid::carriage_return() {
  cursor_col_ = 0;
}

// ===========================================================================
// scroll_up — shift rows within the scroll region
// ===========================================================================

void terminal_grid::scroll_up() {
  if (rows_ == 0) return;

  // ── Full-screen case ─────────────────────────────────────────────────
  // Use the efficient circular-buffer trick: increment scrollback_count_
  // converts the old top visible row into a scrollback row without copies.
  if (scroll_top_ == 0 && scroll_bottom_ == rows_ - 1) {
    if (scrollback_count_ < k_scrollback_max) {
      scrollback_count_++;
    } else {
      scrollback_head_ = (scrollback_head_ + 1) % total_capacity_rows_;
    }

    // Clear the new bottom visible row.
    uint32_t const bottom_logical = scrollback_count_ + rows_ - 1;
    uint32_t const bottom_phys = physical_index(bottom_logical);
    size_t const offset = static_cast<size_t>(bottom_phys) * cols_;
    std::fill_n(cells_.data() + offset, cols_, grid_cell{});

    if (viewport_scroll_ > 0) {
      viewport_scroll_ = std::min(viewport_scroll_ + 1, scrollback_count_);
    }
    return;
  }

  // ── Sub-region case ──────────────────────────────────────────────────
  // Manually shift rows [scroll_top_+1 .. scroll_bottom_] up by 1.
  // No scrollback interaction — the scrolled-off row is lost.
  uint32_t const region_height = scroll_bottom_ - scroll_top_ + 1;
  if (region_height == 0) return;

  for (uint32_t r = scroll_top_; r < scroll_bottom_; ++r) {
    uint32_t const src_logical = scrollback_count_ + r + 1;
    uint32_t const dst_logical = scrollback_count_ + r;
    uint32_t const src_phys = physical_index(src_logical);
    uint32_t const dst_phys = physical_index(dst_logical);
    size_t const src_off = static_cast<size_t>(src_phys) * cols_;
    size_t const dst_off = static_cast<size_t>(dst_phys) * cols_;
    std::copy_n(cells_.data() + src_off, cols_, cells_.data() + dst_off);
  }

  // Clear the bottom row of the region.
  uint32_t const bottom_logical = scrollback_count_ + scroll_bottom_;
  uint32_t const bottom_phys = physical_index(bottom_logical);
  size_t const offset = static_cast<size_t>(bottom_phys) * cols_;
  std::fill_n(cells_.data() + offset, cols_, grid_cell{});
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
// set_scroll_region — DECSTBM (CSI Ps ; Ps r)
// ===========================================================================

void terminal_grid::set_scroll_region(uint32_t top, uint32_t bottom) {
  if (rows_ == 0) return;

  // Default: top = 1 if absent or 0.
  if (top < 1) top = 1;

  // bottom = 0 means "reset to full screen".
  if (bottom < 1 || bottom > rows_) bottom = rows_;

  // Clamp top to valid range.
  if (top > rows_) top = rows_;

  // If top >= bottom, the sequence should be ignored per VT100 spec.
  if (top >= bottom) return;

  // Convert to 0-based.
  scroll_top_ = top - 1;
  scroll_bottom_ = bottom - 1;

  // Move cursor to home position.
  cursor_row_ = 0;
  cursor_col_ = 0;
}

// ===========================================================================
// insert_lines — IL: insert n blank lines at cursor within scroll region
// ===========================================================================

void terminal_grid::insert_lines(uint32_t n) {
  if (cols_ == 0 || rows_ == 0) return;
  if (n == 0) return;

  // Ignore if cursor is outside the scroll region.
  if (cursor_row_ < scroll_top_ || cursor_row_ > scroll_bottom_) return;

  // Clamp n to the space remaining within the region below the cursor.
  uint32_t const space = scroll_bottom_ - cursor_row_ + 1;
  if (n > space) n = space;

  // Shift rows [cursor_row_ .. scroll_bottom_ - n] down by n.
  // Work from bottom up to avoid overwriting.
  for (uint32_t dst = scroll_bottom_; dst >= cursor_row_ + n; --dst) {
    uint32_t const src = dst - n;
    uint32_t const src_logical = scrollback_count_ + src;
    uint32_t const dst_logical = scrollback_count_ + dst;
    uint32_t const src_phys = physical_index(src_logical);
    uint32_t const dst_phys = physical_index(dst_logical);
    size_t const src_off = static_cast<size_t>(src_phys) * cols_;
    size_t const dst_off = static_cast<size_t>(dst_phys) * cols_;
    std::copy_n(cells_.data() + src_off, cols_, cells_.data() + dst_off);
  }

  // Fill the newly vacated rows with blank cells.
  for (uint32_t r = cursor_row_; r < cursor_row_ + n && r <= scroll_bottom_; ++r) {
    uint32_t const logical = scrollback_count_ + r;
    uint32_t const phys = physical_index(logical);
    size_t const offset = static_cast<size_t>(phys) * cols_;
    std::fill_n(cells_.data() + offset, cols_, grid_cell{});
  }

  // Reset cursor column (VT100 spec).
  cursor_col_ = 0;
}

// ===========================================================================
// delete_lines — DL: delete n lines at cursor within scroll region
// ===========================================================================

void terminal_grid::delete_lines(uint32_t n) {
  if (cols_ == 0 || rows_ == 0) return;
  if (n == 0) return;

  // Ignore if cursor is outside the scroll region.
  if (cursor_row_ < scroll_top_ || cursor_row_ > scroll_bottom_) return;

  // Clamp n to the space remaining within the region below the cursor.
  uint32_t const space = scroll_bottom_ - cursor_row_ + 1;
  if (n > space) n = space;

  // Shift rows [cursor_row_ + n .. scroll_bottom_] up by n.
  // Work from top down.
  for (uint32_t dst = cursor_row_; dst + n <= scroll_bottom_; ++dst) {
    uint32_t const src = dst + n;
    uint32_t const src_logical = scrollback_count_ + src;
    uint32_t const dst_logical = scrollback_count_ + dst;
    uint32_t const src_phys = physical_index(src_logical);
    uint32_t const dst_phys = physical_index(dst_logical);
    size_t const src_off = static_cast<size_t>(src_phys) * cols_;
    size_t const dst_off = static_cast<size_t>(dst_phys) * cols_;
    std::copy_n(cells_.data() + src_off, cols_, cells_.data() + dst_off);
  }

  // Fill the newly vacated rows at the bottom of the region with blanks.
  for (uint32_t r = scroll_bottom_ - n + 1; r <= scroll_bottom_; ++r) {
    uint32_t const logical = scrollback_count_ + r;
    uint32_t const phys = physical_index(logical);
    size_t const offset = static_cast<size_t>(phys) * cols_;
    std::fill_n(cells_.data() + offset, cols_, grid_cell{});
  }

  // Reset cursor column (VT100 spec).
  cursor_col_ = 0;
}

// ===========================================================================
// insert_chars — ICH: insert n blank cells at cursor, shift row right
// ===========================================================================

void terminal_grid::insert_chars(uint32_t n) {
  if (cols_ == 0 || rows_ == 0) return;
  if (n == 0) return;

  // Clamp n to remaining columns.
  uint32_t const space = cols_ - cursor_col_;
  if (n > space) n = space;
  if (n == 0) return;

  uint32_t const logical = scrollback_count_ + cursor_row_;
  uint32_t const phys = physical_index(logical);
  size_t const offset = static_cast<size_t>(phys) * cols_;

  // Shift cells right-to-left to avoid overwriting.
  for (uint32_t c = cols_ - 1; c >= cursor_col_ + n; --c) {
    cells_[offset + c] = cells_[offset + c - n];
  }

  // Fill the n vacated positions with blank cells.
  for (uint32_t c = cursor_col_; c < cursor_col_ + n && c < cols_; ++c) {
    cells_[offset + c] = grid_cell{};
  }
  // Cursor is unchanged.
}

// ===========================================================================
// delete_chars — DCH: delete n cells at cursor, shift row left
// ===========================================================================

void terminal_grid::delete_chars(uint32_t n) {
  if (cols_ == 0 || rows_ == 0) return;
  if (n == 0) return;

  // Clamp n to remaining columns.
  uint32_t const space = cols_ - cursor_col_;
  if (n > space) n = space;
  if (n == 0) return;

  uint32_t const logical = scrollback_count_ + cursor_row_;
  uint32_t const phys = physical_index(logical);
  size_t const offset = static_cast<size_t>(phys) * cols_;

  // Shift cells left-to-right.
  for (uint32_t c = cursor_col_; c + n < cols_; ++c) {
    cells_[offset + c] = cells_[offset + c + n];
  }

  // Fill the n vacated positions at the right edge with blank cells.
  for (uint32_t c = cols_ - n; c < cols_; ++c) {
    cells_[offset + c] = grid_cell{};
  }
  // Cursor is unchanged.
}

// ===========================================================================
// erase_chars — ECH: overwrite n cells at cursor with blanks (no shift)
// ===========================================================================

void terminal_grid::erase_chars(uint32_t n) {
  if (cols_ == 0 || rows_ == 0) return;
  if (n == 0) return;

  // Clamp n to remaining columns.
  uint32_t const space = cols_ - cursor_col_;
  if (n > space) n = space;
  if (n == 0) return;

  uint32_t const logical = scrollback_count_ + cursor_row_;
  uint32_t const phys = physical_index(logical);
  size_t const offset = static_cast<size_t>(phys) * cols_;

  // Overwrite n cells with blank cells.
  for (uint32_t c = cursor_col_; c < cursor_col_ + n && c < cols_; ++c) {
    cells_[offset + c] = grid_cell{};
  }
  // Cursor is unchanged.
}

// ===========================================================================
// scroll_page_up — SU: scroll the scroll region up by n lines
// ===========================================================================

void terminal_grid::scroll_page_up(uint32_t n) {
  if (cols_ == 0 || rows_ == 0) return;
  if (n == 0) return;

  uint32_t const region_height = scroll_bottom_ - scroll_top_ + 1;
  if (n > region_height) n = region_height;

  // Full-screen region: push rows into scrollback (via repeated scroll_up).
  if (scroll_top_ == 0 && scroll_bottom_ == rows_ - 1) {
    for (uint32_t i = 0; i < n; ++i) {
      scroll_up();
    }
    return;
  }

  // Sub-region: manual shift, no scrollback interaction.
  for (uint32_t r = scroll_top_; r + n <= scroll_bottom_; ++r) {
    uint32_t const src_logical = scrollback_count_ + r + n;
    uint32_t const dst_logical = scrollback_count_ + r;
    uint32_t const src_phys = physical_index(src_logical);
    uint32_t const dst_phys = physical_index(dst_logical);
    size_t const src_off = static_cast<size_t>(src_phys) * cols_;
    size_t const dst_off = static_cast<size_t>(dst_phys) * cols_;
    std::copy_n(cells_.data() + src_off, cols_, cells_.data() + dst_off);
  }

  // Clear the bottom n rows of the region.
  for (uint32_t r = scroll_bottom_ - n + 1; r <= scroll_bottom_; ++r) {
    uint32_t const logical = scrollback_count_ + r;
    uint32_t const phys = physical_index(logical);
    size_t const offset = static_cast<size_t>(phys) * cols_;
    std::fill_n(cells_.data() + offset, cols_, grid_cell{});
  }
}

// ===========================================================================
// scroll_page_down — SD: scroll the scroll region down by n lines
// ===========================================================================

void terminal_grid::scroll_page_down(uint32_t n) {
  if (cols_ == 0 || rows_ == 0) return;
  if (n == 0) return;

  uint32_t const region_height = scroll_bottom_ - scroll_top_ + 1;
  if (n > region_height) n = region_height;

  // Shift rows [scroll_top_ .. scroll_bottom_ - n] down by n.
  // Work from bottom up to avoid overwriting.
  for (uint32_t dst = scroll_bottom_; dst >= scroll_top_ + n; --dst) {
    uint32_t const src = dst - n;
    uint32_t const src_logical = scrollback_count_ + src;
    uint32_t const dst_logical = scrollback_count_ + dst;
    uint32_t const src_phys = physical_index(src_logical);
    uint32_t const dst_phys = physical_index(dst_logical);
    size_t const src_off = static_cast<size_t>(src_phys) * cols_;
    size_t const dst_off = static_cast<size_t>(dst_phys) * cols_;
    std::copy_n(cells_.data() + src_off, cols_, cells_.data() + dst_off);
  }

  // Fill the top n rows of the region with blanks.
  // No scrollback interaction.
  for (uint32_t r = scroll_top_; r < scroll_top_ + n && r <= scroll_bottom_; ++r) {
    uint32_t const logical = scrollback_count_ + r;
    uint32_t const phys = physical_index(logical);
    size_t const offset = static_cast<size_t>(phys) * cols_;
    std::fill_n(cells_.data() + offset, cols_, grid_cell{});
  }
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
      dst.attr = static_cast<uint8_t>(src.attr);

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

  // Reset scroll region to full screen on resize (matches real terminals).
  scroll_top_ = 0;
  scroll_bottom_ = rows_ > 0 ? rows_ - 1 : 0;

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
