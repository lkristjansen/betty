#include <catch2/catch_test_macros.hpp>
#include "platform/shell.hpp"

using namespace betty::platform;

// ===========================================================================
// shell_settings
// ===========================================================================

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
