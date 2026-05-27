# C2 — Refactor: glyph_renderer accepts font family and size

## Goal

Remove the hardcoded `L"Consolas"` and `k_font_size_px` (18) from the glyph rendering subsystem. Thread `font_family` (as `std::string_view`) and `font_size_pt` (as `float`) from `make_application()` through `make_renderer_context()` into `make_glyph_renderer()`. Internally convert pt→px using the window's DPI.

**User sees:** No change. Text still renders with Consolas at the current default size.

---

## Design Decisions (from interview)

1. **String API: `std::string_view` in public interface.** `make_glyph_renderer` accepts `std::string_view font_family`. Internally converts to wide string using `platform::widen()` (moved from `window.cpp` anonymous namespace to `unicode`).

2. **DPI: passed as explicit `uint32_t` parameter.** `make_renderer_context` queries `GetDpiForWindow()` and passes it to `make_glyph_renderer`. Clean separation from window APIs.

3. **Parameter order (make_glyph_renderer):** device first, then font config, then geometry.
   ```cpp
   auto make_glyph_renderer(d3d_device const& device,
                             std::string_view font_family, float font_size_pt, uint32_t dpi,
                             window_dimensions const& window_size)
   ```

4. **pt→px conversion:** `std::lround(font_size_pt * dpi / 72.0f)`. At 96 DPI with 14.0 pt → 19 px.

5. **`init_font_face` takes explicit `const wchar_t*`:** Pass the widened font family name to both `init_font_face` and the italic lookup block.

---

## Files Changed

| File | Change |
|---|---|
| `src/platform/unicode.hpp` | Add `widen()` declaration. |
| `src/platform/unicode.cpp` | Add `widen()` implementation (moved from `window.cpp`). |
| `src/platform/window.cpp` | Remove `widen()` from anonymous namespace. Add `#include "unicode.hpp"`. Replace calls with `platform::widen()`. |
| `src/platform/text.hpp` | Update `make_glyph_renderer` declaration with new parameters. |
| `src/platform/text.cpp` | Remove `k_font_size_px` constant. Add `font_size_px` to `impl`. Update `make_glyph_renderer` implementation. Update `init_font_face` to accept `const wchar_t*`. Update italic lookup block. Replace all `k_font_size` usages. |
| `src/platform/gfx.hpp` | Update `friend auto make_glyph_renderer(...)` declaration. |
| `src/platform/renderer_context.hpp` | Update `make_renderer_context` declaration to accept font params. |
| `src/platform/renderer_context.cpp` | Update `make_renderer_context` implementation to accept font params, query DPI, forward to `make_glyph_renderer`. |
| `src/application.cpp` | Pass `"Consolas"` and `14.0f` to `make_renderer_context`. |

---

## Step-by-step Implementation

### Step 1: Move `widen` to `platform/unicode`

**`unicode.hpp`** — add declaration:
```cpp
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

// Convert UTF-8 string to UTF-16 wstring.
[[nodiscard]] auto widen(std::string_view sv) -> std::expected<std::wstring, std::error_code>;
```

**`unicode.cpp`** — add implementation (identical to current code in `window.cpp`):
```cpp
#include <windows.h>
#include "error.hpp"

auto widen(std::string_view sv) -> std::expected<std::wstring, std::error_code> {
  if (sv.empty()) return std::wstring{};
  int needed = MultiByteToWideChar(CP_UTF8, 0, sv.data(),
                                    static_cast<int>(sv.size()), nullptr, 0);
  if (needed <= 0) {
    return std::unexpected(make_win32_error());
  }
  std::wstring result(needed, L'\0');
  int converted = MultiByteToWideChar(CP_UTF8, 0, sv.data(),
                                       static_cast<int>(sv.size()), result.data(), needed);
  if (converted <= 0) {
    return std::unexpected(make_win32_error());
  }
  result.resize(static_cast<size_t>(converted));
  return result;
}
```

**`window.cpp`** — remove the `widen` function from the anonymous namespace. Add `#include "platform/unicode.hpp"`. Replace all anonymous-namespace `widen(...)` calls with `platform::widen(...)`.

### Step 2: Add `font_size_px` to `glyph_renderer::impl`

