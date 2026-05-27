# C1 — Refactor: scrollback lines as runtime parameter

## Goal

Remove the compile-time constant `k_scrollback_max` from `terminal_grid` and thread a runtime `scrollback_max_lines` value from `make_application()` through `terminal_session` into `terminal_grid`. The `scrollback_buffer` already accepts `max_scrollback` as a constructor/resize parameter, so the buffer side needs no changes.

**User sees:** No change. betty still launches and scrolls as before.

---

## Design Decisions (from interview)

1. **Scope: exact task spec (Option A).** Store the value on `terminal_grid` only; don't thread into `application` yet. C6 will wire differently if needed.

2. **Constructor parameter order: append.** `terminal_grid(uint32_t cols, uint32_t rows, uint32_t scrollback_max_lines)` — consistent with `scrollback_buffer(cols, rows, max_scrollback)`.

3. **`resize` signature unchanged.** `terminal_grid::resize(cols, rows)` reads the stored member internally. No new parameter.

4. **Session constructor: dimensions grouped.** `terminal_session(cols, rows, scrollback_max_lines, shell)`.

5. **Tests: file-local constexpr.** Each test file gets a `constexpr uint32_t k_test_scrollback = 10000;` at the top. Every `terminal_grid(...)` call passes it explicitly. No defaults, no helpers.

6. **Naming: `scrollback_max_lines`.** The grid stores `scrollback_max_lines_`, constructor parameter is `scrollback_max_lines`, session forwards as `scrollback_max_lines`. Doesn't rename the buffer's internal `max_scrollback_`.

7. **Hardcoded value placement: local `constexpr` in `make_application()`.** A `constexpr uint32_t k_default_scrollback = 10000;` inside the function, passed when constructing `terminal_session`.

---

## Files Changed

| File | Change |
|---|---|
| `src/terminal/grid.hpp` | Remove `k_scrollback_max`. Add `scrollback_max_lines_` member. Update constructor signature. |
| `src/terminal/grid.cpp` | Update constructor to store `scrollback_max_lines_`. Update `resize()` to use stored member instead of `k_scrollback_max`. |
| `src/terminal/session.hpp` | Update constructor signature to accept `scrollback_max_lines`. |
| `src/terminal/session.cpp` | Forward `scrollback_max_lines` to `grid_` constructor. |
| `src/application.cpp` | Add local `constexpr` in `make_application()`, pass to `terminal_session` constructor. |
| `tests/grid_test.cpp` | Add `constexpr uint32_t k_test_scrollback = 10000;`. Pass to every `terminal_grid(...)` call. |
| `tests/scrollback_buffer_test.cpp` | No changes needed (buffer already takes runtime `max_scrollback`). |

---

## Step-by-step Implementation

### Step 1: `grid.hpp` — remove constant, add member, update constructor

```cpp
// BEFORE
class terminal_grid {
public:
  terminal_grid(uint32_t cols, uint32_t rows);
  // ...
  static constexpr uint32_t k_scrollback_max = 10000;
  // ...
};

// AFTER
class terminal_grid {
public:
  terminal_grid(uint32_t cols, uint32_t rows, uint32_t scrollback_max_lines);
  // ... (k_scrollback_max removed entirely)
private:
  uint32_t scrollback_max_lines_;
  // ...
};
```

### Step 2: `grid.cpp` — update constructor and resize

```cpp
// Constructor — BEFORE
terminal_grid::terminal_grid(uint32_t cols, uint32_t rows)
  : cols_(cols)
  , rows_(rows)
  , buffer_(cols, rows, k_scrollback_max)
  , cursor_() { ... }

// Constructor — AFTER
terminal_grid::terminal_grid(uint32_t cols, uint32_t rows, uint32_t scrollback_max_lines)
  : cols_(cols)
  , rows_(rows)
  , scrollback_max_lines_(scrollback_max_lines)
  , buffer_(cols, rows, scrollback_max_lines)
  , cursor_() { ... }

// resize — BEFORE
void terminal_grid::resize(uint32_t new_cols, uint32_t new_rows) {
  // ...
  buffer_.resize(new_cols, new_rows, k_scrollback_max);
  // ...
}

// resize — AFTER
void terminal_grid::resize(uint32_t new_cols, uint32_t new_rows) {
  // ...
  buffer_.resize(new_cols, new_rows, scrollback_max_lines_);
  // ...
}
```

### Step 3: `session.hpp` — update constructor signature

```cpp
// BEFORE
terminal_session(uint32_t cols, uint32_t rows,
                 std::optional<platform::shell> shell);

// AFTER
terminal_session(uint32_t cols, uint32_t rows,
                 uint32_t scrollback_max_lines,
                 std::optional<platform::shell> shell);
```

### Step 4: `session.cpp` — forward new parameter

```cpp
// BEFORE
terminal_session::terminal_session(uint32_t cols, uint32_t rows,
                                   std::optional<platform::shell> shell)
    : grid_(cols, rows)
    , shell_(std::move(shell))
    , input_() { ... }

// AFTER
terminal_session::terminal_session(uint32_t cols, uint32_t rows,
                                   uint32_t scrollback_max_lines,
                                   std::optional<platform::shell> shell)
    : grid_(cols, rows, scrollback_max_lines)
    , shell_(std::move(shell))
    , input_() { ... }
```

### Step 5: `application.cpp` — pass hardcoded value

```cpp
auto make_application() -> std::expected<application, std::error_code> {
  // ... (existing window, renderer, shell setup) ...

  constexpr uint32_t k_default_scrollback = 10000;

  terminal::terminal_session session(cols, rows, k_default_scrollback, std::move(shell));

  return application{std::move(window), std::move(renderer_ctx), std::move(session)};
}
```

### Step 6: `tests/grid_test.cpp` — add constexpr and update all grid constructions

- Add `constexpr uint32_t k_test_scrollback = 10000;` near the top, after the `using` declaration.
- Replace every `terminal_grid(N, M)` call with `terminal_grid(N, M, k_test_scrollback)`. This affects ~40+ test cases.

### Step 7: Verify `tests/scrollback_buffer_test.cpp` needs no changes

The buffer already takes `max_scrollback` in its constructor — e.g., `scrollback_buffer buf(80, 24, 10000)`. No changes needed.

---

## Build and Test

```bash
cmake --build build/debug && ctest --test-dir build/debug --output-on-failure
```

All existing tests must pass. No new tests added (C1 is pure refactor).

---

## Acceptance

- `k_scrollback_max` no longer exists anywhere in the codebase.
- `terminal_grid` constructor requires `scrollback_max_lines`.
- `terminal_grid::resize` uses the stored runtime value.
- `terminal_session` accepts and forwards `scrollback_max_lines`.
- `make_application` passes `10000` via a local `constexpr`.
- All existing tests pass.
- betty launches and scrollback behavior is identical to before.
