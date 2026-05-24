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

// Split semicolon-delimited parameter buffer into param_values_.
// Empty tokens become 0 (ANSI convention: missing param = 0).
// Returns a span over the internal param_values_ buffer.
auto vt_parser::split_params() -> std::span<const uint32_t> {
  std::string_view const s = param_buffer_;
  param_values_.clear();
  size_t pos = 0;
  while (pos <= s.size()) {
    size_t const semi = s.find(';', pos);
    std::string_view const token = s.substr(pos, semi - pos);
    if (token.empty()) {
      param_values_.push_back(0);
    } else {
      uint32_t val = 0;
      auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), val);
      param_values_.push_back((ec == std::errc{}) ? val : 0);
    }
    if (semi == std::string::npos) break;
    pos = semi + 1;
  }
  // Empty buffer → implicit 0 (ANSI default for SGR).
  if (param_values_.empty()) param_values_.push_back(0);
  return param_values_;
}

// ===========================================================================
// dispatch — produce actions from a CSI final byte + parameter buffer
// ===========================================================================

void vt_parser::dispatch(char const final_byte) {
  if (final_byte == 'm') {
    // ── SGR ────────────────────────────────────────────────────────────
    auto const params = split_params();
    size_t i = 0;

    // Accumulate attribute changes across the full sequence.
    uint8_t pending_on  = 0;  // attributes to turn ON  (cell_attr bitmask)
    uint8_t pending_off = 0;  // attributes to turn OFF

    auto consume_38_48 = [&](bool is_bg) {
      if (i + 1 >= params.size()) return;
      uint32_t const mode = params[i + 1];
      if (mode == 2 && i + 4 < params.size()) {
        // 38;2;R;G;B  or  48;2;R;G;B
        output_.push_back(action{
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
        output_.push_back(action{
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
        output_.push_back(action{.type = action_type::sgr_reset});
        ++i;
      } else if (n >= 30 && n <= 37) {
        output_.push_back(action{.type = action_type::sgr_set_fg,
                                  .color = catppuccin_palette[n - 30]});
        ++i;
      } else if (n >= 40 && n <= 47) {
        output_.push_back(action{.type = action_type::sgr_set_bg,
                                  .color = catppuccin_palette[n - 40]});
        ++i;
      } else if (n >= 90 && n <= 97) {
        output_.push_back(action{.type = action_type::sgr_set_fg,
                                  .color = catppuccin_palette[(n - 90) + 8]});
        ++i;
      } else if (n >= 100 && n <= 107) {
        output_.push_back(action{.type = action_type::sgr_set_bg,
                                  .color = catppuccin_palette[(n - 100) + 8]});
        ++i;
      } else if (n == 38) {
        consume_38_48(false);  // advances i
      } else if (n == 48) {
        consume_38_48(true);   // advances i
      } else if (n == 39) {
        output_.push_back(action{.type = action_type::sgr_set_fg,
                                  .color = default_fg()});
        ++i;
      } else if (n == 49) {
        output_.push_back(action{.type = action_type::sgr_set_bg,
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
      output_.push_back(action{
        .type = action_type::sgr_set_attr,
        .count = pending_on
      });
    }
    if (pending_off != 0) {
      output_.push_back(action{
        .type = action_type::sgr_clear_attr,
        .count = pending_off
      });
    }

    return;
  }

  // ── ED: Erase in Display ────────────────────────────────────────────
  if (final_byte == 'J') {
    auto const params = split_params();
    uint32_t const mode = params[0];
    output_.push_back(action{
      .type = action_type::erase_display,
      .count = mode
    });
    return;
  }

  // ── EL: Erase in Line ───────────────────────────────────────────────
  if (final_byte == 'K') {
    auto const params = split_params();
    uint32_t const mode = params[0];
    output_.push_back(action{
      .type = action_type::erase_line,
      .count = mode
    });
    return;
  }

  // ── IL: Insert Lines ────────────────────────────────────────────────
  if (final_byte == 'L') {
    auto const params = split_params();
    uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
    output_.push_back(action{
      .type = action_type::insert_lines,
      .count = count
    });
    return;
  }

  // ── DL: Delete Lines ────────────────────────────────────────────────
  if (final_byte == 'M') {
    auto const params = split_params();
    uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
    output_.push_back(action{
      .type = action_type::delete_lines,
      .count = count
    });
    return;
  }

  // ── SU: Scroll Up ───────────────────────────────────────────────────
  if (final_byte == 'S') {
    auto const params = split_params();
    uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
    output_.push_back(action{
      .type = action_type::scroll_up_page,
      .count = count
    });
    return;
  }

  // ── SD: Scroll Down ─────────────────────────────────────────────────
  if (final_byte == 'T') {
    auto const params = split_params();
    uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
    output_.push_back(action{
      .type = action_type::scroll_down_page,
      .count = count
    });
    return;
  }

  // ── DECSTBM: Set scrolling region ───────────────────────────────────
  if (final_byte == 'r') {
    auto const params = split_params();
    uint32_t const top = (params.size() > 0) ? params[0] : 1;
    uint32_t const bottom = (params.size() > 1) ? params[1] : 0;
    output_.push_back(action{
      .type = action_type::set_scroll_region,
      .row = top,
      .col = bottom
    });
    return;
  }

  // ── ICH: Insert Characters ──────────────────────────────────────────
  if (final_byte == '@') {
    auto const params = split_params();
    uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
    output_.push_back(action{
      .type = action_type::insert_chars,
      .count = count
    });
    return;
  }

  // ── DCH: Delete Characters ──────────────────────────────────────────
  if (final_byte == 'P') {
    auto const params = split_params();
    uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
    output_.push_back(action{
      .type = action_type::delete_chars,
      .count = count
    });
    return;
  }

  // ── ECH: Erase Characters ────────────────────────────────────────────
  if (final_byte == 'X') {
    auto const params = split_params();
    uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
    output_.push_back(action{
      .type = action_type::erase_chars,
      .count = count
    });
    return;
  }

  // ── Cursor movement ─────────────────────────────────────────────────
  auto const [p1, p2] = parse_params();

  switch (final_byte) {
  case 'A': // CUU — Cursor Up
    output_.push_back(action{
      .type = action_type::move_cursor_up,
      .count = p1
    });
    return;
  case 'B': // CUD — Cursor Down
    output_.push_back(action{
      .type = action_type::move_cursor_down,
      .count = p1
    });
    return;
  case 'C': // CUF — Cursor Forward
    output_.push_back(action{
      .type = action_type::move_cursor_forward,
      .count = p1
    });
    return;
  case 'D': // CUB — Cursor Back
    output_.push_back(action{
      .type = action_type::move_cursor_back,
      .count = p1
    });
    return;
  case 'H':   // CUP — Cursor Position
  case 'f': { // HVP — Horizontal Vertical Position
    output_.push_back(action{
      .type = action_type::move_cursor,
      .row = (p1 > 0) ? p1 - 1 : 0,
      .col = (p2 > 0) ? p2 - 1 : 0
    });
    return;
  }
  default:
    // Unrecognised final byte → discard the whole sequence.
    return;
  }
}

// ===========================================================================
// dispatch_osc — produce an action from an OSC parameter string
// ===========================================================================

void vt_parser::dispatch_osc() {
  // Parse "Ps;Pt" format.
  auto const semi = osc_buffer_.find(';');
  if (semi == std::string::npos) return;  // malformed — no semicolon

  // Parse Ps (OSC number).
  uint32_t ps = 0;
  auto const ps_str = std::string_view(osc_buffer_).substr(0, semi);
  auto [ptr, ec] = std::from_chars(ps_str.data(), ps_str.data() + ps_str.size(), ps);
  if (ec != std::errc{}) return;  // non-numeric Ps — ignore

  // Only OSC 0, 1, 2 are handled.
  if (ps > 2) return;

  // Extract title text (Pt).
  std::string_view title(osc_buffer_.data() + semi + 1, osc_buffer_.size() - semi - 1);

  // Ignore empty title.
  if (title.empty()) return;

  // Truncate to 255 characters.
  if (title.size() > 255) {
    title = title.substr(0, 255);
  }

  output_.push_back(action{
    .type = action_type::set_window_title,
    .title = std::string(title)
  });
}

// ===========================================================================
// parse — state machine entry point
// ===========================================================================

auto vt_parser::parse(unsigned char const byte) -> std::span<const action> {
  output_.clear();
  switch (state_) {

  // -------------------------------------------------------------------
  // GROUND — normal text (including UTF-8 multi-byte start)
  // -------------------------------------------------------------------
  case state::ground:
    switch (byte) {
    case '\r':
      output_.push_back(action{.type = action_type::carriage_return});
      return output_;
    case '\n':
      output_.push_back(action{.type = action_type::newline});
      return output_;
    case 0x1B:   // ESC
      state_ = state::escape;
      return output_;
    default:
      if (byte < 0x80) {
        // ASCII: printable (0x20–0x7E) or C0 control (ignore).
        if (byte >= 0x20) {
          output_.push_back(action{
            .type = action_type::write_char,
            .codepoint = static_cast<char32_t>(byte)
          });
          return output_;
        }
        return output_;
      }
      // byte >= 0x80 — UTF-8 multi-byte start
      if (byte >= 0xC0 && byte <= 0xF4) {
        // Determine expected sequence length.
        if (byte <= 0xDF) {
          utf8_.remaining = 1;
          utf8_.codepoint = byte & 0x1Fu;
        } else if (byte <= 0xEF) {
          utf8_.remaining = 2;
          utf8_.codepoint = byte & 0x0Fu;
        } else {
          utf8_.remaining = 3;
          utf8_.codepoint = byte & 0x07u;
        }
        state_ = state::utf8_accum;
        return output_;
      }
      // Stray continuation byte (0x80–0xBF) or invalid (0xF5–0xFF) —
      // silently ignore (don't emit anything).
      return output_;
    }

  // -------------------------------------------------------------------
  // UTF8_ACCUM — accumulating continuation bytes
  // -------------------------------------------------------------------
  case state::utf8_accum:
    // Control characters abort the UTF-8 sequence and are re-processed.
    if (byte < 0x20 || byte == 0x7F || byte == 0x1B) {
      state_ = state::ground;
      return parse(byte);
    }
    // Continuation bytes must be 0x80–0xBF.
    if ((byte & 0xC0u) != 0x80u) {
      // Invalid — abort sequence and emit replacement character.
      state_ = state::ground;
      output_.push_back(action{
        .type = action_type::write_char,
        .codepoint = 0xFFFDu
      });
      return output_;
    }
    utf8_.codepoint = (utf8_.codepoint << 6) | (byte & 0x3Fu);
    utf8_.remaining--;
    if (utf8_.remaining == 0) {
      state_ = state::ground;
      // Validate the decoded codepoint.
      char32_t const cp = utf8_.codepoint;
      // Reject surrogates (U+D800–U+DFFF).
      if (cp >= 0xD800 && cp <= 0xDFFF) {
        output_.push_back(action{.type = action_type::write_char, .codepoint = 0xFFFDu});
        return output_;
      }
      // Reject overlong 2-byte sequences (codepoint < 0x80).
      if (cp < 0x80) {
        output_.push_back(action{.type = action_type::write_char, .codepoint = 0xFFFDu});
        return output_;
      }
      // Reject codepoints beyond Unicode range.
      if (cp > 0x10FFFF) {
        output_.push_back(action{.type = action_type::write_char, .codepoint = 0xFFFDu});
        return output_;
      }
      output_.push_back(action{
        .type = action_type::write_char,
        .codepoint = cp
      });
      return output_;
    }
    return output_;

  // -------------------------------------------------------------------
  // ESCAPE — just saw ESC
  // -------------------------------------------------------------------
  case state::escape:
    switch (byte) {
    case '[':   // CSI introducer
      state_ = state::csi_entry;
      param_buffer_.clear();
      return output_;
    case ']': { // OSC introducer
      state_ = state::osc;
      osc_buffer_.clear();
      return output_;
    }
    case '7': { // DECSC — Save Cursor
      state_ = state::ground;
      output_.push_back(action{.type = action_type::save_cursor});
      return output_;
    }
    case '8': { // DECRC — Restore Cursor
      state_ = state::ground;
      output_.push_back(action{.type = action_type::restore_cursor});
      return output_;
    }
    default:
      // Unknown escape sequence — discard.
      state_ = state::ground;
      return output_;
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
      dispatch(static_cast<char>(byte));
      reset_csi();
      return output_;
    } else {
      reset_csi();  // invalid byte → discard CSI sequence
    }
    return output_;

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
      dispatch(static_cast<char>(byte));
      reset_csi();
      return output_;
    } else {
      reset_csi();
    }
    return output_;

  // -------------------------------------------------------------------
  // CSI_INTERMEDIATE — saw intermediate byte(s)
  // -------------------------------------------------------------------
  case state::csi_intermediate:
    if (byte >= 0x20 && byte <= 0x2F) {
      // stay in CSI_INTERMEDIATE (intermediate bytes are ignored)
    } else if (byte >= 0x40 && byte <= 0x7E) {
      dispatch(static_cast<char>(byte));
      reset_csi();
      return output_;
    } else {
      reset_csi();
    }
    return output_;

  // -------------------------------------------------------------------
  // OSC — inside ESC ] … collecting OSC string
  // -------------------------------------------------------------------
  case state::osc:
    switch (byte) {
    case 0x07: { // BEL — terminates OSC
      dispatch_osc();
      state_ = state::ground;
      return output_;
    }
    case 0x1B:   // ESC — might be ST
      state_ = state::osc_esc;
      return output_;
    default:
      if (osc_buffer_.size() < 1024) {
        osc_buffer_ += static_cast<char>(byte);
      }
      return output_;
    }

  // -------------------------------------------------------------------
  // OSC_ESC — saw ESC inside OSC, waiting for \ to confirm ST
  // -------------------------------------------------------------------
  case state::osc_esc:
    switch (byte) {
    case '\\': {  // ST — terminates OSC
      dispatch_osc();
      state_ = state::ground;
      return output_;
    }
    case 0x07: { // BEL — also terminates OSC (ESC BEL as string terminator)
      dispatch_osc();
      state_ = state::ground;
      return output_;
    }
    case '[': {  // ESC [ — start new CSI, discard OSC
      state_ = state::csi_entry;
      param_buffer_.clear();
      osc_buffer_.clear();
      return output_;
    }
    default:
      // Treat the prior ESC as start of a new escape sequence.
      // Re-enter the escape state and re-process the current byte.
      state_ = state::escape;
      osc_buffer_.clear();
      return parse(byte);
    }
  }

  return output_;
}

} // namespace betty::terminal
