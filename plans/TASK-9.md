# Task 9 — Window Title

**Goal:** The window title bar updates to reflect the shell's current working directory (e.g. `~`, `C:\Users\...`). Apps that set the title via OSC escape sequences work. Restores default title on shell exit.

**Dependencies:** Tasks 1–6 (colours not strictly required, but OSC parsing piggybacks on the existing parser; grid + shell I/O must work).

---

## Design Decisions (from Q&A)

| Decision | Choice |
|---|---|
| Routing to window | Observer/callback on `terminal_grid` (set by `Application`) |
| OSC terminators | Both BEL (0x07) and ST (`ESC \`) |
| OSC numbers handled | 0, 1, 2 — all treated identically (set window title) |
| Title string encoding | Raw `std::string` (UTF-8 bytes); convert at `SetWindowTextW` call site |
| Title max length | 255 characters (after UTF-8 → UTF-16 conversion? actual: 255 UTF-8 chars, then truncate) |
| OSC buffer max | 1024 bytes |
| Malformed OSC | Silently ignore, return to ground |
| Unrecognized OSC number | Silently ignore |
| Empty title (e.g. `OSC 0;`) | Ignore — do not set title |

---

## Step-by-Step Implementation Plan

### Step 1 — Add new `action_type` and `action` field

**File:** `src/terminal/vt_parser.hpp`

1. Add `set_window_title` to `action_type` enum.
2. Add `std::string title` field to `action` struct (carries the title string).

```cpp
enum class action_type : uint8_t {
  // ... existing ...
  set_window_title,   // OSC 0/1/2 — set window title
};

struct action {
  // ... existing fields ...
  std::string title{};  // for set_window_title (255 chars max)
};
```

---

### Step 2 — Extend `vt_parser` state machine for OSC

**File:** `src/terminal/vt_parser.hpp`

Add new parser states:
```cpp
enum class state : uint8_t {
  // ... existing ...
  osc,       // inside ESC ] ... collecting OSC string
  osc_esc,   // saw ESC inside OSC — waiting for \ to confirm ST
};
```

Add OSC storage:
```cpp
std::string osc_buffer_;  // collects OSC parameter string (max 1024 bytes)
```

Add helper method:
```cpp
auto dispatch_osc() -> std::vector<action>;
```

**File:** `src/terminal/vt_parser.cpp`

**2a. Escape state — detect `]` (0x5D) as OSC introducer:**

In `parse()`, in the `escape` state, add a case for `0x5D` (`']'`) that transitions to `state::osc` with `osc_buffer_` cleared:

```
case 0x5D:   // OSC introducer
  state_ = state::osc;
  osc_buffer_.clear();
  return {};
```

**2b. OSC state — collect bytes, handle BEL, handle ESC for ST:**

```
case state::osc:
  switch (byte) {
  case 0x07: {  // BEL — terminates OSC
    auto result = dispatch_osc();
    state_ = state::ground;
    return result;
  }
  case 0x1B:     // ESC — might be ST
    state_ = state::osc_esc;
    return {};
  default:
    if (osc_buffer_.size() < 1024) {
      osc_buffer_ += static_cast<char>(byte);
    }
    return {};
  }
```

**2c. OSC_ESC state — check for `\` (ST) or restart:**

```
case state::osc_esc:
  switch (byte) {
  case '\\': {   // ST — terminates OSC
    auto result = dispatch_osc();
    state_ = state::ground;
    return result;
  }
  case 0x5B:     // '[' — treat the prior ESC as a new CSI introducer
    state_ = state::csi_entry;
    param_buffer_.clear();
    osc_buffer_.clear();
    return {};
  default:
    // Treat the prior ESC as the start of a new escape sequence.
    // Re-enter the escape state and re-process the current byte.
    state_ = state::escape;
    osc_buffer_.clear();
    return parse(byte);
  }
