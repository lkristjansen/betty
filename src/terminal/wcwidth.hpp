#pragma once
#include <cstdint>

namespace betty::terminal {

// Returns the display width of a Unicode codepoint in a monospace terminal:
//   2 — wide (East Asian Wide or Fullwidth, emoji)
//   1 — normal
//   0 — zero-width (combining marks, zero-width spaces)
//  -1 — non-printable control character
[[nodiscard]] int wcwidth(char32_t cp) noexcept;

} // namespace betty::terminal
