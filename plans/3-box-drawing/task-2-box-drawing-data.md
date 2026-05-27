# Task 2: Define the box-drawing / block-element lookup data

## Goal

Create a new module `src/platform/box_drawing.hpp` + `src/platform/box_drawing.cpp` that provides:

1. A predicate `is_box_drawing_or_block()` for quick range checks.
2. A normalized-rectangle decomposition for each supported codepoint.
3. A lookup function that returns the rectangles for a given codepoint.

## Struct Definition

```cpp
// src/platform/box_drawing.hpp

#pragma once
#include <cstdint>

namespace betty::platform {

// Normalized rectangle within a cell. Coordinates are fractions [0.0, 1.0]
// of cell_width (horizontal) and cell_height (vertical).
// (0,0) = top-left corner of the cell; (1,1) = bottom-right.
struct cell_rect {
  float left;
  float top;
  float right;
  float bottom;
};

// Returns true if cp is a box-drawing (U+2500–U+257F) or block-element
// (U+2580–U+259F) codepoint that should be vector-rendered.
// Diagonals (U+2571, U+2572) and dashed lines (U+2504–U+250B) return false
// because they cannot be expressed as axis-aligned rectangles.
[[nodiscard]] bool is_box_drawing_or_block(char32_t cp) noexcept;

// Fills `out` with up to `max_rects` normalized rectangles for the given
// codepoint. Returns the actual number of rectangles (0 if not vector-rendered).
// Callers should provide at least 6 elements (the maximum for any single codepoint).
[[nodiscard]] uint32_t get_box_drawing_rects(char32_t cp, cell_rect* out, uint32_t max_rects) noexcept;

} // namespace betty::platform
```

## Box-Drawing Decomposition Design

Every box-drawing character can be decomposed into a combination of **4 directional arms** extending from the cell center, plus a **center block** where arms intersect. The arms and center have thickness determined by the weight class.

### Weight Classes

| Class | Horizontal stroke | Vertical stroke | Example |
|-------|-------------------|-----------------|---------|
| Light | 1/8 cell height | 1/8 cell width | `─`, `│`, `├` |
| Heavy | 3/8 cell height | 3/8 cell width | `━`, `┃`, `┣` |
| Double| Two 1/8 strokes with 1/8 gap | Two 1/8 strokes with 1/8 gap | `═`, `║`, `╠` |

### Arm Model

Each arm is a thin rectangle:

- **Left arm:** `left=0.0`, `right=center_left`, `top=center_top`, `bottom=center_bottom`
- **Right arm:** `left=center_right`, `right=1.0`, `top=center_top`, `bottom=center_bottom`
- **Up arm:** `left=center_left`, `right=center_right`, `top=0.0`, `bottom=center_top`
- **Down arm:** `left=center_left`, `right=center_right`, `top=center_bottom`, `bottom=1.0`

For **light single lines:**
- center_left ≈ 7/16, center_right ≈ 9/16 (symmetric around 0.5)
- center_top ≈ 7/16, center_bottom ≈ 9/16

For **heavy lines:**
- center_left ≈ 5/16, center_right ≈ 11/16

For **double lines:**
- Two parallel arms: left stroke + right stroke + center gap
- This produces 4–6 rectangles per character instead of 2–4

### Codepoint Groups

Rather than enumerating all ~180 codepoints individually, organize by structure:

| Group | Codepoints | Description |
|-------|-----------|-------------|
| Horizontal lines | U+2500, U+2501, U+2550 | Left arm + right arm + center |
| Vertical lines | U+2502, U+2503, U+2551 | Up arm + down arm + center |
| Corners (4 per weight) | U+250C–U+2510, U+2514–U+2518, U+250F–U+2513, U+2519–U+251D, U+2554–U+2557, U+255A–U+255D | Two arms + center |
| Tees (4 per weight) | U+251C–U+2524, U+2528–U+252C, U+2560–U+2563, U+2566–U+2569 | Three arms + center |
| Crosses | U+253C, U+254B, U+256C | Four arms + center |
| Light/dash variants | U+2504–U+250B | **Skip** (dashed, not expressible as rects) |
| Diagonals | U+2571–U+2572 | **Skip** (diagonal, not expressible as rects) |
| Dots/corners | U+2573, U+2574, U+2575, U+2576, U+2577, U+2578, U+2579, U+257A, U+257B, U+257C–U+257F | Partial lines, left/right/top/bottom halves |

