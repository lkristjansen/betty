#include "input_handler.hpp"

namespace betty::terminal {

// ===========================================================================
// ANSI escape sequence helpers
// ===========================================================================

namespace {

// CSI (Control Sequence Introducer) prefix.
inline constexpr std::string_view k_csi = "\x1B[";

auto ansi_csi(char const* suffix) -> std::string {
  return std::string(k_csi) + suffix;
}

} // anonymous namespace

// ===========================================================================
// input_handler::on_keydown
// ===========================================================================

auto input_handler::on_keydown(vk_code vk, bool control, bool shift, bool alt) const
  -> std::string {

  // --- Ctrl modifiers -------------------------------------------------------
  if (control && !alt) {
    // Ctrl+letter → ASCII 0x01–0x1A (e.g. Ctrl+A → SOH, Ctrl+C → ETX).
    if (vk >= vk_code::printable_a && vk <= vk_code::printable_z) {
      char c = static_cast<char>(static_cast<uint32_t>(vk) - static_cast<uint32_t>(vk_code::printable_a) + 1);
      return std::string(1, c);
    }
  }

  // --- Alt modifier — prefix with ESC ---------------------------------------
  // Alt+key is often sent as ESC + key in terminal emulators.
  // We'll skip full Alt support for now (Task 3 only needs basics).
  (void)shift;
  (void)alt;

  // --- Core keys ------------------------------------------------------------
  switch (vk) {
  case vk_code::enter:     return "\r";
  case vk_code::backspace: return "\x7F";  // DEL
  case vk_code::tab:       return "\t";
  case vk_code::escape:    return "\x1B";
  case vk_code::space:     return " ";

  // Navigation keys — ANSI escape sequences
  case vk_code::arrow_up:    return ansi_csi("A");
  case vk_code::arrow_down:  return ansi_csi("B");
  case vk_code::arrow_right: return ansi_csi("C");
  case vk_code::arrow_left:  return ansi_csi("D");
  case vk_code::home:        return ansi_csi("H");
  case vk_code::end_:        return ansi_csi("F");
  case vk_code::page_up:     return ansi_csi("5~");
  case vk_code::page_down:   return ansi_csi("6~");
  case vk_code::delete_:     return ansi_csi("3~");
  case vk_code::insert_:     return ansi_csi("2~");

  // Function keys
  case vk_code::f1:  return ansi_csi("OP");
  case vk_code::f2:  return ansi_csi("OQ");
  case vk_code::f3:  return ansi_csi("OR");
  case vk_code::f4:  return ansi_csi("OS");
  case vk_code::f5:  return ansi_csi("15~");
  case vk_code::f6:  return ansi_csi("17~");
  case vk_code::f7:  return ansi_csi("18~");
  case vk_code::f8:  return ansi_csi("19~");
  case vk_code::f9:  return ansi_csi("20~");
  case vk_code::f10: return ansi_csi("21~");
  case vk_code::f11: return ansi_csi("23~");
  case vk_code::f12: return ansi_csi("24~");

  default:
    break;
  }

  // Printable ASCII — pass through as-is.
  // vk_code values for printable_a–printable_z, printable_0–printable_9
  // match their ASCII codes.
  uint32_t const cp = static_cast<uint32_t>(vk);
  if (cp >= 0x20 && cp <= 0x7E) {
    return std::string(1, static_cast<char>(cp));
  }

  // Unknown key — return empty (caller should not write anything).
  return {};
}

} // namespace betty::terminal