```

**2d. `dispatch_osc()` — parse OSC buffer and produce action:**

```
auto vt_parser::dispatch_osc() -> std::vector<action> {
  // Parse "Ps;Pt" format.
  auto semi = osc_buffer_.find(';');
  if (semi == std::string::npos) return {};  // malformed — no semicolon

  // Parse Ps (OSC number).
  std::string_view ps_str(osc_buffer_.data(), semi);
  uint32_t ps = 0;
  auto result = std::from_chars(ps_str.data(), ps_str.data() + ps_str.size(), ps);
  if (result.ec != std::errc{}) return {};  // non-numeric Ps — ignore

  // Only OSC 0, 1, 2 are handled.
  if (ps > 2) return {};

  // Extract title text (Pt).
  std::string_view title(osc_buffer_.data() + semi + 1, osc_buffer_.size() - semi - 1);

  // Ignore empty title.
  if (title.empty()) return {};

  // Truncate to 255 characters.
  if (title.size() > 255) {
    title = title.substr(0, 255);
  }

  return {action{
    .type = action_type::set_window_title,
    .title = std::string(title)
  }};
}
```

**2e. Ground state — no change needed.** OSC sequences always start with ESC, which is already handled.

**2f. Reset OSC state on CSI reset.** Update `reset_csi()` to also clear `osc_buffer_` (defensive, for any state transitions that bypass the OSC state cleanup).

---

### Step 3 — Wire up observer on `terminal_grid`

**File:** `src/terminal/grid.hpp`

Add observer type and setter to `terminal_grid`:

```cpp
#include <functional>

class terminal_grid {
public:
  // ... existing ...

  // Set a callback for out-of-band terminal events (e.g. window title changes).
  // Pass nullptr to unset.
  void set_observer(std::function<void(std::string_view)> on_title);

private:
  // ... existing members ...
  std::function<void(std::string_view)> on_window_title_;
};
```

**File:** `src/terminal/grid.cpp`

1. Implement `set_observer()`.
2. In `apply()`, add a case for `action_type::set_window_title`:

```cpp
case action_type::set_window_title:
  if (on_window_title_) {
    on_window_title_(a.title);
  }
  break;
