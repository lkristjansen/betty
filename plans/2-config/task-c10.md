# C10 — Hot-reload: error recovery

## Goal

Handle all edge cases when `config.toml` is modified, deleted, or corrupted while betty is running:
- **Corrupted file** → message box + revert all settings to built-in defaults.
- **File deleted** → silent revert to built-in defaults (no message box).
- **File restored** → re-apply settings automatically.

**User sees:** Robust error recovery around hot-reload. The terminal never crashes or gets stuck in a bad state due to a malformed config.

---

## Design Decisions (from interview)

1. **Watcher detects deletions.** Small change to the poll loop: when `config.toml` exists at one poll and is absent at the next, set the flag. Then `on_config_changed` handles it.

2. **Single handler replaces C9.** `on_config_changed` handles all cases: missing file, parse error, validation error, valid file. C9's logic is absorbed into the new handler.

---

## Implementation

### Step 1: Add deletion detection to `config_watcher`

**`config_watcher.cpp`** — in the poll loop, after checking modification:

```cpp
// After the existing modification check:
if (exists && current_write != last_write) {
    changed->store(true, std::memory_order_release);
    last_write = current_write;
}

// Also detect deletion (file existed before, now gone).
bool const existed_before = (last_write != std::filesystem::file_time_type{});
if (!exists && existed_before) {
    changed->store(true, std::memory_order_release);
    last_write = std::filesystem::file_time_type{};
    continue;
}
```

This fires the flag when the file is deleted. The "restoration" case (file reappears) is already handled by the existing `exists && current_write != last_write` check since `last_write` was reset to the zero value on deletion.

### Step 2: Replace `on_config_changed` in `application.cpp`

The new handler replaces the C9 temporary:

```cpp
void application::on_config_changed() {
  auto const config_path = config_dir_ / "config.toml";

  // --- File deleted ---
  if (!std::filesystem::exists(config_path)) {
    // Silent revert to built-in defaults.
    betty_config const defaults{};
    apply_config(defaults);
    config_ = defaults;
    return;
  }

  // --- File exists — try to parse ---
  auto result = parse_config(config_dir_);

  // --- Parse error (TOML syntax) ---
  if (!result.errors.empty()) {
    bool const is_syntax = !result.errors.empty() && result.errors[0].key == "(syntax)";
    if (is_syntax) {
      betty::platform::show_error_message(
          "betty - Configuration Error",
          result.errors[0].message);
    } else {
      auto msg = format_validation_errors(result.errors);
      betty::platform::show_error_message("betty - Configuration Error", msg);
    }

    // Revert ALL settings to built-in defaults.
    betty_config const defaults{};
    apply_config(defaults);
    config_ = defaults;
    return;
  }

  // --- Valid config — apply changed fields (C9 logic) ---
  auto const& new_cfg = result.config;

  if (new_cfg.font_family != config_.font_family ||
      new_cfg.font_size != config_.font_size) {
    auto const family = *new_cfg.font_family;
    auto const size = *new_cfg.font_size;
    if (auto res = renderer_ctx_.recreate_font(family, size); !res) {
      util::log_error(res.error(), "hot-reload: recreate font");
      return;
    }
    config_.font_family = family;
    config_.font_size = size;
  }

  if (new_cfg.cursor_style != config_.cursor_style) {
    config_.cursor_style = new_cfg.cursor_style;
  }

  if (new_cfg.scrollback_lines != config_.scrollback_lines) {
    session_.resize_scrollback(*new_cfg.scrollback_lines);
    config_.scrollback_lines = new_cfg.scrollback_lines;
  }

  bool const cols_changed = (new_cfg.columns != config_.columns);
  bool const rows_changed = (new_cfg.rows != config_.rows);
  if (cols_changed || rows_changed) {
    uint32_t const new_cols = cols_changed ? *new_cfg.columns : *config_.columns;
    uint32_t const new_rows = rows_changed ? *new_cfg.rows : *config_.rows;

    uint32_t const cell_w = renderer_ctx_.cell_width();
    uint32_t const cell_h = renderer_ctx_.cell_height();
    uint32_t const pad = platform::k_padding_px;
    auto const exact_size = compute_window_size(new_cols, new_rows, cell_w, cell_h, pad);

    window_.resize_client_area(exact_size.width, exact_size.height);
    if (auto res = renderer_ctx_.handle_resize(exact_size.width, exact_size.height); !res) {
      util::log_error(res.error(), "hot-reload: resize renderer");
      return;
    }
    session_.resize(new_cols, new_rows);

    config_.columns = new_cols;
    config_.rows = new_rows;
  }

  // shell: silently ignored (requires restart).
}
```

### Step 3: Extract `apply_config()` helper

To avoid duplicating the "set everything to defaults" logic, extract the field-application code into a private helper that takes a full config and applies every field unconditionally (used for revert). The C9 incremental logic stays separate.

```cpp
// Apply ALL settings from `cfg` unconditionally — used for reverting to defaults.
void application::apply_config_all(betty_config const& cfg) {
  if (auto res = renderer_ctx_.recreate_font(*cfg.font_family, *cfg.font_size); !res) {
    util::log_error(res.error(), "hot-reload: recreate font");
  }
  session_.resize_scrollback(*cfg.scrollback_lines);

  uint32_t const cell_w = renderer_ctx_.cell_width();
  uint32_t const cell_h = renderer_ctx_.cell_height();
  uint32_t const pad = platform::k_padding_px;
  auto const exact_size = compute_window_size(*cfg.columns, *cfg.rows, cell_w, cell_h, pad);

  window_.resize_client_area(exact_size.width, exact_size.height);
  if (auto res = renderer_ctx_.handle_resize(exact_size.width, exact_size.height); !res) {
    util::log_error(res.error(), "hot-reload: resize renderer");
  }
  session_.resize(*cfg.columns, *cfg.rows);
}
```

Then the revert path is:
```cpp
betty_config const defaults{};
apply_config_all(defaults);
config_ = defaults;
```

### Step 4: Update `application.hpp`

Add `apply_config_all` declaration:
```cpp
void apply_config_all(betty_config const& cfg);
```

---

## Files Changed

| File | Change |
|---|---|
| `src/config/config_watcher.cpp` | Add deletion detection to poll loop. |
| `src/application.hpp` | Add `apply_config_all()` private method. |
| `src/application.cpp` | Replace `on_config_changed` with full error-handling version. Add `apply_config_all` for default reversion. |

---

## Build and Test

```bash
cmake --build build/debug && ctest --test-dir build/debug --output-on-failure
```

Manual verification:
1. Launch betty with a valid `config.toml`
2. **Corrupt it:** add `=== invalid ===` → save → message box appears, all settings revert to defaults (font, grid size, scrollback, cursor)
3. **Fix it:** restore valid content → save → settings re-apply without restart
4. **Delete it:** remove `config.toml` → settings silently revert to defaults (no message box)
5. **Restore it:** recreate `config.toml` with valid content → settings re-apply

---

## Acceptance

- Corrupted `config.toml` (TOML syntax error) → message box + all settings revert to hardcoded defaults.
- `config.toml` has invalid values (e.g., `cursor_style = "beam"`) → message box listing errors + all settings revert to defaults.
- File deleted → silent revert to defaults (no message box).
- File restored to valid state → settings re-applied automatically.
- Hot-reload of individual fields still works (C9 behavior preserved).
- All existing tests pass.
