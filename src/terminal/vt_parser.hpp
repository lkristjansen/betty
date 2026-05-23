#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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

// ===========================================================================
// rgb_color — packed 8-bit-per-channel colour + flags
// ===========================================================================

struct rgb_color {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t flags = 0;  // bit 0: is_default (1 = use terminal default colour)
};

// Sentinel constructors for "default" colours.
constexpr auto default_fg() -> rgb_color { return {0, 0, 0, 1}; }
constexpr auto default_bg() -> rgb_color { return {0, 0, 0, 1}; }

// Resolved default terminal colours.
inline constexpr rgb_color k_default_fg_color = {0xCD, 0xD6, 0xF4, 0};  // text
inline constexpr rgb_color k_default_bg_color = {0x1E, 0x1E, 0x2E, 0};  // base (reference only)

// ===========================================================================
// Catppuccin Mocha ANSI palette (16 standard colours)
// ===========================================================================

inline constexpr std::array<rgb_color, 16> catppuccin_palette = {{
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

constexpr auto xterm_256_color(uint8_t index) -> rgb_color {
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
// action_type — kind of terminal action produced by the VT parser
// ===========================================================================

enum class action_type : uint8_t {
  write_char,
  carriage_return,
  newline,
  move_cursor,        // absolute row, col (0-based)
  move_cursor_up,     // N cells
  move_cursor_down,
  move_cursor_forward,
  move_cursor_back,
  save_cursor,         // DECSC — save current cursor position
  restore_cursor,      // DECRC — restore saved cursor position
  sgr_reset,          // reset fg/bg to defaults (SGR 0)
  sgr_set_fg,         // set foreground colour
  sgr_set_bg,         // set background colour
  sgr_set_attr,       // turn ON a bitmask of attributes (payload in action::count)
  sgr_clear_attr,     // turn OFF a bitmask of attributes (payload in action::count)
  erase_display,      // ED: clear cells in display (mode in action::count)
  erase_line,         // EL: clear cells in current line (mode in action::count)
  insert_lines,       // IL: insert n blank lines at cursor (count)
  delete_lines,       // DL: delete n lines at cursor (count)
  insert_chars,       // ICH: CSI Ps @ — insert n blank cells at cursor, shift right
  delete_chars,       // DCH: CSI Ps P — delete n cells at cursor, shift left
  erase_chars,        // ECH: CSI Ps X — overwrite n cells at cursor with blanks
  scroll_up_page,     // SU: scroll up within scroll region (count lines)
  scroll_down_page,   // SD: scroll down within scroll region (count lines)
  set_scroll_region,  // DECSTBM: set top/bottom scroll margins (row, col, 1-based)
  set_window_title,   // OSC 0/1/2 — set window title
};

// ===========================================================================
// action — a single terminal operation
// ===========================================================================

struct action {
  action_type type = action_type::write_char;
  char32_t codepoint = 0;  // for write_char
  uint32_t count     = 1;  // for relative moves
  uint32_t row       = 0;  // for move_cursor (absolute, 0-based)
  uint32_t col       = 0;  // for move_cursor
  rgb_color color{};       // payload for sgr_set_fg / sgr_set_bg
  std::string title{};     // payload for set_window_title (255 chars max)
};

// ===========================================================================
// vt_parser — minimal VT/ANSI escape-sequence parser
// ===========================================================================

class vt_parser {
public:
  // Feed one byte.  Returns zero or more actions for each complete sequence.
  auto parse(unsigned char byte) -> std::vector<action>;

private:
  enum class state : uint8_t {
    ground,
    utf8_accum,  // accumulating UTF-8 multi-byte sequence
    escape,
    csi_entry,
    csi_param,
    csi_intermediate,
    osc,         // inside ESC ] … collecting OSC string
    osc_esc,     // saw ESC inside OSC — waiting for \ to confirm ST
  };

  void reset_csi();
  auto dispatch(char final_byte) -> std::vector<action>;
  auto dispatch_osc() -> std::vector<action>;
  auto parse_params() -> std::pair<uint32_t, uint32_t>;

  state state_ = state::ground;
  std::string param_buffer_;  // collects CSI parameter bytes (digits, ';')
  std::string osc_buffer_;    // collects OSC string (max 1024 bytes)

  // UTF-8 accumulation state.
  struct {
    char32_t codepoint = 0;
    uint8_t  remaining = 0;  // expected continuation bytes
  } utf8_;
};

} // namespace betty::terminal
