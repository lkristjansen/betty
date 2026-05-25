#pragma once
#include <cstdint>

namespace betty::terminal {

// ===========================================================================
// cursor_state — cursor position, saved position, and scroll region
// ===========================================================================

class cursor_state {
public:
  [[nodiscard]] auto row() const noexcept -> uint32_t { return row_; }
  [[nodiscard]] auto col() const noexcept -> uint32_t { return col_; }
  [[nodiscard]] auto saved_row() const noexcept -> uint32_t { return saved_row_; }
  [[nodiscard]] auto saved_col() const noexcept -> uint32_t { return saved_col_; }
  [[nodiscard]] auto scroll_top() const noexcept -> uint32_t { return scroll_top_; }
  [[nodiscard]] auto scroll_bottom() const noexcept -> uint32_t { return scroll_bottom_; }

  void move_to(uint32_t row, uint32_t col, uint32_t max_row, uint32_t max_col);
  void move_up(uint32_t n, uint32_t max_row);
  void move_down(uint32_t n, uint32_t max_row);
  void move_forward(uint32_t n, uint32_t max_col);
  void move_back(uint32_t n);

  // Increment and clamp. max_col for increment_col is the column count
  // (cols_), not the last valid index, because the cursor is allowed to
  // reach cols_ to signal auto-wrap.
  void increment_row(uint32_t max_row);
  void increment_col(uint32_t max_col);

  void reset_col();
  void set_col(uint32_t col, uint32_t max_col);

  void save();
  void restore(uint32_t max_row, uint32_t max_col);
  void reset_saved();

  [[nodiscard]] auto at_scroll_bottom() const noexcept -> bool;
  [[nodiscard]] auto in_scroll_region() const noexcept -> bool;

  void set_region(uint32_t top, uint32_t bottom, uint32_t max_row);
  void reset_region(uint32_t max_row);

private:
  uint32_t row_ = 0;
  uint32_t col_ = 0;
  uint32_t saved_row_ = 0;
  uint32_t saved_col_ = 0;
  uint32_t scroll_top_ = 0;
  uint32_t scroll_bottom_ = 0;
};

} // namespace betty::terminal
