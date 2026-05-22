#include "vt_parser.hpp"
#include <charconv>
#include <string_view>

namespace betty::terminal {

// ===========================================================================
// internal helpers
// ===========================================================================

void vt_parser::reset_csi() {
  state_ = state::ground;
  param_buffer_.clear();
}

// Parse the accumulated parameter buffer into two integers.
// Empty segments and 0 are both treated as 1 (ANSI default).
auto vt_parser::parse_params() -> std::pair<uint32_t, uint32_t> {
  uint32_t p1 = 1;
  uint32_t p2 = 1;

  if (param_buffer_.empty()) {
    return {p1, p2};
  }

  auto const semi_pos = param_buffer_.find(';');
  std::string_view const first =
    std::string_view(param_buffer_).substr(0, semi_pos);

  auto parse_uint = [](std::string_view s) -> uint32_t {
    if (s.empty()) return 1;
    uint32_t val = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), val);
    if (result.ec != std::errc{}) return 1;
    return (val == 0) ? 1 : val;
  };

  p1 = parse_uint(first);

  if (semi_pos != std::string::npos) {
    std::string_view const second =
      std::string_view(param_buffer_).substr(semi_pos + 1);
    p2 = parse_uint(second);
  }

  return {p1, p2};
}

// ===========================================================================
// dispatch — produce an action from a CSI final byte + parameter buffer
// ===========================================================================

auto vt_parser::dispatch(char const final_byte) -> std::optional<action> {
  auto const [p1, p2] = parse_params();

  switch (final_byte) {
  case 'A': { // CUU — Cursor Up
    action a{};
    a.type  = action_type::move_cursor_up;
    a.count = p1;
    tracked_cursor_row_ =
      (tracked_cursor_row_ > p1) ? tracked_cursor_row_ - p1 : 0;
    return a;
  }
  case 'B': { // CUD — Cursor Down
    action a{};
    a.type  = action_type::move_cursor_down;
    a.count = p1;
    tracked_cursor_row_ += p1;
    return a;
  }
  case 'C': { // CUF — Cursor Forward
    action a{};
    a.type  = action_type::move_cursor_forward;
    a.count = p1;
    tracked_cursor_col_ += p1;
    return a;
  }
  case 'D': { // CUB — Cursor Back
    action a{};
    a.type  = action_type::move_cursor_back;
    a.count = p1;
    tracked_cursor_col_ =
      (tracked_cursor_col_ > p1) ? tracked_cursor_col_ - p1 : 0;
    return a;
  }
  case 'H':   // CUP — Cursor Position
  case 'f': { // HVP — Horizontal Vertical Position
    action a{};
    a.type = action_type::move_cursor;
    // Convert from 1-based to 0-based.
    a.row = (p1 > 0) ? p1 - 1 : 0;
    a.col = (p2 > 0) ? p2 - 1 : 0;
    tracked_cursor_row_ = a.row;
    tracked_cursor_col_ = a.col;
    return a;
  }
  default:
    // Unrecognised final byte → discard the whole sequence.
    return std::nullopt;
  }
}

// ===========================================================================
// parse — state machine entry point
// ===========================================================================

auto vt_parser::parse(unsigned char const byte) -> std::optional<action> {
  switch (state_) {

  // -------------------------------------------------------------------
  // GROUND — normal text
  // -------------------------------------------------------------------
  case state::ground:
    switch (byte) {
    case '\r':
      return action{action_type::carriage_return};
    case '\n':
      return action{action_type::newline};
    case 0x1B:   // ESC
      state_ = state::escape;
      return std::nullopt;
    default:
      if (byte >= 0x20) {
        action a{};
        a.type       = action_type::write_char;
        a.codepoint  = static_cast<char32_t>(byte);
        tracked_cursor_col_++;
        return a;
      }
      // Other C0 controls are silently ignored.
      return std::nullopt;
    }

  // -------------------------------------------------------------------
  // ESCAPE — just saw ESC
  // -------------------------------------------------------------------
  case state::escape:
    switch (byte) {
    case '[':   // CSI introducer
      state_ = state::csi_entry;
      param_buffer_.clear();
      return std::nullopt;
    case '7': { // DECSC — Save Cursor
      saved_cursor_row_ = tracked_cursor_row_;
      saved_cursor_col_ = tracked_cursor_col_;
      state_ = state::ground;
      return std::nullopt;
    }
    case '8': { // DECRC — Restore Cursor
      state_ = state::ground;
      action a{};
      a.type = action_type::move_cursor;
      a.row  = saved_cursor_row_;
      a.col  = saved_cursor_col_;
      tracked_cursor_row_ = a.row;
      tracked_cursor_col_ = a.col;
      return a;
    }
    default:
      // Unknown escape sequence — discard.
      state_ = state::ground;
      return std::nullopt;
    }

  // -------------------------------------------------------------------
  // CSI_ENTRY — just saw ESC [
  // -------------------------------------------------------------------
  case state::csi_entry:
    if (byte >= 0x30 && byte <= 0x3F) {
      param_buffer_ += static_cast<char>(byte);
      state_ = state::csi_param;
    } else if (byte >= 0x20 && byte <= 0x2F) {
      state_ = state::csi_intermediate;
    } else if (byte >= 0x40 && byte <= 0x7E) {
      auto result = dispatch(static_cast<char>(byte));
      reset_csi();
      return result;
    } else {
      reset_csi();  // invalid byte → discard CSI sequence
    }
    return std::nullopt;

  // -------------------------------------------------------------------
  // CSI_PARAM — accumulating parameter bytes
  // -------------------------------------------------------------------
  case state::csi_param:
    if (byte >= 0x30 && byte <= 0x3F) {
      param_buffer_ += static_cast<char>(byte);
      // stay in CSI_PARAM
    } else if (byte >= 0x20 && byte <= 0x2F) {
      state_ = state::csi_intermediate;
    } else if (byte >= 0x40 && byte <= 0x7E) {
      auto result = dispatch(static_cast<char>(byte));
      reset_csi();
      return result;
    } else {
      reset_csi();
    }
    return std::nullopt;

  // -------------------------------------------------------------------
  // CSI_INTERMEDIATE — saw intermediate byte(s)
  // -------------------------------------------------------------------
  case state::csi_intermediate:
    if (byte >= 0x20 && byte <= 0x2F) {
      // stay in CSI_INTERMEDIATE (intermediate bytes are ignored)
    } else if (byte >= 0x40 && byte <= 0x7E) {
      auto result = dispatch(static_cast<char>(byte));
      reset_csi();
      return result;
    } else {
      reset_csi();
    }
    return std::nullopt;
  }

  return std::nullopt;
}

} // namespace betty::terminal
