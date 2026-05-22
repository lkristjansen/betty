#include <catch2/catch_test_macros.hpp>
#include "terminal/grid.hpp"

using namespace betty::terminal;

// ===========================================================================
// Construction and basic properties
// ===========================================================================

TEST_CASE("Grid — construction sets dimensions", "[grid][construction]") {
    terminal_grid g(80, 24);
    CHECK(g.cols() == 80);
    CHECK(g.rows() == 24);
}

TEST_CASE("Grid — cursor starts at (0,0)", "[grid][construction]") {
    terminal_grid g(80, 24);
    CHECK(g.cursor_col() == 0);
    CHECK(g.cursor_row() == 0);
}

TEST_CASE("Grid — all cells initialised to space", "[grid][construction]") {
    terminal_grid g(5, 3);
    for (uint32_t r = 0; r < 3; ++r) {
        for (uint32_t c = 0; c < 5; ++c) {
            CHECK(g.cell(r, c).codepoint == U' ');
        }
    }
}

TEST_CASE("Grid — zero-size grid", "[grid][edge]") {
    terminal_grid g(0, 0);
    CHECK(g.cols() == 0);
    CHECK(g.rows() == 0);
    CHECK(g.cursor_col() == 0);
    CHECK(g.cursor_row() == 0);
}

// ===========================================================================
// write_char
// ===========================================================================

TEST_CASE("Grid — write_char places codepoint and advances cursor", "[grid][write_char]") {
    terminal_grid g(10, 5);
    g.write_char(U'a');
    CHECK(g.cell(0, 0).codepoint == U'a');
    CHECK(g.cursor_col() == 1);
    CHECK(g.cursor_row() == 0);
}

TEST_CASE("Grid — write_char multiple chars fill row", "[grid][write_char]") {
    terminal_grid g(3, 3);
    g.write_char(U'a');
    g.write_char(U'b');
    g.write_char(U'c');
    CHECK(g.cell(0, 0).codepoint == U'a');
    CHECK(g.cell(0, 1).codepoint == U'b');
    CHECK(g.cell(0, 2).codepoint == U'c');
    // Auto-wrap fired at column boundary; cursor is now at next row, col 0.
    CHECK(g.cursor_col() == 0);
    CHECK(g.cursor_row() == 1);
}

// ===========================================================================
// write_char — auto-wrap
// ===========================================================================

TEST_CASE("Grid — write_char wraps at column boundary", "[grid][write_char][wrap]") {
    terminal_grid g(2, 3);
    g.write_char(U'a');  // col 0 → col 1
    g.write_char(U'b');  // col 1 → col 2 (past end, wraps to row 1 col 0)
    CHECK(g.cell(0, 0).codepoint == U'a');
    CHECK(g.cell(0, 1).codepoint == U'b');
    CHECK(g.cursor_col() == 0);
    CHECK(g.cursor_row() == 1);
}

TEST_CASE("Grid — write_char after wrap places on next row", "[grid][write_char][wrap]") {
    terminal_grid g(2, 3);
    g.write_char(U'a');  // (0,0), cursor → (0,1)
    g.write_char(U'b');  // (0,1), cursor wraps → (1,0)
    g.write_char(U'c');  // (1,0), cursor → (1,1)
    CHECK(g.cell(0, 0).codepoint == U'a');
    CHECK(g.cell(0, 1).codepoint == U'b');
    CHECK(g.cell(1, 0).codepoint == U'c');
    CHECK(g.cell(1, 1).codepoint == U' ');
    CHECK(g.cursor_col() == 1);
    CHECK(g.cursor_row() == 1);
}

// ===========================================================================
// write_char — scroll at last row
// ===========================================================================

TEST_CASE("Grid — write_char scrolls when past last row", "[grid][write_char][scroll]") {
    terminal_grid g(2, 2);
    g.write_char(U'a');  // (0,0), cursor → (0,1)
    g.write_char(U'b');  // (0,1), cursor wraps → (1,0)
    g.write_char(U'c');  // (1,0), cursor → (1,1)
    g.write_char(U'd');  // (1,1), cursor wraps → (2,0) → scroll_up, cursor → (1,0)
    // After scroll, row 0 = old row 1 ('c','d'), row 1 = cleared.
    CHECK(g.cell(0, 0).codepoint == U'c');
    CHECK(g.cell(0, 1).codepoint == U'd');
    CHECK(g.cell(1, 0).codepoint == U' ');
    CHECK(g.cell(1, 1).codepoint == U' ');
    CHECK(g.cursor_col() == 0);
    CHECK(g.cursor_row() == 1);
}

