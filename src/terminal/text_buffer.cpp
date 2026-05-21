#include "text_buffer.hpp"

namespace betty::terminal {

text_buffer::text_buffer(uint32_t max_rows)
  : max_rows_(max_rows) {
  // Reserve space up front to avoid reallocations.
  lines_.reserve(max_rows_);
}

void text_buffer::append_line(std::string line) {
  if (lines_.size() == max_rows_) {
    // Drop the oldest line (front) by shifting.
    // For a ring buffer this would be O(1), but for simplicity and because
    // terminal screen heights are small (~50–200), std::vector erase at
    // begin is acceptable.
    lines_.erase(lines_.begin());
  }
  lines_.push_back(std::move(line));
}

auto text_buffer::lines() const -> std::span<const std::string> {
  return lines_;
}

void text_buffer::clear() {
  lines_.clear();
}

void text_buffer::append_to_last(std::string_view text) {
  if (lines_.empty()) {
    lines_.push_back(std::string(text));
  } else {
    lines_.back().append(text);
  }
}

} // namespace betty::terminal
