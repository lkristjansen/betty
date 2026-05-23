# TASK 14 — Character Operations (ICH, DCH, ECH)

**Goal:** Inline editing in the shell works correctly — inserting text shifts characters right, deleting characters pulls them left, erasing characters blanks them in place. The cursor stays at the correct position.

---

## Design Decisions *(confirmed)*

| Decision | Choice |
|---|---|
| Blank cells inserted by ICH / written by ECH | `grid_cell{}` — space, default fg/bg, no attributes (matches IL/DL) |
| Clamping when `n > available space` | Clamp to remaining columns (matches IL/DL pattern) |
| Cursor after ICH/DCH/ECH | Unchanged — stays at same row & column (VT100 spec) |
| ECH (Erase Characters) included? | Yes — `CSI Ps X` |
| Tests | Parser tests + grid unit tests |

---

## Sequence Overview

```
Step 1 ──► Step 2 ──► Step 3 ──► Step 4
(vt_parser)  (grid.hpp)  (grid.cpp)  (tests)
```

All four steps are independent to write but tests (Step 4) validate everything end-to-end.

---

## Step 1 — VT Parser: new action types and CSI dispatch

**File:** `src/terminal/vt_parser.hpp`

### 1a. Add action types

In `enum class action_type` (just before `set_scroll_region` is a good spot), add three new entries:

```cpp
insert_chars,      // ICH: CSI Ps @ — insert blank cells at cursor, shift right
delete_chars,      // DCH: CSI Ps P — delete cells at cursor, shift left
erase_chars,       // ECH: CSI Ps X — overwrite cells at cursor with blanks
```

### 1b. Add to dispatch()

**File:** `src/terminal/vt_parser.cpp`

In `vt_parser::dispatch()`, before the `// ── Cursor movement (existing)` section, add three handlers:

- **`'@'` (0x40) — ICH (Insert Characters):** Parse params with `split_params`. Default count = 1 (when param is 0 or missing). Return one `action{ .type = action_type::insert_chars, .count = count }`.

- **`'P'` (0x50) — DCH (Delete Characters):** Same pattern. Return `action{ .type = action_type::delete_chars, .count = count }`.

- **`'X'` (0x58) — ECH (Erase Characters):** Same pattern. Return `action{ .type = action_type::erase_chars, .count = count }`.

The `count` field in `action` already carries a `uint32_t` — reuse it.

---

## Step 2 — Grid header: declare new methods

**File:** `src/terminal/grid.hpp`

Add three public method declarations in the `terminal_grid` class, next to `insert_lines` / `delete_lines`:

```cpp
// --- Character operations (Task 14) ----------------------------------------

// ICH — insert n blank cells at cursor, shifting the row right.
// Cells shifted past the right edge are lost. Cursor is unchanged.
void insert_chars(uint32_t n);

// DCH — delete n cells at cursor, shifting the row left.
// Blank cells fill the vacated positions on the right. Cursor is unchanged.
void delete_chars(uint32_t n);

// ECH — overwrite n cells at cursor with blank cells (no shifting).
// Cursor is unchanged.
void erase_chars(uint32_t n);
```

---

## Step 3 — Grid implementation: insert_chars, delete_chars, erase_chars

**File:** `src/terminal/grid.cpp`

### 3a. `insert_chars(uint32_t n)`

Algorithm:

1. Guard: if `cols_ == 0 || rows_ == 0 || n == 0` → return.
2. Clamp `n` to remaining columns: `uint32_t space = cols_ - cursor_col_; if (n > space) n = space;`
3. Locate the row: compute `phys = physical_index(scrollback_count_ + cursor_row_)`, offset = `phys * cols_`.
4. Shift cells **right-to-left** to avoid overwriting: for `c = cols_ - 1` down to `cursor_col_ + n`, copy `cells_[offset + c - n]` → `cells_[offset + c]`.
5. Fill the `n` positions `[cursor_col_, cursor_col_ + n - 1]` with `grid_cell{}`.
6. Cursor is **not** moved.

### 3b. `delete_chars(uint32_t n)`

Algorithm:

1. Guard: if `cols_ == 0 || rows_ == 0 || n == 0` → return.
2. Clamp `n` to remaining columns: `uint32_t space = cols_ - cursor_col_; if (n > space) n = space;`
3. Locate the row (same as above).
4. Shift cells **left-to-right**: for `c = cursor_col_; c + n < cols_; ++c`, copy `cells_[offset + c + n]` → `cells_[offset + c]`.
5. Fill the `n` positions `[cols_ - n, cols_ - 1]` with `grid_cell{}`.
6. Cursor is **not** moved.

