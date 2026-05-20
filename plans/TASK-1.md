# TASK-1: Project scaffold + bare window

## Goal

Launch `betty.exe` → a fixed-size window filled with Catppuccin Mocha background (`#1e1e2e`), with a standard title bar, closeable. **Zero Win32 or DirectX types leak outside the `platform` module.** Consumers (`main.cpp`) see only type-safe C++23 wrapper types with rich methods.

---

## 1. Project structure

```
betty/
├── CMakeLists.txt
├── CMakePresets.json
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   └── platform/
│       ├── CMakeLists.txt
│       ├── error.hpp
│       ├── error.cpp
│       ├── types.hpp
│       ├── window.hpp
│       ├── window.cpp
│       ├── gfx.hpp
│       └── gfx.cpp
```

- `platform/` is a **static library**, linked by the executable target.
- All filenames are lowercase with underscores.
- All type names are `snake_case`. Concepts use `PascalCase`. Enum values use `snake_case`.

---

## 2. Build system

### 2.1 `CMakePresets.json`

Two presets:

| Preset         | Build type | Notes                          |
|----------------|------------|--------------------------------|
| `debug`        | Debug      | Debug layer enabled            |
| `release`      | Release    | No debug layer                 |

Both target VS 2026, Windows 10 SDK minimum `10.0.19041.0`.

### 2.2 Root `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.28)
project(betty LANGUAGES CXX)

add_subdirectory(src)
```

### 2.3 `src/CMakeLists.txt`

```cmake
add_subdirectory(platform)
add_executable(betty main.cpp)
target_link_libraries(betty PRIVATE platform)
target_compile_features(betty PRIVATE cxx_std_23)
set_target_properties(betty PROPERTIES
    WIN32_EXECUTABLE TRUE
)
```

### 2.4 `platform/CMakeLists.txt`

```cmake
add_library(platform STATIC
    error.cpp
    window.cpp
    gfx.cpp
)

