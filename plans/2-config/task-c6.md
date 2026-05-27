# C6 — Wire config values into the application

## Goal

Pass `betty_config` through `make_application()` into every subsystem that consumes configurable settings: font rendering, grid dimensions, scrollback buffer, shell command line, and initial window geometry.

**User sees:** Editing any supported field in `config.toml` changes behavior on launch. Custom fonts render. Custom scrollback depth works. Initial window size respects configured columns × rows. Custom shell launches.

---

## Design Decisions (from interview)

1. **`make_application(betty_config const& cfg)`.** Single config parameter. Extracts fields internally. `application` stores a copy for hot-reload (C8–C10).

2. **Two-phase window sizing.** Create window at default size → create renderer context → query cell metrics → resize window to fit `cfg.columns × cfg.rows` exactly. Minor reorder, one resize, no visual flash in practice.

3. **Shell created after window resize.** Reordered: window → renderer → resize window to exact grid size → shell (with config.columns/rows) → session. Shell gets the correct ConPTY size from the start.

4. **Public `resize_client_area` on `win32_window`.** Clean API using `SetWindowPos`, reusable for C9 hot-reload.

5. **Direct config field mapping.** All fields read via `*cfg.field_name` (always has a value due to in-class defaults + validation reset).

6. **Unit test for window size computation.** Extract math into `compute_window_size()`; test that. No process/GPU/window in tests.

---

## Config → Subsystem Mapping

| Config field | Subsystem | How |
|---|---|---|
| `font_family` | `make_renderer_context` → `make_glyph_renderer` | Pass `*cfg.font_family.c_str()` |
| `font_size` | `make_renderer_context` → `make_glyph_renderer` | Pass `*cfg.font_size` |
| `scrollback_lines` | `terminal_session` → `terminal_grid` | Pass `*cfg.scrollback_lines` |
| `columns` | Window resize + `shell_settings.cols` + `terminal_session` | Compute pixel size from `*cfg.columns × cell_w` |
| `rows` | Window resize + `shell_settings.rows` + `terminal_session` | Compute pixel size from `*cfg.rows × cell_h` |
| `shell` | `shell_settings.command_line` | Pass `*cfg.shell` |
| `cursor_style` | (C7 — not wired yet) | — |

---

## Files Changed

| File | Change |
|---|---|
| `src/platform/window.hpp` | Add `resize_client_area()` public method + friend declaration if needed. |
| `src/platform/window.cpp` | Implement `resize_client_area()` using `SetWindowPos` with `SWP_NOZORDER`. |
| `src/application.hpp` | Add `betty_config config_` member. Update `make_application` declaration. |
| `src/application.cpp` | Accept config, reorder creation steps, wire all fields. Store config. Add `compute_window_size()` helper. |
| `src/main.cpp` | Pass config result to `make_application(cfg)`. |
| `tests/config_test.cpp` (or new file) | Add `compute_window_size` unit test. |

---

## Step-by-step Implementation

### Step 1: Add `resize_client_area` to `win32_window`

**`window.hpp`** — add public method:
```cpp
// Resize the client area to the given dimensions (in pixels).
// Uses SetWindowPos, preserving the window's Z-order and current position.
auto resize_client_area(uint32_t client_width, uint32_t client_height) -> void;
```

**`window.cpp`** — implementation:
```cpp
auto win32_window::resize_client_area(uint32_t client_width, uint32_t client_height) -> void {
  HWND hwnd = static_cast<HWND>(handle_);
  if (!hwnd || !IsWindow(hwnd)) return;

  RECT rect{0, 0, static_cast<LONG>(client_width), static_cast<LONG>(client_height)};
  DWORD const style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
  if (AdjustWindowRectEx(&rect, style, FALSE, 0)) {
    SetWindowPos(hwnd, nullptr,
                 0, 0,
                 rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
  }
}
```

### Step 2: Update `application.hpp`