// ===========================================================================
// newline
// ===========================================================================

TEST_CASE("Grid — newline moves to next row column 0", "[grid][newline]") {
    terminal_grid g(10, 5);
    g.write_char(U'x');
    g.newline();
    CHECK(g.cursor_col() == 0);
    CHECK(g.cursor_row() == 1);
}

TEST_CASE("Grid — newline at last row scrolls", "[grid][newline][scroll]") {
    terminal_grid g(3, 2);
    g.write_char(U'a');  // (0,0)
    g.write_char(U'b');  // (0,1)
    g.newline();         // → (1,0)
    CHECK(g.cursor_row() == 1);

    // Fill row 1
    g.write_char(U'c');  // (1,0)
    g.write_char(U'd');  // (1,1)
    g.newline();         // would be row 2 → scrolls, cursor stays at row 1
    CHECK(g.cursor_row() == 1);
    CHECK(g.cursor_col() == 0);
    // Row 0 should now have old row 1 content.
    CHECK(g.cell(0, 0).codepoint == U'c');
    CHECK(g.cell(0, 1).codepoint == U'd');
}

// ===========================================================================
// carriage_return
// ===========================================================================

TEST_CASE("Grid — carriage_return moves to column 0", "[grid][carriage_return]") {
    terminal_grid g(10, 5);
    g.write_char(U'a');
    g.write_char(U'b');
    g.write_char(U'c');
    g.carriage_return();
    CHECK(g.cursor_col() == 0);
    CHECK(g.cursor_row() == 0);
}

// ===========================================================================
// scroll_up
// ===========================================================================

TEST_CASE("Grid — scroll_up shifts rows up and clears bottom row", "[grid][scroll_up]") {
    // Set up a 3×2 grid with row 0 = A B C, row 1 = D E ' '.
    terminal_grid g(3, 2);
    g.write_char(U'A'); g.write_char(U'B'); g.write_char(U'C'); // wraps to (1,0)
    g.write_char(U'D'); g.write_char(U'E');                     // (1,0), (1,1)
    // Grid: row0 = A B C, row1 = D E ' '
    g.scroll_up();
    // After: row0 = D E ' ', row1 = ' ' ' ' ' '
    CHECK(g.cell(0, 0).codepoint == U'D');
    CHECK(g.cell(0, 1).codepoint == U'E');
    CHECK(g.cell(0, 2).codepoint == U' ');
    CHECK(g.cell(1, 0).codepoint == U' ');
    CHECK(g.cell(1, 1).codepoint == U' ');
    CHECK(g.cell(1, 2).codepoint == U' ');
}

TEST_CASE("Grid — scroll_up on single-row grid clears cells", "[grid][scroll_up][edge]") {
    terminal_grid g(5, 1);
    g.write_char(U'A'); g.write_char(U'B'); g.write_char(U'C');
    g.scroll_up();
    CHECK(g.cell(0, 0).codepoint == U' ');
    CHECK(g.cell(0, 1).codepoint == U' ');
    CHECK(g.cell(0, 2).codepoint == U' ');
    CHECK(g.cell(0, 3).codepoint == U' ');
    CHECK(g.cell(0, 4).codepoint == U' ');
}

// ===========================================================================
// Access — cells() and codepoints()
// ===========================================================================

TEST_CASE("Grid — cells() returns full span", "[grid][access]") {
    terminal_grid g(2, 2);
    g.write_char(U'a');
    auto c = g.cells();
    REQUIRE(c.size() == 4);
    CHECK(c[0].codepoint == U'a');
    CHECK(c[1].codepoint == U' ');
}

TEST_CASE("Grid — codepoints() returns codepoint view", "[grid][access]") {
    terminal_grid g(2, 2);
    g.write_char(U'a');
    g.write_char(U'b');
    auto cp = g.codepoints();
    REQUIRE(cp.size() == 4);
    CHECK(cp[0] == U'a');
    CHECK(cp[1] == U'b');
    CHECK(cp[2] == U' ');
    CHECK(cp[3] == U' ');
}

// ===========================================================================
// write_bytes
// ===========================================================================

TEST_CASE("Grid — write_bytes processes simple text", "[grid][write_bytes]") {
    terminal_grid g(10, 5);
    g.write_bytes("Hello");
    CHECK(g.cell(0, 0).codepoint == U'H');
    CHECK(g.cell(0, 1).codepoint == U'e');
    CHECK(g.cell(0, 2).codepoint == U'l');
    CHECK(g.cell(0, 3).codepoint == U'l');
    CHECK(g.cell(0, 4).codepoint == U'o');
}

