# Task 5 — Cursor Movement Sequences

## Goal

After this task, the shell prompt appears at the correct position. Cursor-aware terminal apps position text correctly. Typing `clear` moves the cursor but doesn't yet erase anything (erase happens in Task 8).

## What the user sees

- PowerShell prompt renders at column 0, advancing correctly as the user types.
- Arrow-key escape sequences from the shell (e.g. `←` in line editing) move the cursor.
- `clear` moves the cursor to row 0, col 0 (no visible erase yet — cells remain).
- Commands like `Write-Host "hello" -NoNewline` display text at the cursor position without spurious newlines.

## Sequences to implement

| Sequence | Name | Action |
|----------|------|--------|
| `CSI Pn A` | CUU — Cursor Up | Move cursor up N cells (default 1). Stop at row 0. |
| `CSI Pn B` | CUD — Cursor Down | Move cursor down N cells (default 1). Stop at bottom row. |
| `CSI Pn C` | CUF — Cursor Forward | Move cursor right N cells (default 1). Stop at rightmost column. |
| `CSI Pn D` | CUB — Cursor Back | Move cursor left N cells (default 1). Stop at column 0. |
| `CSI Pn ; Pn H` | CUP — Cursor Position | Move cursor to row N, column M (1-based, default 1). |
| `CSI Pn ; Pn f` | HVP — Horizontal Vertical Position | Identical to CUP. |
| `ESC 7` | DECSC — Save Cursor | Save current cursor position. |
| `ESC 8` | DECRC — Restore Cursor | Restore previously saved cursor position. |

All sequences use **1-based** coordinates in the escape sequence. The parser converts to 0-based before emitting an action.

Missing/default parameters are treated as 1.

---

## Design

### Architecture

```
shell output (raw bytes)
        │
        ▼
  terminal_grid::write_bytes()
        │
        ▼
  vt_parser::parse(byte)  ──► std::optional<action>
        │
        ▼
  terminal_grid::apply(action)
```

- `write_bytes` stays as the single entry point called from `main.cpp`.
- `write_bytes` internally feeds bytes one-by-one into `vt_parser::parse()`.
- Each complete sequence produces an `action`; `write_bytes` applies it to the grid.
- The parser owns the saved-cursor state (for DECSC/DECRC).

### Action enum (`src/terminal/vt_parser.hpp`)

```cpp
enum class action_type : uint8_t {
  write_char,
  carriage_return,
  newline,
  move_cursor,       // absolute row, col (0-based)
  move_cursor_up,    // N cells
  move_cursor_down,
  move_cursor_forward,
  move_cursor_back,
};

struct action {
  action_type type;
  union {
    char32_t codepoint;       // for write_char
    uint32_t count;           // for relative moves (up/down/forward/back)
    struct {                  // for move_cursor (absolute)
      uint32_t row;
      uint32_t col;
    } cursor;
  };
};
```

No `save_cursor` / `restore_cursor` actions exist — the parser handles those internally and emits a `move_cursor` for DECRC.

### VT Parser (`src/terminal/vt_parser.hpp` + `.cpp`)

Minimal state machine with three states:

```
GROUND  ──ESC(0x1B)──► ESCAPE
ESCAPE  ──'['───────► CSI_ENTRY
ESCAPE  ──'7'───────► emit nothing, save cursor internally, → GROUND
ESCAPE  ──'8'───────► emit move_cursor(saved_row, saved_col), → GROUND
ESCAPE  ──other─────► → GROUND (ignore unsupported)

CSI_ENTRY ──0x30-0x3F─► CSI_PARAM   (parameter bytes: digits, ';')
CSI_ENTRY ──0x20-0x2F─► CSI_INTERMEDIATE
CSI_ENTRY ──0x40-0x7E─► CSI_FINAL (dispatch)

CSI_PARAM ──0x30-0x3F──► CSI_PARAM   (more parameter bytes)
CSI_PARAM ──0x20-0x2F──► CSI_INTERMEDIATE
CSI_PARAM ──0x40-0x7E──► CSI_FINAL (dispatch)

CSI_INTERMEDIATE ──0x20-0x2F──► CSI_INTERMEDIATE
CSI_INTERMEDIATE ──0x40-0x7E──► CSI_FINAL (dispatch)
```

On CSI_FINAL, dispatch the final byte:
- `A` → parse parameter → `move_cursor_up(count)`
- `B` → `move_cursor_down(count)`
- `C` → `move_cursor_forward(count)`
- `D` → `move_cursor_back(count)`
- `H` → parse two params → `move_cursor(row-1, col-1)`
- `f` → parse two params → `move_cursor(row-1, col-1)` (same as H)
- any other → ignore, return nullopt

The parser internally tracks the cursor position so it can save/restore:
- Initialized to `(0, 0)`.
- Updated every time a cursor-moving action is emitted.
- On `ESC 7`, saves current `(row, col)`.
- On `ESC 8`, emits `move_cursor` with the saved position.

Any invalid state transition (unexpected byte) returns to GROUND silently.

### Grid changes (`src/terminal/grid.hpp` + `.cpp`)

**New public method:**

```cpp
void apply(action const& a);
```

- `write_char` → calls existing `write_char(codepoint)` (advances cursor, wraps, scrolls).
- `carriage_return` → calls existing `carriage_return()`.
- `newline` → calls existing `newline()`.
- `move_cursor` → absolute position, **clamped** to valid range: `row = min(row, rows_-1)`, `col = min(col, cols_-1)`.
- `move_cursor_up/down/forward/back` → relative offset, clamped (stop at edges).

