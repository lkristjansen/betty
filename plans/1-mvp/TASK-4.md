# TASK 4: Terminal Grid

## Summary

Replace the simple line-based `text_buffer` with a proper 2D terminal grid. Characters are placed at a cursor position on a rows×columns grid. Newlines advance the cursor to the next row. When output reaches the bottom row, the grid scrolls up. The shell layer is updated to provide raw byte output (preserving `\r` and `\n`) so the grid can process every byte individually.

---

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Shell output format | **Raw bytes** (preserve `\r`, `\n`) | Grid needs per-byte control; sets foundation for Task 5 escape parsing |
| Cell codepoint type | **`char32_t`** | Future-proof for Task 15 Unicode; trivial overhead |
| `text_buffer` fate | **Replaced** by `terminal_grid` | Grid subsumes all functionality; no dead code |
| Renderer interface | **New `draw_grid()`** method | Per-cell rendering needed for Task 6 colours + Task 12 attributes |
| Grid size | **Fixed at startup** (window size / cell size) | Resize support comes in Task 10 |
| Auto-wrap | **Wrap to next row, col 0** | Standard DECAWM behaviour; matches ConPTY expectations |

---

## Files Changed

| File | Action |
|---|---|
| `src/platform/shell.hpp` | Add `read_shell_output_raw()` declaration |
| `src/platform/shell.cpp` | Preserve `\r`/`\n` in VT stripping; add raw byte buffer; implement `read_shell_output_raw()` |
| `src/terminal/grid.hpp` | **New** — `grid_cell`, `terminal_grid` class declarations |
| `src/terminal/grid.cpp` | **New** — `terminal_grid` implementation |
| `src/terminal/CMakeLists.txt` | Add `grid.cpp`; remove `text_buffer.cpp` |
| `src/platform/text.hpp` | Add `draw_grid()` declaration |
| `src/platform/text.cpp` | Implement `draw_grid()` |
| `src/main.cpp` | Replace `text_buffer` with `terminal_grid`; use raw shell output; render via `draw_grid()` |
| `src/terminal/text_buffer.hpp` | **Delete** |
| `src/terminal/text_buffer.cpp` | **Delete** |

---

## Step-by-Step Implementation

### Step 1 — Shell layer: raw byte output

**Goal:** The shell provides a function that returns raw bytes (VT-stripped, but `\r` and `\n` preserved) instead of pre-split lines.

#### 1a. Preserve `\r` and `\n` in `strip_vt` (`shell.cpp`)

Current code drops `\r`:
```cpp
if (c == '\n' || (c >= 0x20 && c < 0x7F) || c >= 0x80) {
    out += static_cast<char>(c);
}
```

Change to also keep `\r`:
```cpp
if (c == '\n' || c == '\r' || (c >= 0x20 && c < 0x7F) || c >= 0x80) {
    out += static_cast<char>(c);
}
```

#### 1b. Add raw byte buffer to `shell_impl` (`shell.cpp`)

Add a new member to the PIMPL struct:
```cpp
std::string raw_buffer;  // protected by output_mutex
```

#### 1c. Write to raw buffer in the read thread (`shell.cpp`)

In `read_thread_fn`, after the existing VT stripping, append the cleaned bytes directly to `raw_buffer` instead of (or in addition to) line-splitting. Remove the line-splitting logic (`find('\n')`, queue push per line) and the 100ms partial-line flush timer.

The thread becomes:
1. Read chunk from pipe → `read_buf`
2. `strip_vt({read_buf, bytes_read})` → `cleaned`
3. `lock; raw_buffer.append(cleaned); unlock`
4. Notify CV

Keep the existing `output_mutex` and `output_cv` for synchronisation.

#### 1d. Add `read_shell_output_raw()` (`shell.hpp` + `shell.cpp`)

New public function in `shell.hpp`:
```cpp
// Read raw VT-stripped output from the shell.
// Returns a string of bytes. \r and \n are preserved.
// Empty string means no new data. Caller checks is_shell_running for exit.
[[nodiscard]] auto read_shell_output_raw(shell& sh) -> std::string;
```

Implementation in `shell.cpp`: lock the mutex, swap `raw_buffer` with a local, return the local.

#### 1e. Keep the existing `read_shell_output()` for now

Don't delete it yet — it's still used by `main.cpp` before Step 4 integrates the grid. It will operate on the same data path. Once the grid is fully wired in, we can remove it (or it can coexist — the read thread populates both buffers if we prefer).

