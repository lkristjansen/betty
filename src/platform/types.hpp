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