```cpp
#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <system_error>
#include "config/config.hpp"            // NEW
#include "platform/window.hpp"
#include "platform/renderer_context.hpp"
#include "terminal/session.hpp"

namespace betty {

class application {
public:
  [[nodiscard]] int run();

private:
  explicit application(platform::win32_window window,
                       platform::renderer_context renderer_ctx,
                       terminal::terminal_session session,
                       betty_config config);          // NEW: stores config

  void on_key(platform::vk_code vk, bool ctrl, bool shift, bool alt);
  void on_char(uint32_t codepoint);
  void on_resize(uint32_t width, uint32_t height, bool completed);

  platform::win32_window window_;
  platform::renderer_context renderer_ctx_;
  terminal::terminal_session session_;
  betty_config config_;                               // NEW: for hot-reload (C8-C10)
  std::optional<std::error_code> fatal_error_;

  friend auto make_application(betty_config const&)   // UPDATED
      -> std::expected<application, std::error_code>;
};

[[nodiscard]] auto make_application(betty_config const& cfg)
    -> std::expected<application, std::error_code>;

} // namespace betty
```

### Step 3: Update `application.cpp` — reorder and wire

```cpp
namespace {

constexpr uint32_t k_min_columns = 80;
constexpr uint32_t k_min_rows    = 1;

// Compute the pixel dimensions for a client area that exactly fits
// `cols` × `rows` grid cells with `padding` on all sides.
// Exposed for unit testing.
[[nodiscard]] auto compute_window_size(uint32_t cols, uint32_t rows,
                                        uint32_t cell_w, uint32_t cell_h,
                                        uint32_t padding)
    -> platform::window_dimensions {
  return {
    cols * cell_w + 2 * padding,
    rows * cell_h + 2 * padding
  };
}

} // anonymous namespace

namespace betty {

application::application(platform::win32_window window,
                         platform::renderer_context renderer_ctx,
                         terminal::terminal_session session,
                         betty_config config)
    : window_(std::move(window))
    , renderer_ctx_(std::move(renderer_ctx))
    , session_(std::move(session))
    , config_(std::move(config)) {}

// (on_key, on_char, on_resize, run — unchanged)

auto make_application(betty_config const& cfg)
    -> std::expected<application, std::error_code> {

  // 1. Create window at a reasonable default size.
  //    We'll resize it to the exact grid dimensions in step 3b.
  auto window_result = platform::make_window(
    platform::window_settings{
      .size = platform::default_window_size,
      .title = "betty"
    }
  );
  if (!window_result) {
    util::show_fatal_error(window_result.error(), "create window");
    return std::unexpected(window_result.error());
  }
  auto window = std::move(*window_result);

  // 2. Create renderer context with configured font.
  auto renderer_ctx_result = platform::make_renderer_context(
      window, *cfg.font_family, *cfg.font_size);
  if (!renderer_ctx_result) {
    util::show_fatal_error(renderer_ctx_result.error(), "create renderer context");
    return std::unexpected(renderer_ctx_result.error());
  }
  auto renderer_ctx = std::move(*renderer_ctx_result);

  // 3. Resize window to match configured columns × rows.
  uint32_t const cell_w = renderer_ctx.cell_width();
  uint32_t const cell_h = renderer_ctx.cell_height();
  uint32_t const pad = platform::k_padding_px;

  uint32_t const cfg_cols = *cfg.columns;
  uint32_t const cfg_rows = *cfg.rows;

  auto const exact_size = compute_window_size(cfg_cols, cfg_rows, cell_w, cell_h, pad);
  window.resize_client_area(exact_size.width, exact_size.height);
  // Renderer must be notified so the constant buffer matches the new size.
  if (auto resize_result = renderer_ctx.handle_resize(exact_size.width, exact_size.height);
      !resize_result) {
    return std::unexpected(resize_result.error());
  }

  // 4. Create shell with configured command line and grid dimensions.
  std::optional<platform::shell> shell;
  auto shell_result = platform::make_shell(platform::shell_settings{
    .cols = cfg_cols,
    .rows = cfg_rows,
    .command_line = *cfg.shell
  });
  if (shell_result) {
    shell = std::move(*shell_result);
  } else {
    util::log_error(shell_result.error(), "Failed to create shell process");
  }

  // 5. Set minimum window size.
  uint32_t const min_win_width  = k_min_columns * cell_w + 2 * pad;
  uint32_t const min_win_height = k_min_rows * cell_h + 2 * pad;
  window.set_min_window_size(min_win_width, min_win_height);

  // 6. Create terminal session with configured scrollback and dimensions.
  terminal::terminal_session session(cfg_cols, cfg_rows,
                                      *cfg.scrollback_lines,
                                      std::move(shell));

  // 7. Assemble application, storing config for hot-reload.
  application app{std::move(window), std::move(renderer_ctx),
                  std::move(session), cfg};

  // Store a mutable copy of the config so that the application struct can own it.
  // (betty_config is all value types — move is fine.)

  return app;
}

} // namespace betty
```

