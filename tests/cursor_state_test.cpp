#include <catch2/catch_test_macros.hpp>
#include "terminal/cursor_state.hpp"

using namespace betty::terminal;

// ===========================================================================
// Construction and basic accessors
// ===========================================================================

TEST_CASE("cursor_state — starts at (0,0)", "[cursor_state]") {
    cursor_state c;
    CHECK(c.row() == 0);
    CHECK(c.col() == 0);
    CHECK(c.scroll_top() == 0);
    CHECK(c.scroll_bottom() == 0);
}

// ===========================================================================
// move_to clamping
// ===========================================================================

TEST_CASE("cursor_state — move_to clamps to max", "[cursor_state]") {
    cursor_state c;
    c.move_to(99, 99, 23, 79);
    CHECK(c.row() == 23);
    CHECK(c.col() == 79);
}

TEST_CASE("cursor_state — move_to within bounds", "[cursor_state]") {
    cursor_state c;
    c.move_to(5, 10, 23, 79);
    CHECK(c.row() == 5);
    CHECK(c.col() == 10);
}

// ===========================================================================
// Directional movement
// ===========================================================================

TEST_CASE("cursor_state — move_up clamps to 0", "[cursor_state]") {
    cursor_state c;
    c.move_to(2, 0, 10, 10);
    c.move_up(5, 10);
    CHECK(c.row() == 0);
    CHECK(c.col() == 0);
}

TEST_CASE("cursor_state — move_down clamps to max", "[cursor_state]") {
    cursor_state c;
    c.move_to(8, 0, 10, 10);
    c.move_down(5, 10);
    CHECK(c.row() == 10);
}

TEST_CASE("cursor_state — move_forward clamps to max", "[cursor_state]") {
    cursor_state c;
    c.move_to(0, 8, 10, 10);
    c.move_forward(5, 10);
    CHECK(c.col() == 10);
}

TEST_CASE("cursor_state — move_back clamps to 0", "[cursor_state]") {
    cursor_state c;
    c.move_to(0, 2, 10, 10);
    c.move_back(5);
    CHECK(c.col() == 0);
}

// ===========================================================================
// save / restore
// ===========================================================================

TEST_CASE("cursor_state — save/restore round-trip", "[cursor_state]") {
    cursor_state c;
    c.move_to(5, 7, 10, 10);
    c.save();
    c.move_to(2, 2, 10, 10);
    c.restore(10, 10);
    CHECK(c.row() == 5);
    CHECK(c.col() == 7);
}

TEST_CASE("cursor_state — restore clamps saved position", "[cursor_state]") {
    cursor_state c;
    c.move_to(5, 5, 10, 10);
    c.save();
    c.restore(3, 3);
    CHECK(c.row() == 3);
    CHECK(c.col() == 3);
}

TEST_CASE("cursor_state — reset_saved", "[cursor_state]") {
    cursor_state c;
    c.move_to(5, 5, 10, 10);
    c.save();
    c.reset_saved();
    c.move_to(0, 0, 10, 10);
    c.restore(10, 10);
    CHECK(c.row() == 0);
    CHECK(c.col() == 0);
}

// ===========================================================================
// Scroll region
// ===========================================================================

TEST_CASE("cursor_state — set_region clamps", "[cursor_state]") {
    cursor_state c;
    c.set_region(0, 99, 23);
    CHECK(c.scroll_top() == 0);
    CHECK(c.scroll_bottom() == 23);
}

TEST_CASE("cursor_state — reset_region", "[cursor_state]") {
    cursor_state c;
    c.set_region(5, 10, 23);
    c.reset_region(23);
    CHECK(c.scroll_top() == 0);
    CHECK(c.scroll_bottom() == 23);
}

// ===========================================================================
// Scroll region predicates
// ===========================================================================

TEST_CASE("cursor_state — at_scroll_bottom", "[cursor_state]") {
    cursor_state c;
    c.set_region(0, 23, 23);
    c.move_to(23, 0, 23, 79);
    CHECK(c.at_scroll_bottom());
    c.move_to(22, 0, 23, 79);
    CHECK(!c.at_scroll_bottom());
}

TEST_CASE("cursor_state — in_scroll_region", "[cursor_state]") {
    cursor_state c;
    c.set_region(5, 15, 23);
    c.move_to(10, 0, 23, 79);
    CHECK(c.in_scroll_region());
    c.move_to(3, 0, 23, 79);
    CHECK(!c.in_scroll_region());
    c.move_to(16, 0, 23, 79);
    CHECK(!c.in_scroll_region());
}

// ===========================================================================
// Unchecked increment
// ===========================================================================

TEST_CASE("cursor_state — increment_row/col", "[cursor_state]") {
    cursor_state c;
    c.increment_row(10);
    c.increment_col(10);
    CHECK(c.row() == 1);
    CHECK(c.col() == 1);
}

TEST_CASE("cursor_state — increment_row clamps at max", "[cursor_state]") {
    cursor_state c;
    c.move_to(10, 0, 10, 10);
    c.increment_row(10);
    CHECK(c.row() == 10);
}

TEST_CASE("cursor_state — increment_col reaches column count for auto-wrap", "[cursor_state]") {
    cursor_state c;
    // col=9 is last valid index; column count=10 allows cursor past it
    c.move_to(0, 9, 10, 9);
    c.increment_col(10);
    CHECK(c.col() == 10);
}

TEST_CASE("cursor_state — reset_col", "[cursor_state]") {
    cursor_state c;
    c.move_to(5, 10, 10, 10);
    c.reset_col();
    CHECK(c.col() == 0);
    CHECK(c.row() == 5);
}

TEST_CASE("cursor_state — set_col clamps", "[cursor_state]") {
    cursor_state c;
    c.set_col(99, 10);
    CHECK(c.col() == 10);
}
