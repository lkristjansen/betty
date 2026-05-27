# C4 — Parse config.toml into a betty_config struct

## Goal

Introduce the `betty_config` struct and `parse_config()` function. On launch, betty looks for `config.toml` next to the executable, parses it via `tomlplusplus`, and populates the config struct. If the file is missing, silent default. If TOML syntax is invalid, an error message is returned (displayed as a message box by `main`).

**User sees:** If a valid `config.toml` exists next to `betty.exe`, betty starts normally (values not yet wired — no visible change). If the file is missing, betty starts silently with defaults. If TOML syntax errors exist, an error message box appears, then betty starts with defaults.

---

## Design Decisions (from interview)

1. **New module: `src/config/`.** Dedicated directory with own `CMakeLists.txt` producing a static library `config`. Keeps config parsing isolated as it grows through C5–C10.

2. **`std::optional<T>` for every field.** All fields are optional with in-class defaults. `parse_config()` starts from an all-default struct and overwrites only keys present in the TOML. Cleanly separates "missing = default" from "present but invalid" (C5).

3. **`cursor_style` is a plain `std::string`.** `std::optional<std::string> cursor_style = "block"`. Mirrors the TOML schema faithfully. Validation in C5, enum conversion in C7.

4. **`parse_config(exe_dir)` takes explicit path.** Caller provides the executable directory as `std::filesystem::path`. `main` resolves it via `GetModuleFileNameW`. Single entry point, fully testable.

5. **FetchContent in `src/config/CMakeLists.txt`.** Local to the config module — the only consumer of tomlplusplus.

6. **Return `std::expected<betty_config, std::string>`.** On file-missing: return default config (success). On TOML parse error: return error string. `main` calls `platform::show_error_message()` for the error case. Clean separation: `parse_config` never touches the Windows GUI.

7. **Explicit per-key reads.** Each known key is read from the parsed `toml::table` individually with a simple `if`/`value<T>()` check. Unknown keys are silently ignored (never iterated).

---

## betty_config Schema

```cpp
struct betty_config {
  std::optional<std::string> font_family      = "Consolas";
  std::optional<float>       font_size        = 14.0f;
  std::optional<std::string> cursor_style     = "block";
  std::optional<uint32_t>    scrollback_lines = 10000;
  std::optional<uint32_t>    columns          = 120;
  std::optional<uint32_t>    rows             = 40;
  std::optional<std::string> shell            = "powershell.exe -NoProfile -NoLogo";
};
```

All fields use `std::optional` with in-class defaults. Consumers always get a value (never `std::nullopt` after a successful parse).

---

## Files Changed / Created

| File | Action |
|---|---|
| `src/config/config.hpp` | **New.** `betty_config` struct and `parse_config()` declaration. |
| `src/config/config.cpp` | **New.** `parse_config()` implementation — path resolution, TOML parsing, key extraction. |
| `src/config/CMakeLists.txt` | **New.** Static library `config` with FetchContent for tomlplusplus. |
| `src/CMakeLists.txt` | Add `add_subdirectory(config)` and link `config` to `betty`. |
| `src/main.cpp` | Resolve exe directory, call `parse_config()`, show error box on failure. |
| `tests/config_test.cpp` | **New.** Unit tests for `parse_config()`. |
| `tests/CMakeLists.txt` | Add `config_test.cpp`, link `config`. |

---

## Step-by-step Implementation

### Step 1: Create `src/config/CMakeLists.txt`

```cmake
# tomlplusplus — header-only TOML parser
include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

add_library(config STATIC
    config.cpp
)

target_include_directories(config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(config PUBLIC
    tomlplusplus::tomlplusplus
)

target_compile_features(config PUBLIC cxx_std_23)

target_compile_options(config PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/utf-8>
  $<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Werror>
)
```

### Step 2: Create `src/config/config.hpp`

```cpp
#pragma once
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace betty {

struct betty_config {
  std::optional<std::string> font_family      = "Consolas";
  std::optional<float>       font_size        = 14.0f;
  std::optional<std::string> cursor_style     = "block";
  std::optional<uint32_t>    scrollback_lines = 10000;
  std::optional<uint32_t>    columns          = 120;
  std::optional<uint32_t>    rows             = 40;
  std::optional<std::string> shell            = "powershell.exe -NoProfile -NoLogo";
};

// Parse config.toml from the given directory (the executable's directory).
// Returns the populated config on success (missing file = success with defaults).
// Returns an error string on TOML syntax errors.
[[nodiscard]] auto parse_config(std::filesystem::path const& exe_dir)
    -> std::expected<betty_config, std::string>;

} // namespace betty
```