### Step 4: Update `main.cpp`

```cpp
int main() {
  // ... (resolve exe_dir, parse config — unchanged) ...

  auto [cfg, errors] = betty::parse_config(exe_dir);

  // ... (handle errors — unchanged) ...

  auto app = betty::make_application(cfg);   // pass config
  if (!app) return 1;
  return app->run();
}
```

### Step 5: Add unit test for `compute_window_size`

In `tests/config_test.cpp` (or a new test file — since `compute_window_size` is in `application.cpp`, we could expose it via `application.hpp` or add a separate test that compiles `application.cpp`):

**Option A:** Expose `compute_window_size` in `application.hpp` with `inline` and test it directly.
**Option B:** Create a test fixture that calls `make_application` with a known config and verifies the window's client size.

Since `make_application` needs a real Windows environment, option A is simpler:

In `application.hpp`, add:
```cpp
// Compute client area pixel size for a grid of `cols` × `rows` with padding.
// Useful for testing and for computing initial window geometry.
[[nodiscard]] inline auto compute_window_size(uint32_t cols, uint32_t rows,
                                               uint32_t cell_w, uint32_t cell_h,
                                               uint32_t padding)
    -> platform::window_dimensions {
  return { cols * cell_w + 2 * padding, rows * cell_h + 2 * padding };
}
```

Then in `tests/config_test.cpp`:
```cpp
#include "application.hpp"
#include "platform/types.hpp"

TEST_CASE("compute_window_size — standard dimensions", "[config][wiring]") {
    auto dims = betty::compute_window_size(120, 40, 10, 20, 8);
    CHECK(dims.width == 120 * 10 + 16);   // 1216
    CHECK(dims.height == 40 * 20 + 16);   // 816
}

TEST_CASE("compute_window_size — minimum dimensions", "[config][wiring]") {
    auto dims = betty::compute_window_size(1, 1, 8, 16, 4);
    CHECK(dims.width == 1 * 8 + 8);    // 16
    CHECK(dims.height == 1 * 16 + 8);  // 24
}

TEST_CASE("compute_window_size — zero padding", "[config][wiring]") {
    auto dims = betty::compute_window_size(80, 24, 10, 20, 0);
    CHECK(dims.width == 80 * 10);   // 800
    CHECK(dims.height == 24 * 20);  // 480
}
```

---

## Build and Test

```bash
cmake --build build/debug && ctest --test-dir build/debug --output-on-failure
```

All existing tests pass plus 3 new `compute_window_size` tests.

---

## Acceptance

- `make_application` takes `betty_config const&`.
- Font family and size from config reach `make_glyph_renderer`.
- `scrollback_lines` from config reaches `terminal_grid`.
- `columns` and `rows` from config determine initial window client area size and shell ConPTY dimensions.
- `shell` from config is passed to `make_shell` as `command_line`.
- `betty_config` stored on `application` for hot-reload.
- `win32_window` has public `resize_client_area()`.
- `compute_window_size` unit tests pass.
- betty launches with settings from `config.toml` next to the executable.
