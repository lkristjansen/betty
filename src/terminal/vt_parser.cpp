#include "vt_parser.hpp"
#include <cassert>
#include <charconv>
#include <string_view>

namespace {

constexpr uint32_t k_osc_buffer_max   = 1024;
constexpr uint32_t k_max_title_length = 255;

// SGR parameter codes (ECMA-48).
namespace sgr {
  constexpr uint32_t reset             = 0;
  constexpr uint32_t bold_on           = 1;
  constexpr uint32_t faint_on          = 2;
  constexpr uint32_t italic_on         = 3;
  constexpr uint32_t underline_on      = 4;
  constexpr uint32_t reverse_on        = 7;
  constexpr uint32_t strikethrough_on  = 9;
  constexpr uint32_t bold_faint_off    = 22;
  constexpr uint32_t italic_off        = 23;
  constexpr uint32_t underline_off     = 24;
  constexpr uint32_t reverse_off       = 27;
  constexpr uint32_t strikethrough_off = 29;
  constexpr uint32_t fg_palette_lo     = 30;
  constexpr uint32_t fg_palette_hi     = 37;
  constexpr uint32_t fg_default        = 39;
  constexpr uint32_t bg_palette_lo     = 40;
  constexpr uint32_t bg_palette_hi     = 47;
  constexpr uint32_t bg_default        = 49;
  constexpr uint32_t fg_extended       = 38;
  constexpr uint32_t bg_extended       = 48;
  constexpr uint32_t fg_bright_lo      = 90;
  constexpr uint32_t fg_bright_hi      = 97;
  constexpr uint32_t bg_bright_lo      = 100;
  constexpr uint32_t bg_bright_hi      = 107;
} // namespace sgr

} // anonymous namespace

