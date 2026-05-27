# Task 4: Bold handling for vector-rendered characters

## Goal

Verify that the bold attribute is correctly handled for vector-rendered characters.

## Current Behavior

For font-rendered glyphs, bold is synthesized by double-drawing the glyph with a 1px horizontal offset (see `k_bold_offset_px` in `draw_grid()`).

## New Behavior

For vector-rendered box-drawing characters, the `continue` in Task 3's code path skips the bold double-draw entirely. This is correct because:

1. Box-drawing strokes already fill precise pixel positions — there's no gap to fill.
2. Making box-drawing strokes wider under bold would break alignment at cell boundaries.
3. Conventional terminals (Windows Terminal, xterm, VTE) do not change box-drawing stroke width for bold.

**No code changes needed** — Task 3's `continue` already skips the bold section. This task exists only to document the decision.

## Potential Future Enhancement

If desired, bold box-drawing could be rendered with heavier (thicker) strokes by adjusting the normalized rectangle dimensions based on the cell's bold attribute. This would require passing `cell.attr` into the rectangle computation. This is not planned for the initial implementation.