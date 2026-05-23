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
  osc_buffer_.clear();
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

    // Accumulate attribute changes across the full sequence.
    uint8_t pending_on  = 0;  // attributes to turn ON  (cell_attr bitmask)
    uint8_t pending_off = 0;  // attributes to turn OFF

    auto consume_38_48 = [&](bool is_bg) {
      if (i + 1 >= params.size()) return;
      uint32_t const mode = params[i + 1];
      if (mode == 2 && i + 4 < params.size()) {
        // 38;2;R;G;B  or  48;2;R;G;B
        actions.push_back(action{
          .type = is_bg ? action_type::sgr_set_bg : action_type::sgr_set_fg,
          .color = {
            static_cast<uint8_t>(std::min(params[i + 2], 255u)),
            static_cast<uint8_t>(std::min(params[i + 3], 255u)),
            static_cast<uint8_t>(std::min(params[i + 4], 255u)),
            0
          }
        });
        i += 5;
      } else if (mode == 5 && i + 2 < params.size()) {
        // 38;5;N  or  48;5;N
        actions.push_back(action{
          .type = is_bg ? action_type::sgr_set_bg : action_type::sgr_set_fg,
          .color = xterm_256_color(static_cast<uint8_t>(std::min(params[i + 2], 255u)))
        });
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
        actions.push_back(action{.type = action_type::sgr_reset});
        ++i;
      } else if (n >= 30 && n <= 37) {
        actions.push_back(action{.type = action_type::sgr_set_fg,
                                  .color = catppuccin_palette[n - 30]});
        ++i;
      } else if (n >= 40 && n <= 47) {
        actions.push_back(action{.type = action_type::sgr_set_bg,
                                  .color = catppuccin_palette[n - 40]});
        ++i;
      } else if (n >= 90 && n <= 97) {
        actions.push_back(action{.type = action_type::sgr_set_fg,
                                  .color = catppuccin_palette[(n - 90) + 8]});
        ++i;
      } else if (n >= 100 && n <= 107) {
        actions.push_back(action{.type = action_type::sgr_set_bg,
                                  .color = catppuccin_palette[(n - 100) + 8]});
        ++i;
      } else if (n == 38) {
        consume_38_48(false);  // advances i
      } else if (n == 48) {
        consume_38_48(true);   // advances i
      } else if (n == 39) {
        actions.push_back(action{.type = action_type::sgr_set_fg,
                                  .color = default_fg()});
        ++i;
      } else if (n == 49) {
        actions.push_back(action{.type = action_type::sgr_set_bg,
                                  .color = default_bg()});
        ++i;
      // ── Attribute SGR codes ────────────────────────────────────
      } else if (n == 1) {
        // Bold on — also clears faint (mutually exclusive).
        pending_on  |= static_cast<uint8_t>(cell_attr::bold);
        pending_off |= static_cast<uint8_t>(cell_attr::faint);
        ++i;
      } else if (n == 2) {
        // Faint on — also clears bold.
        pending_on  |= static_cast<uint8_t>(cell_attr::faint);
        pending_off |= static_cast<uint8_t>(cell_attr::bold);
        ++i;
      } else if (n == 3) {
        pending_on |= static_cast<uint8_t>(cell_attr::italic);
        ++i;
      } else if (n == 4) {
        pending_on |= static_cast<uint8_t>(cell_attr::underline);
        ++i;
      } else if (n == 7) {
        pending_on |= static_cast<uint8_t>(cell_attr::reverse);
        ++i;
      } else if (n == 9) {
        pending_on |= static_cast<uint8_t>(cell_attr::strikethrough);
        ++i;
      } else if (n == 22) {
        // Normal intensity — clears both bold and faint.
        pending_off |= static_cast<uint8_t>(cell_attr::bold);
        pending_off |= static_cast<uint8_t>(cell_attr::faint);
        ++i;
      } else if (n == 23) {
        pending_off |= static_cast<uint8_t>(cell_attr::italic);
        ++i;
      } else if (n == 24) {
        pending_off |= static_cast<uint8_t>(cell_attr::underline);
        ++i;
      } else if (n == 27) {
        pending_off |= static_cast<uint8_t>(cell_attr::reverse);
        ++i;
      } else if (n == 29) {
        pending_off |= static_cast<uint8_t>(cell_attr::strikethrough);
        ++i;
      } else {
        // Other SGR (5,6,8,21,25,28, etc.) — silently ignore.
        ++i;
      }
    }

    // Emit accumulated attribute changes.
    if (pending_on != 0) {
      actions.push_back(action{
        .type = action_type::sgr_set_attr,
        .count = pending_on
      });
    }
    if (pending_off != 0) {
      actions.push_back(action{
        .type = action_type::sgr_clear_attr,
        .count = pending_off
      });
    }

    return actions;
  }

  // ── ED: Erase in Display ────────────────────────────────────────────
  if (final_byte == 'J') {
    // Default mode is 0 (unlike cursor moves which default to 1).
    // split_params treats empty tokens as 0, which is correct for ED.
    auto const params = split_params(param_buffer_);
    uint32_t const mode = params[0];
    return {action{
      .type = action_type::erase_display,
      .count = mode
    }};
  }

  // ── EL: Erase in Line ───────────────────────────────────────────────
  if (final_byte == 'K') {
    auto const params = split_params(param_buffer_);
    uint32_t const mode = params[0];
    return {action{
      .type = action_type::erase_line,
      .count = mode
    }};
  }

  // ── IL: Insert Lines ────────────────────────────────────────────────
  if (final_byte == 'L') {
    auto const params = split_params(param_buffer_);
    uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
    return {action{
      .type = action_type::insert_lines,
      .count = count
    }};
  }

  // ── DL: Delete Lines ────────────────────────────────────────────────
  if (final_byte == 'M') {
    auto const params = split_params(param_buffer_);
    uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
    return {action{
      .type = action_type::delete_lines,
      .count = count
    }};
  }

  // ── SU: Scroll Up ───────────────────────────────────────────────────
  if (final_byte == 'S') {
    auto const params = split_params(param_buffer_);
    uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
    return {action{
      .type = action_type::scroll_up_page,
      .count = count
    }};
  }

  // ── SD: Scroll Down ─────────────────────────────────────────────────
  if (final_byte == 'T') {
    auto const params = split_params(param_buffer_);
    uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
    return {action{
      .type = action_type::scroll_down_page,
      .count = count
    }};
  }

  // ── DECSTBM: Set scrolling region ───────────────────────────────────
  if (final_byte == 'r') {
    auto const params = split_params(param_buffer_);
    // params are 1-based; 0 means "reset to full screen" (stored as 0).
    uint32_t const top = (params.size() > 0) ? params[0] : 1;
    uint32_t const bottom = (params.size() > 1) ? params[1] : 0;
    return {action{
      .type = action_type::set_scroll_region,
      .row = top,
      .col = bottom
    }};
  }

  // ── Cursor movement (existing) ───────────────────────────────────────
  auto const [p1, p2] = parse_params();

  switch (final_byte) {
  case 'A': { // CUU — Cursor Up
    return {action{
      .type = action_type::move_cursor_up,
      .count = p1
    }};
  }
  case 'B': { // CUD — Cursor Down
    return {action{
      .type = action_type::move_cursor_down,
      .count = p1
    }};
  }
  case 'C': { // CUF — Cursor Forward
    return {action{
      .type = action_type::move_cursor_forward,
      .count = p1
    }};
  }
  case 'D': { // CUB — Cursor Back
    return {action{
      .type = action_type::move_cursor_back,
      .count = p1
    }};
  }
  case 'H':   // CUP — Cursor Position
  case 'f': { // HVP — Horizontal Vertical Position
    return {action{
      .type = action_type::move_cursor,
      // Convert from 1-based to 0-based.
      .row = (p1 > 0) ? p1 - 1 : 0,
      .col = (p2 > 0) ? p2 - 1 : 0
    }};
  }
  default:
    // Unrecognised final byte → discard the whole sequence.
    return {};
  }
}

