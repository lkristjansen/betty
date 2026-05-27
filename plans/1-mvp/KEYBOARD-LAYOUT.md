# Danish Keyboard Layout Support — Implementation Plan

## Problem

betty's `input_handler` currently uses a hardcoded **US keyboard shift table** (`k_shift_table`) to map virtual-key codes to characters. On a Danish keyboard layout, this is incorrect:

- Danish letters **Æ, Ø, Å** (and their lowercase/shifted variants) don't exist in the US table.
- Punctuation is rearranged: Shift+2 produces `"` (not `@`), the `'` key produces `ø`, etc.
- Dead-key compositions (e.g. `´` + `e` → `é`) are completely unsupported.
- The fallback of casting `vk_code` to `char` only works for ASCII 0x20–0x7E.

## Architecture Insight

The Windows message loop in `window.cpp` already captures **two** keyboard events:

| Message | Callback | What it gives | Currently wired? |
|---------|----------|---------------|------------------|
| `WM_KEYDOWN` | `on_key(vk, ctrl, shift, alt)` | Raw virtual-key code + modifiers | ✅ Yes |
| `WM_CHAR` | `on_char(codepoint)` | Unicode codepoint (Windows translates based on active keyboard layout) | ❌ No — defined but never connected |

`WM_CHAR` is the key to layout independence: Windows translates the raw key press through the user's active keyboard layout (Danish, US, German, etc.) and delivers the correct Unicode codepoint — including dead-key sequences, AltGr combos, and locale-specific punctuation.

The infrastructure to fix this is **already half-built**. The plan wires up `WM_CHAR` and simplifies `WM_KEYDOWN` to only handle non-printable keys.

## Design Decisions (from interview)

| # | Decision |
|---|----------|
| 1 | Full Danish layout support: all letters (Æ/Ø/Å), punctuation, dead-key compositions, AltGr combos. |
| 2 | **Split approach:** `WM_CHAR` handles all printable characters; `WM_KEYDOWN` handles only non-printable keys + Ctrl+letter combos. |
| 3 | AltGr+key output goes through `WM_CHAR` naturally (Windows translates it). Alt+non-printable keeps the ESC-prefix path in `WM_KEYDOWN`. |
| 4 | Ctrl+letter extended beyond A–Z: compute control code via `codepoint & 0x1F`. |
| 5 | Separate session methods: `write_keyboard(vk, ctrl, shift, alt)` and `write_char(uint32_t codepoint)`. |
| 6 | UTF-8 encoder lives as a private helper in `terminal_session.cpp`. |
| 7 | **Remove dead code:** `k_shift_table`, `lookup_shifted()`, and the printable-character fallback in `input_handler::on_keydown`. |
| 8 | Scrollback shortcuts (Ctrl+Shift+Arrows/PgUp/PgDn) are checked only in `write_keyboard`, not in `write_char`. |

---

## Step-by-Step Implementation Plan

### Step 1: Add UTF-8 encoding helper in `terminal_session.cpp`

Add a private free function in the anonymous namespace of `session.cpp`:

```cpp
// Encode a single Unicode codepoint into a UTF-8 byte sequence.
// Returns the UTF-8 bytes as a std::string.
[[nodiscard]] auto utf8_encode(uint32_t codepoint) -> std::string;
```

**Encoding rules:**

| Codepoint range | UTF-8 bytes | Bit layout |
|-----------------|-------------|------------|
| U+0000 – U+007F | 1 byte | `0xxxxxxx` |
| U+0080 – U+07FF | 2 bytes | `110xxxxx 10xxxxxx` |
| U+0800 – U+FFFF | 3 bytes | `1110xxxx 10xxxxxx 10xxxxxx` |
| U+10000 – U+10FFFF | 4 bytes | `11110xxx 10xxxxxx 10xxxxxx 10xxxxxx` |

**Test coverage:** Add test cases in a new `tests/session_test.cpp` (or extend existing tests):
- ASCII boundary: `'A'` (U+0041) → 1 byte
- Danish lowercase: `'æ'` (U+00E6) → 2 bytes
- Danish uppercase: `'Ø'` (U+00D8) → 2 bytes
- Euro sign: `'€'` (U+20AC) → 3 bytes
- Surrogate range: U+D800 – U+DFFF → reject (return empty, log warning)
- Above U+10FFFF → reject
- Emoji: `'😀'` (U+1F600) → 4 bytes

---

### Step 2: Add `write_char` method to `terminal_session`

