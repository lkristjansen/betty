#pragma once
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace betty {

// ===========================================================================
// betty_config — all user-configurable settings
// ===========================================================================
// Every field is optional with an in-class default matching the built-in
// hardcoded value.  After a successful parse_config(), all fields have a
// value (the default or the user's override).

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
// parse_config — load and parse config.toml
// ===========================================================================
// Looks for `config.toml` inside `exe_dir` (the directory containing
// betty.exe).  Callers obtain `exe_dir` via GetModuleFileNameW +
// parent_path().
//
// Returns:
//   • The populated config on success (missing file = success with defaults).
//   • An error string on TOML syntax errors (malformed file).
//
// Unknown keys are silently ignored for forward compatibility.
// Type validation of individual values is deferred to validate_config() (C5).

[[nodiscard]] auto parse_config(std::filesystem::path const& exe_dir)
    -> std::expected<betty_config, std::string>;

} // namespace betty
