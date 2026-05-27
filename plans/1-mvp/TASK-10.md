# Task 10 — Window Resizing

**Goal:** Dragging the window edge reflows the terminal content. Text wraps to the new column count. Shell commands like `ls` re-layout to fill the new width.

**Dependencies:** Tasks 1–9. Requires terminal grid, shell I/O, ConPTY, and the glyph renderer.

---

## Design Decisions (from Q&A)

| Decision | Choice |
|---|---|
| Resize trigger | **Hybrid**: swap chain + viewport update continuously on `WM_SIZE`; grid + ConPTY resize only on `WM_EXITSIZEMOVE` (drag finished) |
| Text reflow on resize | **Standard**: rows preserved as-is, no reflow. Only new shell output uses the resized dimensions |
| Swap chain resize API | **Standalone function** `resize_swap_chain(device, swap_chain, old_rtv, new_size)` that releases old RTV, calls `ResizeBuffers`, returns new RTV |
| Resize notification | **Callback** `on_resize(width, height, completed)` in `window_callbacks`, mirroring `on_key` pattern |
| Minimum window size | **80 columns × 1 row** (enforced via `WM_GETMINMAXINFO` + clamping in Application) |
| Window style | Full `WS_OVERLAPPEDWINDOW` (re-enable both `WS_THICKFRAME` and `WS_MAXIMIZEBOX`) |

---

## Implementation Plan

### Step 0 — Enable window resizing (prerequisite)

**File:** `src/platform/window.cpp` — `make_window()`

The current window style strips `WS_THICKFRAME` and `WS_MAXIMIZEBOX`:

```cpp
DWORD dwStyle = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
```

Restore the full standard window style so the border is resizable and the maximize button is present:

```cpp
DWORD dwStyle = WS_OVERLAPPEDWINDOW;
```

**Rationale:** Without `WS_THICKFRAME`, the window border cannot be dragged to resize. Without `WS_MAXIMIZEBOX`, the maximize/restore button is missing.

---

### Step 1 — Add `on_resize` callback to `window_callbacks`

**File:** `src/platform/window.hpp`

1. Add a new callback member to `detail::window_callbacks`:

```cpp
struct window_callbacks {
  std::function<void(vk_code, bool ctrl, bool shift, bool alt)> on_key;
  std::function<void(uint32_t)> on_char;
  // NEW: resize callback — called on WM_SIZE (completed=false) and WM_EXITSIZEMOVE (completed=true)
  std::function<void(uint32_t width, uint32_t height, bool completed)> on_resize;
  // NEW: minimum client area dimensions for WM_GETMINMAXINFO
  uint32_t min_client_width = 0;
  uint32_t min_client_height = 0;
};
```

2. Declare the setter function:

```cpp
auto set_resize_callback(win32_window& window,
    std::function<void(uint32_t width, uint32_t height, bool completed)> cb) -> void;
```

3. Declare a function to set the minimum client area size:

```cpp
auto set_min_window_size(win32_window& window,
    uint32_t client_width, uint32_t client_height) -> void;
```

4. Add friend declarations to `win32_window` for these new functions:

```cpp
friend auto set_resize_callback(win32_window&, std::function<void(uint32_t, uint32_t, bool)>) -> void;
friend auto set_min_window_size(win32_window&, uint32_t, uint32_t) -> void;
```

---

### Step 2 — Handle `WM_SIZE`, `WM_EXITSIZEMOVE`, and `WM_GETMINMAXINFO` in `WndProc`

**File:** `src/platform/window.cpp`

Add three new cases to the `WndProc` switch:

**2a. `WM_SIZE` — continuous resize during drag:**

```cpp
case WM_SIZE: {
  if (auto* cbs = get_callbacks(hwnd)) {
    if (cbs->on_resize) {
      uint32_t const width  = static_cast<uint32_t>(LOWORD(lParam));
      uint32_t const height = static_cast<uint32_t>(HIWORD(lParam));
      cbs->on_resize(width, height, false);
    }
  }
  return 0;
}
```

