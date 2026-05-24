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
  , buffer_(cols, rows, k_scrollback_max)
  , cursor_() {
  if (rows > 0) {
    cursor_.reset_region(rows > 0 ? rows - 1 : 0);
  }
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
    if (cursor_.col() >= cols_ - 1 && cols_ > 0) {
      newline();
    }

    // Write the actual glyph to the first cell.
    if (cursor_.col() < cols_ && rows_ > 0) {
      auto row = buffer_.active_row(cursor_.row());
      row[cursor_.col()] = grid_cell{cp, sgr_.fg, sgr_.bg,
                                      sgr_.attr | cell_attr::wide};
    }
    cursor_.increment_col();

    // Mark the continuation cell.
    if (cursor_.col() < cols_ && rows_ > 0) {
      auto row = buffer_.active_row(cursor_.row());
      row[cursor_.col()] = grid_cell{U' ', sgr_.fg, sgr_.bg,
                                      sgr_.attr | cell_attr::wide_tail};
    }
    cursor_.increment_col();
  } else {
    // Normal width-1 character.
    if (cursor_.col() < cols_ && rows_ > 0) {
      auto row = buffer_.active_row(cursor_.row());
      row[cursor_.col()] = grid_cell{cp, sgr_.fg, sgr_.bg, sgr_.attr};
    }
    cursor_.increment_col();
  }

  // Auto-wrap: if cursor is past the last column, move to next row.
  if (cursor_.col() >= cols_) {
    cursor_.reset_col();
    if (cursor_.at_scroll_bottom()) {
      scroll_up();
    } else {
      cursor_.increment_row();
    }
  }
}

// ===========================================================================
// write_cell — set a single cell at (cursor_row_, col)
// ===========================================================================

void terminal_grid::write_cell(uint32_t col, char32_t cp,
                                terminal_color fg, terminal_color bg, cell_attr attr) {
  auto row = buffer_.active_row(cursor_.row());
  row[col] = grid_cell{cp, fg, bg, attr};
}

// ===========================================================================
// write_combining_char — NFC pre-composition for zero-width marks
// ===========================================================================

void terminal_grid::write_combining_char(char32_t cp) {
  // If at column 0, can't compose with anything.
  if (cursor_.col() == 0) {
    if (rows_ > 0) {
      auto row = buffer_.active_row(cursor_.row());
      row[cursor_.col()] = grid_cell{cp, sgr_.fg, sgr_.bg, sgr_.attr};
    }
    cursor_.increment_col();
    return;
  }

  // Look at the previous cell.
  auto row = buffer_.active_row(cursor_.row());
  auto& prev_cell = row[cursor_.col() - 1];

  // Don't compose onto wide_tail cells.
  if ((to_uint8(prev_cell.attr) & to_uint8(cell_attr::wide_tail)) != 0) {
    if (rows_ > 0) {
      row[cursor_.col()] = grid_cell{cp, sgr_.fg, sgr_.bg, sgr_.attr};
    }
    cursor_.increment_col();
    return;
  }

  char32_t const base = prev_cell.codepoint;
  if (base == U' ' || base == 0) {
    // Empty cell — write combining char as width 1.
    if (rows_ > 0) {
      row[cursor_.col()] = grid_cell{cp, sgr_.fg, sgr_.bg, sgr_.attr};
    }
    cursor_.increment_col();
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
      row[cursor_.col()] = grid_cell{cp, sgr_.fg, sgr_.bg, sgr_.attr};
    }
    cursor_.increment_col();
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
  uint32_t const max_row = rows_ > 0 ? rows_ - 1 : 0;
  uint32_t const max_col = cols_ > 0 ? cols_ - 1 : 0;

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
    cursor_.move_to(a.row, a.col, max_row, max_col);
    break;
  case action_type::move_cursor_up:
    cursor_.move_up(a.count, max_row);
    break;
  case action_type::move_cursor_down:
    cursor_.move_down(a.count, max_row);
    break;
  case action_type::move_cursor_forward:
    cursor_.move_forward(a.count, max_col);
    break;
  case action_type::move_cursor_back:
    cursor_.move_back(a.count);
    break;
  case action_type::save_cursor:
    cursor_.save();
    break;
  case action_type::restore_cursor:
    cursor_.restore(max_row, max_col);
    break;
  case action_type::sgr_reset:
    sgr_ = sgr_state{};
    break;
  case action_type::sgr_set_fg:
    sgr_.fg = a.color;
    break;
  case action_type::sgr_set_bg:
    sgr_.bg = a.color;
    break;
  case action_type::sgr_set_attr:
    sgr_.attr = sgr_.attr | static_cast<cell_attr>(a.count);
    break;
  case action_type::sgr_clear_attr:
    sgr_.attr = sgr_.attr & ~static_cast<cell_attr>(a.count);
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
  cursor_.reset_col();

  if (cursor_.at_scroll_bottom()) {
    scroll_up();  // region-aware scroll
    // cursor stays at scroll_bottom_
  } else {
    cursor_.increment_row();
  }
}

