# C3 — Refactor: shell command line as parameter

## Goal

Remove the hardcoded `L"powershell.exe -NoProfile -NoLogo"` from `make_shell`. Add a `std::string command_line` field to `shell_settings` with an in-class default, and use it in `make_shell` to spawn the process.

**User sees:** No change. betty still launches PowerShell with `-NoProfile -NoLogo`.

---

## Design Decisions (from interview)

1. **In-class default on `shell_settings`.** `std::string command_line = "powershell.exe -NoProfile -NoLogo";` — matches the PRD spec, clean call sites.

2. **Widen with fallback.** In `make_shell`, call `platform::widen(settings.command_line)`. On failure, log the error and fall back to the default `L"powershell.exe -NoProfile -NoLogo"`. More resilient than propagating the error.

3. **Struct-level unit test only.** New `tests/shell_test.cpp` verifies default construction and custom command_line assignment. No process spawning.

---

## Files Changed

| File | Change |
|---|---|
| `src/platform/shell.hpp` | Add `std::string command_line` field to `shell_settings` with in-class default. |
| `src/platform/shell.cpp` | Replace hardcoded `L"powershell.exe ..."` with widened `settings.command_line`. Fall back to default on widen failure. Add `#include "unicode.hpp"`. |
| `src/application.cpp` | (Optional — not strictly needed) `make_application` already constructs `shell_settings{.cols = cols, .rows = rows}` — the new field defaults, so no change required. |
| `tests/shell_test.cpp` | **New file.** Unit tests for `shell_settings` default and custom command line. |
| `tests/CMakeLists.txt` | Add `shell_test.cpp` to the test executable. |

---

## Step-by-step Implementation

### Step 1: Update `shell_settings` in `shell.hpp`

Add the new field with in-class default:

```cpp
struct shell_settings {
  uint32_t cols = 120;            // initial column count
  uint32_t rows = 40;             // initial row count
  std::string command_line = "powershell.exe -NoProfile -NoLogo";  // shell command line
};
```

Also add in-class defaults for `cols` and `rows` while we're here, since `make_shell` already falls back to 120×40 when they're zero. This makes the defaults explicit and documented in one place.

### Step 2: Update `make_shell` in `shell.cpp`

Add `#include "unicode.hpp"` and replace the hardcoded command line:

```cpp
// BEFORE (line ~236)
std::wstring cmd_line = L"powershell.exe -NoProfile -NoLogo";

// AFTER
auto wide_result = widen(settings.command_line);
std::wstring cmd_line;
if (wide_result) {
  cmd_line = std::move(*wide_result);
} else {
  util::log_error(wide_result.error(), "widen shell command_line");
  cmd_line = L"powershell.exe -NoProfile -NoLogo";
}
```

The `log_error` include is already present (`util/log.hpp`), and `widen` is already available from `unicode.hpp`.

### Step 3: Verify `make_application` needs no change

`make_application` currently constructs:
```cpp
platform::shell_settings{
  .cols = cols,
  .rows = rows
}
```

Since `command_line` has an in-class default, this works unchanged. The default `"powershell.exe -NoProfile -NoLogo"` is used automatically. No edit needed.

### Step 4: Add unit tests — `tests/shell_test.cpp`

Create a new test file:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "platform/shell.hpp"

using namespace betty::platform;

TEST_CASE("shell_settings — default command_line", "[shell_settings]") {
    shell_settings s{};
    CHECK(s.command_line == "powershell.exe -NoProfile -NoLogo");
}

TEST_CASE("shell_settings — custom command_line", "[shell_settings]") {
    shell_settings s{
        .command_line = "cmd.exe /c echo hello"
    };
    CHECK(s.command_line == "cmd.exe /c echo hello");
}

TEST_CASE("shell_settings — command_line survives move", "[shell_settings]") {
    shell_settings s{
        .command_line = "pwsh.exe -NoLogo"
    };
    shell_settings moved{std::move(s)};
    CHECK(moved.command_line == "pwsh.exe -NoLogo");
}

TEST_CASE("shell_settings — all fields with custom values", "[shell_settings]") {
    shell_settings s{
        .cols = 100,
        .rows = 30,
        .command_line = "bash.exe --login"
    };
    CHECK(s.cols == 100);
    CHECK(s.rows == 30);
    CHECK(s.command_line == "bash.exe --login");
}
```

### Step 5: Update `tests/CMakeLists.txt`

Add `shell_test.cpp` to the test sources list. Let me check the current list format:

The test sources are listed in CMakeLists.txt — add `shell_test.cpp` to that list.

---

## Build and Test

```bash
cmake --build build/debug && ctest --test-dir build/debug --output-on-failure
```

All tests must pass, including the new `shell_settings` tests.

---

## Acceptance

- `shell_settings::command_line` exists with default `"powershell.exe -NoProfile -NoLogo"`.
- `make_shell` uses `settings.command_line` (widened) instead of the hardcoded literal.
- On widen failure, `make_shell` logs the error and falls back to the default command line.
- `make_application` needs no change (in-class default used).
- New `shell_test.cpp` tests pass: default value, custom value, move semantics, all-fields.
- betty launches PowerShell identically to before.
