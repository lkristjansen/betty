#include "config.hpp"

#include <fstream>
#include <toml++/toml.hpp>

namespace betty {

auto parse_config(std::filesystem::path const& exe_dir)
    -> std::expected<betty_config, std::string> {

  auto const config_path = exe_dir / "config.toml";

  // Missing file → silent default.
  if (!std::filesystem::exists(config_path)) {
    return betty_config{};
  }

  // Parse the TOML file.  toml::parse_file throws toml::parse_error on
  // syntax errors.
  toml::table tbl;
  try {
    tbl = toml::parse_file(config_path.string());
  } catch (toml::parse_error const& e) {
    return std::unexpected(std::string{e.description()});
  }

  betty_config cfg{};

  // Extract each known key.  If a key is missing or has the wrong type,
  // value<T>() returns std::nullopt and we keep the in-class default.
  // Unknown keys are implicitly ignored — we only read the 7 known keys.

  if (auto val = tbl["font_family"].value<std::string>()) {
    cfg.font_family = *val;
  }

  if (auto val = tbl["font_size"].value<double>()) {
    cfg.font_size = static_cast<float>(*val);
  } else if (auto val = tbl["font_size"].value<int64_t>()) {
    // Accept integer values for font_size too (e.g. font_size = 14).
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

  return cfg;
}

} // namespace betty
