#pragma once
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

namespace betty::platform {

// Attempt NFC composition of base + combining character.
// Returns the composed codepoint, or 0 on failure (uncomposable).
[[nodiscard]] char32_t nfc_compose(char32_t base, char32_t combining) noexcept;

// Convert a UTF-8 string to a UTF-16 wstring.
[[nodiscard]] auto widen(std::string_view sv) -> std::expected<std::wstring, std::error_code>;

} // namespace betty::platform
