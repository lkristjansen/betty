#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace betty::platform {

// ===========================================================================
// Normalised float [0.0, 1.0] — always clamps out-of-range values.
// ===========================================================================

class normalized_float {
public:
  constexpr normalized_float() noexcept : value_(0.0f) {}

  explicit constexpr normalized_float(float v) : value_(std::clamp(v, 0.0f, 1.0f)) {}

  [[nodiscard]] constexpr explicit operator float() const noexcept { return value_; }
  [[nodiscard]] constexpr float get() const noexcept { return value_; }

private:
  float value_;
};

// ===========================================================================
// RGBA colour — stores raw floats in a contiguous array for DirectX.
// Layout-verified at compile time.
// ===========================================================================

struct rgba_color {
  constexpr rgba_color(normalized_float r, normalized_float g,
                       normalized_float b, normalized_float a)
    : data_{ r.get(), g.get(), b.get(), a.get() } {}

  [[nodiscard]] constexpr auto operator[](std::size_t i) const noexcept -> float { return data_[i]; }
  // Raw pointer for DirectX calls (e.g. ClearRenderTargetView).
  [[nodiscard]] constexpr auto data() const noexcept -> float const* { return data_.data(); }

private:
  std::array<float, 4> data_;
};

static_assert(sizeof(float) == 4);
static_assert(std::is_standard_layout_v<rgba_color>);
static_assert(sizeof(rgba_color) == 4 * sizeof(float));

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
// 8-bit-per-channel RGB colour (no flags, no alpha).
// Used by render_cell to carry resolved foreground/background colours.
// ===========================================================================

struct rgb_color {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

// Resolved default terminal colours (Catppuccin Mocha).
inline constexpr rgb_color k_default_fg_color = {0xCD, 0xD6, 0xF4};  // text
inline constexpr rgb_color k_default_bg_color = {0x1E, 0x1E, 0x2E};  // base

// ===========================================================================
// Generic 2D size (cols/rows, width/height).
// ===========================================================================

struct size2d {
  std::uint32_t width  = 0;
  std::uint32_t height = 0;
};

// ===========================================================================
// Generic 2D point (row/col, y/x).
// ===========================================================================

struct point2d {
  std::uint32_t row = 0;
  std::uint32_t col = 0;
};

// ===========================================================================
// Render cell — a single cell ready for the glyph renderer.
// Colours are fully resolved (no default-colour sentinel).
// ===========================================================================

struct render_cell {
  char32_t codepoint = U' ';
  rgb_color fg;
  rgb_color bg;
  uint8_t attr = 0;  // terminal::cell_attr bitmask
};

// ===========================================================================
// Window client-area dimensions in logical pixels.
// ===========================================================================

struct window_dimensions {
  std::uint32_t width;
  std::uint32_t height;
};

// Default window size.
inline constexpr window_dimensions default_window_size{ 960u, 600u };

// Uniform padding inside the terminal window (px on each side).
inline constexpr uint32_t k_padding_px = 8;

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

// ===========================================================================
// Abstract virtual-key codes (decoupled from winuser.h constants)
// ===========================================================================

enum class vk_code : uint32_t {
  unknown        = 0,
  // Control keys
  enter          = 0x01,
  backspace      = 0x02,
  tab            = 0x03,
  escape         = 0x04,
  space          = 0x05,
  // Navigation
  arrow_up       = 0x10,
  arrow_down     = 0x11,
  arrow_left     = 0x12,
  arrow_right    = 0x13,
  home           = 0x14,
  end_           = 0x15,
  page_up        = 0x16,
  page_down      = 0x17,
  delete_        = 0x18,
  insert_        = 0x19,
  // Function keys (placed above 0xFF to avoid collision with printable ASCII)
  f1             = 0x100,
  f2             = 0x101,
  f3             = 0x102,
  f4             = 0x103,
  f5             = 0x104,
  f6             = 0x105,
  f7             = 0x106,
  f8             = 0x107,
  f9             = 0x108,
  f10            = 0x109,
  f11            = 0x10A,
  f12            = 0x10B,
  // Printable ASCII range (codepoint == ASCII value) — used only for
  // Ctrl+letter control-code generation in input_handler::on_keydown().
  // All other printable characters arrive through the WM_CHAR path.
  printable_a    = 'a',
  printable_z    = 'z',
  printable_0    = '0',
  printable_9    = '9',
};

} // namespace betty::platform