**Files:** `src/terminal/session.hpp`, `src/terminal/session.cpp`

Add a new public method:

```cpp
// session.hpp
void write_char(uint32_t codepoint);
```

```cpp
// session.cpp
void terminal_session::write_char(uint32_t codepoint) {
  if (!shell_ || !platform::is_shell_running(*shell_)) return;
  if (shell_input_failed_) return;

  std::string bytes = utf8_encode(codepoint);
  if (!bytes.empty()) {
    if (auto res = platform::write_shell_input(*shell_, bytes); !res) {
      util::log_error(res.error(), "write shell char input");
      shell_input_failed_ = true;
    }
  }
}
```

This mirrors the existing `write_keyboard` structure — guard against dead/missing shell, encode, write, log on failure.

**Design note:** No scrollback-shortcut checks in `write_char` (per decision #8). Those shortcuts produce `WM_KEYDOWN` messages only.

---

### Step 3: Wire `on_char` callback in `application.cpp`

**Files:** `src/application.hpp`, `src/application.cpp`

Add a new callback handler to the `application` class:

```cpp
// application.hpp — new private method
void on_char(uint32_t codepoint);
```

```cpp
// application.cpp — implementation
void application::on_char(uint32_t codepoint) {
  session_.write_char(codepoint);
}
```

Wire it in `application::run()` alongside the existing `set_key_callback`:

```cpp
// In run(), after set_key_callback:
platform::set_char_callback(window_,
  [this](uint32_t codepoint) {
    on_char(codepoint);
  });
```

---

### Step 4: Simplify `input_handler` — remove US layout assumptions

**Files:** `src/terminal/input_handler.hpp`, `src/terminal/input_handler.cpp`

**Remove from `input_handler.cpp`:**
- The entire `k_shift_table` array (lines defining key_mapping entries for digits, punctuation).
- The `lookup_shifted()` function.
- The `#include <string_view>` and `k_csi` / `ansi_csi` helpers if they move elsewhere (they're still used for non-printable keys, so keep them).

**Modify `on_keydown`:**
- Remove the fallback block at the end that calls `lookup_shifted()` and the "printable ASCII vk_code value passes through as-is" fallback:

```cpp
  // --- Printable characters with shift support ---
  if (base.empty()) {
    char const c = lookup_shifted(vk, shift);       // DELETE
    if (c != '\0') {                                 // DELETE
      base = std::string(1, c);                      // DELETE
    } else {                                         // DELETE
      // Fallback: any printable ASCII vk_code...    // DELETE
      uint32_t const cp = static_cast<uint32_t>(vk); // DELETE
      if (cp >= 0x20 && cp <= 0x7E) {                // DELETE
        base = std::string(1, static_cast<char>(cp)); // DELETE
      }                                              // DELETE
    }                                                // DELETE
  }                                                  // DELETE
```

**Extend Ctrl+letter handling** to support any letter (not just A–Z):

Replace:
```cpp
  // --- Ctrl+letter → control character (0x01–0x1A) ---
  if (control && vk >= vk_code::printable_a && vk <= vk_code::printable_z) {
    char const c = static_cast<char>(
      static_cast<uint32_t>(vk) - static_cast<uint32_t>(vk_code::printable_a) + 1);
    base = std::string(1, c);
  }
```

With a general approach that also works for Ctrl+non-ASCII letters. However, `WM_KEYDOWN` only gives us `vk_code` values for A–Z and non-printable keys. For Ctrl+Æ/Ø/Å, Windows would actually send the Ctrl-modified codepoint through `WM_CHAR` (e.g., Ctrl+æ → U+0006). Since `WM_CHAR` already gives us the right control code via Windows' own translation, we get Ctrl+Æ/Ø/Å support for free through the `write_char` path.

So: **keep the existing Ctrl+A–Z logic as-is** (it still works, and non-ASCII Ctrl combos arrive via `WM_CHAR`). Add a comment explaining this.

**Result:** `on_keydown` now handles exclusively:
1. Ctrl+A–Z → control characters (0x01–0x1A)
2. Non-printable special keys (Enter, Backspace, Tab, Escape, Space, arrows, Home, End, PgUp, PgDn, Delete, Insert, F1–F12)
3. Alt prefix for non-printable keys (ESC-prefix logic stays)

---

### Step 5: Update `vk_code` enum in `platform/types.hpp`

The `vk_code` enum currently names punctuation keys with US-centric comments (e.g., `semicolon // ;`). Since these enums are used only for `WM_KEYDOWN` special-key dispatch now (not for character lookup), rename them to be layout-neutral. Remove the hardcoded US-character comments.

**Before:**
```cpp
  // Punctuation keys (US keyboard layout; unshifted character shown)
  semicolon      = 0x10C,  // ;
  comma          = 0x10D,  // ,
  period_        = 0x10E,  // .
  slash          = 0x10F,  // /
  backslash      = 0x110,
  bracket_left   = 0x111,  // [
  bracket_right  = 0x112,  // ]
  apostrophe     = 0x113,  // '
  minus          = 0x114,  // -
  equal_         = 0x115,  // =
  grave          = 0x116,  // `
```

**After:**
```cpp
  // OEM keys (physical key position; character varies by keyboard layout)
  oem_1           = 0x10C,  // VK_OEM_1  (US: ;/: , DK: æ/Æ)
  oem_comma       = 0x10D,  // VK_OEM_COMMA
  oem_period      = 0x10E,  // VK_OEM_PERIOD
  oem_2           = 0x10F,  // VK_OEM_2   (US: / /? , DK: -/_)
  oem_5           = 0x110,  // VK_OEM_5   (US: \ /| , DK: '/*)
  oem_4           = 0x111,  // VK_OEM_4   (US: [/ , DK: ½/§)
  oem_6           = 0x112,  // VK_OEM_6   (US: ]/ , DK: nothing common)
  oem_7           = 0x113,  // VK_OEM_7   (US: '/" , DK: ø/Ø)
  oem_minus       = 0x114,  // VK_OEM_MINUS
  oem_plus        = 0x115,  // VK_OEM_PLUS
  oem_3           = 0x116,  // VK_OEM_3   (US: `/~ , DK: ´/`)
```

Update all references in `window.cpp` (`map_vk`) and `input_handler.cpp` (`on_keydown` switch) to use the new names.

**Note:** These OEM keys aren't actually used in the simplified `on_keydown` anymore (no printable char path), so verify whether they're still referenced. If they're only used in `map_vk` for mapping Win32 VK codes → `vk_code` but never consumed downstream, they can be collapsed into `unknown` in `map_vk`. Only keep the ones that have a configured case in `on_keydown`. If none do, remove their enum values and simplify `map_vk` to return `unknown` for OEM keys.

---

### Step 6: Remove OEM key handling from `map_vk` in `window.cpp` (if unused)

After Step 4 + 5, check whether any OEM vk_code values still appear in `input_handler::on_keydown`. If not:

In `window.cpp`'s `map_vk` function, collapse all `VK_OEM_*` cases into the `default` branch (which returns `vk_code::unknown`). This simplifies the Win32→abstract-VK mapping and makes it clear that printable characters are handled by `WM_CHAR` exclusively.

**Before (snippet):**
```cpp
  case VK_OEM_1:       return vk_code::semicolon;
  case VK_OEM_COMMA:   return vk_code::comma;
  // ... all OEM cases
```

**After:**
```cpp
  // All OEM (punctuation) keys: let WM_CHAR handle character translation.
  // Return unknown to skip WM_KEYDOWN processing for these.
  // (VK_OEM_1 through VK_OEM_8, VK_OEM_102 fall through to default)
```

Remove the corresponding enum values from `vk_code` in step 5 if unreferenced.

---

### Step 7: Update tests for `input_handler`

**Files affected:** `tests/input_handler_test.cpp`

Existing tests likely test the old behavior (e.g., Shift+2 → `@`, Shift+`'` → `"`). Update them:

1. **Remove tests** that assert US-specific shift behavior (these are now handled by `WM_CHAR`, not `input_handler`).
2. **Update Ctrl+letter tests** — verify A–Z still produce control codes 0x01–0x1A.
3. **Keep/update tests** for non-printable keys (Enter → `\r`, Backspace → `\x7F`, arrows → CSI sequences, F-keys).
4. **Add a test** verifying that after the change, `on_keydown(vk_code::printable_a, false, true, false)` returns empty (shifted letters are now `WM_CHAR` territory, not `on_keydown`).
5. **Add tests** for boundary cases: calling `on_keydown` with an OEM key that has no non-printable mapping should return empty string.

---

### Step 8: Write integration-style test for `utf8_encode`

**Files affected:** `tests/session_test.cpp` (new if it doesn't exist), `tests/CMakeLists.txt`

Add a test file (or extend an existing one) for the UTF-8 encoder:

```
TEST_CASE("UTF-8 encoding") {
  // ASCII
  CHECK(utf8_encode(0x41) == "A");
  // Danish lowercase
  CHECK(utf8_encode(0xE6) == "\xC3\xA6"); // æ
  CHECK(utf8_encode(0xF8) == "\xC3\xB8"); // ø
  CHECK(utf8_encode(0xE5) == "\xC3\xA5"); // å
  // Danish uppercase
  CHECK(utf8_encode(0xC6) == "\xC3\x86"); // Æ
  CHECK(utf8_encode(0xD8) == "\xC3\x98"); // Ø
  CHECK(utf8_encode(0xC5) == "\xC3\x85"); // Å
  // 3-byte
  CHECK(utf8_encode(0x20AC) == "\xE2\x82\xAC"); // €
  // 4-byte
  CHECK(utf8_encode(0x1F600) == "\xF0\x9F\x98\x80"); // 😀
  // Invalid: surrogates
  CHECK(utf8_encode(0xD800).empty());
  // Invalid: beyond U+10FFFF
  CHECK(utf8_encode(0x110000).empty());
}
```

---

### Step 9: Build, compile, and manual verification

1. **Compile** with CMake (debug preset).
2. **Run the test suite:** ensure all existing tests pass, new tests pass.
3. **Manual smoke test on Danish keyboard layout:**
   - Type `æ`, `ø`, `å` (lowercase) → appear correctly in shell.
   - Shift+Æ, Shift+Ø, Shift+Å → uppercase Æ, Ø, Å.
   - Type `@` via AltGr+2 → appears.
   - Type `$` via AltGr+4 → appears.
   - Shift+2 → `"` (not `@` — Danish layout behavior).
   - Dead keys: `´` + `e` → `é`.
   - Ctrl+C → interrupts command.
   - Ctrl+Shift+Up/Down → scrollback navigation still works.
   - Arrows, Home, End, PgUp, PgDn → produce correct escape sequences.
   - Alt+letter → ESC-prefixed letter in shell (e.g., Alt+F → `^[f`).

---

## Files Changed Summary

| File | Change |
|------|--------|
| `src/terminal/session.hpp` | Add `write_char(uint32_t)` declaration |
| `src/terminal/session.cpp` | Add `utf8_encode()` helper + `write_char()` implementation |
| `src/application.hpp` | Add `on_char(uint32_t)` private method |
| `src/application.cpp` | Add `on_char()` impl + wire `set_char_callback` in `run()` |
| `src/terminal/input_handler.hpp` | No API change (signature unchanged) |
| `src/terminal/input_handler.cpp` | Remove `k_shift_table` + `lookup_shifted()` + printable fallback; extend Ctrl+letter comment |
| `src/platform/types.hpp` | Rename OEM enum values to layout-neutral names; remove unreferenced entries |
| `src/platform/window.cpp` | Collapse unused OEM VK mappings into `default` in `map_vk()` |
| `tests/input_handler_test.cpp` | Remove US-shift tests; add empty-result tests for OEM/shifted-printable |
| `tests/session_test.cpp` (new) | Add UTF-8 encoding tests |
| `tests/CMakeLists.txt` | Register new test file |
| `plans/KEYBOARD-LAYOUT.md` | This plan |

---

## Risk Assessment

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Legacy key combinations that relied on `WM_KEYDOWN` for all input break | Low | The only removed path is the printable-character fallback. Non-printable keys are untouched. `WM_CHAR` is the canonical way Windows delivers characters. |
| Ctrl+Æ/Ø/Å produce unexpected control codes | Low | Windows translates Ctrl+letter through `WM_CHAR` before we receive it. The resulting codepoint is the standard control character. |
| Dead-key state gets out of sync | Low | Windows manages dead-key state internally. `WM_CHAR` only fires when composition is complete. |
| Performance regression from two-callback path | Negligible | Both callbacks are simple function calls; no additional allocations beyond the UTF-8 string. |
| Test breakage from renamed enums | Moderate | `input_handler_test.cpp` references `vk_code` enum values. Update names consistently. |

---

## Out of Scope

- Runtime keyboard layout detection (Danish vs US auto-switch).
- Configuration file / command-line flag for layout choice.
- Support for IME-based input (CJK composition windows).
- Non-Windows platform support (this is a Windows-specific fix using `WM_CHAR`).
- Changing the shell from PowerShell to something else.
