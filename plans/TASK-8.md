# Task 8 — Erase Operations: Implementation Plan

## Goal

Parse ED (Erase in Display) and EL (Erase in Line) escape sequences. When the
user types `clear` or presses Ctrl+L, the screen actually clears. Shell line
editing (backspace, rewrites) erases characters properly. Erased grid cells
are reset to space with default foreground and background colours.

---

## Architecture overview

The data path remains unchanged from Task 7:

```
ConPTY output → grid.write_bytes() → vt_parser.parse() → action
                                   → grid.apply(action) → erase cells
                                                         → cursor unchanged
```

Two new `action_type` values are added: `erase_display` and `erase_line`.
The parser emits them on CSI `J` and `K` final bytes. The grid's `apply()`
dispatches to private helper methods that fill cell ranges with default state.

The cursor position does **not** change after an erase operation — only cell
contents are affected.

---

## Design decisions

### 1. Erased cell content

Erased cells are set to:

```cpp
grid_cell{ .codepoint = U' ', .fg = default_fg(), .bg = default_bg() }
```

This follows the TASKS.md specification: *"Grid cells are erased to default
background/foreground."* Most modern terminals fill with the *current* SGR
background colour; we deliberately use defaults so that `clear` always
restores the Catppuccin Mocha base colour regardless of active SGR state.

> **Open question:** Confirm this is the desired behaviour. If you'd prefer
> erased cells to use the current SGR background (matching xterm / Windows
> Terminal), this is a one-line change in `erase_cell_range()`.

### 2. ED mode 3 (clear scrollback)

`CSI 3 J` is an xterm extension that clears the visible screen **and** the
scrollback buffer. Since scrollback doesn't exist yet (Task 11), `3J` is
treated identically to `2J` (clear entire visible screen). A comment marks
the spot for Task 11 enhancement.

### 3. Where the erase logic lives

Rather than inlining erase loops in `apply()`, two private helper methods are
added to `terminal_grid`:

- `erase_display(uint32_t mode)` — handles ED modes 0, 1, 2, 3
- `erase_line(uint32_t mode)` — handles EL modes 0, 1, 2

These are straightforward to unit test and keep `apply()` readable.

### 4. Parameter handling

The `action::count` field is reused as the *mode* parameter for erase
actions. No new fields are added to the `action` struct — `count` is unused
by erase operations and has the correct type (`uint32_t`).

The ANSI default for both ED and EL is `0` (erase forward from cursor).
When no parameter bytes are present, `parse_params()` returns `(1, 1)` for
compatibility with cursor movement sequences. The ED/EL dispatch explicitly
checks for the empty-buffer case and forces mode to 0:

```cpp
case 'J': {
    // ED: default mode is 0 (unlike cursor moves which default to 1).
    uint32_t mode = param_buffer_.empty() ? 0 : p1;
    ...
}
```

---

## Sequences to implement

### ED — Erase in Display: `CSI Ps J`

| Ps  | Name | Behaviour |
|-----|------|-----------|
| 0   | Erase from cursor to end of display | Clear cells from `(cursor_row, cursor_col)` inclusive to `(rows-1, cols-1)` |
| 1   | Erase from beginning to cursor | Clear cells from `(0, 0)` to `(cursor_row, cursor_col)` inclusive |
| 2   | Erase entire display | Clear all cells `(0, 0)` to `(rows-1, cols-1)` |
| 3   | Erase entire display + scrollback | Treated as 2 for now (scrollback added in Task 11) |

### EL — Erase in Line: `CSI Ps K`

| Ps  | Name | Behaviour |
|-----|------|-----------|
| 0   | Erase from cursor to end of line | Clear cells on current row from `cursor_col` to `cols-1` |
| 1   | Erase from beginning of line to cursor | Clear cells on current row from `0` to `cursor_col` inclusive |
| 2   | Erase entire line | Clear all cells on current row from `0` to `cols-1` |

---

## Files to modify

| File | Change |
|------|--------|
| `src/terminal/vt_parser.hpp` | Add `erase_display`, `erase_line` to `action_type` enum |
| `src/terminal/vt_parser.cpp` | Add `J` and `K` cases in `dispatch()` |
| `src/terminal/grid.hpp` | Declare `erase_display()`, `erase_line()`, `erase_cell_range()` |
| `src/terminal/grid.cpp` | Implement erase helpers; add cases in `apply()` |
| `tests/vt_parser_test.cpp` | Tests for ED/EL parsing |
| `tests/grid_test.cpp` | Tests for erase operations on the grid |

