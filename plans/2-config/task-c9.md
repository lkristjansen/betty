# C9 — Hot-reload: apply config changes at runtime

## Goal

When the file watcher detects a `config.toml` change, re-parse it and apply changed settings immediately where possible. `shell` changes are silently ignored (restart required). This is the happy-path implementation — C10 adds error recovery.

**User sees:** Editing `font_family`, `font_size`, `cursor_style`, `scrollback_lines`, `columns`, or `rows` in `config.toml` while betty is running takes effect immediately. Editing `shell` has no effect until restart.

---

## Design Decisions (from interview)

1. **Granular field-by-field comparison.** Re-parse into a new `betty_config`, compare each field to `config_`, apply only changed fields. Avoids recreating the D3D pipeline for unrelated edits.

2. **`recreate_font()` on `renderer_context`.** New method that re-creates the internal `glyph_renderer` with new font family/size. Stores DPI from construction so it's available at reload time.

3. **`resize_scrollback()` on `terminal_session`.** Exposes the existing grid resize with new scrollback capacity but same columns/rows. Grid reflows content and adjusts capacity.

4. **Programmatic window resize for columns/rows.** Uses existing `resize_client_area()` + `handle_resize()` + `session.resize()` flow. The `on_resize` callback fires but is a no-op since dimensions match.

5. **`config_dir_` stored on `application`.** Both the watcher and the reload handler need it. `make_application` copies it.

6. **Best-effort, no error messaging.** If parse succeeds, apply valid fields (bad fields keep current runtime values). If parse fails entirely, do nothing. C10 adds message boxes and full default revert.

---

## Hot-Reload Per-Field Actions

| Field | Action on change |
|---|---|
| `font_family` | `renderer_ctx_.recreate_font(*new_cfg.font_family, *config_.font_size)` |
| `font_size` | `renderer_ctx_.recreate_font(*config_.font_family, *new_cfg.font_size)` |
| `cursor_style` | Just update `config_.cursor_style` — read every frame already |
| `scrollback_lines` | `session_.resize_scrollback(*new_cfg.scrollback_lines)` |
| `columns` | Resize window + renderer + session to new cols × current rows |
| `rows` | Resize window + renderer + session to current cols × new rows |
| `shell` | Silently ignored (requires restart) |

When both `columns` and `rows` change simultaneously, do a single combined resize.

---

## Files Changed

| File | Change |
|---|---|
| `src/platform/renderer_context.hpp` | Add `recreate_font()` method declaration. Store `dpi_` and `font_family_`. |
| `src/platform/renderer_context.cpp` | Implement `recreate_font()`. Store DPI from `make_renderer_context`. |
| `src/terminal/session.hpp` | Add `resize_scrollback()` method. |
| `src/terminal/session.cpp` | Implement `resize_scrollback()` via grid resize with same cols/rows. |
| `src/application.hpp` | Add `config_dir_` member. Add private `on_config_changed()` handler. |
| `src/application.cpp` | Fill in the `config_changed_` handler: re-parse, compare, apply. Store `config_dir_`. |

---

## Step-by-step Implementation

### Step 1: Add `recreate_font()` to `renderer_context`

**`renderer_context.hpp`** — add public method:
```cpp
// Recreate the glyph renderer with new font settings while keeping the
// D3D device and swap chain intact.
[[nodiscard]] auto recreate_font(std::string_view font_family,
                                  float font_size_pt)
    -> std::expected<void, std::error_code>;
```

Add private members to store font identity and DPI:
```cpp
private:
  // ... existing members ...
  uint32_t dpi_ = 96;                        // stored from construction
  std::string font_family_;                  // current font family
  float font_size_pt_ = 14.0f;               // current font size in pt
```

**`renderer_context.cpp`** — implement:
```cpp
auto renderer_context::recreate_font(std::string_view font_family,
                                      float font_size_pt)
    -> std::expected<void, std::error_code> {
  // Get current window dimensions for the constant buffer.
  window_dimensions const dims{window_width_, window_height_};
  // (window_width_/window_height_ need to be stored — see below)

  auto new_renderer = make_glyph_renderer(device_, font_family, font_size_pt, dpi_, dims);
  if (!new_renderer) return std::unexpected(new_renderer.error());

  renderer_ = std::move(*new_renderer);
  font_family_ = font_family;
  font_size_pt_ = font_size_pt;
  return {};
}
```

Also update `make_renderer_context` to store `dpi_` and initial `font_family_`/`font_size_pt_`. The `handle_resize` already stores window dimensions — we need to track those for `recreate_font` to pass the right dimensions to the constant buffer. Add `uint32_t window_width_ = 0; uint32_t window_height_ = 0;` members and update them in `handle_resize`.

### Step 2: Add `resize_scrollback()` to `terminal_session`

**`session.hpp`** — add public method:
```cpp
// Change the scrollback capacity without changing grid dimensions.
// Preserves existing scrollback content where possible.
void resize_scrollback(uint32_t max_lines);
```

**`session.cpp`** — implement:
```cpp
void terminal_session::resize_scrollback(uint32_t max_lines) {
  grid_.resize_scrollback(max_lines);
}
```