target_include_directories(platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(platform PUBLIC cxx_std_23)

find_library(D3D11_LIB d3d11 REQUIRED)
find_library(DXGI_LIB dxgi REQUIRED)

target_link_libraries(platform PUBLIC
    ${D3D11_LIB}
    ${DXGI_LIB}
    user32
    gdi32
)
```

No precompiled headers.

---

## 3. Platform types (`types.hpp`)

Shared lightweight types used across the platform module. These are the **only** types from `platform` that consumers see.

```cpp
#pragma once
#include <cstdint>

namespace betty::platform {

// RGBA colour stored as 4 floats (0..1) for DirectX.
// Layout-compatible with const FLOAT[4] so it can be passed to ClearRenderTargetView.
static_assert(sizeof(float) == 4);
struct rgba_color {
    float r, g, b, a;
};

// Window client-area dimensions in logical pixels.
struct window_dimensions {
    uint32_t width;
    uint32_t height;
};

// Default window size (no cells yet, just a pleasant blank window).
inline constexpr window_dimensions default_window_size{ 960, 600 };

// Catppuccin Mocha base colour (#1e1e2e).
// 30/255 ≈ 0.117647, 46/255 ≈ 0.180392
inline constexpr rgba_color mocha_base{ 0.1176470588f, 0.1176470588f, 0.1803921569f, 1.0f };

} // namespace betty::platform
```

---

## 4. Error handling (`error.hpp` / `error.cpp`)

### 4.1 Custom `std::error_category`

Two categories:

| Category           | Source            | Code type       |
|--------------------|-------------------|-----------------|
| `win32_category()` | `GetLastError()`  | `DWORD`         |
| `d3d_category()`   | COM/DXGI `HRESULT`| `HRESULT`       |

Each provides `name()` and `message()` via `FormatMessageW` / `DXGetErrorString`.

These categories are **only used internally** by the platform module. External code receives `std::error_code` values and can call `.message()` on them, but never creates them directly.

### 4.2 Convenience functions (internal to platform)

```cpp
namespace betty::platform {

auto make_win32_error() -> std::error_code;             // calls GetLastError()
auto make_win32_error(DWORD code) -> std::error_code;
auto make_d3d_error(HRESULT hr) -> std::error_code;

} // namespace betty::platform
```

### 4.3 Usage pattern

All factory functions that can fail return `std::expected<T, std::error_code>`. No exceptions thrown from platform code.

---

## 5. Header isolation strategy

**The platform module's public headers must not expose any Win32 or DirectX types.** Consumers must not need to `#include <windows.h>`, `<d3d11.h>`, or `<dxgi.h>`.

Techniques used:

| Concern                        | Technique                                            |
|--------------------------------|------------------------------------------------------|
| `HWND`                         | Forward-declared as `using HWND = struct HWND__*;` in `window.hpp`. `<windows.h>` is only included in `window.cpp`. |
| `HINSTANCE`                    | Never exposed. `make_window()` calls `GetModuleHandleW(nullptr)` internally. |
| `ID3D11Device`, `ID3D11DeviceContext` | PIMPL idiom. Header stores `std::unique_ptr<impl>`. `impl` defined in `gfx.cpp` and holds the `ComPtr`s. |
| `IDXGISwapChain1`              | PIMPL (same as above).                              |
| `ID3D11RenderTargetView`       | PIMPL (same as above).                              |
| `MSG`, `PeekMessageW`          | Wrapped behind a free function `dispatch_pending_messages() -> bool`. Declared in `window.hpp`, defined in `window.cpp`. |

`ComPtr<T>` (from `<wrl/client.h>`) is **only used in `.cpp` files**, never in headers.

---

## 6. Window wrapper (`window.hpp` / `window.cpp`)

### 6.1 Type: `win32_window`

A move-only RAII handle wrapping `HWND`. The `HWND` is **private** — consumers never see it.

```cpp
// window.hpp
#pragma once
#include <memory>
#include <expected>
#include <system_error>
#include <string_view>
#include "types.hpp"

// Forward-declare HWND without pulling in <windows.h>.
using HWND = struct HWND__*;

namespace betty::platform {

struct window_settings {
    window_dimensions size{ default_window_size };
    std::wstring_view class_name{ L"betty_window_class" };
    std::wstring_view title{ L"betty" };
    int show_command{ SW_SHOW };
    // Note: no HINSTANCE — make_window() calls GetModuleHandleW(nullptr) internally.
};

// Move-only window handle. Closes the window on destruction.
struct win32_window {
    win32_window();
    ~win32_window();
    win32_window(win32_window&& other) noexcept;
    win32_window& operator=(win32_window&& other) noexcept;

    win32_window(win32_window const&) = delete;
    win32_window& operator=(win32_window const&) = delete;

private:
    HWND handle_{ nullptr };

    friend auto make_window(window_settings const&)
        -> std::expected<win32_window, std::error_code>;
    friend auto make_swap_chain(struct d3d_device const&, win32_window const&, struct swap_chain_settings const&)
        -> std::expected<struct d3d_swap_chain, std::error_code>;
};

auto make_window(window_settings const& settings)
    -> std::expected<win32_window, std::error_code>;

// Returns false when WM_QUIT is received (application should exit).
// Wraps PeekMessageW / TranslateMessage / DispatchMessageW internally.
auto dispatch_pending_messages() -> bool;

} // namespace betty::platform
```

### 6.2 Factory: `make_window`

Implementation (in `window.cpp`):
1. **Register window class** — `WNDCLASSEXW` with:
   - `cbSize` = `sizeof(WNDCLASSEXW)`
   - `style` = `CS_HREDRAW | CS_VREDRAW` (no `CS_OWNDC`)
   - `lpfnWndProc` = file-static `WndProc`
   - `hInstance` = `GetModuleHandleW(nullptr)`
   - `hIcon` = `LoadIconW(nullptr, IDI_APPLICATION)`
   - `hCursor` = `LoadCursorW(nullptr, IDC_ARROW)`
   - `hbrBackground` = `nullptr` (D3D clears the colour)
   - `lpszClassName` = `settings.class_name`
   - `hIconSm` = `nullptr`

2. **Adjust window rect** — `AdjustWindowRectEx` with `WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)` to convert client-area dimensions to window dimensions. No DPI scaling.

3. **Create window** — `CreateWindowExW` with:
   - `dwExStyle` = 0
   - `lpClassName` = `settings.class_name`
   - `lpWindowName` = `settings.title`
   - `dwStyle` = `WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)` (not resizable)
   - `x`, `y` = `CW_USEDEFAULT`
   - `nWidth`, `nHeight` = adjusted dimensions
   - `hWndParent` = `nullptr`, `hMenu` = `nullptr`
   - `hInstance` = `GetModuleHandleW(nullptr)`
   - `lpParam` = `nullptr`

   If `nullptr`, return `make_win32_error()`.

4. **Show window** — `ShowWindow(handle, settings.show_command)`.

5. Return `win32_window` with the HWND set.

### 6.3 Internal window procedure

A file-static `WndProc` in `window.cpp`. Handles:
- `WM_CLOSE` → `DestroyWindow(hwnd)` → return 0.
- `WM_DESTROY` → `PostQuitMessage(0)` → return 0.
- Everything else → `DefWindowProcW(hwnd, msg, wParam, lParam)`.

### 6.4 `dispatch_pending_messages`

```cpp
// Defined in window.cpp
auto dispatch_pending_messages() -> bool {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}
```

Returns `false` when `WM_QUIT` is received (the application should exit its main loop).

No Win32 types (`MSG`, `PeekMessageW`, etc.) appear in the header.

---

## 7. Graphics wrappers (`gfx.hpp` / `gfx.cpp`)

All types use the **PIMPL idiom** to hide COM pointers from headers. The header declares the public API; the `.cpp` defines `impl` structs holding `ComPtr<T>`.

### 7.1 PIMPL pattern (used by all three types below)

```cpp
// In header:
struct d3d_device {
    auto clear(d3d_render_target_view const& rtv, rgba_color const& color) const -> void;

    d3d_device();
    ~d3d_device();
    d3d_device(d3d_device&&) noexcept;
    d3d_device& operator=(d3d_device&&) noexcept;
    d3d_device(d3d_device const&) = delete;
    d3d_device& operator=(d3d_device const&) = delete;

private:
    struct impl;
    std::unique_ptr<impl> impl_;

    friend auto make_device() -> std::expected<d3d_device, std::error_code>;
    friend auto make_swap_chain(d3d_device const&, win32_window const&, swap_chain_settings const&)
        -> std::expected<d3d_swap_chain, std::error_code>;
    friend auto make_render_target_view(d3d_device const&, d3d_swap_chain const&)
        -> std::expected<d3d_render_target_view, std::error_code>;
};
```

In the `.cpp` file:
```cpp
struct d3d_device::impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
};

d3d_device::d3d_device() = default;
d3d_device::~d3d_device() = default;
d3d_device::d3d_device(d3d_device&&) noexcept = default;
d3d_device& d3d_device::operator=(d3d_device&&) noexcept = default;
```

The destructor and move operations are defined in the `.cpp` (where `impl` is complete) so `std::unique_ptr<impl>` can properly destroy/move the `impl`.

### 7.2 Type: `d3d_device`

```cpp
// gfx.hpp
namespace betty::platform {

struct d3d_device {
    // Clear the render target view to the given colour.
    // Internally calls OMSetRenderTargets + ClearRenderTargetView.
    auto clear(d3d_render_target_view const& rtv, rgba_color const& color) const -> void;

    d3d_device();
    ~d3d_device();
    d3d_device(d3d_device&&) noexcept;
    d3d_device& operator=(d3d_device&&) noexcept;
    d3d_device(d3d_device const&) = delete;
    d3d_device& operator=(d3d_device const&) = delete;

private:
    struct impl;
    std::unique_ptr<impl> impl_;

    friend auto make_device() -> std::expected<d3d_device, std::error_code>;
    friend auto make_swap_chain(d3d_device const&, win32_window const&, swap_chain_settings const&)
        -> std::expected<d3d_swap_chain, std::error_code>;
    friend auto make_render_target_view(d3d_device const&, d3d_swap_chain const&)
        -> std::expected<d3d_render_target_view, std::error_code>;
};

auto make_device() -> std::expected<d3d_device, std::error_code>;

} // namespace betty::platform
```

#### `make_device()` implementation (in `gfx.cpp`):

1. **Creation flags** — `D3D11_CREATE_DEVICE_DEBUG` in debug builds (`#ifndef NDEBUG`), `0` otherwise.
2. **Feature levels** — `{ D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 }`.
3. Call `D3D11CreateDevice`:
   - `pAdapter` = `nullptr`
   - `DriverType` = `D3D_DRIVER_TYPE_HARDWARE`
   - `Flags` = from step 1
   - `SDKVersion` = `D3D11_SDK_VERSION`
4. On failure (`FAILED(hr)`), return `make_d3d_error(hr)`.
5. Populate `impl` with the resulting device and context, wrap in `d3d_device`, return.

No `D3D11_CREATE_DEVICE_BGRA_SUPPORT`. Added in task 2.

#### `clear()` implementation:

```cpp
void d3d_device::clear(d3d_render_target_view const& rtv, rgba_color const& color) const {
    impl_->context->OMSetRenderTargets(1, rtv.impl_->rtv.GetAddressOf(), nullptr);
    impl_->context->ClearRenderTargetView(rtv.impl_->rtv.Get(), &color.r);
}
```

`rgba_color` is layout-compatible with `const FLOAT[4]` — verified by a `static_assert` in types.hpp.

### 7.3 Type: `d3d_swap_chain`

```cpp
// gfx.hpp
namespace betty::platform {

struct swap_chain_settings {
    uint32_t width;
    uint32_t height;
    // Buffer count, format, scaling, alpha mode, swap effect — all set
    // to sensible defaults inside make_swap_chain.
    // Note: no HWND — the window is passed directly to make_swap_chain.
};

struct d3d_swap_chain {
    // Present the back buffer to the screen (vsynced).
    auto present() const -> void;

    d3d_swap_chain();
    ~d3d_swap_chain();
    d3d_swap_chain(d3d_swap_chain&&) noexcept;
    d3d_swap_chain& operator=(d3d_swap_chain&&) noexcept;
    d3d_swap_chain(d3d_swap_chain const&) = delete;
    d3d_swap_chain& operator=(d3d_swap_chain const&) = delete;

private:
    struct impl;
    std::unique_ptr<impl> impl_;

    friend auto make_swap_chain(d3d_device const&, win32_window const&, swap_chain_settings const&)
        -> std::expected<d3d_swap_chain, std::error_code>;
    friend auto make_render_target_view(d3d_device const&, d3d_swap_chain const&)
        -> std::expected<d3d_render_target_view, std::error_code>;
};

auto make_swap_chain(d3d_device const& device, win32_window const& window, swap_chain_settings const& settings)
    -> std::expected<d3d_swap_chain, std::error_code>;

} // namespace betty::platform
```

#### `make_swap_chain()` implementation:

1. **Create DXGI factory** — `CreateDXGIFactory2(0, ...)`. Use `DXGI_CREATE_FACTORY_DEBUG` in debug builds.
2. **Describe swap chain** (`DXGI_SWAP_CHAIN_DESC1`):
   - `Width` = `settings.width`
   - `Height` = `settings.height`
   - `Format` = `DXGI_FORMAT_R8G8B8A8_UNORM`
   - `BufferCount` = 2
   - `Scaling` = `DXGI_SCALING_NONE`
   - `SwapEffect` = `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`
   - `AlphaMode` = `DXGI_ALPHA_MODE_IGNORE`
   - `SampleDesc` = `{ 1, 0 }`, `Stereo` = `FALSE`, `BufferUsage` = `DXGI_USAGE_RENDER_TARGET_OUTPUT`, `Flags` = 0
3. Call `factory->CreateSwapChainForHwnd(device.impl_->device.Get(), window.handle_, &desc, nullptr, nullptr, &swap_chain)`.
   - Accesses `device.impl_->device` via friendship.
   - Accesses `window.handle_` via friendship with `win32_window`.
4. On failure, `make_d3d_error(hr)`.
5. Return `d3d_swap_chain` wrapping the swap chain.

#### `present()` implementation:

```cpp
void d3d_swap_chain::present() const {
    impl_->swap_chain->Present(1, 0);  // vsync on
}
```

### 7.4 Type: `d3d_render_target_view`

```cpp
// gfx.hpp
namespace betty::platform {

struct d3d_render_target_view {
    // Opaque handle. No public methods needed for task 1;
    // d3d_device::clear() accesses it via friendship.

    d3d_render_target_view();
    ~d3d_render_target_view();
    d3d_render_target_view(d3d_render_target_view&&) noexcept;
    d3d_render_target_view& operator=(d3d_render_target_view&&) noexcept;
    d3d_render_target_view(d3d_render_target_view const&) = delete;
    d3d_render_target_view& operator=(d3d_render_target_view const&) = delete;

private:
    struct impl;
    std::unique_ptr<impl> impl_;

    friend auto make_render_target_view(d3d_device const&, d3d_swap_chain const&)
        -> std::expected<d3d_render_target_view, std::error_code>;
    friend struct d3d_device;  // needs impl_->rtv for OMSetRenderTargets / ClearRenderTargetView
};

auto make_render_target_view(d3d_device const& device, d3d_swap_chain const& swap_chain)
    -> std::expected<d3d_render_target_view, std::error_code>;

} // namespace betty::platform
```

#### `make_render_target_view()` implementation:

1. Get the back buffer: `swap_chain.impl_->swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))`.
2. Create RTV: `device.impl_->device->CreateRenderTargetView(back_buffer.Get(), nullptr, &rtv)`.
3. On failure, `make_d3d_error(hr)`.
4. Return `d3d_render_target_view` wrapping the RTV.

---

## 8. Application entry point (`main.cpp`)

### 8.1 Overview

`main()` is the only translation unit outside `platform/`. It uses **only** the public types from the platform module:

- `betty::platform::win32_window`
- `betty::platform::d3d_device`
- `betty::platform::d3d_swap_chain`
- `betty::platform::d3d_render_target_view`
- `betty::platform::rgba_color` / `mocha_base`
- `betty::platform::window_dimensions` / `default_window_size`
- `betty::platform::window_settings`
- `betty::platform::swap_chain_settings`
- Free functions: `make_window`, `make_device`, `make_swap_chain`, `make_render_target_view`, `dispatch_pending_messages`

No `#include <windows.h>`, no `#include <d3d11.h>`, no `HWND`, no `ComPtr`, no `MSG`, no `PeekMessageW`. The linker subsystem is `WINDOWS` (set via CMake).

### 8.2 Setup

```cpp
#include <cstdio>
#include <string_view>
#include <system_error>

#include "platform/types.hpp"
#include "platform/window.hpp"
#include "platform/gfx.hpp"

namespace platform = betty::platform;

namespace {

void log_error(std::error_code ec, std::string_view context) {
    // Format: "betty: <context>: <message> (<category>:<code>)\n"
    auto msg = ec.message();
    std::fprintf(stderr, "betty: %.*s: %s (%s:%d)\n",
                 static_cast<int>(context.size()), context.data(),
                 msg.c_str(),
                 ec.category().name(),
                 ec.value());
}

} // anonymous namespace

int main() {
    // 1. Create window
    auto window_result = platform::make_window(
        platform::window_settings{
            .size = platform::default_window_size,
            .title = L"betty"
        }
    );
    if (!window_result) {
        log_error(window_result.error(), "create window");
        return 1;
    }
    auto& window = *window_result;

    // 2. Create D3D11 device
    auto device_result = platform::make_device();
    if (!device_result) {
        log_error(device_result.error(), "create device");
        return 1;
    }
    auto& device = *device_result;

    // 3. Create swap chain
    auto swap_chain_result = platform::make_swap_chain(
        device,
        window,
        platform::swap_chain_settings{
            .width = platform::default_window_size.width,
            .height = platform::default_window_size.height
        }
    );
    if (!swap_chain_result) {
        log_error(swap_chain_result.error(), "create swap chain");
        return 1;
    }
    auto& swap_chain = *swap_chain_result;

    // 4. Create render target view
    auto rtv_result = platform::make_render_target_view(device, swap_chain);
    if (!rtv_result) {
        log_error(rtv_result.error(), "create render target view");
        return 1;
    }
    auto& rtv = *rtv_result;

    // 5. Message loop (Pattern A: render on idle)
    while (platform::dispatch_pending_messages()) {
        device.clear(rtv, platform::mocha_base);
        swap_chain.present();
    }

    return 0;
}
```

### 8.3 Cleanup

- `dispatch_pending_messages()` returns `false` when `WM_QUIT` is posted by `WndProc` (in response to `WM_CLOSE` / `WM_DESTROY`).
- The loop exits, stack unwinds, destructors run in reverse order:
  1. `d3d_render_target_view` destroyed → `ComPtr<ID3D11RenderTargetView>` released.
  2. `d3d_swap_chain` destroyed → `ComPtr<IDXGISwapChain1>` released.
  3. `d3d_device` destroyed → `ComPtr<ID3D11DeviceContext>` and `ComPtr<ID3D11Device>` released.
  4. `win32_window` destroyed → `DestroyWindow` called.
- This order is correct: D3D resources outlive the window, so no "device lost" issues on cleanup.

---

## 9. Constraints and invariants

- Window is **not resizable** (`WS_THICKFRAME` removed, `WS_MAXIMIZEBOX` removed).
- No DPI awareness (system default).
- No `WM_PAINT` handling — D3D `Present` handles display.
- Debug builds enable the D3D11 debug layer; release builds do not.
- Swap chain buffer count is 2; `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`.
- VSync is on (`Present(1, 0)`).
- Window class registered with `IDI_APPLICATION` icon and `IDC_ARROW` cursor.
- **Zero Win32 or DirectX types** are visible in any public header. `main.cpp` compiles cleanly without `<windows.h>`, `<d3d11.h>`, or `<dxgi.h>`.
- `HINSTANCE` is obtained internally via `GetModuleHandleW(nullptr)` — not passed by the consumer.
- `HWND` is forward-declared in `window.hpp`; the full `<windows.h>` is only included in `window.cpp`.
- COM types are hidden behind PIMPL; `ComPtr<T>` is only used in `.cpp` files.

---

## 10. Testing / verification

Manual verification:
1. Launch `betty.exe` (Debug and Release).
2. Confirm the window opens at ~960×600 (client area, title bar adds ~30px height).
3. Confirm the client area is filled with `#1e1e2e` (no white flash, no black borders).
4. Confirm the window has a standard title bar reading "betty".
5. Click the close button (X) — window closes, process exits cleanly (return code 0).
6. In debug builds, check Visual Studio Output window for D3D11 debug layer messages — there should be no warnings or errors.
7. Confirm no console window appears alongside the betty window.
8. Press Alt+F4 — window closes, process exits cleanly.

Compile-time verification:
9. `main.cpp` must not need `#include <windows.h>`, `<d3d11.h>`, `<dxgi.h>`, or `<wrl/client.h>`. Verify by attempting to use `HWND` or `ComPtr` in `main.cpp` — it should fail to compile.

---

## 11. Dependencies

```
(none) ──► TASK-1 ──► TASK-2
```

Task 1 has no dependencies on prior tasks. It is the foundation for all subsequent work.

---

## 12. Files to create (summary)

| File | Purpose |
|---|---|
| `CMakeLists.txt` | Root project definition |
| `CMakePresets.json` | Debug + Release build presets |
| `src/CMakeLists.txt` | Executable + platform subdirectory |
| `src/platform/CMakeLists.txt` | Static library target |
| `src/platform/types.hpp` | `rgba_color`, `window_dimensions`, constants. No Win32/DX includes. |
| `src/platform/error.hpp` | Error category declarations, `make_win32_error`, `make_d3d_error`. Includes `<system_error>`. |
| `src/platform/error.cpp` | Error category implementations. Includes `<windows.h>`, `<d3d11.h>`. |
| `src/platform/window.hpp` | `win32_window`, `window_settings`, `make_window`, `dispatch_pending_messages`. Forward-declares `HWND`. No `<windows.h>`. |
| `src/platform/window.cpp` | Window class registration, `CreateWindowExW`, `WndProc`, `dispatch_pending_messages`. Includes `<windows.h>`. |
| `src/platform/gfx.hpp` | `d3d_device`, `d3d_swap_chain`, `d3d_render_target_view`, `swap_chain_settings`, factories. PIMPL — no COM includes. |
| `src/platform/gfx.cpp` | PIMPL `impl` definitions, device/swap-chain/RTV creation, `clear()`, `present()`. Includes `<d3d11.h>`, `<dxgi.h>`, `<wrl/client.h>`. |
| `src/main.cpp` | Entry point, setup, message loop. Includes only platform headers. No `<windows.h>`. |

---

## 13. Friendship diagram

```
                        ┌──────────────────┐
                        │   win32_window   │
                        │  - HWND handle_  │
                        └───┬──────────┬───┘
                            │ friend   │ friend
                            ▼          ▼
                    make_window    make_swap_chain
                                       │
    ┌──────────────────┐              │
    │   d3d_device     │◄── friend ───┘
    │  - impl (PIMPL)  │
    │  + clear()       │◄── friend ── make_render_target_view
    └──┬───────────────┘              │
       │ friend                       │
       ▼                              │
    make_device                       │
                                      │
    ┌──────────────────┐              │
    │  d3d_swap_chain  │◄── friend ───┘
    │  - impl (PIMPL)  │
    │  + present()     │
    └──────────────────┘
       ▲ friend
       │
    make_swap_chain ── friend ──► win32_window (needs handle_ for CreateSwapChainForHwnd)

    ┌──────────────────────────┐
    │ d3d_render_target_view   │
    │  - impl (PIMPL)          │
    └──────────────────────────┘
       ▲ friend                 ▲ friend
       │                        │
    make_render_target_view    d3d_device (needs rtv for clear())
```

All cross-type access (e.g., `make_swap_chain` reading `win32_window::handle_` and `d3d_device::impl_->device`) is mediated through `friend` declarations. Consumers never touch private members.