No new files. No changes to `main.cpp`, the renderer, the shell, or the input
handler.

---

## Step 1 — Add erase action types (`vt_parser.hpp`)

Add two new values to the `action_type` enum:

```cpp
enum class action_type : uint8_t {
  // ... existing values ...
  erase_display,      // ED: clear cells in display (mode in action::count)
  erase_line,         // EL: clear cells in current line (mode in action::count)
};
```

The `action` struct needs no changes — the `count` field carries the mode
parameter (0, 1, 2, or 3).

---

## Step 2 — Parse ED and EL in the VT parser (`vt_parser.cpp`)

### 2a. Add dispatch cases for `J` and `K`

In `vt_parser::dispatch()`, after the SGR (`m`) case and before the cursor
movement switch, add:

```cpp
// ── ED: Erase in Display ──────────────────────────────────────────────
if (final_byte == 'J') {
    // ANSI default for ED is 0 (unlike cursor moves which default to 1).
    // parse_params() returns (1,1) for empty buffer, so we check explicitly.
    uint32_t mode = 0;
    if (!param_buffer_.empty()) {
        auto const [p1, p2] = parse_params();
        (void)p2;
        mode = p1;
        // parse_params treats 0 as 1; correct it back for ED where 0 is valid.
        if (param_buffer_ == "0") mode = 0;
    }
    action a{};
    a.type  = action_type::erase_display;
    a.count = mode;
    return {a};
}

// ── EL: Erase in Line ─────────────────────────────────────────────────
if (final_byte == 'K') {
    uint32_t mode = 0;
    if (!param_buffer_.empty()) {
        auto const [p1, p2] = parse_params();
        (void)p2;
        mode = p1;
        if (param_buffer_ == "0") mode = 0;
    }
    action a{};
    a.type  = action_type::erase_line;
    a.count = mode;
    return {a};
}
```

> **Note:** `parse_params()` treats an empty segment or `"0"` as `1` (ANSI
> default for cursor moves). For ED/EL, the default is `0`, not `1`. We
> handle this by checking `param_buffer_` directly before falling back to
> `parse_params()`. This is a small wart in the existing param parser —
> a future refactor could make the "treat 0 as 1" behaviour opt-in per
> sequence type, but that's out of scope for this task.

### 2b. Intermediate byte handling

ED and EL can arrive with private/intermediate bytes (e.g. `CSI ? J`).
The existing `csi_intermediate` state already passes the final byte to
`dispatch()` unchanged, so no additional work is needed — these sequences
will parse correctly (intermediate bytes are silently ignored).

---

## Step 3 — Declare erase helpers (`grid.hpp`)

Add three private method declarations to `terminal_grid`:

```cpp
private:
  // ... existing members ...

  // Erase a contiguous range of cells. Both start and end are inclusive.
  // start_idx and end_idx are flat indices into cells_ (row-major).
  void erase_cell_range(size_t start_idx, size_t end_idx);

  // ED — Erase in Display (CSI Ps J).
  void erase_display(uint32_t mode);

  // EL — Erase in Line (CSI Ps K).
  void erase_line(uint32_t mode);
```

---

## Step 4 — Implement erase logic (`grid.cpp`)

### 4a. `erase_cell_range(start_idx, end_idx)`

Low-level helper that sets a contiguous span of cells to their default state:

```cpp
void terminal_grid::erase_cell_range(size_t start_idx, size_t end_idx) {
    assert(end_idx >= start_idx);
    assert(end_idx < cells_.size());
    for (size_t i = start_idx; i <= end_idx; ++i) {
        cells_[i] = grid_cell{};  // space, default fg, default bg
    }
}
```

`grid_cell{}` default-constructs to `{U' ', default_fg(), default_bg()}` as
defined in the struct — exactly the erased state we want.

### 4b. `erase_display(mode)`