**Simplest approach:** Modify the read thread to *only* write to `raw_buffer`. Temporarily make `read_shell_output()` drain from `raw_buffer` and split on `\n` itself (so main.cpp still works during the transition). Then in Step 4, switch main.cpp to use `read_shell_output_raw()` directly.

---

### Step 2 — `terminal_grid` class (`src/terminal/grid.hpp`, `grid.cpp`)

**Goal:** A 2D cell grid with cursor tracking, character placement, newline handling, and auto-scroll.

#### 2a. Cell type

```cpp
struct grid_cell {
    char32_t codepoint = U' ';  // default: space
    // Future (Task 6):  rgba_color fg, bg;
    // Future (Task 12): uint8_t attrs;  // bold, italic, underline, etc.
};
```

#### 2b. `terminal_grid` class API

```cpp
class terminal_grid {
public:
    // Create a grid of `rows` × `cols` cells, all initialised to space.
    terminal_grid(uint32_t cols, uint32_t rows);

    // --- Dimensions ---
    [[nodiscard]] uint32_t cols() const noexcept;
    [[nodiscard]] uint32_t rows() const noexcept;

    // --- Cursor ---
    [[nodiscard]] uint32_t cursor_col() const noexcept;
    [[nodiscard]] uint32_t cursor_row() const noexcept;

    // --- Write operations ---

    // Write a single printable codepoint at the cursor, advance cursor.
    // Handles auto-wrap and scroll.
    void write_char(char32_t cp);

    // Process a sequence of raw bytes (after VT stripping).
    // Handles \r, \n, and printable ASCII. Ignores other control chars.
    void write_bytes(std::string_view data);

    // --- Explicit control ---
    void newline();          // col = 0, row++; scroll if needed
    void carriage_return();  // col = 0
    void scroll_up();        // shift all rows up by one, clear last row

    // --- Access (for rendering) ---
    [[nodiscard]] grid_cell const& cell(uint32_t row, uint32_t col) const;
    [[nodiscard]] std::span<const grid_cell> cells() const noexcept;  // flat span, row-major

    // --- Resize (placeholder for Task 10) ---
    void resize(uint32_t new_cols, uint32_t new_rows);

private:
    uint32_t cols_, rows_;
    uint32_t cursor_col_ = 0;
    uint32_t cursor_row_ = 0;
    std::vector<grid_cell> cells_;  // size = cols_ * rows_
};
```

#### 2c. Internal layout

Cells stored in a flat `std::vector<grid_cell>` of size `cols_ × rows_`, row-major. Index for `(row, col)` is `row * cols_ + col`.

#### 2d. `write_char(char32_t cp)` logic

1. If `cursor_col_ < cols_`: place `cp` at `cells_[cursor_row_ * cols_ + cursor_col_]`
2. `cursor_col_++`
3. If `cursor_col_ == cols_` (auto-wrap):
   - `cursor_col_ = 0`
   - `cursor_row_++`
   - If `cursor_row_ == rows_`: `scroll_up()` and `cursor_row_ = rows_ - 1`

*Edge case:* `write_char` only handles printable codepoints (≥ 0x20). The caller (`write_bytes`) is responsible for filtering control characters before calling `write_char`.

#### 2e. `write_bytes(std::string_view data)` logic

For each byte `b` in `data`:
- `b == '\r'` → `carriage_return()`
- `b == '\n'` → `newline()`
- `b >= 0x20` (printable ASCII) → `write_char(static_cast<char32_t>(b))`
- Otherwise → ignore (C0 control chars, high bytes)

*Note:* Non-ASCII bytes (≥ 0x80) are ignored for now. The renderer's glyph atlas is ASCII-only. Task 15 adds proper UTF-8 decoding.

#### 2f. `newline()` logic

1. `cursor_col_ = 0`
2. `cursor_row_++`
3. If `cursor_row_ == rows_`: `scroll_up()` and `cursor_row_ = rows_ - 1`

*Note:* A newline does **not** clear the rest of the line — it just moves the cursor. The shell may have already written content to the current row, and a `\n` means "move to next row, keep what's on this row."

#### 2g. `scroll_up()` logic

1. `std::memmove` rows [1..rows_-1] to [0..rows_-2] — shifts all rows up
2. Clear the last row (row `rows_ - 1`): fill with `grid_cell{}` (space)
3. `cursor_row_` is **not** modified — the caller adjusts it if needed

#### 2h. `carriage_return()` logic

`cursor_col_ = 0`

#### 2i. Resize (placeholder)

For Task 10. Implement as a stub that reallocates the vector and copies existing content where it fits, filling new cells with spaces.

---

### Step 3 — `draw_grid()` renderer method (`text.hpp`, `text.cpp`)