// ===========================================================================
// carriage_return
// ===========================================================================

void terminal_grid::carriage_return() {
  cursor_.reset_col();
}

// ===========================================================================
// scroll_up — shift rows within the scroll region
// ===========================================================================

void terminal_grid::scroll_up() {
  if (rows_ == 0) return;

  // ── Full-screen case ─────────────────────────────────────────────────
  if (cursor_.scroll_top() == 0 && cursor_.scroll_bottom() == rows_ - 1) {
    buffer_.push_scrollback();
    return;
  }

  // ── Sub-region case ──────────────────────────────────────────────────
  uint32_t const region_height = cursor_.scroll_bottom() - cursor_.scroll_top() + 1;
  if (region_height == 0) return;

  for (uint32_t r = cursor_.scroll_top(); r < cursor_.scroll_bottom(); ++r) {
    auto src = buffer_.active_row(r + 1);
    auto dst = buffer_.active_row(r);
    std::copy_n(src.data(), cols_, dst.data());
  }

  // Clear the bottom row of the region.
  auto bottom_row = buffer_.active_row(cursor_.scroll_bottom());
  std::fill_n(bottom_row.data(), cols_, grid_cell{});
}

// ===========================================================================
// scroll_viewport — adjust the scrollback viewport offset
// ===========================================================================

auto terminal_grid::scroll_viewport(int32_t delta) -> uint32_t {
  return buffer_.scroll_viewport(delta);
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
  cursor_.set_region(top - 1, bottom - 1, rows_ > 0 ? rows_ - 1 : 0);

  // Move cursor to home position.
  cursor_.move_to(0, 0, rows_ > 0 ? rows_ - 1 : 0, cols_ > 0 ? cols_ - 1 : 0);
}

// ===========================================================================
// insert_lines — IL: insert n blank lines at cursor within scroll region
// ===========================================================================

void terminal_grid::insert_lines(uint32_t n) {
  if (cols_ == 0 || rows_ == 0) return;
  if (n == 0) return;

  // Ignore if cursor is outside the scroll region.
  if (!cursor_.in_scroll_region()) return;

  // Clamp n to the space remaining within the region below the cursor.
  uint32_t const space = cursor_.scroll_bottom() - cursor_.row() + 1;
  if (n > space) n = space;

  // Shift rows [cursor_row_ .. scroll_bottom_ - n] down by n.
  // Work from bottom up to avoid overwriting.
  for (uint32_t dst = cursor_.scroll_bottom(); dst >= cursor_.row() + n; --dst) {
    auto src = buffer_.active_row(dst - n);
    auto dst_row = buffer_.active_row(dst);
    std::copy_n(src.data(), cols_, dst_row.data());
  }

  // Fill the newly vacated rows with blank cells.
  for (uint32_t r = cursor_.row(); r < cursor_.row() + n && r <= cursor_.scroll_bottom(); ++r) {
    auto row = buffer_.active_row(r);
    std::fill_n(row.data(), cols_, grid_cell{});
  }

  // Reset cursor column (VT100 spec).
  cursor_.reset_col();
}

// ===========================================================================
// delete_lines — DL: delete n lines at cursor within scroll region
// ===========================================================================

void terminal_grid::delete_lines(uint32_t n) {
  if (cols_ == 0 || rows_ == 0) return;
  if (n == 0) return;

  // Ignore if cursor is outside the scroll region.
  if (!cursor_.in_scroll_region()) return;

  // Clamp n to the space remaining within the region below the cursor.
  uint32_t const space = cursor_.scroll_bottom() - cursor_.row() + 1;
  if (n > space) n = space;

  // Shift rows [cursor_row_ + n .. scroll_bottom_] up by n.
  // Work from top down.
  for (uint32_t dst = cursor_.row(); dst + n <= cursor_.scroll_bottom(); ++dst) {
    auto src = buffer_.active_row(dst + n);
    auto dst_row = buffer_.active_row(dst);
    std::copy_n(src.data(), cols_, dst_row.data());
  }

  // Fill the newly vacated rows at the bottom of the region with blanks.
  for (uint32_t r = cursor_.scroll_bottom() - n + 1; r <= cursor_.scroll_bottom(); ++r) {
    auto row = buffer_.active_row(r);
    std::fill_n(row.data(), cols_, grid_cell{});
  }

  // Reset cursor column (VT100 spec).
  cursor_.reset_col();
}

