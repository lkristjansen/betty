# Task 13 — Line Operations: Implementation Plan

## Overview

Implement line-oriented VT escape sequences (IL, DL, SU, SD) and scrolling regions (DECSTBM). These are used by terminal apps like `top`, `htop`, and progress bars to insert/delete/scroll lines within a defined region while keeping headers fixed.

## Design Decisions Summary

| Decision | Choice |
|----------|--------|
| DECSTBM (scroll regions) | Included — required for `top`/`htop` |
| `newline()` / `write_char()` auto-scroll | Modified to be scroll-region-aware |
| SD on full screen | Always insert blank rows at top (no scrollback pull) |
| SU on full screen | Push scrolled-off rows into scrollback (matches existing `scroll_up()`) |
| IL/DL cursor column | Reset to 0 per VT100 spec |
| IL/DL outside scroll region | Ignored (cursor above `scroll_top_` or below `scroll_bottom_`) |
| DECSTBM: top ≥ bottom | Ignored per VT100 spec |
| DECSTBM: `0;0` params | Reset to full screen |

---

## Step 1 — Add new action types to `vt_parser.hpp`

Add five new entries to `action_type` enum:

```cpp
insert_lines,       // IL  — CSI Ps L (Ps lines, default 1)
delete_lines,       // DL  — CSI Ps M
scroll_up_page,     // SU  — CSI Ps S
scroll_down_page,   // SD  — CSI Ps T
set_scroll_region,  // DECSTBM — CSI Ps ; Ps r
```

The `action` struct already has `count` (for IL/DL/SU/SD line counts) and `row`/`col` (for DECSTBM top/bottom margins, 1-based).

---

## Step 2 — Parse new CSI sequences in `vt_parser.cpp`

Add these final-byte cases in `dispatch()`, between the ED/EL block and the cursor-movement switch:

| Final byte | Sequence | Logic |
|-----------|----------|-------|
| `L` | IL — Insert Lines | `split_params`, count = max(params[0], 1), emit `insert_lines` |
| `M` | DL — Delete Lines | Same, emit `delete_lines` |
| `S` | SU — Scroll Up | Same, emit `scroll_up_page` |
| `T` | SD — Scroll Down | Same, emit `scroll_down_page` |
| `r` | DECSTBM | `split_params`, top = param[0] (0 → 1), bottom = param[1] (0 → 0 = "full screen"), emit `set_scroll_region` |

All five must also work with intermediate bytes (e.g. `CSI ? r`), which the existing `csi_intermediate` state already handles — the final byte dispatch doesn't need to change.

---

## Step 3 — Add scroll region state to `terminal_grid`

Add two private members initialized in the constructor:

```cpp
uint32_t scroll_top_ = 0;       // 0-based, inclusive
uint32_t scroll_bottom_ = 0;    // 0-based, inclusive
```

Initialize `scroll_bottom_` to `rows_ - 1` in the constructor.  
On `resize()`, clamp `scroll_bottom_` to `max(scroll_bottom_, rows_ - 1)` and clamp `scroll_top_` to 0.

---

## Step 4 — Implement `set_scroll_region()` method

```cpp
// top, bottom are 1-based (as received from DECSTBM)
void set_scroll_region(uint32_t top, uint32_t bottom);
```

Validation:
- If `top < 1`: top = 1
- If `bottom < 1` or `bottom > rows_`: bottom = rows_ (full screen reset)
- If `top > bottom`: ignore (no change)
- Convert to 0-based: `scroll_top_ = top - 1`, `scroll_bottom_ = bottom - 1`
- Clamp to grid bounds
- Move cursor to row 0, column 0 (home position per VT100 spec)

---

## Step 5 — Modify `scroll_up()` to be scroll-region-aware

Current behavior: scrolls the entire visible grid, pushing the top visible row into scrollback.

New behavior:
- Only scrolls rows `[scroll_top_ .. scroll_bottom_]`
- Rows above `scroll_top_` are untouched
- Rows within the region shift up by 1
- A blank row is inserted at `scroll_bottom_`
- **Scrollback interaction**: only push to scrollback if `scroll_top_ == 0` (the row being scrolled off is at the very top of the visible area). If `scroll_top_ > 0`, the scrolled-off row is simply lost.

