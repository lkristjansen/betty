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

  uint32_t const new_capacity = new_rows + new_max_scrollback;
  std::vector<grid_cell> new_cells(
      static_cast<size_t>(new_cols) * new_capacity, grid_cell{});

  // Extract all existing logical rows (scrollback + visible).
  uint32_t const total_old_logical = scrollback_count_ + rows_;
  uint32_t new_logical_idx = 0;

  for (uint32_t log_old = 0; log_old < total_old_logical; ++log_old) {
    uint32_t const old_phys = physical_index(log_old);
    size_t const old_offset = static_cast<size_t>(old_phys) * cols_;

    for (uint32_t c = 0; c < cols_; c += new_cols) {
      uint32_t const copy_count = std::min(new_cols, cols_ - c);
      uint32_t const new_phys = new_logical_idx % new_capacity;
      size_t const new_offset = static_cast<size_t>(new_phys) * new_cols;
      std::copy_n(cells_.data() + old_offset + c, copy_count,
                  new_cells.data() + new_offset);
      new_logical_idx++;
    }
  }

  uint32_t new_scrollback_count = 0;
  uint32_t new_scrollback_head = 0;

  if (new_logical_idx > new_rows) {
    uint32_t const excess = new_logical_idx - new_rows;
    new_scrollback_count = std::min(excess, new_max_scrollback);
    if (excess > new_max_scrollback) {
      new_scrollback_head = (excess - new_max_scrollback) % new_capacity;
    }
  }

  cells_ = std::move(new_cells);
  scrollback_head_ = new_scrollback_head;
  scrollback_count_ = new_scrollback_count;
  cols_ = new_cols;
  rows_ = new_rows;
  max_scrollback_ = new_max_scrollback;
  total_capacity_rows_ = new_capacity;

  if (viewport_scroll_ > scrollback_count_) {
    viewport_scroll_ = scrollback_count_;
  }
}

auto scrollback_buffer::visible_base_logical() const noexcept -> uint32_t {
  return scrollback_count_ - viewport_scroll_;
}

} // namespace betty::terminal
