#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "types.hpp"

namespace betty::terminal {

// ===========================================================================
// vt_bytes — byte classification helpers for VT/ANSI parsing
// ===========================================================================

namespace vt_bytes {

constexpr bool is_c0_control(unsigned char b)     { return b < 0x20; }
constexpr bool is_del(unsigned char b)            { return b == 0x7F; }
constexpr bool is_esc(unsigned char b)            { return b == 0x1B; }
constexpr bool is_bel(unsigned char b)            { return b == 0x07; }
constexpr bool is_bs(unsigned char b)             { return b == 0x08; }
constexpr bool is_cr(unsigned char b)             { return b == '\r'; }
constexpr bool is_lf(unsigned char b)             { return b == '\n'; }
constexpr bool is_ascii_printable(unsigned char b) { return b >= 0x20 && b < 0x7F; }

constexpr bool is_utf8_lead(unsigned char b)      { return b >= 0xC0 && b <= 0xF4; }
constexpr bool is_utf8_continuation(unsigned char b) { return (b & 0xC0u) == 0x80u; }

// Parameter bytes per ECMA-48 are 0x30–0x3B (0-9 and ;).  We also accept
// 0x3C–0x3F (< = > ?) to accommodate terminal emulators that use '?' as a
// parameter character (e.g. SGR with '?' interpretation).
// NOTE: DEC private modes (CSI ? … h/l) treat '?' as an intermediate byte,
// not a parameter byte.  If private-mode support is added, this range should
// be narrowed to 0x30–0x3B and '?' handled in csi_entry/csi_intermediate instead.
constexpr bool is_csi_param(unsigned char b)      { return b >= 0x30 && b <= 0x3F; }
constexpr bool is_csi_intermediate(unsigned char b) { return b >= 0x20 && b <= 0x2F; }
constexpr bool is_csi_final(unsigned char b)      { return b >= 0x40 && b <= 0x7E; }

} // namespace vt_bytes

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
// cursor_pos — absolute cursor position (row, col, 0-based)
// ===========================================================================

struct cursor_pos {
  uint32_t row = 0;
  uint32_t col = 0;
};

// ===========================================================================
// action — a single terminal operation
// ===========================================================================

using action_payload = std::variant<
  std::monostate,       // carriage_return, newline, save/restore_cursor, sgr_reset
  char32_t,             // write_char codepoint
  uint32_t,             // count (moves), mode (erase), attr mask (sgr_set/clear_attr)
  cursor_pos,           // move_cursor, set_scroll_region
  terminal_color,       // sgr_set_fg, sgr_set_bg
  std::string           // set_window_title
>;

struct action {
  action_type type;
  action_payload payload;
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

  // Returned by each state handler: should parse() return output_ to the
  // caller, or loop and re-process the same byte in the new state?
  enum class handler_result : uint8_t { done, reprocess };

  // ---- UTF-8 decoder --------------------------------------------------

  class utf8_decoder {
  public:
    enum class result : uint8_t { incomplete, complete, error };

    // Start decoding from a valid lead byte (0xC0–0xF4).
    void start(unsigned char lead_byte);

    // Feed a continuation byte (0x80–0xBF).
    [[nodiscard]] auto continue_byte(unsigned char byte) -> result;

    void reset();
    [[nodiscard]] auto codepoint() const -> char32_t { return codepoint_; }

  private:
    char32_t codepoint_ = 0;
    uint8_t  remaining_ = 0;
  };

  // -------------------------------------------------------------------

  void reset_csi();
  void dispatch(char final_byte);
  void dispatch_osc();
  [[nodiscard]] auto split_params() -> std::span<const uint32_t>;

  // Per-CSI-final-byte dispatch methods.
  void dispatch_sgr();
  // Helper for dispatch_sgr: consume 38;5;N or 38;2;R;G;B extended colour.
  // Returns number of param slots consumed (5 for RGB, 3 for 256-colour, 2 for malformed).
  [[nodiscard]] auto dispatch_sgr_extended(std::span<const uint32_t> params, size_t i, bool is_bg) -> size_t;
  void dispatch_ed();
  void dispatch_el();
  void dispatch_il();
  void dispatch_dl();
  void dispatch_su();
  void dispatch_sd();
  void dispatch_decstbm();
  void dispatch_ich();
  void dispatch_dch();
  void dispatch_ech();
  void dispatch_cursor(char final_byte);

  // Per-state handlers — one for each state in the VT state machine.
  [[nodiscard]] auto handle_ground(unsigned char byte) -> handler_result;
  [[nodiscard]] auto handle_utf8_accum(unsigned char byte) -> handler_result;
  [[nodiscard]] auto handle_escape(unsigned char byte) -> handler_result;
  [[nodiscard]] auto handle_csi_entry(unsigned char byte) -> handler_result;
  [[nodiscard]] auto handle_csi_param(unsigned char byte) -> handler_result;
  [[nodiscard]] auto handle_csi_intermediate(unsigned char byte) -> handler_result;
  [[nodiscard]] auto handle_osc(unsigned char byte) -> handler_result;
  [[nodiscard]] auto handle_osc_esc(unsigned char byte) -> handler_result;

  state state_ = state::ground;
  std::string param_buffer_;  // collects CSI parameter bytes (digits, ';')
  std::string osc_buffer_;    // collects OSC string (max 1024 bytes)

  // Reusable output buffers (avoid per-call heap allocations).
  std::vector<action> output_;           // actions produced by the current parse()
  std::vector<uint32_t> param_values_;   // split CSI parameter values

  utf8_decoder utf8_;
};

} // namespace betty::terminal
