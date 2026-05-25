#pragma once
#include <cstdint>
#include <string>

namespace betty::util {

// Encode a single Unicode codepoint into a UTF-8 byte sequence.
// Returns empty string for invalid codepoints (surrogates, above U+10FFFF).
[[nodiscard]] inline auto utf8_encode(uint32_t codepoint) -> std::string {
  // Surrogates (U+D800–U+DFFF) are invalid.
  if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return {};
  // Beyond the Unicode maximum.
  if (codepoint > 0x10FFFF) return {};

  if (codepoint <= 0x7F) {
    // 1 byte: 0xxxxxxx
    return std::string(1, static_cast<char>(codepoint));
  }
  if (codepoint <= 0x7FF) {
    // 2 bytes: 110xxxxx 10xxxxxx
    return {
      static_cast<char>(0xC0 | (codepoint >> 6)),
      static_cast<char>(0x80 | (codepoint & 0x3F))
    };
  }
  if (codepoint <= 0xFFFF) {
    // 3 bytes: 1110xxxx 10xxxxxx 10xxxxxx
    return {
      static_cast<char>(0xE0 | (codepoint >> 12)),
      static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)),
      static_cast<char>(0x80 | (codepoint & 0x3F))
    };
  }
  // 4 bytes: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
  return {
    static_cast<char>(0xF0 | (codepoint >> 18)),
    static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)),
    static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)),
    static_cast<char>(0x80 | (codepoint & 0x3F))
  };
}

} // namespace betty::util
