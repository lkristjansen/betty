#pragma once
#include <cstdint>

namespace betty::platform {

// Normalised rectangle within a cell. Coordinates are fractions [0.0, 1.0]
// of cell_width (horizontal) and cell_height (vertical).
// (0, 0) = top-left corner of the cell; (1, 1) = bottom-right.
struct cell_rect {
  float left;
  float top;
  float right;
  float bottom;
};

// Returns true if cp is a box-drawing (U+2500–U+257F) or block-element
// (U+2580–U+259F) codepoint that should be vector-rendered.
// Diagonals (U+2571, U+2572), dashed lines (U+2504–U+250B), shade
// characters (U+2591–U+2593), and arcs (U+256D–U+2570) return false
// because they cannot be expressed as axis-aligned rectangles.
[[nodiscard]] bool is_box_drawing_or_block(char32_t cp) noexcept;

// Fills `out` with up to `max_rects` normalised rectangles for the given
// codepoint. Returns the actual number of rectangles (0 if not vector-rendered).
// Callers should provide at least 8 elements (the maximum for any single
// codepoint — a double-line cross).
[[nodiscard]] uint32_t get_box_drawing_rects(char32_t cp, cell_rect* out, uint32_t max_rects) noexcept;

} // namespace betty::platform