**Goal:** Render the entire grid in one draw call, iterating cells row-by-row and column-by-column.

#### 3a. Declaration (`text.hpp`)

Add to `glyph_renderer`:
```cpp
// Draw a terminal grid. `cells` is a row-major flat array of `rows × cols` cells.
// Each cell's codepoint is rendered at its grid position.
// Non-ASCII codepoints are rendered as '?'.
[[nodiscard]] auto draw_grid(d3d_device const& device, d3d_render_target_view const& rtv,
                             std::span<const char32_t> cells,
                             uint32_t cols, uint32_t rows) const
    -> std::expected<void, std::error_code>;
```

*Note:* The parameter is `span<const char32_t>` (not `span<const grid_cell>`) to avoid a dependency from `platform/text.hpp` on `terminal/grid.hpp`. The caller extracts the codepoint array from the grid.

#### 3b. Implementation (`text.cpp`)

Structure mirrors `draw_text()`:
1. Map dynamic vertex buffer with `D3D11_MAP_WRITE_DISCARD`
2. Iterate `row` from 0 to `rows-1`, `col` from 0 to `cols-1`:
   - `char32_t cp = cells[row * cols + col]`
   - If `cp > 127`, use `'?'` (63)
   - Compute pixel position: `x0 = col * cell_width`, `y0 = row * cell_height`
   - Emit quad vertices with UVs from `glyph_slots[cp]`
   - Track quad count; clamp at `k_max_glyphs_per_frame`
3. Unmap, bind pipeline, draw indexed

**Optimisation note:** Render *every* cell including spaces. When Task 6 adds background colours, the cell quad must be drawn even if the character is a space (to fill the background). Rendering all cells now ensures the pipeline is ready.

**Performance:** At ~53×33 = 1749 cells, well within the 8192 glyph limit. Even after resize to e.g. 200×60 = 12000 cells we'd exceed the limit — but that's a Task 10 concern (batch or increase limit).

#### 3c. Renderer decoupling

The renderer receives `span<const char32_t>` — just codepoints. The grid provides this via a helper or the caller converts. This keeps the platform layer completely unaware of the terminal layer's `grid_cell` type.

---

### Step 4 — Integrate in `main.cpp`

**Goal:** Replace `text_buffer` with `terminal_grid`, wire raw shell output through the grid, render via `draw_grid()`.

#### 4a. Include and instantiation

```cpp
// Remove:
// #include "terminal/text_buffer.hpp"

// Add:
#include "terminal/grid.hpp"

// Replace:
// terminal::text_buffer buffer(rows);

// With:
terminal::terminal_grid grid(cols, rows);
```

#### 4b. Shell output processing

Replace:
```cpp
auto output = platform::read_shell_output(*shell);
if (output && !output->empty()) {
    for (auto& line : *output) {
        if (line.empty()) continue;
        bool const all_ws = line.find_first_not_of(" \t") == std::string::npos;
        if (all_ws) continue;
        buffer.append_line(std::move(line));
    }
}
```

With:
```cpp
std::string raw = platform::read_shell_output_raw(*shell);
if (!raw.empty()) {
    grid.write_bytes(raw);
}
```

Also update the "drain remaining output after exit" block similarly.

#### 4c. Rendering

Replace:
```cpp
auto const& completed = buffer.lines();
std::vector<std::string> render_owned;
std::vector<std::string_view> render_lines;
// ... build render_lines ...
if (!render_lines.empty()) {
    uint32_t const screen_rows = renderer.cell_height()
        ? platform::default_window_size.height / renderer.cell_height()
        : 1;
    uint32_t const start_row =
        render_lines.size() > screen_rows
            ? render_lines.size() - screen_rows
            : 0;
    if (auto draw_result = renderer.draw_text(device, rtv, render_lines, start_row); !draw_result) {
        log_error(draw_result.error(), "draw text");
        return 1;
    }
}
```

With:
```cpp
// Extract codepoints from grid for rendering
std::vector<char32_t> render_cells;
render_cells.reserve(grid.rows() * grid.cols());
for (auto const& cell : grid.cells()) {
    render_cells.push_back(cell.codepoint);
}

if (auto draw_result = renderer.draw_grid(device, rtv, render_cells, grid.cols(), grid.rows());
    !draw_result) {
    log_error(draw_result.error(), "draw grid");
    return 1;
}
```

**Note:** The `render_cells` copy is temporary. Once the renderer interface is refined (or the grid provides a direct `codepoints()` span), this copy can be eliminated. For now it keeps the platform/terminal decoupling clean.

#### 4d. Remove old scroll logic

