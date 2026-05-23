# Task 12 — Text Attributes

## Summary

Parse SGR bold, italic, faint, underline, strikethrough, and reverse video escape sequences. Render each attribute correctly: bold via synthetic double-draw, italic via a second glyph atlas face, faint via colour modulation, underline/strikethrough via line quads, and reverse video by swapping foreground/background at render time.

---

## Phase 1 — Attribute flags & data model (terminal layer)

**Files:** `src/terminal/vt_parser.hpp`, `src/terminal/grid.hpp`, `src/terminal/grid.cpp`

### 1.1 Define attribute bitmask

In `vt_parser.hpp`, add a `cell_attr` enum:

```cpp
enum class cell_attr : uint8_t {
  none          = 0,
  bold          = 1 << 0,
  italic        = 1 << 1,
  faint         = 1 << 2,
  underline     = 1 << 3,
  strikethrough = 1 << 4,
  reverse       = 1 << 5,
};
```

Add `uint8_t attr = 0` to `grid_cell` (replace the `// Future (Task 12)` comment).

### 1.2 Add action types

In the `action_type` enum, add two new values:

```
sgr_set_attr,    // turn ON a bitmask of attributes (payload in action::count)
sgr_clear_attr,  // turn OFF a bitmask of attributes (payload in action::count)
```

### 1.3 Track attribute state in terminal_grid

Add `uint8_t current_attr_ = 0` member alongside `current_fg_` / `current_bg_`.

### 1.4 Wire up apply()

In `terminal_grid::apply()`:

- **`sgr_reset`:** also set `current_attr_ = 0`.
- **`sgr_set_attr`:** `current_attr_ |= static_cast<uint8_t>(a.count)`.
- **`sgr_clear_attr`:** `current_attr_ &= ~static_cast<uint8_t>(a.count)`.

### 1.5 Apply attributes on write_char

In `terminal_grid::write_char()`, after setting `cell.fg` / `cell.bg`, also set `cell.attr = current_attr_`.

### 1.6 Thread attributes through render_cells

In `terminal_grid::render_cells()`: for each cell, copy `src.attr` to `dst.attr`.

---

## Phase 2 — VT parser: parse attribute SGR codes

**File:** `src/terminal/vt_parser.cpp`

### 2.1 Modify the SGR `'m'` dispatch

Currently the parser loop handles colour codes immediately. Up to now, attribute codes (1-9, 22-29) fall through the `else` branch and are silently skipped.

Add two accumulators before the `while` loop:

```cpp
uint8_t pending_on = 0;   // attributes to turn ON
uint8_t pending_off = 0;  // attributes to turn OFF
```

In the `while (i < params.size())` loop, add cases **before** the fallback `else`:

| Code(s) | Action |
|---------|--------|
| `1`     | `pending_on \|= bold` |
| `2`     | `pending_on \|= faint` ; also `pending_off \|= bold` (faint and bold are mutually exclusive) |
| `3`     | `pending_on \|= italic` |
| `4`     | `pending_on \|= underline` |
| `7`     | `pending_on \|= reverse` |
| `9`     | `pending_on \|= strikethrough` |
| `22`    | `pending_off \|= bold \| faint` (22 = normal intensity) |
| `23`    | `pending_off \|= italic` |
| `24`    | `pending_off \|= underline` |
| `27`    | `pending_off \|= reverse` |
| `29`    | `pending_off \|= strikethrough` |

After the loop, before `return actions;`, emit:

```cpp
if (pending_on != 0)
  actions.push_back({.type = sgr_set_attr, .count = pending_on});
if (pending_off != 0)
  actions.push_back({.type = sgr_clear_attr, .count = pending_off});
```

**Note on code 2 (faint):** ANSI specifies faint and bold are mutually exclusive — setting faint should also clear bold. We handle this in the parser by setting `pending_off |= bold` alongside `pending_on |= faint`. The grid's sequential apply handles this correctly: first apply `sgr_clear_attr(bold)`, then `sgr_set_attr(faint)`.