```cpp
void terminal_grid::erase_display(uint32_t mode) {
    if (cols_ == 0 || rows_ == 0) return;

    size_t const total = static_cast<size_t>(cols_) * rows_;
    if (total == 0) return;

    size_t const cursor_idx =
        static_cast<size_t>(cursor_row_) * cols_ + cursor_col_;

    switch (mode) {
    case 0: // Erase from cursor to end of display (inclusive).
        erase_cell_range(cursor_idx, total - 1);
        break;

    case 1: // Erase from beginning of display to cursor (inclusive).
        erase_cell_range(0, cursor_idx);
        break;

    case 2: // Erase entire display.
    case 3: // Erase entire display + scrollback (Task 11 adds scrollback).
        erase_cell_range(0, total - 1);
        break;

    default:
        // Unknown mode — treat as 0 (safe default).
        erase_cell_range(cursor_idx, total - 1);
        break;
    }
}
```

### 4c. `erase_line(mode)`

```cpp
void terminal_grid::erase_line(uint32_t mode) {
    if (cols_ == 0 || rows_ == 0) return;

    size_t const row_start = static_cast<size_t>(cursor_row_) * cols_;
    size_t const row_end   = row_start + cols_ - 1;

    size_t const cursor_idx = row_start + cursor_col_;

    switch (mode) {
    case 0: // Erase from cursor to end of line (inclusive).
        erase_cell_range(cursor_idx, row_end);
        break;

    case 1: // Erase from beginning of line to cursor (inclusive).
        erase_cell_range(row_start, cursor_idx);
        break;

    case 2: // Erase entire line.
        erase_cell_range(row_start, row_end);
        break;

    default:
        // Unknown mode — treat as 0 (safe default).
        erase_cell_range(cursor_idx, row_end);
        break;
    }
}
```

### 4d. Wire up in `apply()`

Add two cases to the `switch (a.type)` in `terminal_grid::apply()`:

```cpp
case action_type::erase_display:
    erase_display(a.count);
    break;
case action_type::erase_line:
    erase_line(a.count);
    break;
```

---

## Step 5 — Unit tests: VT parser (`tests/vt_parser_test.cpp`)

Add a new test section for ED and EL parsing.

### ED tests

```cpp
TEST_CASE("CSI ED — no param defaults to mode 0", "[csi][ed]") {
    auto const v = parse_sequence("\x1B[J");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_display);
    CHECK(v[0].count == 0);
}

TEST_CASE("CSI ED — mode 0 (erase to end)", "[csi][ed]") {
    auto const v = parse_sequence("\x1B[0J");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_display);
    CHECK(v[0].count == 0);
}

TEST_CASE("CSI ED — mode 1 (erase from beginning)", "[csi][ed]") {
    auto const v = parse_sequence("\x1B[1J");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_display);
    CHECK(v[0].count == 1);
}

TEST_CASE("CSI ED — mode 2 (erase entire display)", "[csi][ed]") {
    auto const v = parse_sequence("\x1B[2J");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_display);
    CHECK(v[0].count == 2);
}

TEST_CASE("CSI ED — mode 3 (erase display + scrollback)", "[csi][ed]") {
    auto const v = parse_sequence("\x1B[3J");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_display);
    CHECK(v[0].count == 3);
}
```

### EL tests

```cpp
TEST_CASE("CSI EL — no param defaults to mode 0", "[csi][el]") {
    auto const v = parse_sequence("\x1B[K");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_line);
    CHECK(v[0].count == 0);
}

TEST_CASE("CSI EL — mode 0 (erase to end of line)", "[csi][el]") {
    auto const v = parse_sequence("\x1B[0K");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_line);
    CHECK(v[0].count == 0);
}

TEST_CASE("CSI EL — mode 1 (erase from beginning of line)", "[csi][el]") {
    auto const v = parse_sequence("\x1B[1K");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_line);
    CHECK(v[0].count == 1);
}

TEST_CASE("CSI EL — mode 2 (erase entire line)", "[csi][el]") {
    auto const v = parse_sequence("\x1B[2K");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_line);
    CHECK(v[0].count == 2);
}
```

### Integration: parser recovers after ED/EL

```cpp
TEST_CASE("CSI ED/EL — parser returns to ground after erase", "[csi][ed][el]") {
    vt_parser p;
    parse_sequence(p, "\x1B[2J");
    auto const v = p.parse('A');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::write_char);
}

TEST_CASE("CSI ED — with intermediate byte", "[csi][ed]") {
    auto const v = parse_sequence("\x1B[?J");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_display);
    CHECK(v[0].count == 0);
}
```

---

## Step 6 — Unit tests: grid erase operations (`tests/grid_test.cpp`)

Add a test section for erase operations on the grid.

### ED tests

