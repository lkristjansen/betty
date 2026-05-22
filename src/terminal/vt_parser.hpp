#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

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
};

// ===========================================================================
// vt_parser — minimal VT/ANSI escape-sequence parser
// ===========================================================================

class vt_parser {
public:
  // Feed one byte.  Returns an action when a complete sequence is recognised,
  // or std::nullopt while waiting for more bytes.
  auto parse(unsigned char byte) -> std::optional<action>;

private:
  enum class state : uint8_t {
    ground,
    escape,
    csi_entry,
    csi_param,
    csi_intermediate,
  };

  void reset_csi();
  auto dispatch(char final_byte) -> std::optional<action>;
  auto parse_params() -> std::pair<uint32_t, uint32_t>;

  state state_ = state::ground;
  std::string param_buffer_;  // collects CSI parameter bytes (digits, ';')

  // Saved-cursor position (for DECSC / DECRC).  Initialised to origin.
  uint32_t saved_cursor_row_ = 0;
  uint32_t saved_cursor_col_ = 0;

  // Tracked cursor position (updated on every emitted action that moves
  // the cursor) so that DECSC captures a meaningful location.
  uint32_t tracked_cursor_row_ = 0;
  uint32_t tracked_cursor_col_ = 0;
};

} // namespace betty::terminal
