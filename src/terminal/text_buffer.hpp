#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace betty::terminal {

// ===========================================================================
// text_buffer — fixed-size rolling line buffer for terminal output
// ===========================================================================
// Holds up to `max_rows` lines.  When full, the oldest line is dropped on
// each new append.  No platform dependencies — pure C++23.

class text_buffer {
public:
  explicit text_buffer(uint32_t max_rows);

  // Append a line; drops the oldest line when the buffer is full.
  void append_line(std::string line);

  // Returns a view into the current contents (oldest → newest).
  [[nodiscard]] auto lines() const -> std::span<const std::string>;

  // Clear all lines.
  void clear();

  // Append text to the last line (creates an empty line first if buffer is empty).
  void append_to_last(std::string_view text);

  // Maximum number of rows the buffer can hold.
  [[nodiscard]] auto max_rows() const -> uint32_t { return max_rows_; }

private:
  uint32_t max_rows_;
  std::vector<std::string> lines_;
};

} // namespace betty::terminal
