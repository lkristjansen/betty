#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include "types.hpp"

namespace betty::terminal {

// ===========================================================================
// scrollback_buffer — circular row store with viewport scrolling
// ===========================================================================

class scrollback_buffer {
public:
  scrollback_buffer(uint32_t cols, uint32_t rows, uint32_t max_scrollback);

  [[nodiscard]] auto cols() const noexcept -> uint32_t { return cols_; }
  [[nodiscard]] auto rows() const noexcept -> uint32_t { return rows_; }
  [[nodiscard]] auto max_scrollback() const noexcept -> uint32_t { return max_scrollback_; }
  [[nodiscard]] auto scrollback_count() const noexcept -> uint32_t { return scrollback_count_; }

  [[nodiscard]] auto active_row(uint32_t vis_row) -> std::span<grid_cell>;
  [[nodiscard]] auto active_row(uint32_t vis_row) const -> std::span<grid_cell const>;

  [[nodiscard]] auto rendered_row(uint32_t vis_row) -> std::span<grid_cell>;
  [[nodiscard]] auto rendered_row(uint32_t vis_row) const -> std::span<grid_cell const>;

  void push_scrollback();
  void clear_all();

  [[nodiscard]] auto scroll_viewport(int32_t delta) -> uint32_t;
  [[nodiscard]] auto viewport_scroll() const noexcept -> uint32_t { return viewport_scroll_; }
  [[nodiscard]] auto is_following_output() const noexcept -> bool { return viewport_scroll_ == 0; }

  void resize(uint32_t new_cols, uint32_t new_rows, uint32_t new_max_scrollback);

  [[nodiscard]] auto visible_base_logical() const noexcept -> uint32_t;

private:
  uint32_t cols_ = 0;
  uint32_t rows_ = 0;
  uint32_t max_scrollback_ = 0;
  uint32_t total_capacity_rows_ = 0;
  uint32_t scrollback_head_ = 0;
  uint32_t scrollback_count_ = 0;
  uint32_t viewport_scroll_ = 0;
  std::vector<grid_cell> cells_;

  [[nodiscard]] auto physical_index(uint32_t logical_row) const -> uint32_t;
};

} // namespace betty::terminal