---

## Phase 3 — Platform types: add attributes to render_cell

**File:** `src/platform/types.hpp`

Add `uint8_t attr = 0` to the `render_cell` struct.

---

## Phase 4 — Glyph renderer: italic atlas

**File:** `src/platform/text.cpp`

### 4.1 Expand atlas

Change constants:
- `k_atlas_rows` from `8u` to `16u`
- `k_atlas_glyphs` from `128u` to `256u`

Expand `glyph_slots` array from `std::array<glyph_slot, 128>` to `std::array<glyph_slot, 256>`.

### 4.2 Get italic font face

Add a new helper function `get_italic_font_face()` that:
1. Obtains the `IDWriteFontFamily` for "Consolas" (reuse existing code from `init_font_face`)
2. Iterates `IDWriteFontFamily::GetFont(i)` for `i = 0 .. GetFontCount()-1`
3. Checks `font->GetStyle()` for `DWRITE_FONT_STYLE_ITALIC`
4. Gets the `IDWriteFontFace` and queries `IDWriteFontFace1`
5. Returns the italic `IDWriteFontFace1`

If no italic variant exists (shouldn't happen with Consolas, but defensively), fall back to the regular font face.

### 4.3 Rasterize italic glyphs

After the existing regular glyph rasterization loop (cp 0–127 in rows 0–7), add a second loop for italic:

```
for (uint32_t cp = 0; cp < 128; ++cp) {
    uint32_t slot = cp + 128;
    uint32_t col = cp % 16;
    uint32_t row = 8 + cp / 16;   // rows 8–15
    uint32_t slot_x = col * p->slot_width;
    uint32_t slot_y = row * p->slot_height;

    rasterize_glyph(..., italic_font_face.Get(), ...);
    // Precompute UVs for slot 128–255.
}
```

### 4.4 Update glyph_slot UVs

The existing UV computation loop already handles slots 0–127. Extend it to 128–255 inside the italic loop.

### 4.5 Update atlas texture dimensions

The atlas texture width stays the same (16 × slot_width). Height doubles: `p->atlas_height = k_atlas_rows * p->slot_height`. The staging buffer and D3D texture creation already use `p->atlas_width`/`p->atlas_height` so these update automatically.

---

## Phase 5 — Glyph renderer: draw_grid with attributes

**File:** `src/platform/text.cpp`

### 5.1 Attribute-aware glyph slot lookup

In `draw_grid()`, compute the slot index based on the italic flag:

```cpp
unsigned char glyph = (cp <= 127) ? static_cast<unsigned char>(cp) : '?';
uint32_t slot_idx = glyph + ((cell.attr & italic_flag) ? 128 : 0);
auto& slot = impl_->glyph_slots[slot_idx];
```

Also apply the same logic to the cursor's reverse-video glyph draw.

### 5.2 Bold — synthetic double-draw

After emitting the foreground glyph quad, if `cell.attr & bold`:

```cpp
if (cell.attr & bold_flag) {
  emit_quad(x0 + 1.0f, y0, x1 + 1.0f, y1,
            slot.u0, slot.v0, slot.u1, slot.v1,
            r, g, b);  // same colour, 1px right offset
}
```

The second quad extends 1 pixel into the next cell. This is acceptable — most GPU-accelerated terminals do this.

### 5.3 Faint — colour modulation

When computing vertex colours for the foreground glyph, if `cell.attr & faint`:

```cpp
float intensity = (cell.attr & faint_flag) ? 0.5f : 1.0f;
float nr = (cell.fg.r / 255.0f) * intensity;
float ng = (cell.fg.g / 255.0f) * intensity;
float nb = (cell.fg.b / 255.0f) * intensity;
```

Do **not** modulate the background quad — faint only affects the glyph.

### 5.4 Underline — line quad

After the glyph quad, if `cell.attr & underline`:

```cpp
if (cell.attr & underline_flag) {
  float uy0 = y1 - 2.0f;                    // 2px from cell bottom
  float uy1 = y1;
  emit_quad(x0, uy0, x1, uy1,
            -1.0f, 0.0f, -1.0f, 0.0f,       // solid colour (negative UV)
            nr, ng, nb);                      // foreground colour
}
```

### 5.5 Strikethrough — line quad

After underline (or before), if `cell.attr & strikethrough`:

```cpp
if (cell.attr & strikethrough_flag) {
  float sy0 = y0 + (impl_->cell_height * 0.4f);
  float sy1 = sy0 + 2.0f;
  emit_quad(x0, sy0, x1, sy1,
            -1.0f, 0.0f, -1.0f, 0.0f,
            nr, ng, nb);
}
```

### 5.6 Reverse video — swap fg/bg

If `cell.attr & reverse` (and the cell is not the cursor — the cursor already handles reverse):

```cpp
if (cell.attr & reverse_flag) {
  // Swap fg and bg colours for bg quad and glyph quad.
  // bg quad uses fg colour, glyph uses bg colour.
}
```

This is effectively the same logic as the existing cursor reverse-video. Extract a helper or duplicate the swap logic, ensuring cursor reverse-video and SGR reverse-video don't double-swap.

### 5.7 Interaction with cursor reverse-video

When the cursor is on a cell that also has SGR reverse video, the two should cancel (swapping twice = original). The simplest approach: toggle `is_cursor` reverse: if the cell has SGR reverse, skip the cursor's color swap. I.e., compute `effective_is_cursor = is_cursor XOR (cell.attr & reverse_flag)`.

---

## Phase 6 — Edge cases & testing

### 6.1 Erase operations

`erase_display()` and `erase_line()` already fill cleared cells with `grid_cell{}`, which has `attr = 0` (none). No changes needed.

### 6.2 Resize

`resize()` fills new cells with `grid_cell{}`. When reflowing content, cell attributes are preserved by `std::copy_n`. No changes needed.

### 6.3 Scrollback

Cell attributes are part of each `grid_cell` and move into scrollback along with colour data. No changes needed.

### 6.4 sgr_reset interaction

When `\e[0m` is received, the existing code emits `sgr_reset`. In `apply()`, this now also sets `current_attr_ = 0`. Subsequent `write_char` calls produce cells with no attributes. Correct.

### 6.5 Bold + faint mutual exclusion

SGR 2 (faint) should clear bold, and SGR 1 (bold) should clear faint. The parser handles this: when code 2 is processed, `pending_off |= bold` ensures the grid clears bold before setting faint. Similarly, code 1 sets `pending_off |= faint`.

### 6.6 Forward compatibility (double underline, concealed, etc.)

Codes like 8 (concealed), 21 (double underline), 25 (blink off), etc. remain in the `else` branch and are silently ignored. They don't interfere with attribute state.

---

## Files changed

| File | Change summary |
|------|---------------|
| `src/terminal/vt_parser.hpp` | Add `cell_attr` enum, `attr` field to `grid_cell`, `sgr_set_attr`/`sgr_clear_attr` to `action_type` |
| `src/terminal/vt_parser.cpp` | Parse attribute SGR codes (1-9, 22-29) in `dispatch('m')`, emit attribute actions |
| `src/terminal/grid.hpp` | Add `current_attr_` member, handle new action types in `apply()` declaration |
| `src/terminal/grid.cpp` | Implement `sgr_set_attr`/`sgr_clear_attr` handlers in `apply()`, apply `current_attr_` in `write_char()`, copy `attr` in `render_cells()` |
| `src/platform/types.hpp` | Add `uint8_t attr` to `render_cell` |
| `src/platform/text.cpp` | Double atlas size (128→256), get italic font face, rasterize italic glyphs (rows 8–15), implement bold/italic/faint/underline/strikethrough/reverse in `draw_grid()` |

## Order of implementation

Phases 1 → 2 → 3 → 4 → 5 → 6. Phases 1-3 are purely data plumbing and can be done together. Phase 4 (italic atlas) is the heaviest. Phase 5 ties everything together. Phase 6 is verification.