**2b. `WM_EXITSIZEMOVE` — user finished dragging:**

```cpp
case WM_EXITSIZEMOVE: {
  if (auto* cbs = get_callbacks(hwnd)) {
    if (cbs->on_resize) {
      RECT rect{};
      if (GetClientRect(hwnd, &rect)) {
        uint32_t const width  = static_cast<uint32_t>(rect.right - rect.left);
        uint32_t const height = static_cast<uint32_t>(rect.bottom - rect.top);
        cbs->on_resize(width, height, true);
      }
    }
  }
  return 0;
}
```

**2c. `WM_GETMINMAXINFO` — enforce minimum window size:**

```cpp
case WM_GETMINMAXINFO: {
  if (auto* cbs = get_callbacks(hwnd)) {
    auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
    if (cbs->min_client_width > 0 && cbs->min_client_height > 0) {
      RECT rect{0, 0,
                static_cast<LONG>(cbs->min_client_width),
                static_cast<LONG>(cbs->min_client_height)};
      DWORD const style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
      if (AdjustWindowRectEx(&rect, style, FALSE, 0)) {
        mmi->ptMinTrackSize.x = rect.right - rect.left;
        mmi->ptMinTrackSize.y = rect.bottom - rect.top;
      }
    }
  }
  return 0;
}
```

---

### Step 3 — Implement `set_resize_callback` and `set_min_window_size`

**File:** `src/platform/window.cpp`

Implement the two new setters (similar pattern to `set_key_callback`):

```cpp
auto set_resize_callback(win32_window& window,
    std::function<void(uint32_t width, uint32_t height, bool completed)> cb) -> void {
  if (window.callbacks_) {
    window.callbacks_->on_resize = std::move(cb);
  }
}

auto set_min_window_size(win32_window& window,
    uint32_t client_width, uint32_t client_height) -> void {
  if (window.callbacks_) {
    window.callbacks_->min_client_width  = client_width;
    window.callbacks_->min_client_height = client_height;
  }
}
```

---

### Step 4 — Add `resize_swap_chain` to the platform graphics layer

**File:** `src/platform/gfx.hpp`

Declare the new function:

```cpp
// Resize the swap chain buffers and create a new render target view.
// Takes ownership of the old RTV (passed by value) and releases it before
// calling IDXGISwapChain::ResizeBuffers. Returns a new RTV for the resized
// back buffer.
[[nodiscard]] auto resize_swap_chain(
    d3d_device const& device,
    d3d_swap_chain& swap_chain,
    d3d_render_target_view old_rtv,   // by value — released inside
    window_dimensions new_size)
  -> std::expected<d3d_render_target_view, std::error_code>;
```

**File:** `src/platform/gfx.cpp`

Implementation:

```cpp
auto resize_swap_chain(d3d_device const& device,
                        d3d_swap_chain& swap_chain,
                        d3d_render_target_view old_rtv,
                        window_dimensions new_size)
  -> std::expected<d3d_render_target_view, std::error_code> {

  if (new_size.width == 0 || new_size.height == 0) {
    return std::unexpected(make_win32_error(ERROR_INVALID_PARAMETER));
  }

  // 1. Unbind any RTV from the pipeline.
  device.impl_->context->OMSetRenderTargets(0, nullptr, nullptr);

  // 2. Release the old RTV (by resetting its impl).
  //    The move-constructed `old_rtv` will still call its destructor later,
  //    but we must ensure the ComPtr is released before ResizeBuffers.
  old_rtv.impl_.reset();

  // 3. Resize the swap chain buffers.
  DXGI_SWAP_CHAIN_DESC1 desc{};
  HRESULT hr = swap_chain.impl_->swap_chain->GetDesc1(&desc);
  if (FAILED(hr)) {
    return std::unexpected(make_hresult_error(hr));
  }

  hr = swap_chain.impl_->swap_chain->ResizeBuffers(
      desc.BufferCount,
      new_size.width,
      new_size.height,
      desc.Format,
      desc.Flags);
  if (FAILED(hr)) {
    return std::unexpected(make_hresult_error(hr));
  }

  // 4. Create a new RTV from the resized back buffer.
  return make_render_target_view(device, swap_chain);
}
```

