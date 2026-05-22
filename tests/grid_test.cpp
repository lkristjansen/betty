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

TEST_CASE("Grid — cells() returns full cell data", "[grid][access]") {
    terminal_grid g(2, 2);
    g.write_char(U'a');
    g.write_char(U'b');
    auto c = g.cells();
    REQUIRE(c.size() == 4);
    CHECK(c[0].codepoint == U'a');
    CHECK(c[1].codepoint == U'b');
    CHECK(c[2].codepoint == U' ');
    CHECK(c[3].codepoint == U' ');
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
// save_cursor / restore_cursor
// ===========================================================================

TEST_CASE("Grid — save_cursor remembers current cursor position", "[grid][cursor_save_restore]") {
    terminal_grid g(10, 10);
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 5; mv.col = 7;
    g.apply(mv);

    action save;
    save.type = action_type::save_cursor;
    g.apply(save);

    // Cursor should not move after save.
    CHECK(g.cursor_row() == 5);
    CHECK(g.cursor_col() == 7);
}

TEST_CASE("Grid — save_cursor and restore_cursor round-trip", "[grid][cursor_save_restore]") {
    terminal_grid g(10, 10);
    // Move to (5, 7).
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 5; mv.col = 7;
    g.apply(mv);

    // Save.
    action save;
    save.type = action_type::save_cursor;
    g.apply(save);

    // Move elsewhere.
    action mv2;
    mv2.type = action_type::move_cursor;
    mv2.row = 2; mv2.col = 2;
    g.apply(mv2);

    // Restore.
    action restore;
    restore.type = action_type::restore_cursor;
    g.apply(restore);

    CHECK(g.cursor_row() == 5);
    CHECK(g.cursor_col() == 7);
}

TEST_CASE("Grid — restore_cursor clamps to grid bounds", "[grid][cursor_save_restore]") {
    terminal_grid g(5, 5);
    // Save default (0, 0).
    action save;
    save.type = action_type::save_cursor;
    g.apply(save);

    // Restore — should be at (0, 0) since that was saved.
    action restore;
    restore.type = action_type::restore_cursor;
    g.apply(restore);

    CHECK(g.cursor_row() == 0);
    CHECK(g.cursor_col() == 0);
}

TEST_CASE("Grid — save/restore via write_bytes (ESC 7 / ESC 8 integration)", "[grid][cursor_save_restore][integration]") {
    terminal_grid g(10, 10);
    // Move to (5, 7) then save.
    g.write_bytes("\x1B[6;8H");  // CUP: row 6, col 8 → 0-based (5, 7)
    g.write_bytes("\x1B" "7");   // DECSC

    // Move elsewhere.
    g.write_bytes("\x1B[3;3H");  // CUP: (2, 2)

    // Restore.
    g.write_bytes("\x1B" "8");   // DECRC

    CHECK(g.cursor_row() == 5);
    CHECK(g.cursor_col() == 7);
}

TEST_CASE("Grid — saved cursor is reset after resize", "[grid][cursor_save_restore][resize]") {
    terminal_grid g(10, 10);
    // Move to (5, 5) and save.
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 5; mv.col = 5;
    g.apply(mv);

    action save;
    save.type = action_type::save_cursor;
    g.apply(save);

    // Resize.
    g.resize(20, 20);

    // Restore — should be at (0, 0) since resize reset the saved position.
    action restore;
    restore.type = action_type::restore_cursor;
    g.apply(restore);

    CHECK(g.cursor_row() == 0);
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

// ===========================================================================
// Task 8 — Erase in Display (ED)
// ===========================================================================

TEST_CASE("Grid — erase_display mode 0 clears from cursor to end", "[grid][erase]") {
    terminal_grid g(5, 3);
    // Fill rows with 'X' using explicit CUP positioning.
    g.write_bytes("\x1B[1;1HXXXXX");  // row 0
    g.write_bytes("\x1B[2;1HXXXXX");  // row 1
    g.write_bytes("\x1B[3;1HXXXX");   // row 2: 4 X's (skip last to avoid scroll)
    // Move to (1,2).
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 1; mv.col = 2;
    g.apply(mv);

    // Erase from cursor to end.
    action ed;
    ed.type = action_type::erase_display;
    ed.count = 0;
    g.apply(ed);

    // All cells BEFORE cursor should still be 'X'.
    CHECK(g.cell(0, 0).codepoint == U'X');
    CHECK(g.cell(0, 1).codepoint == U'X');
    CHECK(g.cell(0, 2).codepoint == U'X');
    CHECK(g.cell(1, 0).codepoint == U'X');
    CHECK(g.cell(1, 1).codepoint == U'X');

    // Cells FROM cursor should be space.
    CHECK(g.cell(1, 2).codepoint == U' ');
    CHECK(g.cell(1, 3).codepoint == U' ');
    CHECK(g.cell(1, 4).codepoint == U' ');
    CHECK(g.cell(2, 0).codepoint == U' ');
    CHECK(g.cell(2, 4).codepoint == U' ');
}

TEST_CASE("Grid — erase_display mode 1 clears from beginning to cursor", "[grid][erase]") {
    terminal_grid g(5, 3);
    // Fill rows with 'X' using explicit CUP positioning to avoid
    // auto-wrap/scroll corrupting the last row.
    g.write_bytes("\x1B[1;1HXXXXX");  // row 0: 5 X's
    g.write_bytes("\x1B[2;1HXXXXX");  // row 1: 5 X's
    g.write_bytes("\x1B[3;1HXXXX");   // row 2: 4 X's (skip last to avoid scroll)

    // Move cursor to (1, 2).
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 1; mv.col = 2;
    g.apply(mv);

    action ed;
    ed.type = action_type::erase_display;
    ed.count = 1;
    g.apply(ed);

    // Cells up to and including cursor should be space.
    CHECK(g.cell(0, 0).codepoint == U' ');
    CHECK(g.cell(0, 1).codepoint == U' ');
    CHECK(g.cell(1, 0).codepoint == U' ');
    CHECK(g.cell(1, 1).codepoint == U' ');
    CHECK(g.cell(1, 2).codepoint == U' ');

    // Cells after cursor should still be 'X'.
    CHECK(g.cell(1, 3).codepoint == U'X');
    CHECK(g.cell(2, 0).codepoint == U'X');
}

TEST_CASE("Grid — erase_display mode 2 clears entire screen", "[grid][erase]") {
    terminal_grid g(5, 3);
    // Fill with 'X' using CUP + write_bytes.
    g.write_bytes("\x1B[1;1HXXXXX");
    g.write_bytes("\x1B[2;1HXXXXX");
    g.write_bytes("\x1B[3;1HXXXXX");

    action ed;
    ed.type = action_type::erase_display;
    ed.count = 2;
    g.apply(ed);

    // Every cell should now be space.
    for (uint32_t r = 0; r < 3; ++r)
        for (uint32_t c = 0; c < 5; ++c)
            CHECK(g.cell(r, c).codepoint == U' ');
}

TEST_CASE("Grid — erase_display does not move cursor", "[grid][erase]") {
    terminal_grid g(5, 3);
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 2; mv.col = 3;
    g.apply(mv);

    action ed;
    ed.type = action_type::erase_display;
    ed.count = 2;
    g.apply(ed);

    CHECK(g.cursor_row() == 2);
    CHECK(g.cursor_col() == 3);
}

// ===========================================================================
// Task 8 — Erase in Line (EL)
// ===========================================================================

TEST_CASE("Grid — erase_line mode 0 clears from cursor to end of line", "[grid][erase]") {
    terminal_grid g(5, 3);
    // Fill row 1 with 'X'.
    for (uint32_t c = 0; c < 5; ++c) {
        action mv;
        mv.type = action_type::move_cursor;
        mv.row = 1; mv.col = c;
        g.apply(mv);
        g.write_char(U'X');
    }
    // Cursor at (1, 2).
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 1; mv.col = 2;
    g.apply(mv);

    action el;
    el.type = action_type::erase_line;
    el.count = 0;
    g.apply(el);

    // Columns 0-1 unchanged.
    CHECK(g.cell(1, 0).codepoint == U'X');
    CHECK(g.cell(1, 1).codepoint == U'X');
    // Columns 2-4 erased.
    CHECK(g.cell(1, 2).codepoint == U' ');
    CHECK(g.cell(1, 3).codepoint == U' ');
    CHECK(g.cell(1, 4).codepoint == U' ');

    // Other rows untouched.
    CHECK(g.cell(0, 0).codepoint == U' ');
    CHECK(g.cell(2, 0).codepoint == U' ');
}

TEST_CASE("Grid — erase_line mode 1 clears from beginning to cursor", "[grid][erase]") {
    terminal_grid g(5, 3);
    for (uint32_t c = 0; c < 5; ++c) {
        action mv;
        mv.type = action_type::move_cursor;
        mv.row = 1; mv.col = c;
        g.apply(mv);
        g.write_char(U'X');
    }
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 1; mv.col = 2;
    g.apply(mv);

    action el;
    el.type = action_type::erase_line;
    el.count = 1;
    g.apply(el);

    CHECK(g.cell(1, 0).codepoint == U' ');
    CHECK(g.cell(1, 1).codepoint == U' ');
    CHECK(g.cell(1, 2).codepoint == U' ');
    CHECK(g.cell(1, 3).codepoint == U'X');
    CHECK(g.cell(1, 4).codepoint == U'X');
}

TEST_CASE("Grid — erase_line mode 2 clears entire line", "[grid][erase]") {
    terminal_grid g(5, 3);
    // Fill row 1 with 'X' using write_bytes (explicit positioning avoids
    // auto-wrap moving the cursor off the row).
    g.write_bytes("\x1B[2;1H");  // CUP to row 2, col 1 (0-based: 1, 0)
    g.write_bytes("XXXXX");
    // Cursor is now at (1,5) → auto-wrapped to (2,0). Move back to row 1.
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 1; mv.col = 2;
    g.apply(mv);

    action el;
    el.type = action_type::erase_line;
    el.count = 2;
    g.apply(el);

    for (uint32_t c = 0; c < 5; ++c)
        CHECK(g.cell(1, c).codepoint == U' ');

    // Other rows untouched.
    CHECK(g.cell(0, 0).codepoint == U' ');
    CHECK(g.cell(2, 0).codepoint == U' ');
}

TEST_CASE("Grid — erase_line does not move cursor", "[grid][erase]") {
    terminal_grid g(5, 3);
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 1; mv.col = 3;
    g.apply(mv);

    action el;
    el.type = action_type::erase_line;
    el.count = 2;
    g.apply(el);

    CHECK(g.cursor_row() == 1);
    CHECK(g.cursor_col() == 3);
}

// ===========================================================================
// Task 8 — Erase edge cases
// ===========================================================================

TEST_CASE("Grid — erase_display on zero-size grid does not crash", "[grid][erase][edge]") {
    terminal_grid g(0, 0);
    action ed;
    ed.type = action_type::erase_display;
    ed.count = 2;
    g.apply(ed);
    SUCCEED("erase_display on zero-size grid did not crash");
}

TEST_CASE("Grid — erase_line on zero-size grid does not crash", "[grid][erase][edge]") {
    terminal_grid g(0, 0);
    action el;
    el.type = action_type::erase_line;
    el.count = 2;
    g.apply(el);
    SUCCEED("erase_line on zero-size grid did not crash");
}

TEST_CASE("Grid — erase_display mode 3 treated as mode 2", "[grid][erase]") {
    terminal_grid g(3, 2);
    // Fill with 'X' using CUP.
    g.write_bytes("\x1B[1;1HXXX");
    g.write_bytes("\x1B[2;1HXXX");

    action ed;
    ed.type = action_type::erase_display;
    ed.count = 3;
    g.apply(ed);

    for (uint32_t r = 0; r < 2; ++r)
        for (uint32_t c = 0; c < 3; ++c)
            CHECK(g.cell(r, c).codepoint == U' ');
}

TEST_CASE("Grid — erase uses default colours", "[grid][erase]") {
    terminal_grid g(5, 3);
    // Set a cell with non-default colours via SGR, then erase.
    g.write_bytes("\x1B[31m");  // SGR red fg
    g.write_char(U'Y');           // at (0,0) with red fg

    // Move back to (0,0) and erase the line.
    action mv;
    mv.type = action_type::move_cursor;
    mv.row = 0; mv.col = 0;
    g.apply(mv);

    action el;
    el.type = action_type::erase_line;
    el.count = 2;
    g.apply(el);

    // Erased cell should have default fg, default bg.
    auto const& cell = g.cell(0, 0);
    CHECK(cell.codepoint == U' ');
    CHECK(cell.fg.flags == 1);  // is_default flag set
    CHECK(cell.bg.flags == 1);
}

TEST_CASE("Grid — erase_display via write_bytes (integration)", "[grid][erase][integration]") {
    terminal_grid g(5, 3);
    // Fill with 'X' using CUP.
    g.write_bytes("\x1B[1;1HXXXXX");
    g.write_bytes("\x1B[2;1HXXXXX");
    g.write_bytes("\x1B[3;1HXXXX");   // 4 X's to avoid scroll
    // Move to (1, 2) and erase from cursor to end via escape sequence.
    g.write_bytes("\x1B[2;3H");   // CUP to row 2, col 3 (0-based: 1, 2)
    g.write_bytes("\x1B[0J");     // ED mode 0

    CHECK(g.cell(0, 0).codepoint == U'X');
    CHECK(g.cell(1, 2).codepoint == U' ');
    CHECK(g.cell(2, 4).codepoint == U' ');
}