### Mixed-Weight Characters

Some characters have one light arm and one heavy arm (U+253F `╊`, U+254A `╊`, etc.). These define the arm direction *and* which weight each side uses. Handle these by specifying per-arm weight:

| U+2500 `─` | left=light, right=light, up=none, down=none |
| U+2578 `╸` | left=light, right=none, up=none, down=none |
| etc. | |

**Implementation approach:** Use a compact lookup table (static array indexed by `cp - 0x2500`) mapping each codepoint to a bitmask of active directions + weight, then compute rectangles on the fly.

## Block Element Decomposition

Block elements (U+2580–U+259F) encode fractions of the cell as filled rectangles. These are straightforward:

| Codepoint | Name | Rectangles |
|-----------|------|------------|
| U+2580 ▀ | Upper half | `{0, 0, 1, 0.5}` |
| U+2581 ▁ | Lower 1/8 | `{0, 0.875, 1, 1}` |
| U+2582 ▂ | Lower 1/4 | `{0, 0.75, 1, 1}` |
| U+2583 ▃ | Lower 3/8 | `{0, 0.625, 1, 1}` |
| U+2584 ▄ | Lower half | `{0, 0.5, 1, 1}` |
| U+2585 ▅ | Lower 5/8 | `{0, 0.375, 1, 1}` |
| U+2586 ▆ | Lower 3/4 | `{0, 0.25, 1, 1}` |
| U+2587 ▇ | Lower 7/8 | `{0, 0.125, 1, 1}` |
| U+2588 █ | Full block | `{0, 0, 1, 1}` |
| U+2589 ▉ | Left 7/8 | `{0, 0, 0.875, 1}` |
| U+258A ▊ | Left 3/4 | `{0, 0, 0.75, 1}` |
| U+258B ▋ | Left 5/8 | `{0, 0, 0.625, 1}` |
| U+258C ▌ | Left half | `{0, 0, 0.5, 1}` |
| U+258D ▍ | Left 3/8 | `{0, 0, 0.375, 1}` |
| U+258E ▎ | Left 1/4 | `{0, 0, 0.25, 1}` |
| U+258F ▏ | Left 1/8 | `{0, 0, 0.125, 1}` |
| U+2590 ▐ | Right half | `{0.5, 0, 1, 1}` |
| U+2591 ░ | Light shade | **Skip** (requires dithering, use font) |
| U+2592 ▒ | Medium shade | **Skip** |
| U+2593 ▓ | Dark shade | **Skip** |
| U+2594 ▔ | Upper 1/8 | `{0, 0, 1, 0.125}` |
| U+2595 ▕ | Right 1/8 | `{0.875, 0, 1, 1}` |
| U+2596 ▖ | Lower left quadrant | `{0, 0.5, 0.5, 1}` |
| U+2597 ▗ | Lower right quadrant | `{0.5, 0.5, 1, 1}` |
| U+2598 ▘ | Upper left quadrant | `{0, 0, 0.5, 0.5}` |
| U+2599 ▙ | Upper left + lower left + lower right | 3 rects |
| U+259A ▚ | Upper left + lower right | 2 rects |
| U+259B ▛ | Upper left + upper right + lower left | 3 rects |
| U+259C ▜ | Upper left + upper right + lower right | 3 rects |
| U+259D █ | Upper right quadrant | `{0.5, 0, 1, 0.5}` |
| U+259E ▞ | Upper right + lower left | 2 rects |
| U+259F ▟ | Upper right + lower left + lower right | 3 rects |

Shade characters (U+2591–U+2593) are intentionally excluded — they need dithering patterns best handled by the font.

## File Structure

```
src/platform/box_drawing.hpp   — struct cell_rect, function declarations
src/platform/box_drawing.cpp   — lookup tables, is_box_drawing_or_block(), get_box_drawing_rects()
```

Add `box_drawing.cpp` to `src/platform/CMakeLists.txt`.

## Testing

Create `tests/box_drawing_tests.cpp` with Catch2 test cases:

1. Boundary codepoints return correct `is_box_drawing_or_block()` values.
2. Representative characters decompose into the expected number of rectangles with reasonable coordinate ranges.
3. Excluded characters (diagonals, shades, ASCII) return 0 rects or false.
4. Corner-case: U+2588 (full block) returns a single `{0,0,1,1}` rectangle.

## Dependencies

None — this is a standalone module with no D3D or rendering dependencies.