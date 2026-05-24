#include "scrollback_buffer.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace betty::terminal {

scrollback_buffer::scrollback_buffer(uint32_t cols, uint32_t rows, uint32_t max_scrollback)
  : cols_(cols)
  , rows_(rows)
  , max_scrollback_(max_scrollback)
  , total_capacity_rows_(rows + max_scrollback)
  , cells_(static_cast<size_t>(cols) * total_capacity_rows_, grid_cell{}) {}

auto scrollback_buffer::physical_index(uint32_t logical_row) const -> uint32_t {
  assert(logical_row < scrollback_count_ + rows_);
  return (scrollback_head_ + logical_row) % total_capacity_rows_;
}

auto scrollback_buffer::active_row(uint32_t vis_row) -> std::span<grid_cell> {
  assert(vis_row < rows_);
  uint32_t const logical = scrollback_count_ + vis_row;
  uint32_t const phys = physical_index(logical);
  size_t const offset = static_cast<size_t>(phys) * cols_;
  return {cells_.data() + offset, cols_};
}

auto scrollback_buffer::active_row(uint32_t vis_row) const -> std::span<grid_cell const> {
  assert(vis_row < rows_);
  uint32_t const logical = scrollback_count_ + vis_row;
  uint32_t const phys = physical_index(logical);
  size_t const offset = static_cast<size_t>(phys) * cols_;
  return {cells_.data() + offset, cols_};
}

auto scrollback_buffer::rendered_row(uint32_t vis_row) -> std::span<grid_cell> {
  assert(vis_row < rows_);
  uint32_t const logical = scrollback_count_ - viewport_scroll_ + vis_row;
  uint32_t const phys = physical_index(logical);
  size_t const offset = static_cast<size_t>(phys) * cols_;
  return {cells_.data() + offset, cols_};
}

auto scrollback_buffer::rendered_row(uint32_t vis_row) const -> std::span<grid_cell const> {
  assert(vis_row < rows_);
  uint32_t const logical = scrollback_count_ - viewport_scroll_ + vis_row;
  uint32_t const phys = physical_index(logical);
  size_t const offset = static_cast<size_t>(phys) * cols_;
  return {cells_.data() + offset, cols_};
}

void scrollback_buffer::push_scrollback() {
  if (rows_ == 0) return;

  if (scrollback_count_ < max_scrollback_) {
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
}

void scrollback_buffer::clear_all() {
  std::fill(cells_.begin(), cells_.end(), grid_cell{});
  scrollback_count_ = 0;
  scrollback_head_ = 0;
  viewport_scroll_ = 0;
}

auto scrollback_buffer::scroll_viewport(int32_t delta) -> uint32_t {
  if (delta > 0) {
    uint32_t const increment = static_cast<uint32_t>(delta);
    viewport_scroll_ = std::min(viewport_scroll_ + increment, scrollback_count_);
  } else if (delta < 0) {
    uint32_t const decrement = static_cast<uint32_t>(-delta);
    viewport_scroll_ = (viewport_scroll_ > decrement) ? viewport_scroll_ - decrement : 0;
  }
  return viewport_scroll_;
}

// ===========================================================================
// reflow_into
//
// Repacks old cells into a new column width by splitting each old row into
// ceil(old_cols / new_cols) chunks.  Example: resizing 5 cols -> 3 cols
// turns one old row [a b c d e] into two new rows [a b c] and [d e  ].
//
// When total_chunks exceeds new_capacity, the oldest chunks are silently
// discarded so the new logical index never wraps past the end of new_cells.
// ===========================================================================

auto scrollback_buffer::reflow_into(uint32_t new_cols, uint32_t new_rows,
                                    uint32_t new_max_scrollback) const -> reflow_result {
  uint32_t const new_capacity = new_rows + new_max_scrollback;
  std::vector<grid_cell> new_cells(
      static_cast<size_t>(new_cols) * new_capacity, grid_cell{});

  // Number of new chunks each old row produces when split across new columns.
  uint32_t const chunks_per_old_row = (cols_ + new_cols - 1) / new_cols;

  uint32_t const total_old_logical = scrollback_count_ + rows_;
  uint32_t const total_chunks = total_old_logical * chunks_per_old_row;

  // When total_chunks exceeds new_capacity, discard the oldest chunks
  // so new_logical_idx never wraps past the end of new_cells.
  // We skip at row granularity where possible, then offset into the first
  // retained row for the remaining chunk-level discard.
  uint32_t old_rows_to_skip = 0;
  uint32_t first_chunk_offset = 0;
  if (total_chunks > new_capacity) {
    uint32_t const skip_chunks = total_chunks - new_capacity;
    old_rows_to_skip = skip_chunks / chunks_per_old_row;
    first_chunk_offset = skip_chunks % chunks_per_old_row;
  }

  uint32_t new_logical_idx = 0;
  for (uint32_t log_old = old_rows_to_skip; log_old < total_old_logical; ++log_old) {
    uint32_t const old_phys = physical_index(log_old);
    size_t const old_offset = static_cast<size_t>(old_phys) * cols_;

    uint32_t const start_c =
        (log_old == old_rows_to_skip) ? first_chunk_offset * new_cols : 0;
    for (uint32_t c = start_c;
         c < cols_ && new_logical_idx < new_capacity;
         c += new_cols) {
      uint32_t const copy_count = std::min(new_cols, cols_ - c);
      size_t const new_offset = static_cast<size_t>(new_logical_idx) * new_cols;
      std::copy_n(cells_.data() + old_offset + c, copy_count,
                  new_cells.data() + new_offset);
      new_logical_idx++;
    }
  }

  uint32_t const new_scrollback_count =
      (new_logical_idx > new_rows) ? new_logical_idx - new_rows : 0;

  return {std::move(new_cells), new_scrollback_count, 0};
}

void scrollback_buffer::resize(uint32_t new_cols, uint32_t new_rows, uint32_t new_max_scrollback) {
  if (new_cols == cols_ && new_rows == rows_ && new_max_scrollback == max_scrollback_) return;

  // Zero-size: reset everything.
  if (new_cols == 0 || new_rows == 0) {
    cols_ = new_cols;
    rows_ = new_rows;
    max_scrollback_ = new_max_scrollback;
    total_capacity_rows_ = new_rows + new_max_scrollback;
    cells_.assign(static_cast<size_t>(new_cols) * total_capacity_rows_, grid_cell{});
    scrollback_count_ = 0;
    scrollback_head_ = 0;
    viewport_scroll_ = 0;
    return;
  }

  auto result = reflow_into(new_cols, new_rows, new_max_scrollback);
  cells_ = std::move(result.cells);
  scrollback_head_ = result.scrollback_head;
  scrollback_count_ = result.scrollback_count;
  cols_ = new_cols;
  rows_ = new_rows;
  max_scrollback_ = new_max_scrollback;
  total_capacity_rows_ = new_rows + new_max_scrollback;

  if (viewport_scroll_ > scrollback_count_) {
    viewport_scroll_ = scrollback_count_;
  }
}

auto scrollback_buffer::visible_base_logical() const noexcept -> uint32_t {
  return scrollback_count_ - viewport_scroll_;
}

} // namespace betty::terminal
