#pragma once
#include <array>
#include <cstdint>

namespace betty::terminal {

// ===========================================================================
// cell_attr — text attribute bitmask (bold, italic, etc.)
// ===========================================================================

enum class cell_attr : uint8_t {
  none          = 0,
  bold          = 1 << 0,
  italic        = 1 << 1,
  faint         = 1 << 2,
  underline     = 1 << 3,
  strikethrough = 1 << 4,
  reverse       = 1 << 5,
  wide          = 1 << 6,  // first cell of a 2-cell-wide character
  wide_tail     = 1 << 7,  // continuation cell (right half of wide char)
};

inline constexpr auto operator|(cell_attr a, cell_attr b) -> cell_attr {
  return static_cast<cell_attr>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline constexpr auto operator|=(cell_attr& a, cell_attr b) -> cell_attr& {
  a = a | b; return a;
}
inline constexpr auto operator&(cell_attr a, cell_attr b) -> cell_attr {
  return static_cast<cell_attr>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline constexpr auto operator~(cell_attr a) -> cell_attr {
  return static_cast<cell_attr>(~static_cast<uint8_t>(a));
}

inline constexpr auto to_uint8(cell_attr a) -> uint8_t {
  return static_cast<uint8_t>(a);
}

// ===========================================================================
// terminal_color — packed 8-bit-per-channel colour + flags
// ===========================================================================

struct terminal_color {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t flags = 0;

  static constexpr uint8_t k_default_flag = 1;  // bit within flags: use terminal default

  [[nodiscard]] constexpr bool is_default() const noexcept {
    return (flags & k_default_flag) != 0;
  }
};

// Sentinel constructors for "default" colours.
constexpr auto default_fg() -> terminal_color { return {0, 0, 0, terminal_color::k_default_flag}; }
constexpr auto default_bg() -> terminal_color { return {0, 0, 0, terminal_color::k_default_flag}; }

// ===========================================================================
// Catppuccin Mocha ANSI palette (16 standard colours)
// ===========================================================================

inline constexpr std::array<terminal_color, 16> catppuccin_palette = {{
  /*  0 */ {0x45, 0x47, 0x5a, 0},  // surface1
  /*  1 */ {0xf3, 0x8b, 0xa8, 0},  // red
  /*  2 */ {0xa6, 0xe3, 0xa1, 0},  // green
  /*  3 */ {0xf9, 0xe2, 0xaf, 0},  // yellow
  /*  4 */ {0x89, 0xb4, 0xfa, 0},  // blue
  /*  5 */ {0xcb, 0xa6, 0xf7, 0},  // mauve
  /*  6 */ {0x94, 0xe2, 0xd5, 0},  // teal
  /*  7 */ {0xba, 0xc2, 0xde, 0},  // subtext1
  /*  8 */ {0x58, 0x5b, 0x70, 0},  // surface2
  /*  9 */ {0xf3, 0x8b, 0xa8, 0},  // red
  /* 10 */ {0xa6, 0xe3, 0xa1, 0},  // green
  /* 11 */ {0xf9, 0xe2, 0xaf, 0},  // yellow
  /* 12 */ {0x89, 0xb4, 0xfa, 0},  // blue
  /* 13 */ {0xf5, 0xc2, 0xe7, 0},  // pink
  /* 14 */ {0x94, 0xe2, 0xd5, 0},  // teal
  /* 15 */ {0xa6, 0xad, 0xc8, 0},  // subtext0
}};

// ===========================================================================
// 256-colour lookup (xterm palette)
// ===========================================================================

constexpr auto xterm_256_color(uint8_t index) -> terminal_color {
  if (index < 16) return catppuccin_palette[index];
  if (index < 232) {
    index -= 16;
    uint8_t const r = (index / 36) * 51;
    uint8_t const g = ((index / 6) % 6) * 51;
    uint8_t const b = (index % 6) * 51;
    return {r, g, b, 0};
  }
  uint8_t const gray = (index - 232) * 10 + 8;
  return {gray, gray, gray, 0};
}

// ===========================================================================
// grid_cell — a single character cell in the terminal grid
// ===========================================================================

struct grid_cell {
  char32_t codepoint = U' ';         // default: space
  terminal_color fg = default_fg();  // foreground colour
  terminal_color bg = default_bg();  // background colour
  cell_attr attr = cell_attr::none;  // text attributes (bold, italic, etc.)
};

} // namespace betty::terminal
