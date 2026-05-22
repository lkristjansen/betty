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

// Split semicolon-delimited parameter buffer into a vector of uint32_t.
// Empty tokens become 0 (ANSI convention: missing param = 0).
static auto split_params(std::string_view s) -> std::vector<uint32_t> {
  std::vector<uint32_t> result;
  size_t pos = 0;
  while (pos <= s.size()) {
    size_t const semi = s.find(';', pos);
    std::string_view const token = s.substr(pos, semi - pos);
    if (token.empty()) {
      result.push_back(0);
    } else {
      uint32_t val = 0;
      auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), val);
      result.push_back((ec == std::errc{}) ? val : 0);
    }
    if (semi == std::string::npos) break;
    pos = semi + 1;
  }
  // Empty buffer → implicit 0 (ANSI default for SGR).
  if (result.empty()) result.push_back(0);
  return result;
}

// ===========================================================================
// dispatch — produce actions from a CSI final byte + parameter buffer
// ===========================================================================

auto vt_parser::dispatch(char const final_byte) -> std::vector<action> {
  if (final_byte == 'm') {
    // ── SGR ────────────────────────────────────────────────────────────
    std::vector<action> actions;
    auto const params = split_params(param_buffer_);
    size_t i = 0;

    auto consume_38_48 = [&](bool is_bg) {
      if (i + 1 >= params.size()) return;
      uint32_t const mode = params[i + 1];
      if (mode == 2 && i + 4 < params.size()) {
        // 38;2;R;G;B  or  48;2;R;G;B
        action a{};
        a.type = is_bg ? action_type::sgr_set_bg : action_type::sgr_set_fg;
        a.color = {
          static_cast<uint8_t>(std::min(params[i + 2], 255u)),
          static_cast<uint8_t>(std::min(params[i + 3], 255u)),
          static_cast<uint8_t>(std::min(params[i + 4], 255u)),
          0};
        actions.push_back(a);
        i += 5;
      } else if (mode == 5 && i + 2 < params.size()) {
        // 38;5;N  or  48;5;N
        action a{};
        a.type = is_bg ? action_type::sgr_set_bg : action_type::sgr_set_fg;
        a.color = xterm_256_color(static_cast<uint8_t>(std::min(params[i + 2], 255u)));
        actions.push_back(a);
        i += 3;
      } else {
        // Malformed extended colour — consume what we can and move on.
        i += 2;
      }
    };

    while (i < params.size()) {
      uint32_t const n = params[i];

      if (n == 0) {
        // Reset all.
        actions.push_back(action{action_type::sgr_reset});
        ++i;
      } else if (n >= 30 && n <= 37) {
        actions.push_back(action{action_type::sgr_set_fg, 0, 0, 0, 0,
                                  catppuccin_palette[n - 30]});
        ++i;
      } else if (n >= 40 && n <= 47) {
        actions.push_back(action{action_type::sgr_set_bg, 0, 0, 0, 0,
                                  catppuccin_palette[n - 40]});
        ++i;
      } else if (n >= 90 && n <= 97) {
        actions.push_back(action{action_type::sgr_set_fg, 0, 0, 0, 0,
                                  catppuccin_palette[(n - 90) + 8]});
        ++i;
      } else if (n >= 100 && n <= 107) {
        actions.push_back(action{action_type::sgr_set_bg, 0, 0, 0, 0,
                                  catppuccin_palette[(n - 100) + 8]});
        ++i;
      } else if (n == 38) {
        consume_38_48(false);  // advances i
      } else if (n == 48) {
        consume_38_48(true);   // advances i
      } else if (n == 39) {
        actions.push_back(action{action_type::sgr_set_fg, 0, 0, 0, 0,
                                  default_fg()});
        ++i;
      } else if (n == 49) {
        actions.push_back(action{action_type::sgr_set_bg, 0, 0, 0, 0,
                                  default_bg()});
        ++i;
      } else {
        // Non-colour SGR (1-9, 21-29, etc.) — silently ignore (for Task 12).
        ++i;
      }
    }
    return actions;
  }

  // ── Cursor movement (existing) ───────────────────────────────────────
  auto const [p1, p2] = parse_params();

  switch (final_byte) {
  case 'A': { // CUU — Cursor Up
    action a{};
    a.type  = action_type::move_cursor_up;
    a.count = p1;
    tracked_cursor_row_ =
      (tracked_cursor_row_ > p1) ? tracked_cursor_row_ - p1 : 0;
    return {a};
  }
  case 'B': { // CUD — Cursor Down
    action a{};
    a.type  = action_type::move_cursor_down;
    a.count = p1;
    tracked_cursor_row_ += p1;
    return {a};
  }
  case 'C': { // CUF — Cursor Forward
    action a{};
    a.type  = action_type::move_cursor_forward;
    a.count = p1;
    tracked_cursor_col_ += p1;
    return {a};
  }
  case 'D': { // CUB — Cursor Back
    action a{};
    a.type  = action_type::move_cursor_back;
    a.count = p1;
    tracked_cursor_col_ =
      (tracked_cursor_col_ > p1) ? tracked_cursor_col_ - p1 : 0;
    return {a};
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
    return {a};
  }
  default:
    // Unrecognised final byte → discard the whole sequence.
    return {};
  }
}

// ===========================================================================
// parse — state machine entry point
// ===========================================================================

auto vt_parser::parse(unsigned char const byte) -> std::vector<action> {
  switch (state_) {

  // -------------------------------------------------------------------
  // GROUND — normal text
  // -------------------------------------------------------------------
  case state::ground:
    switch (byte) {
    case '\r':
      return {action{action_type::carriage_return}};
    case '\n':
      return {action{action_type::newline}};
    case 0x1B:   // ESC
      state_ = state::escape;
      return {};
    default:
      if (byte >= 0x20) {
        action a{};
        a.type       = action_type::write_char;
        a.codepoint  = static_cast<char32_t>(byte);
        tracked_cursor_col_++;
        return {a};
      }
      // Other C0 controls are silently ignored.
      return {};
    }

  // -------------------------------------------------------------------
  // ESCAPE — just saw ESC
  // -------------------------------------------------------------------
  case state::escape:
    switch (byte) {
    case '[':   // CSI introducer
      state_ = state::csi_entry;
      param_buffer_.clear();
      return {};
    case '7': { // DECSC — Save Cursor
      saved_cursor_row_ = tracked_cursor_row_;
      saved_cursor_col_ = tracked_cursor_col_;
      state_ = state::ground;
      return {};
    }
    case '8': { // DECRC — Restore Cursor
      state_ = state::ground;
      action a{};
      a.type = action_type::move_cursor;
      a.row  = saved_cursor_row_;
      a.col  = saved_cursor_col_;
      tracked_cursor_row_ = a.row;
      tracked_cursor_col_ = a.col;
      return {a};
    }
    default:
      // Unknown escape sequence — discard.
      state_ = state::ground;
      return {};
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
    return {};

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
    return {};

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
    return {};
  }

  return {};
}

} // namespace betty::terminal