```

---

### Step 4 — Integrate into `Application`

**File:** `src/app.hpp`

Add a helper method or inline the observer setup in the Application constructor:

```cpp
// In Application constructor, or as a private helper called from the constructor:
void setup_window_title_observer();
```

**File:** `src/app.cpp`

**4a. In the Application constructor** (after `grid_` is constructed):

```cpp
grid_.set_observer([this](std::string_view title) {
  // Convert UTF-8 title to UTF-16 for SetWindowTextW.
  // Reuse the widen() pattern from window.cpp (or make widen() accessible).
  auto wide_title = widen(title);
  SetWindowTextW(static_cast<HWND>(window_.native_handle()), wide_title.c_str());
});
```

**Implementation detail**: The `widen()` function is currently a file-static helper inside `window.cpp`. Options:
- Make `widen()` a public utility (e.g., in `platform/utf.hpp` or `platform/window.hpp`).
- Duplicate the conversion inline (simple, but not DRY).
- Expose a new `platform::set_window_title(win32_window&, std::string_view)` function that wraps the conversion + `SetWindowTextW`.

**Recommended**: Add a `set_window_title(win32_window&, std::string_view title)` function to `window.hpp`/`window.cpp`. This keeps the Win32 call encapsulated and avoids exposing `HWND` to `app.cpp`.

**4b. Restore default title on shell exit.** In `Application::run()`, when the shell has exited (the `else if` branch), add:

```cpp
// Restore default window title after shell exit.
platform::set_window_title(window_, "betty");
```

Place this before the "Drain remaining output" block, so it only fires once.

---

### Step 5 — Expose `set_window_title` from platform layer

**File:** `src/platform/window.hpp`

Declare:

```cpp
auto set_window_title(win32_window& window, std::string_view title) -> void;
```

**File:** `src/platform/window.cpp`

Implement:

```cpp
auto set_window_title(win32_window& window, std::string_view title) -> void {
  auto const wide_title = widen(title);
  SetWindowTextW(static_cast<HWND>(window.handle_), wide_title.c_str());
}
```

Note: This requires `window` or a new friend declaration giving `set_window_title` access to `window.handle_`. Currently `handle_` is private with `friend class win32_window`-only friends. Add:

```cpp
friend auto set_window_title(win32_window&, std::string_view) -> void;
```

---

### Step 6 — Add parser tests for OSC sequences

**File:** `tests/vt_parser_test.cpp`

Add test cases:

| Test | Sequence | Expected |
|---|---|---|
| `[osc][bel]` OSC 0 with title | `ESC ] 0 ; hello BEL` | 1 action: `set_window_title` with `"hello"` |
| `[osc][st]` OSC 2 with ST terminator | `ESC ] 2 ; world ESC \` | 1 action: `set_window_title` with `"world"` |
| `[osc][osc1]` OSC 1 treated as title | `ESC ] 1 ; test BEL` | 1 action: `set_window_title` with `"test"` |
| `[osc][empty]` OSC 0 empty title | `ESC ] 0 ; BEL` | 0 actions (empty title ignored) |
| `[osc][no-semi]` Malformed — no semicolon | `ESC ] garbage BEL` | 0 actions, parser returns to ground |
| `[osc][unknown-ps]` Unrecognized OSC number | `ESC ] 4 ; data BEL` | 0 actions |
| `[osc][truncate]` Title > 255 chars | `ESC ] 0 ; <256 chars> BEL` | Title truncated to 255 chars, valid action |
| `[osc][unicode]` UTF-8 title bytes | `ESC ] 0 ; café BEL` | Raw bytes passed through as-is |
| `[osc][buffer-limit]` OSC buffer > 1024 bytes | `ESC ] 0 ; <1025 bytes> BEL` | Buffer truncated to 1024 bytes, still parses |
| `[osc][st-esc-other]` ESC inside OSC not followed by `\` | `ESC ] 0 ; abc ESC x` | OSC discarded, current byte `x` processed in escape state |
| `[osc][st-esc-csi]` ESC `[` inside OSC | `ESC ] 0 ; abc ESC [ 1 H` | OSC discarded, new CSI sequence parsed correctly |
| `[osc][recovery]` After OSC, parser returns to ground | `ESC ] 0 ; hi BEL A` | 1 OSC title action, then 1 write_char('A') |
| `[osc][multi-byte]` Byte-by-byte feeding of OSC | Feed `ESC`, `]`, `0`, `;`, `x`, `BEL` individually | Final byte (BEL) produces 1 set_window_title action |

---

### Step 7 — Build and manual test

1. Build the project: `cmake --build build/debug`
2. Launch `betty.exe`.
3. Verify:
   - Initial title bar shows "betty".
   - The PowerShell prompt sets the title to the current directory (e.g. `~`).
   - `cd C:\Users` updates the title bar.
   - Typing `exit` restores the title to "betty" before the app closes.
   - Run a command that produces OSC: `echo -ne "\e]0;custom title\a"` sets the title.

---

## Files Changed

| File | Change |
|---|---|
| `src/terminal/vt_parser.hpp` | Add `set_window_title` to `action_type`; add `title` to `action`; add `osc`/`osc_esc` states; add `osc_buffer_` member; declare `dispatch_osc()` |
| `src/terminal/vt_parser.cpp` | Handle `']'` in escape state; add `osc`/`osc_esc` state logic; implement `dispatch_osc()`; update `reset_csi()` |
| `src/terminal/grid.hpp` | Declare `set_observer()` and `on_window_title_` callback |
| `src/terminal/grid.cpp` | Implement `set_observer()`; handle `set_window_title` in `apply()` |
| `src/platform/window.hpp` | Declare `set_window_title()` |
| `src/platform/window.cpp` | Implement `set_window_title()`; add friend declaration to `win32_window` |
| `src/app.cpp` | Set observer on `grid_`; restore title on shell exit |
| `tests/vt_parser_test.cpp` | Add OSC parsing tests (~12 new test cases) |

---

## Verification Checklist

- [ ] `ESC ] 0 ; title BEL` → window title bar updates to "title"
- [ ] `ESC ] 2 ; title ESC \` → window title bar updates to "title" (ST terminator)
- [ ] `ESC ] 1 ; title BEL` → window title bar updates to "title" (OSC 1 treated like 0/2)
- [ ] PowerShell prompt shows current directory in title bar
- [ ] `cd` commands update the title bar
- [ ] After `exit`, title bar reverts to "betty"
- [ ] Malformed OSC sequences do not crash or corrupt display
- [ ] All existing tests still pass (`ctest`)