---

### Step 5 — Wire up resize handling in `Application`

**File:** `src/app.cpp`

**5a. In `make_application()`** — after the renderer is created and cell dimensions are known, compute the minimum window size and set it:

```cpp
// After renderer is created and cell_w/cell_h are known:
// Set minimum window size: 80 columns × 1 row.
uint32_t const min_win_width  = 80 * cell_w;
uint32_t const min_win_height = 1 * cell_h;
platform::set_min_window_size(window, min_win_width, min_win_height);
```

**5b. In `Application::run()`** — set the resize callback before entering the message loop:

```cpp
platform::set_resize_callback(window_,
  [this](uint32_t width, uint32_t height, bool completed) {
    // Ignore zero-area resize (minimized window).
    if (width == 0 || height == 0) return;

    // --- Always: resize swap chain + update renderer dimensions ---
    auto new_rtv = platform::resize_swap_chain(
        device_, swap_chain_, std::move(rtv_),
        platform::window_dimensions{width, height});
    if (new_rtv) {
      rtv_ = std::move(*new_rtv);
    } else {
      util::log_error(new_rtv.error(), "resize swap chain");
      return;
    }

    if (auto result = renderer_.update_dimensions(
            device_, platform::window_dimensions{width, height});
        !result) {
      util::log_error(result.error(), "update renderer dimensions");
    }

    // --- Only on resize completion: resize grid + ConPTY ---
    if (completed) {
      uint32_t const cell_w = renderer_.cell_width();
      uint32_t const cell_h = renderer_.cell_height();

      // Compute new terminal dimensions, clamped to minimum.
      uint32_t const new_cols = std::max(80u, width / cell_w);
      uint32_t const new_rows = std::max(1u,  height / cell_h);

      if (new_cols != grid_.cols() || new_rows != grid_.rows()) {
        grid_.resize(new_cols, new_rows);

        if (shell_ && platform::is_shell_running(*shell_)) {
          if (auto result = platform::resize_shell(*shell_, new_cols, new_rows);
              !result) {
            util::log_error(result.error(), "resize shell");
          }
        }
      }
    }
  });
```

**Note:** The existing `terminal_grid::resize()` method (in `src/terminal/grid.cpp`) already handles the grid buffer resize. Review it to ensure it handles edge cases:
- Clamping saved cursor is already done
- The simple row-by-row copy (no reflow) is the intended behavior per Q2
- No changes needed to `grid.cpp` for this task

---

### Step 6 — Handle maximize and restore

With `WS_MAXIMIZEBOX` enabled (Step 0), maximize/restore triggers `WM_SIZE`. However, `WM_EXITSIZEMOVE` is **not** sent for maximize/restore — only for drag-resize. Without special handling, maximizing the window would update the swap chain but would **not** resize the grid or notify ConPTY, leaving the terminal at its old dimensions inside a now-larger window.

**Fix:** In the `WM_SIZE` handler, treat `SIZE_MAXIMIZED` and `SIZE_RESTORED` as completed resizes, since no `WM_EXITSIZEMOVE` will follow. Update `WndProc`'s `WM_SIZE` case (from Step 2a):

```cpp
case WM_SIZE: {
  if (auto* cbs = get_callbacks(hwnd)) {
    if (cbs->on_resize) {
      uint32_t const width  = static_cast<uint32_t>(LOWORD(lParam));
      uint32_t const height = static_cast<uint32_t>(HIWORD(lParam));
      // WM_EXITSIZEMOVE is only sent for drag-resize. For maximize/restore,
      // treat WM_SIZE itself as the completed resize.
      bool const completed = (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED);
      cbs->on_resize(width, height, completed);
    }
  }
  return 0;
}
```

