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

// ===========================================================================
// Shift lookup table — maps vk_code to (unshifted, shifted) character.
// Letters are handled arithmetically from their vk_code range.
// ===========================================================================

struct key_mapping {
  vk_code vk;
  char unshifted;
  char shifted;
};

constexpr key_mapping k_shift_table[] = {
  // Digits 0–9
  {vk_code::printable_0, '0', ')'},
  {static_cast<vk_code>('1'), '1', '!'},
  {static_cast<vk_code>('2'), '2', '@'},
  {static_cast<vk_code>('3'), '3', '#'},
  {static_cast<vk_code>('4'), '4', '$'},
  {static_cast<vk_code>('5'), '5', '%'},
  {static_cast<vk_code>('6'), '6', '^'},
  {static_cast<vk_code>('7'), '7', '&'},
  {static_cast<vk_code>('8'), '8', '*'},
  {vk_code::printable_9, '9', '('},
  // Punctuation
  {vk_code::semicolon,     ';', ':'},
  {vk_code::comma,         ',', '<'},
  {vk_code::period_,       '.', '>'},
  {vk_code::slash,         '/', '?'},
  {vk_code::backslash,     '\\', '|'},
  {vk_code::bracket_left,  '[', '{'},
  {vk_code::bracket_right, ']', '}'},
  {vk_code::apostrophe,    '\'', '"'},
  {vk_code::minus,         '-', '_'},
  {vk_code::equal_,        '=', '+'},
  {vk_code::grave,         '`', '~'},
};

// Return the character for a given key and shift state.
// Returns '\0' if the vk_code has no printable representation.
[[nodiscard]] auto lookup_shifted(vk_code vk, bool shift) -> char {
  // Letters a–z
  if (vk >= vk_code::printable_a && vk <= vk_code::printable_z) {
    char const c = static_cast<char>(static_cast<uint32_t>(vk));
    return shift ? static_cast<char>(c - 32) : c;  // uppercase / lowercase
  }

  // Digits and punctuation — lookup table
  for (auto const& m : k_shift_table) {
    if (m.vk == vk) {
      return shift ? m.shifted : m.unshifted;
    }
  }

  return '\0';
}

} // anonymous namespace

// ===========================================================================
// input_handler::on_keydown
// ===========================================================================

auto input_handler::on_keydown(vk_code vk, bool control, bool shift, bool alt) const
  -> std::string {

  std::string base;

  // --- Ctrl+letter → control character (0x01–0x1A) ---
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

  // --- Printable characters with shift support ---
  if (base.empty()) {
    char const c = lookup_shifted(vk, shift);
    if (c != '\0') {
      base = std::string(1, c);
    } else {
      // Fallback: any printable ASCII vk_code value passes through as-is.
      uint32_t const cp = static_cast<uint32_t>(vk);
      if (cp >= 0x20 && cp <= 0x7E) {
        base = std::string(1, static_cast<char>(cp));
      }
    }
  }

  // --- Alt modifier — prefix with ESC ---
  if (alt && !base.empty()) {
    base.insert(0, "\x1B");
  }

  return base;
}

} // namespace betty::terminal
