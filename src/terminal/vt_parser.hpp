#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "types.hpp"

namespace betty::terminal {

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
  terminal_color color{};  // payload for sgr_set_fg / sgr_set_bg
  std::string title{};     // payload for set_window_title (255 chars max)
};

// ===========================================================================
// vt_parser — minimal VT/ANSI escape-sequence parser
// ===========================================================================

class vt_parser {
public:
  // Feed one byte.  Returns zero or more actions for each complete sequence.
  // The returned span refers to internal storage that is invalidated on the next call.
  [[nodiscard]] auto parse(unsigned char byte) -> std::span<const action>;

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
  void dispatch(char final_byte);
  void dispatch_osc();
  [[nodiscard]] auto parse_params() -> std::pair<uint32_t, uint32_t>;
  [[nodiscard]] auto split_params() -> std::span<const uint32_t>;

  state state_ = state::ground;
  std::string param_buffer_;  // collects CSI parameter bytes (digits, ';')
  std::string osc_buffer_;    // collects OSC string (max 1024 bytes)

  // Reusable output buffers (avoid per-call heap allocations).
  std::vector<action> output_;           // actions produced by the current parse()
  std::vector<uint32_t> param_values_;   // split CSI parameter values

  // UTF-8 accumulation state.
  struct {
    char32_t codepoint = 0;
    uint8_t  remaining = 0;  // expected continuation bytes
  } utf8_;
};

} // namespace betty::terminal
