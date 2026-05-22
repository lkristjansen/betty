# Task 7 — Block Cursor: Implementation Plan

## Goal

Render a solid block cursor at the current grid cursor position, with colours
inverted (reverse video: foreground ↔ background).  The cursor is static (no
blink) and follows text as the user types or the shell moves the cursor.

---

## Architecture overview

The data path is:

```
ConPTY output → grid.write_bytes() → grid.apply(action) → grid cursor updates
                                                         → cells get written

main loop:  renderer.draw_grid(cells, cursor_row, cursor_col)
              → D3D11 vertex buffer → pixel shader → swap chain present
```

The cursor is **purely a rendering concern** — the terminal grid already
tracks `cursor_row_` / `cursor_col_` accurately; we only need to tell the
renderer *which cell* to invert.

---

## Files to modify

| File | Change |
|------|--------|
| `src/platform/text.hpp` | Add `cursor_row`, `cursor_col` parameters to `draw_grid` |
| `src/platform/text.cpp` | Invert colours for the cursor cell inside `draw_grid` |
| `src/main.cpp` | Pass `grid.cursor_row()`, `grid.cursor_col()` to `draw_grid` |

No new files.  No changes to the grid, parser, or shell.

---

## Step 1 — Update `draw_grid` signature (`text.hpp`)

**Current:**

```cpp
[[nodiscard]] auto draw_grid(d3d_device const& device, d3d_render_target_view const& rtv,
                              std::span<const terminal::grid_cell> cells,
                              uint32_t cols, uint32_t rows) const
    -> std::expected<void, std::error_code>;
```

**After:**

```cpp
[[nodiscard]] auto draw_grid(d3d_device const& device, d3d_render_target_view const& rtv,
                              std::span<const terminal::grid_cell> cells,
                              uint32_t cols, uint32_t rows,
                              uint32_t cursor_row, uint32_t cursor_col) const
    -> std::expected<void, std::error_code>;
```

---

## Step 2 — Implement cursor rendering in `draw_grid` (`text.cpp`)

### 2a. Add a `resolve_bg` lambda (mirrors existing `resolve_fg`)

Next to the existing `resolve_fg` lambda at the start of `draw_grid`:

```cpp
auto resolve_bg = [](terminal::rgb_color c) -> terminal::rgb_color {
    if (c.flags & 1) return terminal::k_default_bg_color;
    return c;
};
```

### 2b. Clamp cursor position to visible area

After computing `draw_rows`/`draw_cols`, guard against the cursor being
outside the rendered viewport (safety net; grid should never let this happen,
but it costs nothing):

```cpp
bool const cursor_visible =
    cursor_row < draw_rows && cursor_col < draw_cols;
```

### 2c. Special-case the cursor cell in the inner loop

Inside `for (row)` / `for (col)`, after fetching the cell and computing
positions, check whether this is the cursor cell:

**When `is_cursor` is true:**

1. Resolve both fg and bg to actual colours.
2. **Always** emit a background quad in the resolved **foreground** colour
   (reverse video: old fg becomes the block fill).
3. If the cell contains a non-space codepoint, emit a glyph quad in the
   resolved **background** colour (reverse video: old bg becomes the text).

```cpp
bool const is_cursor = cursor_visible && row == cursor_row && col == cursor_col;

if (is_cursor) {
    auto const fg = resolve_fg(cell.fg);
    auto const bg = resolve_bg(cell.bg);

    // Solid block in fg colour (reverse of normal bg quad).
    float const nr = static_cast<float>(fg.r) / 255.0f;
    float const ng = static_cast<float>(fg.g) / 255.0f;
    float const nb = static_cast<float>(fg.b) / 255.0f;
    emit_quad(x0, y0, x1, y1, -1.0f, 0.0f, -1.0f, 0.0f, nr, ng, nb);

    // If the cell has a character, draw it in the bg colour.
    if (cell.codepoint != U' ' && cell.codepoint != 0) {
        unsigned char glyph =
            (cell.codepoint <= 127) ? static_cast<unsigned char>(cell.codepoint)
                                    : static_cast<unsigned char>('?');
        auto& slot = impl_->glyph_slots[glyph];
        float const cr = static_cast<float>(bg.r) / 255.0f;
        float const cg = static_cast<float>(bg.g) / 255.0f;
        float const cb = static_cast<float>(bg.b) / 255.0f;
        emit_quad(x0, y0, x1, y1,
                  slot.u0, slot.v0, slot.u1, slot.v1,
                  cr, cg, cb);
    }
} else {
    // (existing normal-cell rendering — unchanged)
}
```

**Edge cases handled by this logic:**

| Scenario | Behaviour |
|----------|-----------|
| Empty cell (space, default fg/bg) | Solid block in `#CDD6F4` (text colour) |
| Cell with coloured text, default bg | Block in the text colour; text in bg colour (`#1E1E2E`) |
| Cell with explicit fg **and** bg | Full reverse video — block in fg, glyph in bg |
| Cursor outside visible window | `cursor_visible = false` → no cursor drawn |
| Zero-size grid / zero draw area | Loop never executes; safe |

---

## Step 3 — Wire up in `main.cpp`

One-line change in the render section of the message loop:

**Before:**

```cpp
renderer.draw_grid(device, rtv, cells, grid.cols(), grid.rows())
```

**After:**

```cpp
renderer.draw_grid(device, rtv, cells, grid.cols(), grid.rows(),
                    grid.cursor_row(), grid.cursor_col())
```

---

## Step 4 — Build and manual test

```powershell
cd build\debug
cmake --build . --config Debug
.\betty.exe
```

### Manual test checklist

| Test | Expected result |
|------|----------------|
| Launch betty | Block cursor visible at shell prompt position |
| Type some characters | Cursor advances; block follows each typed char |
| Press Enter | Cursor moves to new line; block appears at new prompt |
| Run `ls` | Cursor appears at end of output (next prompt) |
| Run a command that moves cursor (e.g. PowerShell tab-completion) | Cursor block reflects position |
| Run `clear` (even though erase isn't wired yet) | Cursor moves to (0,0); block appears at top-left |
| Empty cell at cursor (initial state after clear) | Solid block in `#CDD6F4` (text colour) on `#1E1E2E` background |
| Coloured prompt (e.g. oh-my-posh with SGR) | Cursor block inverts the prompt's colours correctly |
| Resize window (Task 10, partial — current placeholder resize) | Cursor position clamps to new bounds; block visible |

### If something looks wrong

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| No cursor visible | `cursor_row`/`cursor_col` out of range | Verify grid cursor tracking (already correct from Task 5) |
| Cursor colour wrong | `resolve_bg` returning unexpected value | Double-check the flags bit for default bg |
| Flickering / flashing | Not expected (static cursor); if seen, check double-buffer | Swap chain is flip-sequential; should be fine |

---

## Dependencies

- ✅ Task 5 (cursor movement sequences) — provides accurate `cursor_row_`/`cursor_col_`
- ✅ Task 6 (SGR colours) — `draw_grid` already handles per-cell fg/bg
- No dependency on Tasks 8–18

---

## Estimated effort

~15–20 lines changed across 3 files.  The bulk of the logic is already in
`draw_grid`; we're just adding an `if (is_cursor)` branch that inverts the
existing colour application.