// ===========================================================================
// insert_chars — ICH: insert n blank cells at cursor, shift row right
// ===========================================================================

void terminal_grid::insert_chars(uint32_t n) {
  if (cols_ == 0 || rows_ == 0) return;
  if (n == 0) return;

  // Clamp n to remaining columns.
  uint32_t const space = cols_ - cursor_.col();
  if (n > space) n = space;
  if (n == 0) return;

  auto row = buffer_.active_row(cursor_.row());

  // Shift cells right-to-left to avoid overwriting.
  for (uint32_t c = cols_ - 1; c >= cursor_.col() + n; --c) {
    row[c] = row[c - n];
  }

  // Fill the n vacated positions with blank cells.
  for (uint32_t c = cursor_.col(); c < cursor_.col() + n && c < cols_; ++c) {
    row[c] = grid_cell{};
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
  uint32_t const space = cols_ - cursor_.col();
  if (n > space) n = space;
  if (n == 0) return;

  auto row = buffer_.active_row(cursor_.row());

  // Shift cells left-to-right.
  for (uint32_t c = cursor_.col(); c + n < cols_; ++c) {
    row[c] = row[c + n];
  }

  // Fill the n vacated positions at the right edge with blank cells.
  for (uint32_t c = cols_ - n; c < cols_; ++c) {
    row[c] = grid_cell{};
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
  uint32_t const space = cols_ - cursor_.col();
  if (n > space) n = space;
  if (n == 0) return;

  auto row = buffer_.active_row(cursor_.row());

  // Overwrite n cells with blank cells.
  for (uint32_t c = cursor_.col(); c < cursor_.col() + n && c < cols_; ++c) {
    row[c] = grid_cell{};
  }
  // Cursor is unchanged.
}

// ===========================================================================
// scroll_page_up — SU: scroll the scroll region up by n lines
// ===========================================================================

void terminal_grid::scroll_page_up(uint32_t n) {
  if (cols_ == 0 || rows_ == 0) return;
  if (n == 0) return;

  uint32_t const region_height = cursor_.scroll_bottom() - cursor_.scroll_top() + 1;
  if (n > region_height) n = region_height;

  // Full-screen region: push rows into scrollback.
  if (cursor_.scroll_top() == 0 && cursor_.scroll_bottom() == rows_ - 1) {
    for (uint32_t i = 0; i < n; ++i) {
      buffer_.push_scrollback();
    }
    return;
  }

  // Sub-region: manual shift, no scrollback interaction.
  for (uint32_t r = cursor_.scroll_top(); r + n <= cursor_.scroll_bottom(); ++r) {
    auto src = buffer_.active_row(r + n);
    auto dst = buffer_.active_row(r);
    std::copy_n(src.data(), cols_, dst.data());
  }

  // Clear the bottom n rows of the region.
  for (uint32_t r = cursor_.scroll_bottom() - n + 1; r <= cursor_.scroll_bottom(); ++r) {
    auto row = buffer_.active_row(r);
    std::fill_n(row.data(), cols_, grid_cell{});
  }
}

// ===========================================================================
// scroll_page_down — SD: scroll the scroll region down by n lines
// ===========================================================================

void terminal_grid::scroll_page_down(uint32_t n) {
  if (cols_ == 0 || rows_ == 0) return;
  if (n == 0) return;

  uint32_t const region_height = cursor_.scroll_bottom() - cursor_.scroll_top() + 1;
  if (n > region_height) n = region_height;

  // Shift rows [scroll_top_ .. scroll_bottom_ - n] down by n.
  // Work from bottom up to avoid overwriting.
  for (uint32_t dst = cursor_.scroll_bottom(); dst >= cursor_.scroll_top() + n; --dst) {
    auto src = buffer_.active_row(dst - n);
    auto dst_row = buffer_.active_row(dst);
    std::copy_n(src.data(), cols_, dst_row.data());
  }

  // Fill the top n rows of the region with blanks.
  // No scrollback interaction.
  for (uint32_t r = cursor_.scroll_top(); r < cursor_.scroll_top() + n && r <= cursor_.scroll_bottom(); ++r) {
    auto row = buffer_.active_row(r);
    std::fill_n(row.data(), cols_, grid_cell{});
  }
}

// ===========================================================================
// erase_display — ED (CSI Ps J)
// ===========================================================================

void terminal_grid::erase_display(uint32_t mode) {
  if (cols_ == 0 || rows_ == 0) return;

  uint32_t const last_vis_row = rows_ - 1;
  uint32_t const last_vis_col = cols_ - 1;

  switch (mode) {
  case 0: // Erase from cursor to end of display.
    erase_visible_range(cursor_.row(), cursor_.col(), last_vis_row, last_vis_col);
    break;
  case 1: // Erase from beginning of display to cursor.
    erase_visible_range(0, 0, cursor_.row(), cursor_.col());
    break;
  case 2: // Erase entire visible display.
    erase_visible_range(0, 0, last_vis_row, last_vis_col);
    break;
  case 3: // Erase entire visible display + scrollback.
    buffer_.clear_all();
    break;
  default:
    erase_visible_range(cursor_.row(), cursor_.col(), last_vis_row, last_vis_col);
    break;
  }
}

// ===========================================================================
// erase_line — EL (CSI Ps K)
// ===========================================================================

void terminal_grid::erase_line(uint32_t mode) {
  if (cols_ == 0 || rows_ == 0) return;

  auto row = buffer_.active_row(cursor_.row());

  switch (mode) {
  case 0: // Erase from cursor to end of line.
    for (uint32_t c = cursor_.col(); c < cols_; ++c)
      row[c] = grid_cell{};
    break;
  case 1: // Erase from beginning of line to cursor.
    for (uint32_t c = 0; c <= cursor_.col() && c < cols_; ++c)
      row[c] = grid_cell{};
    break;
  case 2: // Erase entire line.
    for (uint32_t c = 0; c < cols_; ++c)
      row[c] = grid_cell{};
    break;
  default:
    for (uint32_t c = cursor_.col(); c < cols_; ++c)
      row[c] = grid_cell{};
    break;
  }
}

// ===========================================================================
// erase_visible_range — clear a rectangular region of the visible grid
// ===========================================================================

void terminal_grid::erase_visible_range(uint32_t vis_start_row, uint32_t vis_start_col,
                                         uint32_t vis_end_row, uint32_t vis_end_col) {
  for (uint32_t r = vis_start_row; r <= vis_end_row; ++r) {
    auto row = buffer_.active_row(r);
    uint32_t const start_c = (r == vis_start_row) ? vis_start_col : 0;
    uint32_t const end_c   = (r == vis_end_row)   ? vis_end_col   : cols_ - 1;
    for (uint32_t c = start_c; c <= end_c; ++c) {
      row[c] = grid_cell{};
    }
  }
}

// ===========================================================================
// access
// ===========================================================================

auto terminal_grid::cell(uint32_t row, uint32_t col) const -> grid_cell const& {
  assert(row < rows_ && col < cols_);
  return buffer_.rendered_row(row)[col];
}

// ===========================================================================
// render_cells — produce resolved render_cell buffer for the platform renderer
// ===========================================================================

auto terminal_grid::render_cells() -> std::span<const platform::render_cell> {
  size_t const n = static_cast<size_t>(cols_) * rows_;
  render_cache_.resize(n);

  for (uint32_t r = 0; r < rows_; ++r) {
    auto const& src_row = buffer_.rendered_row(r);
    size_t const dst_offset = static_cast<size_t>(r) * cols_;

    for (uint32_t c = 0; c < cols_; ++c) {
      auto const& src = src_row[c];
      auto& dst = render_cache_[dst_offset + c];

      dst.codepoint = src.codepoint;
      dst.attr = to_uint8(src.attr);

      if (src.fg.is_default()) {
        dst.fg = platform::k_default_fg_color;
      } else {
        dst.fg = {src.fg.r, src.fg.g, src.fg.b};
      }

      if (src.bg.is_default()) {
        dst.bg = platform::k_default_bg_color;
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

  buffer_.resize(new_cols, new_rows, k_scrollback_max);

  cols_ = new_cols;
  rows_ = new_rows;

  // Clamp cursor.
  if (cols_ > 0 && cursor_.col() >= cols_) {
    cursor_.set_col(cols_ - 1, cols_ - 1);
  }
  if (rows_ > 0 && cursor_.row() >= rows_) {
    cursor_.move_to(rows_ - 1, cursor_.col(), rows_ - 1, cols_ > 0 ? cols_ - 1 : 0);
  }

  // Reset scroll region to full screen on resize (matches real terminals).
  if (rows_ > 0) {
    cursor_.reset_region(rows_ - 1);
  } else {
    cursor_.set_region(0, 0, 0);
  }

  // Reset saved cursor — it's stale after a resize.
  cursor_.reset_saved();
}

// ===========================================================================
// set_observer
// ===========================================================================

void terminal_grid::set_observer(std::function<void(std::string_view)> on_title) {
  on_window_title_ = std::move(on_title);
}

} // namespace betty::terminal
