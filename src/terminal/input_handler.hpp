#pragma once
#include <cstdint>
#include <string>

#include "platform/types.hpp"

namespace betty::terminal {

// Re-export the platform vk_code enum for convenience.
using platform::vk_code;

// ===========================================================================
// input_handler — translates keyboard events into byte sequences for ConPTY
// ===========================================================================

class input_handler {
public:
  // Primary path: called on WM_KEYDOWN.
  // Returns the byte string to write to the shell input pipe.
  [[nodiscard]] auto on_keydown(vk_code vk, bool control, bool shift, bool alt) const -> std::string;

  // Fallback: called on WM_CHAR for printable characters not handled by WM_KEYDOWN.
  [[nodiscard]] auto on_char(uint32_t codepoint) const -> std::string;
};

} // namespace betty::terminal
