#pragma once
#include <cstdint>

namespace betty::platform {

// Attempt NFC composition of base + combining character.
// Returns the composed codepoint, or 0 on failure (uncomposable).
[[nodiscard]] char32_t nfc_compose(char32_t base, char32_t combining) noexcept;

} // namespace betty::platform
