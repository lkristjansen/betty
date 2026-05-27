#include <catch2/catch_test_macros.hpp>
#include "config/config.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// Helper: write a string to a config.toml in the given directory.
static auto write_temp_config(fs::path const& dir, std::string_view content) -> void {
  std::ofstream ofs(dir / "config.toml", std::ios::binary);
  ofs << content;
}

// ===========================================================================
// Missing file → defaults
// ===========================================================================

TEST_CASE("parse_config — missing file returns defaults", "[config]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_missing";
    fs::create_directories(dir);
    fs::remove(dir / "config.toml");

    auto result = betty::parse_config(dir);
    CHECK(result.errors.empty());
    auto const& cfg = result.config;

    CHECK(cfg.font_family.value()      == "Consolas");
    CHECK(cfg.font_size.value()        == 14.0f);
    CHECK(cfg.cursor_style.value()     == "block");
    CHECK(cfg.scrollback_lines.value() == 10000);
    CHECK(cfg.columns.value()          == 120);
    CHECK(cfg.rows.value()             == 40);
    CHECK(cfg.shell.value()            == "powershell.exe -NoProfile -NoLogo");

    fs::remove_all(dir);
}

// ===========================================================================
// Valid TOML — all fields overridden
// ===========================================================================

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
    CHECK(result.errors.empty());
    auto const& cfg = result.config;

    CHECK(cfg.font_family.value()      == "Cascadia Code");
    CHECK(cfg.font_size.value()        == 13.0f);
    CHECK(cfg.cursor_style.value()     == "none");
    CHECK(cfg.scrollback_lines.value() == 20000);
    CHECK(cfg.columns.value()          == 140);
    CHECK(cfg.rows.value()             == 50);
    CHECK(cfg.shell.value()            == "pwsh.exe -NoLogo");

    fs::remove_all(dir);
}

// ===========================================================================
// Partial TOML — mix of overrides and defaults
// ===========================================================================

