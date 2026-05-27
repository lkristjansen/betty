# Task 3: Integrate vector rendering into draw_grid()

## Goal

Modify the `draw_grid()` render loop in `text.cpp` to intercept box-drawing and block-element characters, rendering them as solid-color rectangles instead of font glyphs.

## Changes

### In `src/platform/text.cpp`

1. **Add include:** `#include "box_drawing.hpp"`

2. **In the per-cell render loop** (inside `draw_grid()`), after emitting the background quad and computing `cp`, `fg_r`, `fg_g`, `fg_b`, `x0`, `y0`, `x1`, `y1`, `is_wide`, add a check **before** the foreground glyph emission:

```cpp
// Vector-rendered box-drawing and block elements.
// These characters are drawn as solid-colour axis-aligned rectangles
// instead of font glyphs, ensuring strokes connect perfectly at cell
// boundaries regardless of font metrics.
if (!is_wide && is_box_drawing_or_block(cp)) {
    cell_rect rects[6];
    uint32_t const count = get_box_drawing_rects(cp, rects, 6);
    if (count > 0) {
        float const cw = static_cast<float>(impl_->cell_width);
        float const ch = static_cast<float>(impl_->cell_height);
        for (uint32_t i = 0; i < count; ++i) {
            float const rx0 = x0 + rects[i].left * cw;
            float const ry0 = y0 + rects[i].top * ch;
            float const rx1 = x0 + rects[i].right * cw;
            float const ry1 = y0 + rects[i].bottom * ch;
            emit_bg_quad(rx0, ry0, rx1, ry1, fg_r, fg_g, fg_b);
        }
        col += k_normal_col_advance;
        continue;
    }
    // count == 0 means "fall through to font rendering" (diagonals, dashes, etc.)
}
```

3. **Ensure the loop uses `continue`** — this skips the glyph-texture lookup, the bold double-draw, and the underline/strikethrough check. Underline and strikethrough are not meaningful for box-drawing characters, so skipping them is correct.

### Note on `continue`

The current inner loop body uses `col += is_wide ? ... : ...;` at the bottom and doesn't use `continue`. The vector-render path needs to advance `col` and `continue` past the font-glyph path. This means wrapping the rest of the loop body in an `else`-block or using `continue` after the vector-render block.

**Preferred approach:** After the vector-render block, if `count > 0`, advance `col` and `continue`. The existing code after the vector-render check stays unchanged.

### Why `!is_wide`?

Box-drawing and block-element characters are all width-1 per Unicode. There should never be a `wide_lead` cell with these codepoints, but guarding against it prevents double-rendering if the VT parser ever creates one.

## Files Changed

- `src/platform/text.cpp` — add include, add vector-render check in draw_grid loop
- `src/platform/CMakeLists.txt` — add `box_drawing.cpp`

## Verification

Build and run. Characters in U+2500–U+259F should render as solid rectangles. All other characters render unchanged.

Visual test: `printf '\033(0lqk\033(B\n'` should show `┌──┐` with seamless horizontal lines and proper corners.