```cpp
TEST_CASE("Grid — erase_display mode 0 clears from cursor to end", "[grid][erase]") {
    terminal_grid g(5, 3);
    // Fill all cells with 'X'.
    for (uint32_t r = 0; r < 3; ++r)
        for (uint32_t c = 0; c < 5; ++c) {
            action mv;
            mv.type = action_type::move_cursor;
            mv.row = r; mv.col = c;
            g.apply(mv);
            g.write_char(U'X');
        }
    // Cursor ends at (2,4) — move to middle (1,2).
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 1; mv.col = 2;
    g.apply(mv);

    // Erase from cursor to end.
    action ed;
    ed.type = action_type::erase_display;
    ed.count = 0;
    g.apply(ed);

    // All cells BEFORE cursor should still be 'X'.
    CHECK(g.cell(0, 0).codepoint == U'X');
    CHECK(g.cell(0, 1).codepoint == U'X');
    CHECK(g.cell(0, 2).codepoint == U'X');
    CHECK(g.cell(1, 0).codepoint == U'X');
    CHECK(g.cell(1, 1).codepoint == U'X');

    // Cells FROM cursor should be space.
    CHECK(g.cell(1, 2).codepoint == U' ');
    CHECK(g.cell(1, 3).codepoint == U' ');
    CHECK(g.cell(1, 4).codepoint == U' ');
    CHECK(g.cell(2, 0).codepoint == U' ');
    CHECK(g.cell(2, 4).codepoint == U' ');
}

TEST_CASE("Grid — erase_display mode 1 clears from beginning to cursor", "[grid][erase]") {
    terminal_grid g(5, 3);
    // Fill all cells with 'X'.
    for (uint32_t r = 0; r < 3; ++r)
        for (uint32_t c = 0; c < 5; ++c) {
            action mv;
            mv.type = action_type::move_cursor;
            mv.row = r; mv.col = c;
            g.apply(mv);
            g.write_char(U'X');
        }
    // Move cursor to (1, 2).
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 1; mv.col = 2;
    g.apply(mv);

    action ed;
    ed.type = action_type::erase_display;
    ed.count = 1;
    g.apply(ed);

    // Cells up to and including cursor should be space.
    CHECK(g.cell(0, 0).codepoint == U' ');
    CHECK(g.cell(0, 1).codepoint == U' ');
    CHECK(g.cell(1, 0).codepoint == U' ');
    CHECK(g.cell(1, 1).codepoint == U' ');
    CHECK(g.cell(1, 2).codepoint == U' ');

    // Cells after cursor should still be 'X'.
    CHECK(g.cell(1, 3).codepoint == U'X');
    CHECK(g.cell(2, 0).codepoint == U'X');
}

TEST_CASE("Grid — erase_display mode 2 clears entire screen", "[grid][erase]") {
    terminal_grid g(5, 3);
    // Fill with 'X'.
    for (uint32_t r = 0; r < 3; ++r)
        for (uint32_t c = 0; c < 5; ++c) {
            action mv;
            mv.type = action_type::move_cursor;
            mv.row = r; mv.col = c;
            g.apply(mv);
            g.write_char(U'X');
        }

    action ed;
    ed.type = action_type::erase_display;
    ed.count = 2;
    g.apply(ed);

    // Every cell should now be space.
    for (uint32_t r = 0; r < 3; ++r)
        for (uint32_t c = 0; c < 5; ++c)
            CHECK(g.cell(r, c).codepoint == U' ');
}

TEST_CASE("Grid — erase_display does not move cursor", "[grid][erase]") {
    terminal_grid g(5, 3);
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 2; mv.col = 3;
    g.apply(mv);

    action ed;
    ed.type = action_type::erase_display;
    ed.count = 2;
    g.apply(ed);

    CHECK(g.cursor_row() == 2);
    CHECK(g.cursor_col() == 3);
}
```

### EL tests

