# C5 — Validate config values and show errors

## Goal

Add validation of parsed config fields against their constraints. Collect all errors into a single structured report. Shell errors are fatal (terminate); all other errors show a message box and continue with defaults.

**User sees:** If `config.toml` has invalid values (e.g., `cursor_style = "beam"`, `scrollback_lines = -50`), betty shows a **single message box** listing **all** validation errors, falls back to defaults for the bad keys, and continues launching. If `shell` is empty or invalid, betty shows an error message box and **terminates**.

---

## Design Decisions (from interview)

1. **Same config module.** Declaration in `config.hpp`, implementation in `config.cpp`.

2. **Structured `config_error`.** `key`, `message`, `value` fields for rich error formatting.

3. **TOML syntax errors as uniform `config_error`.** Wrapped in the same vector with a sentinel key `"(syntax)"` for uniform handling.

4. **`bool fatal` flag.** Shell validation errors set `fatal = true`. `main` checks for any fatal errors and exits.

5. **`parse_config` calls `validate_config` internally.** Single integration point for `main`.

6. **`parse_result` struct.** Contains both `betty_config config` and `std::vector<config_error> errors`. Always yields a usable config (bad fields reset to defaults).

---

## Validation Rules

| Key | Rule | Error on violation | Fatal? |
|---|---|---|---|
| `font_family` | Non-empty string | Yes — error, reset to `"Consolas"` | No |
| `font_size` | Positive float | **Clamp to [6.0, 36.0]** — silent | — |
| `cursor_style` | `"block"` or `"none"` (case-sensitive) | Yes — error, reset to `"block"` | No |
| `scrollback_lines` | Integer >= 1 | Yes — error, reset to `10000` | No |
| `columns` | Integer >= 1 | Yes — error, reset to `120` | No |
| `rows` | Integer >= 1 | Yes — error, reset to `40` | No |
| `shell` | Non-empty string | Yes — error, **fatal** | Yes |

Note: `font_size` clamping is NOT an error. It's a silent correction. The PRD explicitly says "clamped, not rejected."

---

## Files Changed

| File | Change |
|---|---|
| `src/config/config.hpp` | Add `config_error` struct, `parse_result` struct, `format_validation_errors()`. Remove `std::expected` return. |
| `src/config/config.cpp` | Add `validate_config()` (anonymous namespace). Integrate into `parse_config`. Add `format_validation_errors()`. |
| `src/main.cpp` | Destructure `parse_result`, check fatal errors, show message box for non-fatal. |
| `tests/config_test.cpp` | Add validation tests. Update existing tests for new return type. |

---

## Step-by-step Implementation

### Step 1: Update `config.hpp`

```cpp
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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

// ---- validation types ----------------------------------------------------

struct config_error {
  std::string key;      // field name, e.g. "cursor_style"
  std::string message;  // human-readable description
  std::string value;    // the offending value from the TOML file
  bool fatal = false;   // if true, betty must terminate after showing the error
};

struct parse_result {
  betty_config config;                    // always populated (defaults if parse fails)
  std::vector<config_error> errors;       // empty on success
};

// Format a vector of config_errors into a multi-line message suitable for
// a Windows message box.  Returns an empty string if the vector is empty.
[[nodiscard]] auto format_validation_errors(std::vector<config_error> const& errors)
    -> std::string;

// ---- parse_config --------------------------------------------------------

// Parse and validate config.toml from `exe_dir` (the executable's directory).
// Always returns a usable config (bad fields are reset to their defaults).
// Check `errors` for any problems — format them with format_validation_errors().
[[nodiscard]] auto parse_config(std::filesystem::path const& exe_dir)
    -> parse_result;

} // namespace betty
```

Note: `<expected>` is no longer needed in config.hpp.

### Step 2: Update `config.cpp`

Add `validate_config` in an anonymous namespace and integrate into `parse_config`:

```cpp
#include "config.hpp"
#include <fstream>
#include <toml++/toml.hpp>

namespace betty {

namespace {

void validate_config(betty_config& cfg, std::vector<config_error>& errors) {
  // font_family: non-empty
  if (cfg.font_family->empty()) {
    errors.push_back({"font_family", "must not be empty",
                      "\"\"", false});
    cfg.font_family = "Consolas";
  }

  // font_size: clamp to [6.0, 36.0] — silent correction, no error
  if (*cfg.font_size < 6.0f) {
    cfg.font_size = 6.0f;
  } else if (*cfg.font_size > 36.0f) {
    cfg.font_size = 36.0f;
  }

  // cursor_style: must be "block" or "none"
  {
    auto const& cs = *cfg.cursor_style;
    if (cs != "block" && cs != "none") {
      errors.push_back({"cursor_style",
                        "must be \"block\" or \"none\", got \"" + cs + "\"",
                        cs, false});
      cfg.cursor_style = "block";
    }
  }

  // scrollback_lines: >= 1
  if (*cfg.scrollback_lines < 1) {
    errors.push_back({"scrollback_lines",
                      "must be >= 1, got " + std::to_string(*cfg.scrollback_lines),
                      std::to_string(*cfg.scrollback_lines), false});
    cfg.scrollback_lines = 10000;
  }

  // columns: >= 1
  if (*cfg.columns < 1) {
    errors.push_back({"columns",
                      "must be >= 1, got " + std::to_string(*cfg.columns),
                      std::to_string(*cfg.columns), false});
    cfg.columns = 120;
  }

  // rows: >= 1
  if (*cfg.rows < 1) {
    errors.push_back({"rows",
                      "must be >= 1, got " + std::to_string(*cfg.rows),
                      std::to_string(*cfg.rows), false});
    cfg.rows = 40;
  }

  // shell: non-empty (fatal)
  if (cfg.shell->empty()) {
    errors.push_back({"shell",
                      "must not be empty (betty cannot launch without a shell)",
                      "\"\"", true});
    // Shell is not reset — betty will exit before using it.
  }
}

} // anonymous namespace

// ---- format_validation_errors --------------------------------------------

auto format_validation_errors(std::vector<config_error> const& errors)
    -> std::string {
  if (errors.empty()) return {};
  
  std::string msg = "The following settings in config.toml are invalid:\r\n\r\n";
  for (auto const& e : errors) {
    msg += "  \xE2\x80\xA2 ";  // bullet (U+2022) in UTF-8
    msg += e.key;
    msg += ": ";
    msg += e.message;
    msg += "\r\n";
  }
  msg += "\r\nThese settings have been reset to their defaults. betty will continue.";
  return msg;
}

// ---- parse_config (updated) ----------------------------------------------

auto parse_config(std::filesystem::path const& exe_dir) -> parse_result {
  parse_result result{};
  auto const config_path = exe_dir / "config.toml";

  // Missing file → silent default.
  if (!std::filesystem::exists(config_path)) {
    return result;  // config is all defaults, errors is empty
  }

  // Parse the TOML file.
  toml::table tbl;
  try {
    tbl = toml::parse_file(config_path.string());
  } catch (toml::parse_error const& e) {
    result.errors.push_back({"(syntax)", std::string{e.description()}, "", false});
    return result;  // config stays at all defaults
  }

  // Extract known keys (unchanged from C4).
  betty_config& cfg = result.config;

  if (auto val = tbl["font_family"].value<std::string>()) {
    cfg.font_family = *val;
  }
  if (auto val = tbl["font_size"].value<double>()) {
    cfg.font_size = static_cast<float>(*val);
  } else if (auto val = tbl["font_size"].value<int64_t>()) {
    cfg.font_size = static_cast<float>(*val);
  }
  if (auto val = tbl["cursor_style"].value<std::string>()) {
    cfg.cursor_style = *val;
  }
  if (auto val = tbl["scrollback_lines"].value<int64_t>()) {
    cfg.scrollback_lines = static_cast<uint32_t>(*val);
  }
  if (auto val = tbl["columns"].value<int64_t>()) {
    cfg.columns = static_cast<uint32_t>(*val);
  }
  if (auto val = tbl["rows"].value<int64_t>()) {
    cfg.rows = static_cast<uint32_t>(*val);
  }
  if (auto val = tbl["shell"].value<std::string>()) {
    cfg.shell = *val;
  }

  // Validate.
  validate_config(cfg, result.errors);

  return result;
}

} // namespace betty
```

### Step 3: Update `main.cpp`

```cpp
#include "application.hpp"
#include "config/config.hpp"
#include "platform/window.hpp"
#include <windows.h>

int main() {
  // Resolve the executable's directory.
  std::filesystem::path exe_dir;
  {
    wchar_t path_buf[MAX_PATH];
    DWORD const len = GetModuleFileNameW(nullptr, path_buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
      exe_dir = std::filesystem::path(std::wstring_view(path_buf, len)).parent_path();
    }
  }

  // Parse and validate configuration.
  auto [cfg, errors] = betty::parse_config(exe_dir);

  // Check for fatal errors (shell) — terminate immediately.
  bool has_fatal = false;
  for (auto const& e : errors) {
    if (e.fatal) has_fatal = true;
  }

  if (!errors.empty()) {
    auto msg = betty::format_validation_errors(errors);
    betty::platform::show_error_message("betty - Configuration Error", msg);
    if (has_fatal) return 1;
  }

  // (cfg is not used yet — will be wired in C6.)

  auto app = betty::make_application();
  if (!app) return 1;
  return app->run();
}
```

### Step 4: Update `tests/config_test.cpp`

All existing tests change from `result.has_value()` / `*result` to `result.errors.empty()` / `result.config`. Add new validation tests:

```cpp
// ---- NEW validation tests ----

TEST_CASE("parse_config — validation: empty font_family is an error", "[config][validation]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_val_font";
    fs::create_directories(dir);
    write_temp_config(dir, "font_family = \"\"\n");

    auto result = betty::parse_config(dir);
    CHECK(result.errors.size() >= 1);
    CHECK(result.errors[0].key == "font_family");
    CHECK(result.config.font_family.value() == "Consolas");  // reset to default

    fs::remove_all(dir);
}

TEST_CASE("parse_config — validation: bad cursor_style is an error", "[config][validation]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_val_cursor";
    fs::create_directories(dir);
    write_temp_config(dir, "cursor_style = \"beam\"\n");

    auto result = betty::parse_config(dir);
    CHECK(result.errors.size() >= 1);
    CHECK(result.errors[0].key == "cursor_style");
    CHECK(result.config.cursor_style.value() == "block");  // reset to default

    fs::remove_all(dir);
}

TEST_CASE("parse_config — validation: scrollback_lines zero is an error", "[config][validation]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_val_scrollback";
    fs::create_directories(dir);
    write_temp_config(dir, "scrollback_lines = 0\n");

    auto result = betty::parse_config(dir);
    CHECK(result.errors.size() >= 1);
    CHECK(result.errors[0].key == "scrollback_lines");
    CHECK(result.config.scrollback_lines.value() == 10000);

    fs::remove_all(dir);
}

TEST_CASE("parse_config — validation: columns zero is an error", "[config][validation]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_val_cols";
    fs::create_directories(dir);
    write_temp_config(dir, "columns = 0\n");

    auto result = betty::parse_config(dir);
    CHECK(result.errors.size() >= 1);
    CHECK(result.errors[0].key == "columns");
    CHECK(result.config.columns.value() == 120);

    fs::remove_all(dir);
}

TEST_CASE("parse_config — validation: rows zero is an error", "[config][validation]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_val_rows";
    fs::create_directories(dir);
    write_temp_config(dir, "rows = 0\n");

    auto result = betty::parse_config(dir);
    CHECK(result.errors.size() >= 1);
    CHECK(result.errors[0].key == "rows");
    CHECK(result.config.rows.value() == 40);

    fs::remove_all(dir);
}

TEST_CASE("parse_config — validation: empty shell is fatal", "[config][validation]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_val_shell";
    fs::create_directories(dir);
    write_temp_config(dir, "shell = \"\"\n");

    auto result = betty::parse_config(dir);
    REQUIRE(result.errors.size() >= 1);
    CHECK(result.errors[0].key == "shell");
    CHECK(result.errors[0].fatal == true);

    fs::remove_all(dir);
}

TEST_CASE("parse_config — validation: font_size is clamped, not errored", "[config][validation]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_val_fontsize";
    fs::create_directories(dir);

    // Too small → clamped to 6.0
    write_temp_config(dir, "font_size = 2.0\n");
    auto result = betty::parse_config(dir);
    CHECK(result.errors.empty());
    CHECK(result.config.font_size.value() == 6.0f);

    // Too large → clamped to 36.0
    write_temp_config(dir, "font_size = 100.0\n");
    result = betty::parse_config(dir);
    CHECK(result.errors.empty());
    CHECK(result.config.font_size.value() == 36.0f);

    fs::remove_all(dir);
}

TEST_CASE("parse_config — validation: multiple errors collected", "[config][validation]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_val_multi";
    fs::create_directories(dir);
    write_temp_config(dir, R"(
cursor_style = "underline"
scrollback_lines = -50
columns = 0
)");

    auto result = betty::parse_config(dir);
    CHECK(result.errors.size() == 3);
    // Verify all bad keys are present.
    bool has_cursor = false, has_scrollback = false, has_columns = false;
    for (auto const& e : result.errors) {
      if (e.key == "cursor_style") has_cursor = true;
      if (e.key == "scrollback_lines") has_scrollback = true;
      if (e.key == "columns") has_columns = true;
    }
    CHECK(has_cursor);
    CHECK(has_scrollback);
    CHECK(has_columns);

    // All bad fields reset to defaults.
    CHECK(result.config.cursor_style.value() == "block");
    CHECK(result.config.scrollback_lines.value() == 10000);
    CHECK(result.config.columns.value() == 120);

    fs::remove_all(dir);
}
```

Also update all existing tests:
- `result.has_value()` → `result.errors.empty()`
- `*result` → `result.config`
- `!result.has_value()` → `!result.errors.empty()`
- `result.error()` → `result.errors[0].message` (for the syntax error test)

---

## Build and Test

```bash
cmake --build build/debug && ctest --test-dir build/debug --output-on-failure
```

---

## Acceptance

- `parse_config` returns `parse_result` with `config` + `errors`.
- Validation rules applied per the PRD: font_family, cursor_style, scrollback_lines, columns, rows, shell.
- `font_size` clamped silently (6.0–36.0), no error.
- Multiple errors collected into single vector.
- Shell empty → fatal error flag set.
- `format_validation_errors()` produces multi-line message with bullet points.
- `main` shows message box for non-fatal errors, continues. Fatal shell error terminates.
- All existing and new tests pass.
