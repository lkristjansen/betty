#include "unicode.hpp"
#include <windows.h>

namespace betty::platform {

char32_t nfc_compose(char32_t base, char32_t combining) noexcept {
  // Encode base and combining to UTF-16.
  wchar_t input[4] = {};
  int input_len = 0;

  auto encode = [](char32_t cp, wchar_t* out) -> int {
    if (cp <= 0xFFFF) {
      out[0] = static_cast<wchar_t>(cp);
      return 1;
    }
    cp -= 0x10000;
    out[0] = static_cast<wchar_t>(0xD800 | (cp >> 10));
    out[1] = static_cast<wchar_t>(0xDC00 | (cp & 0x3FF));
    return 2;
  };

  input_len = encode(base, input);
  input_len += encode(combining, input + input_len);

  wchar_t composed[4] = {};
  int const len = NormalizeString(
    NormalizationC,  // NFC
    input, input_len,
    composed, 4
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
