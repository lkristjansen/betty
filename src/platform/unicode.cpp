#include "unicode.hpp"

#include <array>
#include <span>
#include <windows.h>

namespace betty::platform {

namespace {

// Encodes a char32_t codepoint to 1 or 2 UTF-16 code units.
// Returns the span slice that was written.
// Precondition: out.size() >= 2.
[[nodiscard]] auto encode(char32_t cp, std::span<wchar_t> out) noexcept
    -> std::span<wchar_t> {
  if (cp <= 0xFFFF) {
    out[0] = static_cast<wchar_t>(cp);
    return out.first(1);
  }
  cp -= 0x10000;
  out[0] = static_cast<wchar_t>(0xD800 | (cp >> 10));
  out[1] = static_cast<wchar_t>(0xDC00 | (cp & 0x3FF));
  return out.first(2);
}

} // namespace

char32_t nfc_compose(char32_t base, char32_t combining) noexcept {
  // Encode base and combining to UTF-16.
  std::array<wchar_t, 4> input{};

  std::size_t offset = 0;
  offset += encode(base, std::span{input}).size();
  offset += encode(combining, std::span{input}.subspan(offset)).size();
  int const input_len = static_cast<int>(offset);

  std::array<wchar_t, 4> composed{};
  int const len = NormalizeString(
    NormalizationC,  // NFC
    input.data(), input_len,
    composed.data(), static_cast<int>(composed.size())
  );

  if (len <= 0 || len > 2) return 0;

  // Decode back to char32_t.
  if (len == 1) {
    return static_cast<char32_t>(composed[0]);
  }
  // Surrogate pair.
  return 0x10000
    + ((static_cast<char32_t>(composed[0]) - 0xD800) << 10)
    + (static_cast<char32_t>(composed[1]) - 0xDC00);
}

} // namespace betty::platform
