#include "wcwidth.hpp"
#include <algorithm>
#include <array>
#include <cstddef>

namespace betty::terminal {

namespace {

// East Asian Wide / Fullwidth character ranges.
// Sorted by start codepoint for binary search.
struct range {
  char32_t lo;
  char32_t hi;
};

constexpr std::array<range, 33> wide_ranges = {{
  // Hangul Jamo
  { 0x1100, 0x115F },
  // Misc Technical (wide angle brackets)
  { 0x2329, 0x232A },
  // CJK Radicals Supplement, Kangxi Radicals, Ideographic Description, CJK Symbols
  { 0x2E80, 0x303E },
  // Hiragana, Katakana, Bopomofo, Hangul Compatibility Jamo, Kanbun, CJK Strokes,
  // Katakana Phonetic Extensions, Enclosed CJK Letters and Months
  { 0x3040, 0x33BF },
  // CJK Unified Ideographs Extension A
  { 0x3400, 0x4DBF },
  // CJK Unified Ideographs, Yi Syllables, Yi Radicals
  { 0x4E00, 0xA4CF },
  // Hangul Jamo Extended-A
  { 0xA960, 0xA97C },
  // Hangul Syllables
  { 0xAC00, 0xD7A3 },
  // CJK Compatibility Ideographs
  { 0xF900, 0xFAFF },
  // Vertical forms
  { 0xFE10, 0xFE19 },
  // CJK Compatibility Forms, Small Form Variants
  { 0xFE30, 0xFE6F },
  // Fullwidth Forms (excluding fullwidth space which maps separately)
  { 0xFF01, 0xFF60 },
  // Fullwidth Signs
  { 0xFFE0, 0xFFE6 },
  // Mahjong tile
  { 0x1F004, 0x1F004 },
  // Playing card
  { 0x1F0CF, 0x1F0CF },
  // Negative squared AB
  { 0x1F18E, 0x1F18E },
  // Squared Katakana / Latin / CJK symbols
  { 0x1F191, 0x1F19A },
  // Enclosed Ideographic Supplement
  { 0x1F200, 0x1F251 },
  // Misc Symbols & Pictographs, Emoticons
  { 0x1F300, 0x1F64F },
  // Ornamental Dingbats
  { 0x1F650, 0x1F67F },
  // Transport & Map Symbols
  { 0x1F680, 0x1F6FF },
  // Supplemental Symbols & Pictographs
  { 0x1F900, 0x1F9FF },
  // Chess Symbols
  { 0x1FA00, 0x1FA6F },
  // Symbols & Pictographs Extended-A
  { 0x1FA70, 0x1FAFF },
  // CJK Unified Ideographs Extension B (and C–F)
  { 0x20000, 0x2FFFD },
  // CJK Unified Ideographs Extension G
  { 0x30000, 0x3FFFD },
  // Additional fullwidth / square characters
  // Enclosed Alphanumeric Supplement
  { 0x1F100, 0x1F1FF },
  // Geometric Shapes Extended
  { 0x1F780, 0x1F7FF },
  // Supplemental Arrows-C
  { 0x1F800, 0x1F8FF },
  // CJK Compatibility Ideographs Supplement
  { 0x2F800, 0x2FA1F },
  // Tags block (some are wide)
  { 0xE0001, 0xE007F },
}};

// Zero-width combining / control character ranges.
constexpr std::array<range, 10> zero_width_ranges = {{
  // Combining Diacritical Marks
  { 0x0300, 0x036F },
  // Combining Diacritical Marks Extended
  { 0x1AB0, 0x1AFF },
  // Combining Diacritical Marks Supplement
  { 0x1DC0, 0x1DFF },
  // Combining Diacritical Marks for Symbols
  { 0x20D0, 0x20FF },
  // Combining Half Marks
  { 0xFE20, 0xFE2F },
  // Variation Selectors
  { 0xFE00, 0xFE0F },
  { 0xE0100, 0xE01EF },  // Variation Selectors Supplement
  // Default ignorable code points (ZWSP, ZWNJ, ZWJ, etc.)
  { 0x200B, 0x200F },
  { 0x2028, 0x202E },
  { 0x2060, 0x206F },
}};

// Check if codepoint is in a sorted array of ranges via binary search.
template <size_t N>
auto in_ranges(char32_t cp, std::array<range, N> const& ranges) -> bool {
  auto const it = std::lower_bound(
      ranges.begin(), ranges.end(), cp,
      [](range const& r, char32_t c) { return r.hi < c; });
  return it != ranges.end() && cp >= it->lo;
}

} // anonymous namespace

auto wcwidth(char32_t cp) noexcept -> int {
  // C0 controls + DEL (U+0000–U+001F, U+007F).
  if (cp < 0x20 || cp == 0x7F) return -1;
  // C1 controls (U+0080–U+009F).
  if (cp >= 0x80 && cp <= 0x9F) return -1;

  // Soft hyphen (SHY) — typically hidden unless line break.
  if (cp == 0x00AD) return 0;

  // Zero-width characters.
  if (in_ranges(cp, zero_width_ranges)) return 0;

  // Wide characters.
  if (in_ranges(cp, wide_ranges)) return 2;

  // Default: width 1.
  return 1;
}

} // namespace betty::terminal
