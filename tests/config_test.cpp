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
    REQUIRE(result.has_value());
    auto const& cfg = *result;

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
    REQUIRE(result.has_value());
    auto const& cfg = *result;
    CHECK(cfg.font_family.value() == "Consolas");
    CHECK(cfg.font_size.value() == 14.0f);

    fs::remove_all(dir);
}

// ===========================================================================
// TOML syntax error
// ===========================================================================

TEST_CASE("parse_config — TOML syntax error returns error string", "[config]") {
    fs::path dir = fs::temp_directory_path() / "betty_test_bad_syntax";
    fs::create_directories(dir);
    write_temp_config(dir, R"(
font_family = "Consolas"
  = invalid syntax!!!
)");

    auto result = betty::parse_config(dir);
    REQUIRE(!result.has_value());
    CHECK(!result.error().empty());

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
    REQUIRE(result.has_value());
    auto const& cfg = *result;

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
    REQUIRE(result.has_value());
    auto const& cfg = *result;
    CHECK(cfg.font_size.value() == 18.0f);

    fs::remove_all(dir);
}