```cpp
TEST_CASE("Grid — erase_line mode 0 clears from cursor to end of line", "[grid][erase]") {
    terminal_grid g(5, 3);
    // Fill row 1 with 'X'.
    for (uint32_t c = 0; c < 5; ++c) {
        action mv;
        mv.type = action_type::move_cursor;
        mv.row = 1; mv.col = c;
        g.apply(mv);
        g.write_char(U'X');
    }
    // Cursor at (1, 2).
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 1; mv.col = 2;
    g.apply(mv);

    action el;
    el.type = action_type::erase_line;
    el.count = 0;
    g.apply(el);

    // Columns 0-1 unchanged.
    CHECK(g.cell(1, 0).codepoint == U'X');
    CHECK(g.cell(1, 1).codepoint == U'X');
    // Columns 2-4 erased.
    CHECK(g.cell(1, 2).codepoint == U' ');
    CHECK(g.cell(1, 3).codepoint == U' ');
    CHECK(g.cell(1, 4).codepoint == U' ');

    // Other rows untouched.
    CHECK(g.cell(0, 0).codepoint == U' ');
    CHECK(g.cell(2, 0).codepoint == U' ');
}

TEST_CASE("Grid — erase_line mode 1 clears from beginning to cursor", "[grid][erase]") {
    terminal_grid g(5, 3);
    for (uint32_t c = 0; c < 5; ++c) {
        action mv;
        mv.type = action_type::move_cursor;
        mv.row = 1; mv.col = c;
        g.apply(mv);
        g.write_char(U'X');
    }
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 1; mv.col = 2;
    g.apply(mv);

    action el;
    el.type = action_type::erase_line;
    el.count = 1;
    g.apply(el);

    CHECK(g.cell(1, 0).codepoint == U' ');
    CHECK(g.cell(1, 1).codepoint == U' ');
    CHECK(g.cell(1, 2).codepoint == U' ');
    CHECK(g.cell(1, 3).codepoint == U'X');
    CHECK(g.cell(1, 4).codepoint == U'X');
}

TEST_CASE("Grid — erase_line mode 2 clears entire line", "[grid][erase]") {
    terminal_grid g(5, 3);
    for (uint32_t c = 0; c < 5; ++c) {
        action mv;
        mv.type = action_type::move_cursor;
        mv.row = 1; mv.col = c;
        g.apply(mv);
        g.write_char(U'X');
    }

    action el;
    el.type = action_type::erase_line;
    el.count = 2;
    g.apply(el);

    for (uint32_t c = 0; c < 5; ++c)
        CHECK(g.cell(1, c).codepoint == U' ');

    // Other rows untouched.
    CHECK(g.cell(0, 0).codepoint == U' ');
    CHECK(g.cell(2, 0).codepoint == U' ');
}

TEST_CASE("Grid — erase_line does not move cursor", "[grid][erase]") {
    terminal_grid g(5, 3);
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 1; mv.col = 3;
    g.apply(mv);

    action el;
    el.type = action_type::erase_line;
    el.count = 2;
    g.apply(el);

    CHECK(g.cursor_row() == 1);
    CHECK(g.cursor_col() == 3);
}
```

### Edge case tests

```cpp
TEST_CASE("Grid — erase_display on zero-size grid does not crash", "[grid][erase][edge]") {
    terminal_grid g(0, 0);
    action ed;
    ed.type = action_type::erase_display;
    ed.count = 2;
    g.apply(ed);
    SUCCEED("erase_display on zero-size grid did not crash");
}

TEST_CASE("Grid — erase_line on zero-size grid does not crash", "[grid][erase][edge]") {
    terminal_grid g(0, 0);
    action el;
    el.type = action_type::erase_line;
    el.count = 2;
    g.apply(el);
    SUCCEED("erase_line on zero-size grid did not crash");
}

TEST_CASE("Grid — erase_display mode 3 treated as mode 2", "[grid][erase]") {
    terminal_grid g(3, 2);
    // Fill with 'X'.
    for (uint32_t c = 0; c < 3; ++c) { g.write_char(U'X'); } // wraps to row 1
    for (uint32_t c = 0; c < 3; ++c) { g.write_char(U'X'); }

    action ed;
    ed.type = action_type::erase_display;
    ed.count = 3;
    g.apply(ed);

    for (uint32_t r = 0; r < 2; ++r)
        for (uint32_t c = 0; c < 3; ++c)
            CHECK(g.cell(r, c).codepoint == U' ');
}

TEST_CASE("Grid — erase uses default colours", "[grid][erase]") {
    terminal_grid g(5, 3);
    // Set a cell with non-default colours.
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 1; mv.col = 2;
    g.apply(mv);
    g.write_char(U'X'); // uses current_fg_/current_bg_ which are defaults.

    // Now apply SGR to change colours, then write to (1, 2).
    // We can't easily set SGR through the public API; instead we use write_bytes
    // with an SGR sequence to set a red foreground, write 'Y', then erase.
    g.write_bytes("\x1B[31m");  // SGR red fg
    g.write_char(U'Y');          // at (1, 3) — with red fg
    // (1,2) still has 'X' with default fg/bg.

    action mv2;
    mv2.type = action_type::move_cursor;
    mv2.row = 1; mv2.col = 2;
    g.apply(mv2);

    action el;
    el.type = action_type::erase_line;
    el.count = 2;
    g.apply(el);

    // Erased cell (1,2) should have default fg, default bg.
    auto const& cell = g.cell(1, 2);
    CHECK(cell.codepoint == U' ');
    CHECK(cell.fg.flags == 1);  // is_default flag set
    CHECK(cell.bg.flags == 1);
}

TEST_CASE("Grid — erase_display via write_bytes (integration)", "[grid][erase][integration]") {
    terminal_grid g(5, 3);
    // Fill with 'X'.
    for (uint32_t r = 0; r < 3; ++r)
        for (uint32_t c = 0; c < 5; ++c) {
            action mv;
            mv.type = action_type::move_cursor;
            mv.row = r; mv.col = c;
            g.apply(mv);
            g.write_char(U'X');
        }
    // Move to (1, 2) and erase from cursor to end via escape sequence.
    g.write_bytes("\x1B[2;3H");   // CUP to row 2, col 3 (0-based: 1, 2)
    g.write_bytes("\x1B[0J");     // ED mode 0

    CHECK(g.cell(0, 0).codepoint == U'X');
    CHECK(g.cell(1, 2).codepoint == U' ');
    CHECK(g.cell(2, 4).codepoint == U' ');
}
```