Wait — `terminal_grid::resize_scrollback` doesn't exist. We need to add it, or use the existing `resize` path. Since the grid's `resize(cols, rows)` calls `buffer_.resize(cols, rows, scrollback_max_lines_)`, we could add a method:

**`grid.hpp`** — add:
```cpp
void resize_scrollback(uint32_t new_max_lines);
```

**`grid.cpp`** — implement:
```cpp
void terminal_grid::resize_scrollback(uint32_t new_max_lines) {
  if (new_max_lines == scrollback_max_lines_) return;
  scrollback_max_lines_ = new_max_lines;
  buffer_.resize(cols_, rows_, new_max_lines);
}
```

This reflows the buffer with the new scrollback capacity but same grid dimensions. Content is preserved.

### Step 3: Add `config_dir_` to `application`

**`application.hpp`** — add:
```cpp
std::filesystem::path config_dir_;
```

And a private method:
```cpp
void on_config_changed();
```

**`application.cpp`** — store `config_dir_` in the constructor:
```cpp
application::application(..., std::filesystem::path config_dir)
    : ...
    , config_dir_(std::move(config_dir))
    , watcher_(std::move(config_dir), config_changed_.get()) {}  // BUG WAIT
```

Wait — the constructor moves `config_dir` into BOTH `config_dir_` and `watcher_`. I need to pass it by copy or adjust the order.

Fix: pass `config_dir` by value to the constructor, store it in `config_dir_`, then construct `watcher_` from `config_dir_` (already stored). But `config_dir_` is declared after `watcher_` — need to reorder.

Better: just pass `config_dir` by `const&` and use it for both. The watcher already captures by value in the lambda.

**Reorder members:** `config_dir_` before `config_changed_` before `watcher_`:
```cpp
std::filesystem::path config_dir_;
std::unique_ptr<std::atomic<bool>> config_changed_ = ...;
config_watcher watcher_;
```

Constructor:
```cpp
application(..., std::filesystem::path const& config_dir)
    : ...
    , config_dir_(config_dir)
    , watcher_(config_dir_, config_changed_.get()) {}
```

### Step 4: Implement `on_config_changed()` in `application.cpp`

```cpp
void application::on_config_changed() {
  // Re-parse config.toml.  On failure, keep current config (C10 adds error UI).
  auto result = parse_config(config_dir_);
  if (!result.errors.empty()) {
    // Parse or validation error — keep current config, C10 will handle.
    // But we still need the config for the fields that DID parse.
  }

  auto const& new_cfg = result.config;

  // Font changes: recreate glyph renderer.
  bool font_changed = false;
  if (new_cfg.font_family != config_.font_family ||
      new_cfg.font_size != config_.font_size) {
    font_changed = true;
    // Use whichever value is valid (new or current).
    auto const family = new_cfg.font_family.has_value()
                            ? *new_cfg.font_family : *config_.font_family;
    auto const size = new_cfg.font_size.has_value()
                          ? *new_cfg.font_size : *config_.font_size;
    if (auto res = renderer_ctx_.recreate_font(family, size); !res) {
      util::log_error(res.error(), "hot-reload: recreate font");
      return;  // Don't update config_ if font recreation fails.
    }
    config_.font_family = family;
    config_.font_size = size;
  }

  // cursor_style: just update the stored value (read every frame).
  if (new_cfg.cursor_style != config_.cursor_style) {
    config_.cursor_style = new_cfg.cursor_style;
  }

  // scrollback_lines.
  if (new_cfg.scrollback_lines != config_.scrollback_lines) {
    session_.resize_scrollback(*new_cfg.scrollback_lines);
    config_.scrollback_lines = new_cfg.scrollback_lines;
  }

  // columns / rows: resize window.
  bool cols_changed = (new_cfg.columns != config_.columns);
  bool rows_changed = (new_cfg.rows != config_.rows);
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

And wire it in the message loop:
```cpp
if (config_changed_->exchange(false, std::memory_order_acquire)) {
    on_config_changed();
}
```

---

## Build and Test

```bash
cmake --build build/debug && ctest --test-dir build/debug --output-on-failure
```

Manual verification:
1. Launch betty
2. Edit `font_size = 20.0` in `config.toml` and save → font grows immediately
3. Edit `cursor_style = "none"` → cursor disappears
4. Edit `scrollback_lines = 1000` → scrollback buffer shrinks (generate lots of output, scroll back)
5. Edit `columns = 80` and `rows = 24` → window resizes instantly
6. Edit `shell = "cmd.exe"` → no change until restart

---

## Acceptance

- `font_family` and `font_size` changes apply without restart (glyph renderer recreated).
- `cursor_style` changes apply immediately.
- `scrollback_lines` changes apply immediately (content preserved).
- `columns` and `rows` changes resize the window, renderer, and terminal session automatically.
- `shell` changes are silently ignored until restart.
- Multiple fields can change simultaneously in a single save.
- If `config.toml` has parse errors, current settings are preserved (no crash, no revert).
- All existing tests pass.