**`write_bytes` refactored:**

Rather than handling `\r` and `\n` itself, `write_bytes` now delegates to `vt_parser`:

```cpp
void write_bytes(std::string_view data) {
    for (auto b : data) {
        auto a = parser_.parse(static_cast<unsigned char>(b));
        if (a) apply(*a);
    }
}
```

**New private member:**

```cpp
vt_parser parser_;
```

The existing `write_char`, `newline`, `carriage_return`, and `scroll_up` methods remain unchanged — `apply()` calls them.

### No changes to `main.cpp`

The message-loop code continues to call `grid.write_bytes(raw)` exactly as before. The parser is an internal detail of `terminal_grid`.

---

## Implementation steps

### Step 1: Create the action enum and struct

**File:** `src/terminal/vt_parser.hpp`

- Define `action_type` enum with all 8 values.
- Define `action` struct with the tagged union.
- Provide a convenience factory: `static action make_write_char(char32_t)`, etc., to simplify construction.

### Step 2: Implement the VT parser

**File:** `src/terminal/vt_parser.hpp` (declaration) + `src/terminal/vt_parser.cpp` (implementation)

- `class vt_parser` with:
  - `std::optional<action> parse(unsigned char byte);`
  - Private: state enum, parameter buffer, saved cursor row/col, tracked cursor row/col.
- State machine as described above.
- Parameter parsing:
  - Accumulate digits and `;` separators in a small string buffer.
  - On dispatch: split by `;`, parse each segment as integer (default 1 if empty).
- On any unrecognized final byte, return to GROUND and produce no action.

### Step 3: Add `apply()` to `terminal_grid`

**File:** `src/terminal/grid.hpp` — declare `void apply(action const& a);`
**File:** `src/terminal/grid.cpp` — implement dispatch.

The existing private methods (`write_char`, `newline`, `carriage_return`) are reused directly.

### Step 4: Refactor `write_bytes` to use the parser

**File:** `src/terminal/grid.hpp` — add `vt_parser parser_;` private member.
**File:** `src/terminal/grid.cpp` — replace the body of `write_bytes` with the parser loop.

The old `switch (b)` block is removed — `\r`, `\n`, and printable ASCII are now handled by the parser which emits `carriage_return`, `newline`, and `write_char` actions respectively.

### Step 5: Update CMakeLists.txt

**File:** `src/CMakeLists.txt`

- Add `terminal/vt_parser.cpp` to the sources list.

### Step 6: Build and test

- `cmake --build build` — ensure clean compilation.
- Launch `betty.exe`:
  - PowerShell prompt appears at column 0.
  - Type characters — cursor advances.
  - Press Up arrow — shell history moves cursor.
  - Type `clear` — cursor jumps to (0, 0); text remains visible (erase is Task 8).
  - Type `Write-Host "hello" -NoNewline` — text appears at cursor without extra newline.
  - `exit` — window closes cleanly.

---

## Edge cases

| Case | Behavior |
|------|----------|
| CUP/HVP row or col = 0 | Treated as 1 (ANSI spec). Parser clamps to 1 minimum before converting to 0-based. |
| CUP row > grid rows | Clamped to last row. |
| CUP col > grid cols | Clamped to last column. |
| CUU with N > current row | Cursor stops at row 0. |
| CUD with N past bottom | Cursor stops at last row. |
| CUF with N past right edge | Cursor stops at last column. |
| CUB with N > current col | Cursor stops at column 0. |
| Missing parameter (e.g. `CSI ; H`) | Both treated as 1 → cursor moves to (0, 0). |
| DECRC without a prior DECSC | Restores to (0, 0) (parser initializes saved position to origin). |
| CSI sequence with invalid final byte | Entire sequence discarded, parser returns to GROUND. |
| `\r\n` embedded in shell output | Parser processes `\r` (carriage_return) then `\n` (newline) — same as before. |
| Parser state persists across `write_bytes` calls | The private `parser_` member lives for the lifetime of `terminal_grid`, so partial sequences across pipe reads are handled correctly. |

---

## Files changed

| File | Change |
|------|--------|
| `src/terminal/vt_parser.hpp` | **New.** Action enum + vt_parser class declaration. |
| `src/terminal/vt_parser.cpp` | **New.** vt_parser implementation (state machine). |
| `src/terminal/grid.hpp` | Add `apply()` declaration, add `parser_` member, include `vt_parser.hpp`. |
| `src/terminal/grid.cpp` | Implement `apply()`, refactor `write_bytes()` to delegate to parser. |
| `src/CMakeLists.txt` | Add `vt_parser.cpp`. |

No other files are changed.

---

## Future extensibility (not in scope)

- Task 6 (SGR colours) — extends the parser's CSI dispatch table; adds colour fields to `action` and `grid_cell`.
- Task 8 (erase) — adds `erase_display` / `erase_line` actions.
- Task 9 (window title) — adds OSC parsing and `set_title` action.
- Task 12 (text attributes) — extends `grid_cell` with bold/italic/underline flags; parser sets them via extended actions.
- Task 18 (resilience) — the parser already drops invalid sequences; extending it to handle UTF-8 partial sequences and other edge cases.