---

## Step 7 — Build and manual test

```powershell
cd build\debug
cmake --build . --config Debug
.\betty.exe
```

### Manual test checklist

| Test | Expected result |
|------|----------------|
| Launch betty | Shell prompt appears normally |
| Type `clear` and press Enter | Screen clears; prompt appears at top-left |
| Type some text, then Ctrl+L | Screen clears; text at current line is gone |
| `ls` (colourful output) then `clear` | Colours gone, screen is uniformly Mocha base |
| Backspace in shell | Characters are erased (shell sends EL to clear remainder of line) |
| `Write-Host "hello"` then `clear` | Screen clears; new prompt at (0, 0) |
| Type `echo "line 1"; echo "line 2"` then `clear` | Both lines cleared |
| Rapid `clear` multiple times | No flicker; each clear works immediately |
| `exit` | Window closes cleanly |

### If something looks wrong

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `clear` only moves cursor (no visible erase) | Parser not emitting `erase_display` for `J` | Check `dispatch()` case for `'J'` |
| `clear` clears wrong area | Off-by-one in `erase_cell_range` indices | Verify inclusive/exclusive bounds |
| Shell line editing looks wrong (characters remain after backspace) | EL not being parsed or applied | Check `'K'` dispatch and `erase_line()` |
| Crash on empty grid | Missing guard in `erase_display`/`erase_line` | Add `if (cols_ == 0 || rows_ == 0) return;` |
| Coloured text persists after clear (wrong background) | `grid_cell{}` not resetting fg/bg flags | Verify `grid_cell` default constructor sets `fg.flags = 1`, `bg.flags = 1` |

---

## Dependencies

- ✅ Task 5 (cursor movement sequences) — parser state machine and `apply()` dispatch exist
- ✅ Task 6 (SGR colours) — `grid_cell` has `fg`/`bg` fields; `default_fg()`/`default_bg()` sentinels exist
- ✅ Task 7 (block cursor) — cursor position tracking is accurate (erase doesn't need it but tests rely on accurate cursor)
- No dependency on Tasks 9–18

---

## Estimated effort

~60 lines of implementation code across 2 files (`vt_parser.cpp`, `grid.cpp`),
~20 lines of declarations across 2 headers, and ~150 lines of test code across
2 test files. Total ~230 lines, most of which are straightforward loop logic
and assertions.

---

## Open questions

1. **Erased cell colours**: The plan uses `default_fg()` / `default_bg()` as
   specified in TASKS.md. If you prefer the xterm/Windows Terminal behaviour
   (erased cells use the *current SGR background*), change `grid_cell{}` in
   `erase_cell_range()` to `grid_cell{.fg = default_fg(), .bg = current_bg_}`.

2. **ED mode 3**: Treated as mode 2 for now. Revisit when scrollback is added
   in Task 11.
