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

  // shift is unused here — printable characters (including Shift and AltGr
  // combos) are now handled by the WM_CHAR path via write_char().
  // This function handles only non-printable keys and Ctrl+letter combos.
  (void)shift;

  std::string base;

  // --- Ctrl+letter → control character (0x01–0x1A) ---
  // Covers A–Z only. Ctrl+non-ASCII letters (Æ, Ø, Å) arrive through
  // WM_CHAR already translated by Windows to the correct control codepoint.
  if (control && vk >= vk_code::printable_a && vk <= vk_code::printable_z) {
    char const c = static_cast<char>(
      static_cast<uint32_t>(vk) - static_cast<uint32_t>(vk_code::printable_a) + 1);
    base = std::string(1, c);
  } else {
    // --- Special keys ---
    switch (vk) {
    case vk_code::enter:      base = "\r";       break;
    case vk_code::backspace:  base = "\x7F";     break;
    case vk_code::tab:        base = "\t";       break;
    case vk_code::escape:     base = "\x1B";     break;
    case vk_code::space:      base = " ";        break;

    // Navigation keys — ANSI escape sequences
    case vk_code::arrow_up:    base = ansi_csi("A");  break;
    case vk_code::arrow_down:  base = ansi_csi("B");  break;
    case vk_code::arrow_right: base = ansi_csi("C");  break;
    case vk_code::arrow_left:  base = ansi_csi("D");  break;
    case vk_code::home:        base = ansi_csi("H");  break;
    case vk_code::end_:        base = ansi_csi("F");  break;
    case vk_code::page_up:     base = ansi_csi("5~"); break;
    case vk_code::page_down:   base = ansi_csi("6~"); break;
    case vk_code::delete_:     base = ansi_csi("3~"); break;
    case vk_code::insert_:     base = ansi_csi("2~"); break;

    // Function keys
    case vk_code::f1:  base = ansi_csi("OP");  break;
    case vk_code::f2:  base = ansi_csi("OQ");  break;
    case vk_code::f3:  base = ansi_csi("OR");  break;
    case vk_code::f4:  base = ansi_csi("OS");  break;
    case vk_code::f5:  base = ansi_csi("15~");  break;
    case vk_code::f6:  base = ansi_csi("17~");  break;
    case vk_code::f7:  base = ansi_csi("18~");  break;
    case vk_code::f8:  base = ansi_csi("19~");  break;
    case vk_code::f9:  base = ansi_csi("20~");  break;
    case vk_code::f10: base = ansi_csi("21~");  break;
    case vk_code::f11: base = ansi_csi("23~");  break;
    case vk_code::f12: base = ansi_csi("24~");  break;

    default: break;
    }
  }

  // --- Alt modifier — prefix with ESC ---
  if (alt && !base.empty()) {
    base.insert(0, "\x1B");
  }

  return base;
}

} // namespace betty::terminal
