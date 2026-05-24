#include <catch2/catch_test_macros.hpp>
#include "terminal/sgr_state.hpp"

using namespace betty::terminal;

TEST_CASE("sgr_state — default construction", "[sgr_state]") {
    sgr_state s;
    CHECK(s.fg.is_default());
    CHECK(s.bg.is_default());
    CHECK(s.attr == cell_attr::none);
}

TEST_CASE("sgr_state — manual assignment", "[sgr_state]") {
    sgr_state s;
    s.fg = {0xFF, 0x00, 0x00, 0};
    s.bg = {0x00, 0xFF, 0x00, 0};
    s.attr = cell_attr::bold | cell_attr::italic;

    CHECK(s.fg.r == 0xFF);
    CHECK(s.bg.g == 0xFF);
    CHECK((s.attr & cell_attr::bold) == cell_attr::bold);
    CHECK((s.attr & cell_attr::italic) == cell_attr::italic);
}

TEST_CASE("sgr_state — reset to defaults", "[sgr_state]") {
    sgr_state s;
    s.fg = {0xFF, 0x00, 0x00, 0};
    s.attr = cell_attr::underline;

    s = sgr_state{};
    CHECK(s.fg.is_default());
    CHECK(s.bg.is_default());
    CHECK(s.attr == cell_attr::none);
}