TEST_CASE("Grid — write_bytes handles newline", "[grid][write_bytes]") {
    terminal_grid g(10, 5);
    g.write_bytes("AB\nCD");
    CHECK(g.cell(0, 0).codepoint == U'A');
    CHECK(g.cell(0, 1).codepoint == U'B');
    CHECK(g.cell(1, 0).codepoint == U'C');
    CHECK(g.cell(1, 1).codepoint == U'D');
}

TEST_CASE("Grid — write_bytes handles carriage_return", "[grid][write_bytes]") {
    terminal_grid g(10, 5);
    g.write_bytes("AB\rC");
    CHECK(g.cell(0, 0).codepoint == U'C');  // overwrote A
    CHECK(g.cell(0, 1).codepoint == U'B');
}

// ===========================================================================
// apply — cursor movement actions
// ===========================================================================

TEST_CASE("Grid — apply move_cursor", "[grid][apply]") {
    terminal_grid g(10, 10);
    action a;
    a.type = action_type::move_cursor;
    a.row = 5;
    a.col = 7;
    g.apply(a);
    CHECK(g.cursor_row() == 5);
    CHECK(g.cursor_col() == 7);
}

TEST_CASE("Grid — apply move_cursor clamps to bounds", "[grid][apply][edge]") {
    terminal_grid g(5, 3);
    action a;
    a.type = action_type::move_cursor;
    a.row = 99;
    a.col = 99;
    g.apply(a);
    CHECK(g.cursor_row() == 2);
    CHECK(g.cursor_col() == 4);
}

TEST_CASE("Grid — apply move_cursor_up", "[grid][apply]") {
    terminal_grid g(10, 10);
    action a;
    a.type = action_type::move_cursor;
    a.row = 5;
    a.col = 5;
    g.apply(a);  // cursor at (5,5)

    action b;
    b.type = action_type::move_cursor_up;
    b.count = 2;
    g.apply(b);
    CHECK(g.cursor_row() == 3);
    CHECK(g.cursor_col() == 5);
}

TEST_CASE("Grid — apply move_cursor_up clamps to 0", "[grid][apply][edge]") {
    terminal_grid g(10, 10);
    action a;
    a.type = action_type::move_cursor;
    a.row = 1;
    a.col = 0;
    g.apply(a);

    action b;
    b.type = action_type::move_cursor_up;
    b.count = 5;
    g.apply(b);
    CHECK(g.cursor_row() == 0);
}

TEST_CASE("Grid — apply move_cursor_down", "[grid][apply]") {
    terminal_grid g(10, 10);
    action b;
    b.type = action_type::move_cursor_down;
    b.count = 3;
    g.apply(b);
    CHECK(g.cursor_row() == 3);
}

TEST_CASE("Grid — apply move_cursor_down clamps to last row", "[grid][apply][edge]") {
    terminal_grid g(10, 3);
    action b;
    b.type = action_type::move_cursor_down;
    b.count = 99;
    g.apply(b);
    CHECK(g.cursor_row() == 2);
}

TEST_CASE("Grid — apply move_cursor_forward", "[grid][apply]") {
    terminal_grid g(10, 5);
    action b;
    b.type = action_type::move_cursor_forward;
    b.count = 4;
    g.apply(b);
    CHECK(g.cursor_col() == 4);
}

TEST_CASE("Grid — apply move_cursor_forward clamps to last col", "[grid][apply][edge]") {
    terminal_grid g(5, 3);
    action b;
    b.type = action_type::move_cursor_forward;
    b.count = 99;
    g.apply(b);
    CHECK(g.cursor_col() == 4);
}

TEST_CASE("Grid — apply move_cursor_back", "[grid][apply]") {
    terminal_grid g(10, 5);
    action a;
    a.type = action_type::move_cursor;
    a.row = 0;
    a.col = 5;
    g.apply(a);

    action b;
    b.type = action_type::move_cursor_back;
    b.count = 3;
    g.apply(b);
    CHECK(g.cursor_col() == 2);
}

TEST_CASE("Grid — apply move_cursor_back clamps to 0", "[grid][apply][edge]") {
    terminal_grid g(10, 5);
    action b;
    b.type = action_type::move_cursor_back;
    b.count = 99;
    g.apply(b);
    CHECK(g.cursor_col() == 0);
}

