#include "cursor_state.hpp"
#include <algorithm>

namespace betty::terminal {

void cursor_state::move_to(uint32_t row, uint32_t col, uint32_t max_row, uint32_t max_col) {
  row_ = std::min(row, max_row);
  col_ = std::min(col, max_col);
}

void cursor_state::move_up(uint32_t n, uint32_t /*max_row*/) {
  row_ = (row_ > n) ? row_ - n : 0;
}

void cursor_state::move_down(uint32_t n, uint32_t max_row) {
  row_ = std::min(row_ + n, max_row);
}

void cursor_state::move_forward(uint32_t n, uint32_t max_col) {
  col_ = std::min(col_ + n, max_col);
}

void cursor_state::move_back(uint32_t n) {
  col_ = (col_ > n) ? col_ - n : 0;
}

void cursor_state::increment_row(uint32_t max_row) {
  row_ = std::min(row_ + 1, max_row);
}

void cursor_state::increment_col(uint32_t max_col) {
  col_ = std::min(col_ + 1, max_col);
}

void cursor_state::reset_col() {
  col_ = 0;
}

void cursor_state::set_col(uint32_t col, uint32_t max_col) {
  col_ = std::min(col, max_col);
}

void cursor_state::save() {
  saved_row_ = row_;
  saved_col_ = col_;
}

void cursor_state::restore(uint32_t max_row, uint32_t max_col) {
  row_ = std::min(saved_row_, max_row);
  col_ = std::min(saved_col_, max_col);
}

void cursor_state::reset_saved() {
  saved_row_ = 0;
  saved_col_ = 0;
}

auto cursor_state::at_scroll_bottom() const noexcept -> bool {
  return row_ >= scroll_bottom_;
}

auto cursor_state::in_scroll_region() const noexcept -> bool {
  return row_ >= scroll_top_ && row_ <= scroll_bottom_;
}

void cursor_state::set_region(uint32_t top, uint32_t bottom, uint32_t max_row) {
  scroll_top_ = std::min(top, max_row);
  scroll_bottom_ = std::min(bottom, max_row);
}

void cursor_state::reset_region(uint32_t max_row) {
  scroll_top_ = 0;
  scroll_bottom_ = max_row;
}

} // namespace betty::terminal