namespace betty::terminal {

// ===========================================================================
// utf8_decoder
// ===========================================================================

void vt_parser::utf8_decoder::start(unsigned char const lead_byte) {
  assert(lead_byte >= 0xC0 && lead_byte <= 0xF4 && "invalid UTF-8 lead byte");
  if (lead_byte <= 0xDF) {
    remaining_  = 1;
    codepoint_  = lead_byte & 0x1Fu;
  } else if (lead_byte <= 0xEF) {
    remaining_  = 2;
    codepoint_  = lead_byte & 0x0Fu;
  } else {
    remaining_  = 3;
    codepoint_  = lead_byte & 0x07u;
  }
}

auto vt_parser::utf8_decoder::continue_byte(unsigned char const byte) -> result {
  // Continuation bytes must be 0x80–0xBF.
  if (!vt_bytes::is_utf8_continuation(byte)) return result::error;

  codepoint_ = (codepoint_ << 6) | (byte & 0x3Fu);
  remaining_--;

  if (remaining_ > 0) return result::incomplete;

  // Validate the decoded codepoint.
  char32_t const cp = codepoint_;
  // Reject surrogates (U+D800–U+DFFF).
  if (cp >= 0xD800 && cp <= 0xDFFF) return result::error;
  // Reject overlong 2-byte sequences (codepoint < 0x80).
  if (cp < 0x80) return result::error;
  // Reject codepoints beyond Unicode range.
  if (cp > 0x10FFFF) return result::error;

  return result::complete;
}

void vt_parser::utf8_decoder::reset() {
  codepoint_ = 0;
  remaining_ = 0;
}

// ===========================================================================
// internal helpers
// ===========================================================================

void vt_parser::reset_csi() {
  state_ = state::ground;
  param_buffer_.clear();
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
  switch (final_byte) {
    case 'm': dispatch_sgr();      return;
    case 'J': dispatch_ed();      return;
    case 'K': dispatch_el();      return;
    case 'L': dispatch_il();      return;
    case 'M': dispatch_dl();      return;
    case 'S': dispatch_su();      return;
    case 'T': dispatch_sd();      return;
    case 'r': dispatch_decstbm(); return;
    case '@': dispatch_ich();     return;
    case 'P': dispatch_dch();     return;
    case 'X': dispatch_ech();     return;
    case 'A': case 'B': case 'C': case 'D':
    case 'H': case 'f':
      dispatch_cursor(final_byte); return;
    default:
      // Unrecognised final byte → discard the whole sequence.
      return;
  }
}

// ===========================================================================
// per-CSI-final-byte dispatch methods
// ===========================================================================

void vt_parser::dispatch_sgr() {
  auto const params = split_params();
  size_t i = 0;

  // Accumulate attribute changes across the full sequence.
  uint8_t pending_on  = 0;
  uint8_t pending_off = 0;

  while (i < params.size()) {
    uint32_t const n = params[i];

    if (n == sgr::reset) {
      output_.push_back(action{.type = action_type::sgr_reset});
      ++i;
    } else if (n >= sgr::fg_palette_lo && n <= sgr::fg_palette_hi) {
      output_.push_back(action{.type = action_type::sgr_set_fg,
                                .color = catppuccin_palette[n - sgr::fg_palette_lo]});
      ++i;
    } else if (n >= sgr::bg_palette_lo && n <= sgr::bg_palette_hi) {
      output_.push_back(action{.type = action_type::sgr_set_bg,
                                .color = catppuccin_palette[n - sgr::bg_palette_lo]});
      ++i;
    } else if (n >= sgr::fg_bright_lo && n <= sgr::fg_bright_hi) {
      output_.push_back(action{.type = action_type::sgr_set_fg,
                                .color = catppuccin_palette[(n - sgr::fg_bright_lo) + 8]});
      ++i;
    } else if (n >= sgr::bg_bright_lo && n <= sgr::bg_bright_hi) {
      output_.push_back(action{.type = action_type::sgr_set_bg,
                                .color = catppuccin_palette[(n - sgr::bg_bright_lo) + 8]});
      ++i;
    } else if (n == sgr::fg_extended) {
      i += dispatch_sgr_extended(params, i, false);
    } else if (n == sgr::bg_extended) {
      i += dispatch_sgr_extended(params, i, true);
    } else if (n == sgr::fg_default) {
      output_.push_back(action{.type = action_type::sgr_set_fg,
                                .color = default_fg()});
      ++i;
    } else if (n == sgr::bg_default) {
      output_.push_back(action{.type = action_type::sgr_set_bg,
                                .color = default_bg()});
      ++i;
    } else if (n == sgr::bold_on) {
      // Bold on — also clears faint (mutually exclusive).
      pending_on  |= to_uint8(cell_attr::bold);
      pending_off |= to_uint8(cell_attr::faint);
      ++i;
    } else if (n == sgr::faint_on) {
      // Faint on — also clears bold.
      pending_on  |= to_uint8(cell_attr::faint);
      pending_off |= to_uint8(cell_attr::bold);
      ++i;
    } else if (n == sgr::italic_on) {
      pending_on |= to_uint8(cell_attr::italic);
      ++i;
    } else if (n == sgr::underline_on) {
      pending_on |= to_uint8(cell_attr::underline);
      ++i;
    } else if (n == sgr::reverse_on) {
      pending_on |= to_uint8(cell_attr::reverse);
      ++i;
    } else if (n == sgr::strikethrough_on) {
      pending_on |= to_uint8(cell_attr::strikethrough);
      ++i;
    } else if (n == sgr::bold_faint_off) {
      // Normal intensity — clears both bold and faint.
      pending_off |= to_uint8(cell_attr::bold);
      pending_off |= to_uint8(cell_attr::faint);
      ++i;
    } else if (n == sgr::italic_off) {
      pending_off |= to_uint8(cell_attr::italic);
      ++i;
    } else if (n == sgr::underline_off) {
      pending_off |= to_uint8(cell_attr::underline);
      ++i;
    } else if (n == sgr::reverse_off) {
      pending_off |= to_uint8(cell_attr::reverse);
      ++i;
    } else if (n == sgr::strikethrough_off) {
      pending_off |= to_uint8(cell_attr::strikethrough);
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
}

auto vt_parser::dispatch_sgr_extended(std::span<const uint32_t> const params,
                                       size_t const i, bool const is_bg) -> size_t {
  if (i + 1 >= params.size()) return 2;  // mode byte missing → skip 38/48 + mode
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
    return 5;
  }
  if (mode == 5 && i + 2 < params.size()) {
    // 38;5;N  or  48;5;N
    output_.push_back(action{
      .type = is_bg ? action_type::sgr_set_bg : action_type::sgr_set_fg,
      .color = xterm_256_color(static_cast<uint8_t>(std::min(params[i + 2], 255u)))
    });
    return 3;
  }
  // Malformed extended colour — consume what we can and move on.
  return 2;
}

void vt_parser::dispatch_ed() {
  auto const params = split_params();
  output_.push_back(action{
    .type = action_type::erase_display,
    .count = params[0]
  });
}

void vt_parser::dispatch_el() {
  auto const params = split_params();
  output_.push_back(action{
    .type = action_type::erase_line,
    .count = params[0]
  });
}

void vt_parser::dispatch_il() {
  auto const params = split_params();
  uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
  output_.push_back(action{
    .type = action_type::insert_lines,
    .count = count
  });
}

void vt_parser::dispatch_dl() {
  auto const params = split_params();
  uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
  output_.push_back(action{
    .type = action_type::delete_lines,
    .count = count
  });
}

void vt_parser::dispatch_su() {
  auto const params = split_params();
  uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
  output_.push_back(action{
    .type = action_type::scroll_up_page,
    .count = count
  });
}

void vt_parser::dispatch_sd() {
  auto const params = split_params();
  uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
  output_.push_back(action{
    .type = action_type::scroll_down_page,
    .count = count
  });
}

void vt_parser::dispatch_decstbm() {
  auto const params = split_params();
  uint32_t const top = (params.size() > 0) ? params[0] : 1;
  uint32_t const bottom = (params.size() > 1) ? params[1] : 0;
  output_.push_back(action{
    .type = action_type::set_scroll_region,
    .row = top,
    .col = bottom
  });
}

void vt_parser::dispatch_ich() {
  auto const params = split_params();
  uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
  output_.push_back(action{
    .type = action_type::insert_chars,
    .count = count
  });
}

void vt_parser::dispatch_dch() {
  auto const params = split_params();
  uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
  output_.push_back(action{
    .type = action_type::delete_chars,
    .count = count
  });
}

void vt_parser::dispatch_ech() {
  auto const params = split_params();
  uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
  output_.push_back(action{
    .type = action_type::erase_chars,
    .count = count
  });
}

void vt_parser::dispatch_cursor(char const final_byte) {
  auto const params = split_params();
  // ANSI default for cursor params: missing or 0 → 1.
  auto const p1 = (params.size() > 0 && params[0] != 0) ? params[0] : 1u;
  auto const p2 = (params.size() > 1 && params[1] != 0) ? params[1] : 1u;

  switch (final_byte) {
  case 'A':
    output_.push_back(action{
      .type = action_type::move_cursor_up,
      .count = p1
    });
    return;
  case 'B':
    output_.push_back(action{
      .type = action_type::move_cursor_down,
      .count = p1
    });
    return;
  case 'C':
    output_.push_back(action{
      .type = action_type::move_cursor_forward,
      .count = p1
    });
    return;
  case 'D':
    output_.push_back(action{
      .type = action_type::move_cursor_back,
      .count = p1
    });
    return;
  case 'H':
  case 'f':
    output_.push_back(action{
      .type = action_type::move_cursor,
      .row = p1 - 1,
      .col = p2 - 1
    });
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

  // Truncate to max length.
  if (title.size() > k_max_title_length) {
    title = title.substr(0, k_max_title_length);
  }

  output_.push_back(action{
    .type = action_type::set_window_title,
    .title = std::string(title)
  });
}

// ===========================================================================
// parse — state machine entry point
// ===========================================================================
// Each byte is dispatched to the handler for the current state.  Handlers
// return `done` to exit (output_ is ready) or `reprocess` to loop back and
// re-process the same byte after a state change — this eliminates the
// recursion that the original implementation used.

auto vt_parser::parse(unsigned char const byte) -> std::span<const action> {
  output_.clear();
  unsigned char cur = byte;
  while (true) {
    handler_result hr;
    switch (state_) {
      case state::ground:           hr = handle_ground(cur); break;
      case state::utf8_accum:       hr = handle_utf8_accum(cur); break;
      case state::escape:           hr = handle_escape(cur); break;
      case state::csi_entry:        hr = handle_csi_entry(cur); break;
      case state::csi_param:        hr = handle_csi_param(cur); break;
      case state::csi_intermediate: hr = handle_csi_intermediate(cur); break;
      case state::osc:              hr = handle_osc(cur); break;
      case state::osc_esc:          hr = handle_osc_esc(cur); break;
    }
    if (hr == handler_result::done) return output_;
    // reprocess: loop again with the same byte in the new state.
  }
}

// ===========================================================================
// per-state handler methods
// ===========================================================================

auto vt_parser::handle_ground(unsigned char const byte) -> handler_result {
  switch (byte) {
  case '\r':  // vt_bytes::is_cr
    output_.push_back(action{.type = action_type::carriage_return});
    return handler_result::done;
  case '\n':  // vt_bytes::is_lf
    output_.push_back(action{.type = action_type::newline});
    return handler_result::done;
  case 0x08:  // vt_bytes::is_bs — backspace
    output_.push_back(action{.type = action_type::move_cursor_back, .count = 1});
    return handler_result::done;
  case 0x7F:  // DEL — silently ignore
    return handler_result::done;
  case 0x1B:  // ESC
    state_ = state::escape;
    return handler_result::done;
  default:
    if (vt_bytes::is_ascii_printable(byte)) {
      output_.push_back(action{
        .type = action_type::write_char,
        .codepoint = static_cast<char32_t>(byte)
      });
      return handler_result::done;
    }
    if (!vt_bytes::is_utf8_lead(byte)) {
      // Stray continuation byte or invalid — silently ignore.
      return handler_result::done;
    }
    // UTF-8 multi-byte start.
    utf8_.start(byte);
    state_ = state::utf8_accum;
    return handler_result::done;
  }
}

auto vt_parser::handle_utf8_accum(unsigned char const byte) -> handler_result {
  // Control characters abort the UTF-8 sequence and are re-processed.
  if (vt_bytes::is_c0_control(byte) || vt_bytes::is_del(byte) || vt_bytes::is_esc(byte)) {
    state_ = state::ground;
    utf8_.reset();
    return handler_result::reprocess;
  }

  auto const result = utf8_.continue_byte(byte);
  switch (result) {
  case utf8_decoder::result::complete:
    state_ = state::ground;
    output_.push_back(action{
      .type = action_type::write_char,
      .codepoint = utf8_.codepoint()
    });
    return handler_result::done;
  case utf8_decoder::result::error:
    state_ = state::ground;
    output_.push_back(action{
      .type = action_type::write_char,
      .codepoint = 0xFFFDu
    });
    return handler_result::done;
  case utf8_decoder::result::incomplete:
    return handler_result::done;
  }
  return handler_result::done;
}

auto vt_parser::handle_escape(unsigned char const byte) -> handler_result {
  switch (byte) {
  case '[':   // CSI introducer
    state_ = state::csi_entry;
    param_buffer_.clear();
    return handler_result::done;
  case ']':   // OSC introducer
    state_ = state::osc;
    osc_buffer_.clear();
    return handler_result::done;
  case '7':   // DECSC — Save Cursor
    state_ = state::ground;
    output_.push_back(action{.type = action_type::save_cursor});
    return handler_result::done;
  case '8':   // DECRC — Restore Cursor
    state_ = state::ground;
    output_.push_back(action{.type = action_type::restore_cursor});
    return handler_result::done;
  default:
    // Unknown escape sequence — discard.
    state_ = state::ground;
    return handler_result::done;
  }
}

auto vt_parser::handle_csi_entry(unsigned char const byte) -> handler_result {
  if (vt_bytes::is_csi_param(byte)) {
    param_buffer_ += static_cast<char>(byte);
    state_ = state::csi_param;
  } else if (vt_bytes::is_csi_intermediate(byte)) {
    state_ = state::csi_intermediate;
  } else if (vt_bytes::is_csi_final(byte)) {
    dispatch(static_cast<char>(byte));
    reset_csi();
  } else {
    reset_csi();  // invalid byte → discard CSI sequence
  }
  return handler_result::done;
}

auto vt_parser::handle_csi_param(unsigned char const byte) -> handler_result {
  if (vt_bytes::is_csi_param(byte)) {
    param_buffer_ += static_cast<char>(byte);
    // stay in CSI_PARAM
  } else if (vt_bytes::is_csi_intermediate(byte)) {
    state_ = state::csi_intermediate;
  } else if (vt_bytes::is_csi_final(byte)) {
    dispatch(static_cast<char>(byte));
    reset_csi();
  } else {
    reset_csi();
  }
  return handler_result::done;
}

auto vt_parser::handle_csi_intermediate(unsigned char const byte) -> handler_result {
  if (vt_bytes::is_csi_intermediate(byte)) {
    // stay in CSI_INTERMEDIATE (intermediate bytes are ignored)
  } else if (vt_bytes::is_csi_final(byte)) {
    dispatch(static_cast<char>(byte));
    reset_csi();
  } else {
    reset_csi();
  }
  return handler_result::done;
}

auto vt_parser::handle_osc(unsigned char const byte) -> handler_result {
  switch (byte) {
  case 0x07:  // vt_bytes::is_bel — terminates OSC
    dispatch_osc();
    state_ = state::ground;
    return handler_result::done;
  case 0x1B:  // ESC — might be ST
    state_ = state::osc_esc;
    return handler_result::done;
  default:
    if (osc_buffer_.size() < k_osc_buffer_max) {
      osc_buffer_ += static_cast<char>(byte);
    }
    return handler_result::done;
  }
}

auto vt_parser::handle_osc_esc(unsigned char const byte) -> handler_result {
  switch (byte) {
  case '\\':  // ST — terminates OSC
    dispatch_osc();
    state_ = state::ground;
    return handler_result::done;
  case 0x07:  // vt_bytes::is_bel — also terminates OSC (ESC BEL as string terminator)
    dispatch_osc();
    state_ = state::ground;
    return handler_result::done;
  case '[':   // ESC [ — start new CSI, discard OSC
    state_ = state::csi_entry;
    param_buffer_.clear();
    osc_buffer_.clear();
    return handler_result::done;
  default:
    // Treat the prior ESC as start of a new escape sequence.
    // Re-enter the escape state and re-process the current byte.
    state_ = state::escape;
    osc_buffer_.clear();
    return handler_result::reprocess;
  }
}

} // namespace betty::terminal