// ===========================================================================
// resize
// ===========================================================================

TEST_CASE("Grid — resize to same size is no-op", "[grid][resize]") {
    terminal_grid g(10, 5);
    g.write_char(U'X');
    g.resize(10, 5);
    CHECK(g.cols() == 10);
    CHECK(g.rows() == 5);
    CHECK(g.cell(0, 0).codepoint == U'X');
}

TEST_CASE("Grid — resize larger preserves content", "[grid][resize]") {
    terminal_grid g(3, 2);
    g.write_char(U'a'); g.write_char(U'b'); g.write_char(U'c'); // wraps to (1,0)
    g.write_char(U'd');  // (1,0)
    // Grid: row0 = a b c, row1 = d ' ' ' '
    g.resize(5, 4);
    CHECK(g.cols() == 5);
    CHECK(g.rows() == 4);
    CHECK(g.cell(0, 0).codepoint == U'a');
    CHECK(g.cell(0, 1).codepoint == U'b');
    CHECK(g.cell(0, 2).codepoint == U'c');
    CHECK(g.cell(1, 0).codepoint == U'd');
    // New cells should be spaces
    CHECK(g.cell(0, 3).codepoint == U' ');
    CHECK(g.cell(0, 4).codepoint == U' ');
    CHECK(g.cell(2, 0).codepoint == U' ');
    CHECK(g.cell(3, 0).codepoint == U' ');
}

TEST_CASE("Grid — resize smaller truncates content", "[grid][resize]") {
    terminal_grid g(5, 5);
    g.write_char(U'a'); g.write_char(U'b'); g.write_char(U'c'); g.write_char(U'd'); g.write_char(U'e');
    g.resize(3, 3);
    CHECK(g.cols() == 3);
    CHECK(g.rows() == 3);
    CHECK(g.cell(0, 0).codepoint == U'a');
    CHECK(g.cell(0, 1).codepoint == U'b');
    CHECK(g.cell(0, 2).codepoint == U'c');
    // Column 3,4 and rows 3,4 are gone.
}

TEST_CASE("Grid — resize clamps cursor to new bounds", "[grid][resize]") {
    terminal_grid g(10, 10);
    action a;
    a.type = action_type::move_cursor;
    a.row = 7;
    a.col = 7;
    g.apply(a);
    g.resize(5, 5);
    CHECK(g.cursor_row() == 4);
    CHECK(g.cursor_col() == 4);
}

TEST_CASE("Grid — resize to zero", "[grid][resize][edge]") {
    terminal_grid g(10, 10);
    g.resize(0, 0);
    CHECK(g.cols() == 0);
    CHECK(g.rows() == 0);
    CHECK(g.cursor_col() == 0);
    CHECK(g.cursor_row() == 0);
}

// ===========================================================================
// Integration — write_bytes with CSI sequences
// ===========================================================================

TEST_CASE("Grid — write_bytes with CUP positions cursor", "[grid][integration]") {
    terminal_grid g(10, 10);
    g.write_bytes("\x1B[5;8H");
    // 0-based: row 4, col 7
    CHECK(g.cursor_row() == 4);
    CHECK(g.cursor_col() == 7);
}

TEST_CASE("Grid — write_bytes with CUU/CUD/CUF/CUB", "[grid][integration]") {
    terminal_grid g(10, 10);
    // Start at (5,5)
    g.write_bytes("\x1B[6;6H");  // 0-based: (5,5)
    g.write_bytes("\x1B[2A");    // CUU 2 → row 3
    CHECK(g.cursor_row() == 3);
    g.write_bytes("\x1B[4B");    // CUD 4 → row 7
    CHECK(g.cursor_row() == 7);
    g.write_bytes("\x1B[3C");    // CUF 3 → col 8
    CHECK(g.cursor_col() == 8);
    g.write_bytes("\x1B[5D");    // CUB 5 → col 3
    CHECK(g.cursor_col() == 3);
}

// ===========================================================================
// Edge: write_char on zero-size grid
// ===========================================================================

TEST_CASE("Grid — write_char on 0x0 grid does not crash", "[grid][edge]") {
    terminal_grid g(0, 0);
    // This exercises the guard `cursor_col_ < cols_` (0 < 0 = false) and
    // the wrap/scroll path.  The only requirement is that it doesn't crash.
    g.write_char(U'a');
    SUCCEED("write_char on zero-size grid did not crash");
}
