#include "config.hpp"

#include <fstream>
#include <toml++/toml.hpp>

namespace betty {

namespace {

// ===========================================================================
// validate_config — apply field-level constraints
// ===========================================================================
// Checks each field and collects errors.  Invalid fields are reset to their
// defaults so the caller always has a usable config.

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
    // Shell is NOT reset — betty will exit before using it.
  }
}

} // anonymous namespace

// ===========================================================================
// format_validation_errors
// ===========================================================================

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

// ===========================================================================
// parse_config
// ===========================================================================

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

  // Extract known keys.
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
    cfg.scrollback_lines = (*val >= 0) ? static_cast<uint32_t>(*val) : 0;
  }
  if (auto val = tbl["columns"].value<int64_t>()) {
    cfg.columns = (*val >= 0) ? static_cast<uint32_t>(*val) : 0;
  }
  if (auto val = tbl["rows"].value<int64_t>()) {
    cfg.rows = (*val >= 0) ? static_cast<uint32_t>(*val) : 0;
  }
  if (auto val = tbl["shell"].value<std::string>()) {
    cfg.shell = *val;
  }

  // Validate.
  validate_config(cfg, result.errors);

  return result;
}

} // namespace betty
