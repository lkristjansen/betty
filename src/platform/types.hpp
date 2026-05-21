#pragma once
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <string>

namespace betty::platform {

// ===========================================================================
// Normalised float [0.0, 1.0] — catches out-of-range values at runtime
// (and at compile time for constexpr / literal usage).
// ===========================================================================

class normalized_float {
  // rgba_color needs direct access to value_ for .data() pointer arithmetic.
  friend struct rgba_color;
public:
  constexpr normalized_float() noexcept : value_(0.0f) {}

  explicit constexpr normalized_float(float v) : value_(v) {
    if (v < 0.0f || v > 1.0f) {
#if defined(_DEBUG) || defined(DEBUG)
      throw std::domain_error("normalized_float out of [0, 1] range");
#else
      // In release, clamp rather than crash — but the debug throw
      // should have caught it during development.
      value_ = std::clamp(v, 0.0f, 1.0f);
#endif
    }
  }

  [[nodiscard]] constexpr explicit operator float() const noexcept { return value_; }
  [[nodiscard]] constexpr float get() const noexcept { return value_; }

private:
  float value_;
};

// ===========================================================================
// RGBA colour — layout-verified for ClearRenderTargetView
// ===========================================================================

struct rgba_color {
  normalized_float r, g, b, a;

  // Provide raw pointer for DirectX calls — layout-verified below.
  [[nodiscard]] auto data() const noexcept -> float const* { return &r.value_; }
};

static_assert(sizeof(float) == 4);
static_assert(std::is_standard_layout_v<rgba_color>);
static_assert(sizeof(rgba_color) == 4 * sizeof(float));
static_assert(offsetof(rgba_color, r) == 0);
static_assert(offsetof(rgba_color, g) == sizeof(float));
static_assert(offsetof(rgba_color, b) == 2 * sizeof(float));
static_assert(offsetof(rgba_color, a) == 3 * sizeof(float));

// Compile-time helper: build a colour from 8-bit RGB components.
inline constexpr rgba_color rgba_from_rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                                           normalized_float alpha = normalized_float(1.0f)) {
  return rgba_color{
    normalized_float(static_cast<float>(red)   / 255.0f),
    normalized_float(static_cast<float>(green) / 255.0f),
    normalized_float(static_cast<float>(blue)  / 255.0f),
    alpha
  };
}

// ===========================================================================
// Window client-area dimensions in logical pixels.
// ===========================================================================

struct window_dimensions {
  std::uint32_t width;
  std::uint32_t height;
};

// Default window size.
inline constexpr window_dimensions default_window_size{ 960u, 600u };

// Catppuccin Mocha base colour (#1e1e2e).
inline constexpr rgba_color mocha_base = rgba_from_rgb(30, 30, 46);

// ===========================================================================
// Typed show-command enum (replaces magic number 5 = SW_SHOW).
// ===========================================================================

enum class window_show_command : int {
  hide = 0,    // SW_HIDE
  normal = 1,  // SW_SHOWNORMAL
  show = 5,    // SW_SHOW
  minimize = 6,// SW_MINIMIZE
  restore = 9, // SW_RESTORE
  show_default = 10 // SW_SHOWDEFAULT
};

inline constexpr window_show_command default_show_command = window_show_command::show;

} // namespace betty::platform