// ===========================================================================
// dispatch_osc — produce an action from an OSC parameter string
// ===========================================================================

auto vt_parser::dispatch_osc() -> std::vector<action> {
  // Parse "Ps;Pt" format.
  auto const semi = osc_buffer_.find(';');
  if (semi == std::string::npos) return {};  // malformed — no semicolon

  // Parse Ps (OSC number).
  uint32_t ps = 0;
  auto const ps_str = std::string_view(osc_buffer_).substr(0, semi);
  auto [ptr, ec] = std::from_chars(ps_str.data(), ps_str.data() + ps_str.size(), ps);
  if (ec != std::errc{}) return {};  // non-numeric Ps — ignore

  // Only OSC 0, 1, 2 are handled.
  if (ps > 2) return {};

  // Extract title text (Pt).
  std::string_view title(osc_buffer_.data() + semi + 1, osc_buffer_.size() - semi - 1);

  // Ignore empty title.
  if (title.empty()) return {};

  // Truncate to 255 characters.
  if (title.size() > 255) {
    title = title.substr(0, 255);
  }

  return {action{
    .type = action_type::set_window_title,
    .title = std::string(title)
  }};
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
      return {action{.type = action_type::carriage_return}};
    case '\n':
      return {action{.type = action_type::newline}};
    case 0x1B:   // ESC
      state_ = state::escape;
      return {};
    default:
      if (byte >= 0x20) {
        return {action{
          .type = action_type::write_char,
          .codepoint = static_cast<char32_t>(byte)
        }};
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
    case ']': { // OSC introducer
      state_ = state::osc;
      osc_buffer_.clear();
      return {};
    }
    case '7': { // DECSC — Save Cursor
      state_ = state::ground;
      return {action{.type = action_type::save_cursor}};
    }
    case '8': { // DECRC — Restore Cursor
      state_ = state::ground;
      return {action{.type = action_type::restore_cursor}};
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

  // -------------------------------------------------------------------
  // OSC — inside ESC ] … collecting OSC string
  // -------------------------------------------------------------------
  case state::osc:
    switch (byte) {
    case 0x07: { // BEL — terminates OSC
      auto result = dispatch_osc();
      state_ = state::ground;
      return result;
    }
    case 0x1B:   // ESC — might be ST
      state_ = state::osc_esc;
      return {};
    default:
      if (osc_buffer_.size() < 1024) {
        osc_buffer_ += static_cast<char>(byte);
      }
      return {};
    }

  // -------------------------------------------------------------------
  // OSC_ESC — saw ESC inside OSC, waiting for \ to confirm ST
  // -------------------------------------------------------------------
  case state::osc_esc:
    switch (byte) {
    case '\\': {  // ST — terminates OSC
      auto result = dispatch_osc();
      state_ = state::ground;
      return result;
    }
    case 0x07: { // BEL — also terminates OSC (ESC BEL as string terminator)
      auto result = dispatch_osc();
      state_ = state::ground;
      return result;
    }
    case '[': {  // ESC [ — start new CSI, discard OSC
      state_ = state::csi_entry;
      param_buffer_.clear();
      osc_buffer_.clear();
      return {};
    }
    default:
      // Treat the prior ESC as start of a new escape sequence.
      // Re-enter the escape state and re-process the current byte.
      state_ = state::escape;
      osc_buffer_.clear();
      return parse(byte);
    }
  }

  return {};
}

} // namespace betty::terminal