---

### Step 7 — Add RTV validity guard to the main loop

**File:** `src/app.cpp`

When `resize_swap_chain` fails in the callback, `rtv_` is left in a moved-from (empty) state because the old RTV was released before `ResizeBuffers`. If the main loop then tries `device_.clear(rtv_, ...)`, it dereferences a null `unique_ptr` and crashes.

**Fix:** Add an early-out in the main loop when `rtv_` is invalid. Add `operator bool` to `d3d_render_target_view` in `src/platform/gfx.hpp`:

```cpp
struct d3d_render_target_view {
  [[nodiscard]] explicit operator bool() const noexcept { return impl_ != nullptr; }
  // ... existing members ...
};
```

And in `Application::run()`, after each frame's dispatch, guard the rendering:

```cpp
if (!rtv_) {
  // RTV was invalidated by a failed resize — skip this frame.
  // The swap chain and renderer still have the old (now-stale) dimensions.
  continue;
}
```

This prevents a crash; the next resize callback will attempt recovery.

---

### Step 8 — Build and manual test

1. Build: `cmake --build build/debug`
2. Launch `betty.exe`
3. Verify:
   - [ ] Window has a thick resizable border (not fixed-size)
   - [ ] Dragging the window edge resizes the swap chain smoothly (no stretching artifacts)
   - [ ] After releasing the edge, the terminal grid reflows to the new size
   - [ ] `dir` / `ls` output re-layouts to fill the new width (because the shell gets `SIGWINCH` via `ResizePseudoConsole`)
   - [ ] Existing scrollback content is preserved (truncated if narrower, padded if wider)
   - [ ] Cursor position is clamped correctly after resize
   - [ ] The cursor is visible at the correct position after resize
   - [ ] Window cannot be resized smaller than 80 columns × 1 row
   - [ ] Minimizing and restoring the window works correctly
   - [ ] Maximizing the window resizes the terminal to fill the screen
   - [ ] Restoring from maximize resizes the terminal back correctly
   - [ ] Already-completed features (colours, cursor, erase, window title) continue to work after resize

---

## Files Changed

| File | Change |
|---|---|
| `src/platform/window.cpp` | Restore full `WS_OVERLAPPEDWINDOW` style; add `WM_SIZE`, `WM_EXITSIZEMOVE`, `WM_GETMINMAXINFO` cases to `WndProc`; implement `set_resize_callback()` and `set_min_window_size()` |
| `src/platform/window.hpp` | Add `on_resize` to `window_callbacks`; add `min_client_width`/`min_client_height`; declare `set_resize_callback()` and `set_min_window_size()`; add friend declarations |
| `src/platform/gfx.hpp` | Declare `resize_swap_chain()`; add `operator bool` to `d3d_render_target_view` |
| `src/platform/gfx.cpp` | Implement `resize_swap_chain()` |
| `src/app.cpp` | Set minimum window size; set resize callback in `run()`; handle resize events (swap chain, renderer, grid, ConPTY); add RTV validity guard |
| `src/terminal/grid.cpp` | Review existing `resize()` — no changes expected (already handles row-by-row copy and cursor clamping) |

---

## Verification Checklist

- [ ] Window is resizable (thick border present, drag works)
- [ ] Smooth resize during drag (swap chain matches window size)
- [ ] Grid resized on drag release (rows/cols match new window size)
- [ ] Shell (`ls`, `dir`) output re-layouts after resize (ConPTY notified)
- [ ] Existing content preserved (truncated/padded as appropriate)
- [ ] Cursor position valid after resize
- [ ] Minimum window size enforced (80 cols × 1 row)
- [ ] All existing tests pass (`ctest`)
- [ ] Colours, cursor, erase, window title continue to work
- [ ] Minimize/restore works correctly