The scrolling (`start_row` calculation) is removed — the grid handles scrolling internally via `scroll_up()`. The renderer always draws rows 0..rows-1.

#### 4e. Shell exit message

Replace:
```cpp
buffer.append_line("[shell exited]");
```

With:
```cpp
grid.write_bytes("[shell exited]\r\n");
```

---

### Step 5 — Remove `text_buffer` and cleanup

#### 5a. Delete files

- `src/terminal/text_buffer.hpp`
- `src/terminal/text_buffer.cpp`

#### 5b. Update `src/terminal/CMakeLists.txt`

```cmake
add_library(terminal STATIC
    grid.cpp
    input_handler.cpp
)
```

#### 5c. Remove old `read_shell_output()` (optional)

Once main.cpp is fully switched to `read_shell_output_raw()`, the old line-based `read_shell_output()` can be removed from `shell.hpp` and `shell.cpp`. The read thread no longer splits on `\n` — it just appends to `raw_buffer`.

#### 5d. Clean up `shell_impl`

Remove `output_queue` (the `deque<string>`), since it's no longer populated. The `output_mutex` and `output_cv` remain for the raw buffer.

---

## Data Flow (after Task 4)

```
┌─────────────┐    raw bytes (VT-stripped, \r \n preserved)
│  ConPTY     │──────────────────────────────────────────────┐
│  (PowerShell)│                                              │
└─────────────┘                                              ▼
                                              ┌──────────────────────┐
                                              │  read_thread_fn      │
                                              │  ReadFile → strip_vt │
                                              │  → raw_buffer        │
                                              └──────────┬───────────┘
                                                         │
                                        read_shell_output_raw()
                                                         │
                                                         ▼
                                              ┌──────────────────────┐
                                              │  main.cpp            │
                                              │  grid.write_bytes()  │
                                              └──────────┬───────────┘
                                                         │
                                              ┌──────────▼───────────┐
                                              │  terminal_grid       │
                                              │  ┌───┬───┬───┬───┐  │
                                              │  │   │   │   │   │  │
                                              │  ├───┼───┼───┼───┤  │
                                              │  │   │   │ C │   │  │  ← cursor
                                              │  ├───┼───┼───┼───┤  │
                                              │  │   │   │   │   │  │
                                              │  └───┴───┴───┴───┘  │
                                              └──────────┬───────────┘
                                                         │
                                              grid.cells() → codepoints
                                                         │
                                                         ▼
                                              ┌──────────────────────┐
                                              │  glyph_renderer      │
                                              │  draw_grid()         │
                                              │  → D3D11 draw call   │
                                              └──────────────────────┘
```

---

## Edge Cases Handled

| Scenario | Behaviour |
|---|---|
| Cursor at last column + printable char | Character placed in last col, auto-wrap to next row col 0 |
| Cursor at last row + newline/auto-wrap | `scroll_up()` shifts all rows up, cursor stays on last row |
| `\r\n` sequence | `\r` sets col=0, `\n` moves to next row — standard Windows line ending |
| `\r` without `\n` (progress output) | Cursor returns to col 0, subsequent output overwrites the same row |
| Empty shell output | `read_shell_output_raw()` returns `""`, no grid changes, no render |
| Shell exit with unflushed data | Read thread flushes raw buffer on exit; main drains it once more |
| Byte >= 0x80 (non-ASCII) | Silently ignored by `write_bytes` (Task 15 adds UTF-8 decoding) |
| Escape sequence remnants | `strip_vt` removes them before they reach the grid |
| Grid full + continuous output | Each new line scrolls the grid up, oldest row is lost |

---

## Verification Checklist

After implementation, launch `betty.exe` and verify:

- [ ] PowerShell prompt appears at the top-left of the window
- [ ] Typing characters appears at the cursor position, cursor advances right
- [ ] Pressing Enter sends command, output appears on the next row below the prompt
- [ ] When output reaches the bottom of the window, the grid scrolls up
- [ ] Old output scrolls off the top (not visible)
- [ ] Characters are aligned in a proper monospace grid (no overlapping, consistent spacing)
- [ ] Running `dir` or `ls` fills rows top-to-bottom and scrolls at the bottom
- [ ] Running `ping -t localhost` shows progress dots overwriting on the same line (`\r` handling)
- [ ] Typing `exit` closes the window cleanly (shell exit message appears in grid)
- [ ] Closing the window via [X] terminates cleanly with no crash

---

## Dependencies

- **Depends on:** Task 3 (live shell I/O) — provides ConPTY output stream
- **Required by:** Task 5 (cursor movement sequences), Task 6 (SGR colours), Task 7 (block cursor)
