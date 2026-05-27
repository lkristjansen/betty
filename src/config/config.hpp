#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace betty {

// ===========================================================================
// betty_config — all user-configurable settings
// ===========================================================================
// Every field is optional with an in-class default matching the built-in
// hardcoded value.  After a successful parse_config(), all fields have a
// value (the default or the user's override).  Invalid fields are reset
// to their defaults during validation.

struct betty_config {
  std::optional<std::string> font_family      = "Consolas";
  std::optional<float>       font_size        = 14.0f;
  std::optional<std::string> cursor_style     = "block";
  std::optional<uint32_t>    scrollback_lines = 10000;
  std::optional<uint32_t>    columns          = 120;
  std::optional<uint32_t>    rows             = 40;
  std::optional<std::string> shell            = "powershell.exe -NoProfile -NoLogo";
};

// ===========================================================================
// Validation types
// ===========================================================================

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

// ===========================================================================
// format_validation_errors
// ===========================================================================
// Format a vector of config_errors into a multi-line message suitable for
// a Windows message box.  Returns an empty string if the vector is empty.

[[nodiscard]] auto format_validation_errors(std::vector<config_error> const& errors)
    -> std::string;

// ===========================================================================
// parse_config — load, parse, and validate config.toml
// ===========================================================================
// Looks for `config.toml` inside `exe_dir` (the directory containing
// betty.exe).  Callers obtain `exe_dir` via GetModuleFileNameW +
// parent_path().
//
// Always returns a usable config — bad fields are reset to their defaults.
// Check `errors` for any problems.  If any error has `fatal == true`,
// betty should exit after displaying the message.
//
// Unknown keys are silently ignored for forward compatibility.

[[nodiscard]] auto parse_config(std::filesystem::path const& exe_dir)
    -> parse_result;

} // namespace betty