TEST_CASE("parse_config — partial TOML mixes defaults with overrides", "[config]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_partial";
    fs::create_directories(dir);
    write_temp_config(dir, R"(
font_family = "JetBrains Mono"
scrollback_lines = 5000
)");

    auto result = betty::parse_config(dir);
    CHECK(result.errors.empty());
    auto const& cfg = result.config;

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

// ===========================================================================
// Unknown keys silently ignored
// ===========================================================================

TEST_CASE("parse_config — unknown keys are silently ignored", "[config]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_unknown";
    fs::create_directories(dir);
    write_temp_config(dir, R"(
font_family = "Consolas"
made_up_key = "whatever"
another_one = 42
)");

    auto result = betty::parse_config(dir);
    CHECK(result.errors.empty());
    auto const& cfg = result.config;
    CHECK(cfg.font_family.value() == "Consolas");
    CHECK(cfg.font_size.value() == 14.0f);

    fs::remove_all(dir);
}

// ===========================================================================
// TOML syntax error
// ===========================================================================

TEST_CASE("parse_config — TOML syntax error returns error vector", "[config]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_bad_syntax";
    fs::create_directories(dir);
    write_temp_config(dir, R"(
font_family = "Consolas"
  = invalid syntax!!!
)");

    auto result = betty::parse_config(dir);
    REQUIRE(!result.errors.empty());
    CHECK(!result.errors[0].message.empty());
    // Config should still be at defaults.
    CHECK(result.config.font_family.value() == "Consolas");

    fs::remove_all(dir);
}

// ===========================================================================
// Empty TOML file → all defaults
// ===========================================================================

TEST_CASE("parse_config — empty TOML file returns all defaults", "[config]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_empty";
    fs::create_directories(dir);
    write_temp_config(dir, "");

    auto result = betty::parse_config(dir);
    CHECK(result.errors.empty());
    auto const& cfg = result.config;

    CHECK(cfg.font_family.value() == "Consolas");
    CHECK(cfg.font_size.value() == 14.0f);

    fs::remove_all(dir);
}

// ===========================================================================
// Integer font_size accepted
// ===========================================================================

TEST_CASE("parse_config — integer font_size is accepted", "[config]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_font_int";
    fs::create_directories(dir);
    write_temp_config(dir, "font_size = 18\n");

    auto result = betty::parse_config(dir);
    CHECK(result.errors.empty());
    auto const& cfg = result.config;
    CHECK(cfg.font_size.value() == 18.0f);

    fs::remove_all(dir);
}

// ===========================================================================
// Validation: empty font_family
// ===========================================================================

TEST_CASE("parse_config — validation: empty font_family is an error", "[config][validation]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_val_font";
    fs::create_directories(dir);
    write_temp_config(dir, "font_family = \"\"\n");

    auto result = betty::parse_config(dir);
    REQUIRE(result.errors.size() >= 1);
    CHECK(result.errors[0].key == "font_family");
    CHECK(result.config.font_family.value() == "Consolas");  // reset to default

    fs::remove_all(dir);
}

// ===========================================================================
// Validation: bad cursor_style
// ===========================================================================

TEST_CASE("parse_config — validation: bad cursor_style is an error", "[config][validation]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_val_cursor";
    fs::create_directories(dir);
    write_temp_config(dir, "cursor_style = \"beam\"\n");

    auto result = betty::parse_config(dir);
    REQUIRE(result.errors.size() >= 1);
    CHECK(result.errors[0].key == "cursor_style");
    CHECK(result.config.cursor_style.value() == "block");  // reset to default

    fs::remove_all(dir);
}

// ===========================================================================
// Validation: scrollback_lines zero
// ===========================================================================

TEST_CASE("parse_config — validation: scrollback_lines zero is an error", "[config][validation]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_val_scrollback";
    fs::create_directories(dir);
    write_temp_config(dir, "scrollback_lines = 0\n");

    auto result = betty::parse_config(dir);
    REQUIRE(result.errors.size() >= 1);
    CHECK(result.errors[0].key == "scrollback_lines");
    CHECK(result.config.scrollback_lines.value() == 10000);

    fs::remove_all(dir);
}

// ===========================================================================
// Validation: columns zero
// ===========================================================================

TEST_CASE("parse_config — validation: columns zero is an error", "[config][validation]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_val_cols";
    fs::create_directories(dir);
    write_temp_config(dir, "columns = 0\n");

    auto result = betty::parse_config(dir);
    REQUIRE(result.errors.size() >= 1);
    CHECK(result.errors[0].key == "columns");
    CHECK(result.config.columns.value() == 120);

    fs::remove_all(dir);
}

// ===========================================================================
// Validation: rows zero
// ===========================================================================

TEST_CASE("parse_config — validation: rows zero is an error", "[config][validation]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_val_rows";
    fs::create_directories(dir);
    write_temp_config(dir, "rows = 0\n");

    auto result = betty::parse_config(dir);
    REQUIRE(result.errors.size() >= 1);
    CHECK(result.errors[0].key == "rows");
    CHECK(result.config.rows.value() == 40);

    fs::remove_all(dir);
}

// ===========================================================================
// Validation: empty shell is fatal
// ===========================================================================

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

// ===========================================================================
// Validation: font_size is clamped, not errored
// ===========================================================================

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

// ===========================================================================
// Validation: multiple errors collected
// ===========================================================================

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

// ===========================================================================
// format_validation_errors
// ===========================================================================

TEST_CASE("format_validation_errors — empty vector returns empty string", "[config]") {
    auto msg = betty::format_validation_errors({});
    CHECK(msg.empty());
}

TEST_CASE("format_validation_errors — single error produces message", "[config]") {
    std::vector<betty::config_error> errors;
    errors.push_back({"cursor_style", "bad value", "beam", false});
    auto msg = betty::format_validation_errors(errors);
    CHECK(!msg.empty());
    CHECK(msg.find("cursor_style") != std::string::npos);
    CHECK(msg.find("bad value") != std::string::npos);
}