Note: `std::expected<T, std::string>` may trigger warnings about non-trivial error types in some compilers. If so, we can wrap the error in a lightweight struct. But for now, this is the simplest approach.

### Step 3: Create `src/config/config.cpp`

Key implementation details:

1. **Path construction:** `exe_dir / "config.toml"`.
2. **Missing file check:** `std::filesystem::exists(config_path)`. If missing, return default `betty_config{}`.
3. **Parse TOML:** `toml::parse_file(config_path.string())` inside a try/catch for `toml::parse_error`. On error, return the error description as the unexpected string.
4. **Extract keys (explicit per-key):**
   ```cpp
   betty_config cfg{};
   auto const& tbl = *parsed_table;  // toml::table
   
   if (auto val = tbl["font_family"].value<std::string>())
       cfg.font_family = *val;
   if (auto val = tbl["font_size"].value<double>())
       cfg.font_size = static_cast<float>(*val);
   if (auto val = tbl["cursor_style"].value<std::string>())
       cfg.cursor_style = *val;
   if (auto val = tbl["scrollback_lines"].value<int64_t>())
       cfg.scrollback_lines = static_cast<uint32_t>(*val);
   if (auto val = tbl["columns"].value<int64_t>())
       cfg.columns = static_cast<uint32_t>(*val);
   if (auto val = tbl["rows"].value<int64_t>())
       cfg.rows = static_cast<uint32_t>(*val);
   if (auto val = tbl["shell"].value<std::string>())
       cfg.shell = *val;
   
   return cfg;
   ```
   Unknown keys are implicitly ignored — we only read the 7 known keys.
   **No type validation here** — if a value has the wrong TOML type (e.g., `font_size = "hello"`), `value<T>()` returns `std::nullopt` and the key is simply skipped (falls back to default). C5 will add validation that checks and reports such errors.

### Step 4: Update `src/main.cpp`

```cpp
#include "application.hpp"
#include "config/config.hpp"
#include "platform/window.hpp"  // for show_error_message
#include <windows.h>

int main() {
  // Resolve the executable's directory.
  std::filesystem::path exe_dir;
  {
    wchar_t path_buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path_buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
      exe_dir = std::filesystem::path(std::wstring_view(path_buf, len)).parent_path();
    }
  }

  // Parse configuration (non-fatal on error).
  auto config_result = betty::parse_config(exe_dir);
  if (!config_result) {
    platform::show_error_message("betty — Configuration Error", config_result.error());
    // Continue with defaults — config is not yet wired, so nothing to fall back.
  }

  auto app = betty::make_application();
  if (!app) return 1;
  return app->run();
}
```

The parsed config isn't used yet — that's C6's job.

### Step 5: Update `src/CMakeLists.txt`

Add:
```cmake
add_subdirectory(config)
```
and update the link line:
```cmake
target_link_libraries(betty PRIVATE platform terminal config)
```

### Step 6: Create `tests/config_test.cpp`

Test cases:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "config/config.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// Helper: write a string to a temp file and return the path.
static auto write_temp_config(fs::path const& dir, std::string_view content) -> fs::path {
  auto p = dir / "config.toml";
  std::ofstream ofs(p, std::ios::binary);
  ofs << content;
  return p;
}

TEST_CASE("parse_config — missing file returns defaults", "[config]") {
    fs::path empty_dir = fs::temp_directory_path() / "betty_test_missing";
    fs::create_directories(empty_dir);
    // Ensure no config.toml exists.
    fs::remove(empty_dir / "config.toml");
    
    auto result = betty::parse_config(empty_dir);
    REQUIRE(result.has_value());
    auto const& cfg = *result;
    
    CHECK(cfg.font_family.value()      == "Consolas");
    CHECK(cfg.font_size.value()        == 14.0f);
    CHECK(cfg.cursor_style.value()     == "block");
    CHECK(cfg.scrollback_lines.value() == 10000);
    CHECK(cfg.columns.value()          == 120);
    CHECK(cfg.rows.value()             == 40);
    CHECK(cfg.shell.value()            == "powershell.exe -NoProfile -NoLogo");
    
    fs::remove_all(empty_dir);
}

