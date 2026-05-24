#pragma once
#include "types.hpp"

namespace betty::terminal {

// ===========================================================================
// sgr_state — current foreground, background, and attribute state
// ===========================================================================

struct sgr_state {
  terminal_color fg = default_fg();
  terminal_color bg = default_bg();
  cell_attr attr = cell_attr::none;
};

} // namespace betty::terminal
