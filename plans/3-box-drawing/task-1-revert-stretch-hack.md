# Task 1: Revert the ASCII stretch hack

## Goal

Remove the `is_horizontal_connector()` and `stretch_glyph_horizontal()` functions and all their call sites, restoring the codebase to a clean state where all characters render through normal font glyph rasterization.

## What to remove

In `src/platform/text.cpp`:

1. **Delete** the `is_horizontal_connector()` function (~15 lines).

2. **Delete** the `stretch_glyph_horizontal()` function (~80 lines).

3. **Delete** the stretch call in the static atlas rasterization loop:
   ```cpp
   // Stretch horizontal connector glyphs so they fill the full cell width,
   // making consecutive dashes, equals signs, etc. connect seamlessly.
   if (is_horizontal_connector(static_cast<char32_t>(cp))) {
     stretch_glyph_horizontal(staging_buffer, p->atlas_width,
                              slot_x + k_glyph_padding, slot_y + k_glyph_padding,
                              p->cell_width, p->cell_height);
   }
   ```

4. **Delete** the stretch call in `ensure_glyph_cached()`:
   ```cpp
   // Stretch horizontal connector glyphs so they fill the full cell width.
   // The dynamic atlas staging buffer covers exactly one slot, so the content
   // area starts at (k_glyph_padding, k_glyph_padding) within the buffer.
   if (rasterized && is_horizontal_connector(cp)) {
     stretch_glyph_horizontal(staging_buffer, p.slot_width,
                              k_glyph_padding, k_glyph_padding,
                              p.cell_width, p.cell_height);
   }
   ```

## Verification

- Build succeeds with no errors.
- Existing tests pass (no test changes needed — these functions had no test coverage).
- Visual: `-`, `=`, `_` render with normal font glyph spacing (gaps between dashes are expected at this stage; Task 3 will fix box-drawing characters only).

## Files Changed

- `src/platform/text.cpp`