TEST_CASE("parse_config — valid TOML populates fields", "[config]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_valid";
    fs::create_directories(dir);
    write_temp_config(dir, R"(
font_family = "Cascadia Code"
font_size = 13.0
cursor_style = "none"
scrollback_lines = 20000
columns = 140
rows = 50
shell = "pwsh.exe -NoLogo"
)");
    
    auto result = betty::parse_config(dir);
    REQUIRE(result.has_value());
    auto const& cfg = *result;
    
    CHECK(cfg.font_family.value()      == "Cascadia Code");
    CHECK(cfg.font_size.value()        == 13.0f);
    CHECK(cfg.cursor_style.value()     == "none");
    CHECK(cfg.scrollback_lines.value() == 20000);
    CHECK(cfg.columns.value()          == 140);
    CHECK(cfg.rows.value()             == 50);
    CHECK(cfg.shell.value()            == "pwsh.exe -NoLogo");
    
    fs::remove_all(dir);
}

TEST_CASE("parse_config — partial TOML mixes defaults with overrides", "[config]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_partial";
    fs::create_directories(dir);
    write_temp_config(dir, R"(
font_family = "JetBrains Mono"
scrollback_lines = 5000
)");
    
    auto result = betty::parse_config(dir);
    REQUIRE(result.has_value());
    auto const& cfg = *result;
    
    CHECK(cfg.font_family.value()      == "JetBrains Mono");
    CHECK(cfg.scrollback_lines.value() == 5000);
    // Unspecified keys use defaults.
    CHECK(cfg.font_size.value()    == 14.0f);
    CHECK(cfg.cursor_style.value() == "block");
    CHECK(cfg.columns.value()      == 120);
    CHECK(cfg.rows.value()         == 40);
    CHECK(cfg.shell.value()        == "powershell.exe -NoProfile -NoLogo");
    
    fs::remove_all(dir);
}

TEST_CASE("parse_config — unknown keys are silently ignored", "[config]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_unknown";
    fs::create_directories(dir);
    write_temp_config(dir, R"(
font_family = "Consolas"
made_up_key = "whatever"
another_one = 42
)");
    
    auto result = betty::parse_config(dir);
    REQUIRE(result.has_value());
    auto const& cfg = *result;
    CHECK(cfg.font_family.value() == "Consolas");
    // All other fields should be at defaults.
    CHECK(cfg.font_size.value() == 14.0f);
    
    fs::remove_all(dir);
}

TEST_CASE("parse_config — TOML syntax error returns error string", "[config]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_bad";
    fs::create_directories(dir);
    write_temp_config(dir, R"(
font_family = "Consolas"
  = invalid syntax!!!
)");
    
    auto result = betty::parse_config(dir);
    REQUIRE(!result.has_value());
    // Error string should contain something meaningful.
    CHECK(!result.error().empty());
    
    fs::remove_all(dir);
}

TEST_CASE("parse_config — empty TOML file returns all defaults", "[config]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_empty";
    fs::create_directories(dir);
    write_temp_config(dir, "");
    
    auto result = betty::parse_config(dir);
    REQUIRE(result.has_value());
    auto const& cfg = *result;
    
    CHECK(cfg.font_family.value() == "Consolas");
    CHECK(cfg.font_size.value() == 14.0f);
    
    fs::remove_all(dir);
}
```

### Step 7: Update `tests/CMakeLists.txt`

Add `config_test.cpp` and link `config`:

```cmake
add_executable(betty_tests
    ...
    shell_test.cpp
    config_test.cpp
)

target_link_libraries(betty_tests PRIVATE
    terminal
    platform
    config
    Catch2::Catch2WithMain
)
```

---

## Build and Test

```bash
cmake --build build/debug && ctest --test-dir build/debug --output-on-failure
```

All tests pass. betty launches with defaults (config values are parsed but not yet wired).

---

## Acceptance

- `betty_config` struct exists with all 7 fields, each `std::optional<T>` with an in-class default matching the PRD.
- `parse_config(exe_dir)` resolves `config.toml` correctly.
- Missing file → default config, no error.
- Valid TOML → populated config with overridden fields.
- TOML syntax error → error string returned, `main` shows message box, betty continues with defaults.
- Unknown keys silently ignored.
- `tomlplusplus` fetched via CMake at build time.
- Tests pass: missing file, valid TOML, partial TOML, unknown keys, syntax error, empty file.
