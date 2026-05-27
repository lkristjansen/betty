# C7 — Support cursor_style = "none"

## Goal

When `cursor_style` is `"none"` in the config, suppress the cursor (pass `std::nullopt` to `draw_grid`). When `"block"` (or default), use the existing scrollback-aware cursor logic.

**User sees:** Setting `cursor_style = "none"` in `config.toml` hides the cursor. `cursor_style = "block"` shows it as before.

---

## Design Decisions (from interview)

1. **Inline check in `run()`.** A small lambda wrapped around the existing ternary. No new methods, no architectural changes. `config_` is read every frame, so hot-reload of cursor_style (C9) works automatically.

---

## Implementation

### Single change in `application.cpp` — the `run()` method

Replace the existing cursor computation (lines 118–121):

```cpp
// BEFORE:
auto const cursor = session_.is_following_output()
    ? std::optional<platform::point2d>{}
    : std::optional<platform::point2d>{{session_.cursor_row(), session_.cursor_col()}};

// AFTER:
auto const cursor = [&]() -> std::optional<platform::point2d> {
    if (config_.cursor_style == "none") return std::nullopt;
    if (!session_.is_following_output()) return std::nullopt;
    return platform::point2d{session_.cursor_row(), session_.cursor_col()};
}();
```

The lambda captures by reference — `config_` and `session_` are both `[&]` accessible from the enclosing `application::run()` method.

---

## Files Changed

| File | Change |
|---|---|
| `src/application.cpp` | Wrap cursor computation with `config_.cursor_style == "none"` check. |

That's it — one file, a few lines changed.

---

## Build and Test

```bash
cmake --build build/debug && ctest --test-dir build/debug --output-on-failure
```

No new automated tests — cursor rendering is visual. Verify manually by launching betty with `cursor_style = "none"` and `cursor_style = "block"` in `config.toml`.

---

## Acceptance

- `cursor_style = "none"` in `config.toml` → no visible cursor.
- `cursor_style = "block"` (or omitted) → block cursor visible at the prompt position.
- Scrollback behavior unchanged: cursor still hidden when scrolled back, shown when following output.
- No regression in any existing behavior.