In `text.cpp`, add to the `impl` struct after `cell_height`:
```cpp
uint32_t font_size_px = 0;  // raster font size in pixels (converted from pt)
```

### Step 3: Update `init_font_face` signature

**Before:**
```cpp
auto init_font_face(IDWriteFactory* factory, IDWriteTextFormat* format,
                    uint32_t& cell_width, uint32_t& cell_height,
                    ComPtr<IDWriteFontFace1>& font_face,
                    uint32_t& font_ascent, uint32_t& font_design_units_per_em) -> HRESULT;
```

**After:** Add `const wchar_t* font_family` parameter after `factory`:
```cpp
auto init_font_face(IDWriteFactory* factory, const wchar_t* font_family,
                    IDWriteTextFormat* format,
                    uint32_t& cell_width, uint32_t& cell_height,
                    ComPtr<IDWriteFontFace1>& font_face,
                    uint32_t& font_ascent, uint32_t& font_design_units_per_em) -> HRESULT;
```

Replace `L"Consolas"` in `FindFamilyName` with `font_family`.

### Step 4: Update `make_glyph_renderer` — declaration and implementation

**`text.hpp`** and **`gfx.hpp`** — update friend and free-function declarations:
```cpp
[[nodiscard]] auto make_glyph_renderer(d3d_device const& device,
                                        std::string_view font_family,
                                        float font_size_pt,
                                        uint32_t dpi,
                                        window_dimensions const& window_size)
    -> std::expected<glyph_renderer, std::error_code>;
```

**`text.cpp`** — update implementation:
1. Remove `inline constexpr uint32_t k_font_size_px = 18u;`
2. Add `#include "unicode.hpp"` and `#include <cmath>`
3. Convert pt→px: `uint32_t const font_size_px = static_cast<uint32_t>(std::lround(font_size_pt * static_cast<float>(dpi) / 72.0f));`
4. Store in impl: `p->font_size_px = font_size_px;`
5. Widen font family: `auto wide_family_result = widen(font_family);` — if failed, return error
6. Replace `constexpr float k_font_size = static_cast<float>(k_font_size_px);` with `float const k_font_size = static_cast<float>(font_size_px);`
7. Replace `L"Consolas"` in `CreateTextFormat` with `wide_family_result->c_str()`
8. Pass `wide_family_result->c_str()` to `init_font_face`
9. Replace `L"Consolas"` in italic face lookup with `wide_family_result->c_str()`

### Step 5: Update rasterize_glyph call sites (dynamic atlas)

In the `ensure_glyph_cached` function (around line 1198), replace:
```cpp
constexpr float k_font_size = static_cast<float>(k_font_size_px);
```
with:
```cpp
float const k_font_size = static_cast<float>(p->font_size_px);
```

### Step 6: Update `make_renderer_context`

**`renderer_context.hpp`** — update declaration:
```cpp
[[nodiscard]] auto make_renderer_context(win32_window const& window,
                                          std::string_view font_family,
                                          float font_size_pt)
    -> std::expected<renderer_context, std::error_code>;
```

**`renderer_context.cpp`**:
1. Query DPI from the window: `uint32_t const dpi = GetDpiForWindow(static_cast<HWND>(window.as_hwnd()));`
2. Pass new params to `make_glyph_renderer`:
```cpp
auto renderer_result = make_glyph_renderer(device, font_family, font_size_pt, dpi, size);
```

### Step 7: Update `make_application`

**`application.cpp`** — pass hardcoded values:
```cpp
auto renderer_ctx_result = platform::make_renderer_context(window, "Consolas", 14.0f);
```

---

## Build and Test

```bash
cmake --build build/debug && ctest --test-dir build/debug --output-on-failure
```

No new tests (font rendering is visual). All existing tests must pass. Verify betty launches and text renders identically to before.

---

## Acceptance

- `k_font_size_px` no longer exists.
- `L"Consolas"` no longer appears in `text.cpp` (replaced by the widened `font_family` parameter).
- `make_glyph_renderer` accepts `font_family`, `font_size_pt`, and `dpi`.
- `make_renderer_context` accepts and forwards `font_family` and `font_size_pt`.
- `make_application` passes `"Consolas"` and `14.0f`.
- `widen` moved to `unicode.hpp`/`.cpp` and callable as `platform::widen()`.
- betty launches and text renders identically.