### 3c. `erase_chars(uint32_t n)`

Algorithm:

1. Guard: if `cols_ == 0 || rows_ == 0 || n == 0` → return.
2. Clamp `n` to remaining columns: `uint32_t space = cols_ - cursor_col_; if (n > space) n = space;`
3. Locate the row.
4. Fill `n` positions `[cursor_col_, cursor_col_ + n - 1]` with `grid_cell{}`.
5. Cursor is **not** moved.

### 3d. Wire into `apply()`

In `terminal_grid::apply()`, add three cases to the `switch (a.type)`:

```cpp
case action_type::insert_chars:
    insert_chars(a.count);
    break;
case action_type::delete_chars:
    delete_chars(a.count);
    break;
case action_type::erase_chars:
    erase_chars(a.count);
    break;
```

---

## Step 4 — Tests

### 4a. Parser tests (`tests/vt_parser_test.cpp`)

Add test cases:

- `CSI 3 @` → action `{ insert_chars, count=3 }`
- `CSI @` (no param) → action `{ insert_chars, count=1 }`
- `CSI 0 @` → action `{ insert_chars, count=1 }` (0 treated as 1)
- `CSI 2 P` → action `{ delete_chars, count=2 }`
- `CSI P` (no param) → action `{ delete_chars, count=1 }`
- `CSI 5 X` → action `{ erase_chars, count=5 }`
- `CSI X` (no param) → action `{ erase_chars, count=1 }`

Use existing `parse_sequence()` helper.

### 4b. Grid tests (`tests/grid_test.cpp`)

Add test cases following the IL/DL test patterns:

**ICH:**
- `Grid — ICH inserts blanks and shifts cells right` — fill a row with "ABCDE", move cursor to col 1, call `insert_chars(2)`, verify row = "A  BC" (A, space, space, B, C), D and E are lost. Cursor unchanged.
- `Grid — ICH count clamped to remaining columns` — cursor at col 3 in 5-col grid, `insert_chars(10)`, verify only 2 blanks inserted, everything past col 2 is blank.
- `Grid — ICH at last column is no-op` — cursor at col 4 in 5-col grid, `insert_chars(1)`, row unchanged.
- `Grid — ICH on zero-size grid does nothing` — `terminal_grid g(0,0)`, `g.insert_chars(1)` — no crash.
- `Grid — ICH cursor unchanged` — verify cursor_col and cursor_row are same before/after.

**DCH:**
- `Grid — DCH deletes cells and shifts left` — fill "ABCDE", cursor at col 1, `delete_chars(2)`, verify row = "A DE  " (A, space, D, E, space), cursor unchanged.
- `Grid — DCH count clamped` — cursor at col 3, `delete_chars(10)`, verify everything from col 3 onward is blank.
- `Grid — DCH at last column is no-op` — cursor at col 4, `delete_chars(1)`, row unchanged.
- `Grid — DCH cursor unchanged` — verify cursor_col and cursor_row are same before/after.

**ECH:**
- `Grid — ECH blanks cells in place` — fill "ABCDE", cursor at col 1, `erase_chars(3)`, verify row = "A   E" (A, space, space, space, E), no shifting.
- `Grid — ECH count clamped` — cursor at col 3, `erase_chars(10)`, verify everything from col 3 onward is blank.
- `Grid — ECH cursor unchanged` — verify cursor_col and cursor_row are same before/after.

---

## Files Changed

| File | Change |
|---|---|
| `src/terminal/vt_parser.hpp` | Add 3 `action_type` enum values |
| `src/terminal/vt_parser.cpp` | Add 3 CSI dispatch handlers (`@`, `P`, `X`) |
| `src/terminal/grid.hpp` | Declare `insert_chars`, `delete_chars`, `erase_chars` |
| `src/terminal/grid.cpp` | Implement 3 methods + wire into `apply()` |
| `tests/vt_parser_test.cpp` | Add parser tests |
| `tests/grid_test.cpp` | Add grid tests |

---

## Edge Cases Covered

| Edge case | Behavior |
|---|---|
| `n == 0` or missing param | Defaults to 1 (parser handles it) |
| `n > cols_ - cursor_col_` | Clamped to available space |
| Cursor at last column | No-op (available space = 0) |
| Zero-size grid (`cols_ == 0` or `rows_ == 0`) | Early return, no crash |
| Scroll regions / DECSTBM | Not applicable — ICH/DCH operate on current row only, and betty has no horizontal margins |
| Wide characters (task 15) | Not handled yet — cells are treated individually; task 15 will add awareness |