Implementation: instead of the current full-screen shift, do a targeted row-shift loop:

```
// For full-screen case (scroll_top_ == 0): use existing scrollback push logic.
// Then clear the row at scroll_bottom_.
// For sub-region (scroll_top_ > 0): copy rows without scrollback interaction,
// then clear scroll_bottom_.
```

---

## Step 6 — Modify `newline()` and `write_char()` auto-scroll

**`newline()`:**
- Cursor moves to column 0.
- If cursor was at `scroll_bottom_`, call `scroll_up()` (now region-aware) and leave cursor at `scroll_bottom_`.
- Otherwise, increment `cursor_row_`.

**`write_char()` auto-wrap:**
- When `cursor_col_ >= cols_`: reset to col 0, increment row.
- If `cursor_row_ > scroll_bottom_`: call `scroll_up()`, clamp cursor to `scroll_bottom_`.

Currently `write_char` checks `cursor_row_ >= rows_` before scrolling — change to `> scroll_bottom_`.

---

## Step 7 — Implement `insert_lines(uint32_t n)`

```cpp
void insert_lines(uint32_t n);
```

- If cursor is outside scroll region (`cursor_row_ < scroll_top_` or `cursor_row_ > scroll_bottom_`): **do nothing**.
- Clamp `n` to `scroll_bottom_ - cursor_row_ + 1` (can't insert more than available space in region).
- Shift rows `[cursor_row_ .. scroll_bottom_ - n]` down by `n` positions (copy row-by-row, working from bottom up).
- Fill rows `[cursor_row_ .. cursor_row_ + n - 1]` with blank cells.
- Set `cursor_col_ = 0`.

---

## Step 8 — Implement `delete_lines(uint32_t n)`

```cpp
void delete_lines(uint32_t n);
```

- If cursor is outside scroll region: **do nothing**.
- Clamp `n` to `scroll_bottom_ - cursor_row_ + 1`.
- Shift rows `[cursor_row_ + n .. scroll_bottom_]` up by `n` positions (copy row-by-row, working from top down).
- Fill rows `[scroll_bottom_ - n + 1 .. scroll_bottom_]` with blank cells.
- Set `cursor_col_ = 0`.

---

## Step 9 — Implement `scroll_page_up(uint32_t n)`

```cpp
void scroll_page_up(uint32_t n);
```

- Clamp `n` to `scroll_bottom_ - scroll_top_ + 1`.
- If the region is the full screen (`scroll_top_ == 0` and `scroll_bottom_ == rows_ - 1`):
  - Call `scroll_up()` `n` times (pushes to scrollback).
- Otherwise (sub-region scroll):
  - Shift rows `[scroll_top_ + n .. scroll_bottom_]` up by `n`.
  - Fill rows `[scroll_bottom_ - n + 1 .. scroll_bottom_]` with blanks.
- Cursor unchanged.

---

## Step 10 — Implement `scroll_page_down(uint32_t n)`

```cpp
void scroll_page_down(uint32_t n);
```

- Clamp `n` to `scroll_bottom_ - scroll_top_ + 1`.
- Shift rows `[scroll_top_ .. scroll_bottom_ - n]` down by `n` (working bottom-up to avoid overwrites).
- Fill rows `[scroll_top_ .. scroll_top_ + n - 1]` with blank cells.
- **No scrollback interaction** (design decision B).
- Cursor unchanged.

---

## Step 11 — Wire up actions in `terminal_grid::apply()`

Add cases to the `switch (a.type)`:

```cpp
case action_type::set_scroll_region:
  set_scroll_region(a.row, a.col);
  break;
case action_type::insert_lines:
  insert_lines(a.count);
  break;
case action_type::delete_lines:
  delete_lines(a.count);
  break;
case action_type::scroll_up_page:
  scroll_page_up(a.count);
  break;
case action_type::scroll_down_page:
  scroll_page_down(a.count);
  break;
```

---

## Step 12 — Add tests

### VT parser tests (`tests/vt_parser_test.cpp`)

| Test | Sequence | Expected |
|------|----------|----------|
| IL — no param defaults to 1 | `ESC [ L` | `insert_lines`, count=1 |
| IL — explicit count | `ESC [ 5 L` | `insert_lines`, count=5 |
| IL — zero param → 1 | `ESC [ 0 L` | `insert_lines`, count=1 |
| DL — no param | `ESC [ M` | `delete_lines`, count=1 |
| DL — explicit count | `ESC [ 3 M` | `delete_lines`, count=3 |
| SU — no param | `ESC [ S` | `scroll_up_page`, count=1 |
| SU — explicit count | `ESC [ 4 S` | `scroll_up_page`, count=4 |
| SD — no param | `ESC [ T` | `scroll_down_page`, count=1 |
| SD — explicit count | `ESC [ 2 T` | `scroll_down_page`, count=2 |
| DECSTBM — set region | `ESC [ 3 ; 10 r` | `set_scroll_region`, row=3, col=10 |
| DECSTBM — reset (0;0) | `ESC [ 0 ; 0 r` | `set_scroll_region`, row=1, col=0 |
| DECSTBM — empty params | `ESC [ r` | `set_scroll_region`, row=1, col=0 |
| IL/DL/SU/SD with intermediate byte | `ESC [ ? L` | works correctly |
| Parser recovery after all new sequences | feed then check `'A'` produces `write_char` |

### Grid tests (`tests/grid_test.cpp`)

| Test | What it verifies |
|------|-----------------|
| Default scroll region is full screen | `scroll_top_ == 0`, `scroll_bottom_ == rows_ - 1` |
| `set_scroll_region` with valid params | region set, cursor home (0,0) |
| `set_scroll_region` with top > bottom | ignored, region unchanged |
| `set_scroll_region` with top ≥ rows | clamped |
| `set_scroll_region` with bottom > rows | clamped |
| `set_scroll_region` with `0;0` | resets to full screen |
| `newline` at bottom margin scrolls region | header row above scroll_top_ unaffected |
| `newline` above bottom margin just advances | no scroll |
| `write_char` auto-wrap respects region | same as newline |
| IL — inserts blank lines at cursor within region | row above region untouched |
| IL — cursor outside region (above) | ignored |
| IL — cursor outside region (below) | ignored |
| IL — cursor col resets to 0 | after insert |
| IL — count clamped to region boundary | no shift past scroll_bottom_ |
| DL — deletes lines at cursor within region | lines below shift up |
| DL — cursor outside region | ignored |
| DL — cursor col resets to 0 | after delete |
| SU — full screen pushes to scrollback | scrollback_count increases |
| SU — sub-region no scrollback interaction | scrollback unchanged, only region scrolls |
| SD — full screen inserts blanks at top | no scrollback interaction |
| SD — sub-region scrolls within region | rows above region untouched |
| IL/DL/SU/SD on zero-size grid | no crash |
| Scroll region preserved across resize | region clamped to new dimensions |
| Integration: `write_bytes` with DECSTBM + newline | header stays fixed |

---

## Step 13 — Build and manual smoke test

- Build `betty.exe`, launch it.
- Run `top` → header should stay fixed, data area should scroll.
- Run `htop` → same behavior.
- Run a script that outputs progress bars (e.g. `for ($i=0; $i -le 100; $i++) { Write-Progress -Activity "Processing" -PercentComplete $i; Start-Sleep -Milliseconds 50 }`).
- Scroll region boundary cases do not crash.
- Existing functionality (text, colours, cursor, resize, scrollback) still works.

---

## Summary of Code Changes

| File | Changes |
|------|---------|
| `src/terminal/vt_parser.hpp` | Add 5 `action_type` enum values |
| `src/terminal/vt_parser.cpp` | Add `'L'`, `'M'`, `'S'`, `'T'`, `'r'` dispatch cases |
| `src/terminal/grid.hpp` | Add `scroll_top_`, `scroll_bottom_` members; declare 5 new methods |
| `src/terminal/grid.cpp` | Implement `set_scroll_region`, `insert_lines`, `delete_lines`, `scroll_page_up`, `scroll_page_down`; modify `scroll_up`, `newline`, `write_char`; update `apply()`, constructor, `resize()` |
| `tests/vt_parser_test.cpp` | ~12 new parser tests |
| `tests/grid_test.cpp` | ~22 new grid tests |
