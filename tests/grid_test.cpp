#include <catch2/catch_test_macros.hpp>
#include "terminal/grid.hpp"
#include "terminal/vt_parser.hpp"
#include "terminal/wcwidth.hpp"

using namespace betty::terminal;

// Helper: feed raw bytes through a VT parser and apply actions to the grid.
static void write_bytes(terminal_grid& g, std::string_view data) {
  vt_parser p;
  for (unsigned char const b : data) {
    for (auto const& a : p.parse(b)) {
      g.apply(a);
    }
  }
}

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
// pending-wrap flag — prevents double row advance with \r\n after auto-wrap
// ===========================================================================

TEST_CASE("Grid — pending-wrap: \\r\\n after auto-wrap does not double-advance", "[grid][pending_wrap]") {
    // Simulate shell output filling exactly one line and then sending \r\n.
    terminal_grid g(3, 3);
    // Fill the last cell to trigger auto-wrap.
    g.write_char(U'a');  // (0,0) → (0,1)
    g.write_char(U'b');  // (0,1) → (0,2)
    g.write_char(U'c');  // (0,2) → auto-wrap to (1,0), pending_wrap_=true

    // Now simulate \r\n from the shell.
    write_bytes(g, "\r\n");

    // Cursor should be at (1,0), NOT (2,0).
    CHECK(g.cursor_row() == 1);
    CHECK(g.cursor_col() == 0);
    // Row 0 should still have 'a','b','c'.
    CHECK(g.cell(0, 0).codepoint == U'a');
    CHECK(g.cell(0, 1).codepoint == U'b');
    CHECK(g.cell(0, 2).codepoint == U'c');
}

TEST_CASE("Grid — pending-wrap: subsequent write after auto-wrap works", "[grid][pending_wrap]") {
    terminal_grid g(3, 3);
    // Trigger auto-wrap.
    g.write_char(U'a');  // (0,0) → (0,1)
    g.write_char(U'b');  // (0,1) → (0,2)
    g.write_char(U'c');  // (0,2) → auto-wrap to (1,0)

    // Write another character (should clear pending_wrap_ and work normally).
    g.write_char(U'd');

    CHECK(g.cell(1, 0).codepoint == U'd');
    CHECK(g.cursor_row() == 1);
    CHECK(g.cursor_col() == 1);
}

TEST_CASE("Grid — pending-wrap: \\n alone after auto-wrap does not double-advance", "[grid][pending_wrap]") {
    terminal_grid g(3, 3);
    // Trigger auto-wrap.
    g.write_char(U'a');
    g.write_char(U'b');
    g.write_char(U'c');  // auto-wrap to (1,0)

    // Send \n via apply.
    action a;
    a.type = action_type::newline;
    g.apply(a);

    // Cursor should still be at (1,0) — pending_wrap consumed the \n.
    CHECK(g.cursor_row() == 1);
    CHECK(g.cursor_col() == 0);
}

TEST_CASE("Grid — pending-wrap: \\r\\n without prior auto-wrap advances normally", "[grid][pending_wrap]") {
    terminal_grid g(3, 3);
    g.write_char(U'a');  // (0,0) → (0,1)
    // No auto-wrap triggered.
    write_bytes(g, "\r\n");
    // Should advance to (1,0) normally.
    CHECK(g.cursor_row() == 1);
    CHECK(g.cursor_col() == 0);
}

TEST_CASE("Grid — pending-wrap: cursor movement clears flag", "[grid][pending_wrap]") {
    terminal_grid g(5, 5);
    // Trigger auto-wrap.
    write_bytes(g, "ABCDE");  // fills row 0, auto-wraps to (1,0)

    // Now move cursor via escape sequence — should clear pending_wrap_.
    write_bytes(g, "\x1B[3;3H");  // CUP to row 3, col 3 (0-based: 2,2)

    // Then \r\n should work normally (not be consumed).
    write_bytes(g, "\r\n");
    CHECK(g.cursor_row() == 3);
    CHECK(g.cursor_col() == 0);
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
// Access — cell()
// ===========================================================================

TEST_CASE("Grid — cell() returns correct codepoint", "[grid][access]") {
    terminal_grid g(2, 2);
    g.write_char(U'a');
    CHECK(g.cell(0, 0).codepoint == U'a');
    CHECK(g.cell(0, 1).codepoint == U' ');
    CHECK(g.cell(1, 0).codepoint == U' ');
    CHECK(g.cell(1, 1).codepoint == U' ');
}

TEST_CASE("Grid — cell() returns full cell data", "[grid][access]") {
    terminal_grid g(2, 2);
    g.write_char(U'a');
    g.write_char(U'b');
    CHECK(g.cell(0, 0).codepoint == U'a');
    CHECK(g.cell(0, 1).codepoint == U'b');
    CHECK(g.cell(1, 0).codepoint == U' ');
    CHECK(g.cell(1, 1).codepoint == U' ');
}

// ===========================================================================
// write_bytes
// ===========================================================================

TEST_CASE("Grid — write_bytes processes simple text", "[grid][write_bytes]") {
    terminal_grid g(10, 5);
    write_bytes(g, "Hello");
    CHECK(g.cell(0, 0).codepoint == U'H');
    CHECK(g.cell(0, 1).codepoint == U'e');
    CHECK(g.cell(0, 2).codepoint == U'l');
    CHECK(g.cell(0, 3).codepoint == U'l');
    CHECK(g.cell(0, 4).codepoint == U'o');
}

TEST_CASE("Grid — write_bytes handles newline", "[grid][write_bytes]") {
    terminal_grid g(10, 5);
    write_bytes(g, "AB\nCD");
    CHECK(g.cell(0, 0).codepoint == U'A');
    CHECK(g.cell(0, 1).codepoint == U'B');
    CHECK(g.cell(1, 0).codepoint == U'C');
    CHECK(g.cell(1, 1).codepoint == U'D');
}

TEST_CASE("Grid — write_bytes handles carriage_return", "[grid][write_bytes]") {
    terminal_grid g(10, 5);
    write_bytes(g, "AB\rC");
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
    a.payload = cursor_pos{5, 7};
    g.apply(a);
    CHECK(g.cursor_row() == 5);
    CHECK(g.cursor_col() == 7);
}

TEST_CASE("Grid — apply move_cursor clamps to bounds", "[grid][apply][edge]") {
    terminal_grid g(5, 3);
    action a;
    a.type = action_type::move_cursor;
    a.payload = cursor_pos{99, 99};
    g.apply(a);
    CHECK(g.cursor_row() == 2);
    CHECK(g.cursor_col() == 4);
}

TEST_CASE("Grid — apply move_cursor_up", "[grid][apply]") {
    terminal_grid g(10, 10);
    action a;
    a.type = action_type::move_cursor;
    a.payload = cursor_pos{5, 5};
    g.apply(a);  // cursor at (5,5)

    action b;
    b.type = action_type::move_cursor_up;
    b.payload = uint32_t{2};
    g.apply(b);
    CHECK(g.cursor_row() == 3);
    CHECK(g.cursor_col() == 5);
}

TEST_CASE("Grid — apply move_cursor_up clamps to 0", "[grid][apply][edge]") {
    terminal_grid g(10, 10);
    action a;
    a.type = action_type::move_cursor;
    a.payload = cursor_pos{1, 0};
    g.apply(a);

    action b;
    b.type = action_type::move_cursor_up;
    b.payload = uint32_t{5};
    g.apply(b);
    CHECK(g.cursor_row() == 0);
}

TEST_CASE("Grid — apply move_cursor_down", "[grid][apply]") {
    terminal_grid g(10, 10);
    action b;
    b.type = action_type::move_cursor_down;
    b.payload = uint32_t{3};
    g.apply(b);
    CHECK(g.cursor_row() == 3);
}

TEST_CASE("Grid — apply move_cursor_down clamps to last row", "[grid][apply][edge]") {
    terminal_grid g(10, 3);
    action b;
    b.type = action_type::move_cursor_down;
    b.payload = uint32_t{99};
    g.apply(b);
    CHECK(g.cursor_row() == 2);
}

TEST_CASE("Grid — apply move_cursor_forward", "[grid][apply]") {
    terminal_grid g(10, 5);
    action b;
    b.type = action_type::move_cursor_forward;
    b.payload = uint32_t{4};
    g.apply(b);
    CHECK(g.cursor_col() == 4);
}

TEST_CASE("Grid — apply move_cursor_forward clamps to last col", "[grid][apply][edge]") {
    terminal_grid g(5, 3);
    action b;
    b.type = action_type::move_cursor_forward;
    b.payload = uint32_t{99};
    g.apply(b);
    CHECK(g.cursor_col() == 4);
}

TEST_CASE("Grid — apply move_cursor_back", "[grid][apply]") {
    terminal_grid g(10, 5);
    action a;
    a.type = action_type::move_cursor;
    a.payload = cursor_pos{0, 5};
    g.apply(a);

    action b;
    b.type = action_type::move_cursor_back;
    b.payload = uint32_t{3};
    g.apply(b);
    CHECK(g.cursor_col() == 2);
}

TEST_CASE("Grid — apply move_cursor_back clamps to 0", "[grid][apply][edge]") {
    terminal_grid g(10, 5);
    action b;
    b.type = action_type::move_cursor_back;
    b.payload = uint32_t{99};
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
    mv.payload = cursor_pos{5, 7};
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
    mv.payload = cursor_pos{5, 7};
    g.apply(mv);

    // Save.
    action save;
    save.type = action_type::save_cursor;
    g.apply(save);

    // Move elsewhere.
    action mv2;
    mv2.type = action_type::move_cursor;
    mv2.payload = cursor_pos{2, 2};
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
    write_bytes(g, "\x1B[6;8H");  // CUP: row 6, col 8 → 0-based (5, 7)
    write_bytes(g, "\x1B" "7");   // DECSC

    // Move elsewhere.
    write_bytes(g, "\x1B[3;3H");  // CUP: (2, 2)

    // Restore.
    write_bytes(g, "\x1B" "8");   // DECRC

    CHECK(g.cursor_row() == 5);
    CHECK(g.cursor_col() == 7);
}

TEST_CASE("Grid — saved cursor is reset after resize", "[grid][cursor_save_restore][resize]") {
    terminal_grid g(10, 10);
    // Move to (5, 5) and save.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{5, 5};
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
    // With scrollback, all rows are preserved (reflowed from 5→3 cols).
    // Original row 0 (a,b,c,d,e) becomes 2 rows: (a,b,c) and (d,e,' ').
    // Since there are now 10 reflowed rows and only 3 visible, the bottom
    // 3 are visible. Scroll to top to check original content.
    CHECK(g.is_following_output() == true);
    (void)g.scroll_viewport(7);  // scroll back to the top
    CHECK(g.cell(0, 0).codepoint == U'a');
    CHECK(g.cell(0, 1).codepoint == U'b');
    CHECK(g.cell(0, 2).codepoint == U'c');
}

TEST_CASE("Grid — resize clamps cursor to new bounds", "[grid][resize]") {
    terminal_grid g(10, 10);
    action a;
    a.type = action_type::move_cursor;
    a.payload = cursor_pos{7, 7};
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

TEST_CASE("Grid — resize preserves SGR colours", "[grid][resize]") {
    terminal_grid g(4, 2);
    // Set row 0 to red text on blue background.
    write_bytes(g, "\x1B[31m\x1B[44m");  // SGR red fg, blue bg
    g.write_char(U'A');
    g.write_char(U'B');
    // Grow columns from 4 to 8.
    g.resize(8, 2);
    auto const& c0 = g.cell(0, 0);
    CHECK(c0.codepoint == U'A');
    // Red fg = palette[1] = {0xf3, 0x8b, 0xa8, 0}
    CHECK(c0.fg.r == 0xf3);
    CHECK(c0.fg.g == 0x8b);
    CHECK(c0.fg.b == 0xa8);
    CHECK(c0.fg.flags == 0);
    // Blue bg = palette[4] = {0x89, 0xb4, 0xfa, 0}
    CHECK(c0.bg.r == 0x89);
    CHECK(c0.bg.g == 0xb4);
    CHECK(c0.bg.b == 0xfa);
    CHECK(c0.bg.flags == 0);
    // Second cell also preserved.
    auto const& c1 = g.cell(0, 1);
    CHECK(c1.codepoint == U'B');
    CHECK(c1.fg.r == 0xf3);
    CHECK(c1.bg.r == 0x89);
    // New cells (col 4–7) should be default.
    auto const& c4 = g.cell(0, 4);
    CHECK(c4.codepoint == U' ');
    CHECK(c4.fg.flags == 1);
    CHECK(c4.bg.flags == 1);
}

TEST_CASE("Grid — resize only rows (same cols)", "[grid][resize]") {
    terminal_grid g(5, 3);
    g.write_char(U'a'); g.write_char(U'b'); g.write_char(U'c');
    g.newline();  // (1, 0)
    g.write_char(U'd');
    g.resize(5, 5);  // grow taller
    CHECK(g.cols() == 5);
    CHECK(g.rows() == 5);
    CHECK(g.cell(0, 0).codepoint == U'a');
    CHECK(g.cell(0, 2).codepoint == U'c');
    CHECK(g.cell(1, 0).codepoint == U'd');
    // New rows should be spaces.
    CHECK(g.cell(3, 0).codepoint == U' ');
    CHECK(g.cell(4, 4).codepoint == U' ');
}

TEST_CASE("Grid — resize only cols (same rows)", "[grid][resize]") {
    terminal_grid g(4, 3);
    g.write_char(U'x'); g.write_char(U'y'); g.write_char(U'z');
    g.resize(6, 3);  // grow wider
    CHECK(g.cols() == 6);
    CHECK(g.rows() == 3);
    CHECK(g.cell(0, 0).codepoint == U'x');
    CHECK(g.cell(0, 1).codepoint == U'y');
    CHECK(g.cell(0, 2).codepoint == U'z');
    // New columns should be spaces.
    CHECK(g.cell(0, 4).codepoint == U' ');
    CHECK(g.cell(0, 5).codepoint == U' ');
    // Other rows untouched.
    CHECK(g.cell(1, 0).codepoint == U' ');
}

TEST_CASE("Grid — resize from zero-size grid", "[grid][resize][edge]") {
    terminal_grid g(0, 0);
    g.resize(10, 5);
    CHECK(g.cols() == 10);
    CHECK(g.rows() == 5);
    // All cells should be default-initialised.
    for (uint32_t r = 0; r < 5; ++r)
        for (uint32_t c = 0; c < 10; ++c) {
            auto const& cell = g.cell(r, c);
            CHECK(cell.codepoint == U' ');
            CHECK(cell.fg.flags == 1);
            CHECK(cell.bg.flags == 1);
        }
}

TEST_CASE("Grid — resize truncates rows beyond copy_rows", "[grid][resize]") {
    // Use a 3×5 grid and only fill rows 0–3 (leave row 4 as buffer to
    // avoid auto-wrap-scroll corrupting the test).
    terminal_grid g(3, 5);
    // Place each row via CUP + exactly 3 characters (no auto-wrap trigger).
    write_bytes(g, "\x1B[1;1H");  write_bytes(g, "aaa");
    write_bytes(g, "\x1B[2;1H");  write_bytes(g, "bbb");
    write_bytes(g, "\x1B[3;1H");  write_bytes(g, "ccc");
    write_bytes(g, "\x1B[4;1H");  write_bytes(g, "ddd");
    // Shrink to 2 rows.
    g.resize(3, 2);
    CHECK(g.rows() == 2);
    CHECK(g.cols() == 3);
    // With scrollback, the bottom 2 rows are visible (rows 3 "ddd" and 4 spaces).
    // Scroll back to the top to verify original content is preserved.
    (void)g.scroll_viewport(3);  // scroll to show rows 0-1 (aaa, bbb)
    CHECK(g.cell(0, 0).codepoint == U'a');
    CHECK(g.cell(0, 1).codepoint == U'a');
    CHECK(g.cell(1, 0).codepoint == U'b');
}

TEST_CASE("Grid — write after resize works correctly", "[grid][resize]") {
    terminal_grid g(5, 3);
    g.resize(10, 5);
    // Write to a position that was outside the old grid.
    write_bytes(g, "\x1B[3;8H");  // CUP to (2, 7)
    g.write_char(U'X');
    CHECK(g.cell(2, 7).codepoint == U'X');
    // Cursor should advance correctly.
    CHECK(g.cursor_col() == 8);
    CHECK(g.cursor_row() == 2);
    // Write more to test auto-wrap in new dimensions.
    g.write_char(U'Y');  // col 8
    g.write_char(U'Z');  // col 9, wraps
    CHECK(g.cell(2, 8).codepoint == U'Y');
    CHECK(g.cell(2, 9).codepoint == U'Z');
    CHECK(g.cursor_row() == 3);
    CHECK(g.cursor_col() == 0);
}

TEST_CASE("Grid — resize preserves render_cells output", "[grid][resize]") {
    terminal_grid g(3, 2);
    g.write_char(U'a'); g.write_char(U'b');
    g.resize(5, 3);
    auto const rc = g.render_cells();
    REQUIRE(rc.size() == 15);  // 5 * 3
    // Original cells should render at their positions.
    CHECK(rc[0].codepoint == U'a');
    CHECK(rc[1].codepoint == U'b');
    // New cells should be space with default colours.
    CHECK(rc[4].codepoint == U' ');
    CHECK(rc[5].codepoint == U' ');
}

// ===========================================================================
// Integration — write_bytes with CSI sequences
// ===========================================================================

TEST_CASE("Grid — write_bytes with CUP positions cursor", "[grid][integration]") {
    terminal_grid g(10, 10);
    write_bytes(g, "\x1B[5;8H");
    // 0-based: row 4, col 7
    CHECK(g.cursor_row() == 4);
    CHECK(g.cursor_col() == 7);
}

TEST_CASE("Grid — write_bytes with CUU/CUD/CUF/CUB", "[grid][integration]") {
    terminal_grid g(10, 10);
    // Start at (5,5)
    write_bytes(g, "\x1B[6;6H");  // 0-based: (5,5)
    write_bytes(g, "\x1B[2A");    // CUU 2 → row 3
    CHECK(g.cursor_row() == 3);
    write_bytes(g, "\x1B[4B");    // CUD 4 → row 7
    CHECK(g.cursor_row() == 7);
    write_bytes(g, "\x1B[3C");    // CUF 3 → col 8
    CHECK(g.cursor_col() == 8);
    write_bytes(g, "\x1B[5D");    // CUB 5 → col 3
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
    write_bytes(g, "\x1B[1;1HXXXXX");  // row 0
    write_bytes(g, "\x1B[2;1HXXXXX");  // row 1
    write_bytes(g, "\x1B[3;1HXXXX");   // row 2: 4 X's (skip last to avoid scroll)
    // Move to (1,2).
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{1, 2};
    g.apply(mv);

    // Erase from cursor to end.
    action ed;
    ed.type = action_type::erase_display;
    ed.payload = uint32_t{0};
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
    write_bytes(g, "\x1B[1;1HXXXXX");  // row 0: 5 X's
    write_bytes(g, "\x1B[2;1HXXXXX");  // row 1: 5 X's
    write_bytes(g, "\x1B[3;1HXXXX");   // row 2: 4 X's (skip last to avoid scroll)

    // Move cursor to (1, 2).
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{1, 2};
    g.apply(mv);

    action ed;
    ed.type = action_type::erase_display;
    ed.payload = uint32_t{1};
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
    write_bytes(g, "\x1B[1;1HXXXXX");
    write_bytes(g, "\x1B[2;1HXXXXX");
    write_bytes(g, "\x1B[3;1HXXXXX");

    action ed;
    ed.type = action_type::erase_display;
    ed.payload = uint32_t{2};
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
    mv.payload = cursor_pos{2, 3};
    g.apply(mv);

    action ed;
    ed.type = action_type::erase_display;
    ed.payload = uint32_t{2};
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
        mv.payload = cursor_pos{1, c};
        g.apply(mv);
        g.write_char(U'X');
    }
    // Cursor at (1, 2).
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{1, 2};
    g.apply(mv);

    action el;
    el.type = action_type::erase_line;
    el.payload = uint32_t{0};
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
        mv.payload = cursor_pos{1, c};
        g.apply(mv);
        g.write_char(U'X');
    }
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{1, 2};
    g.apply(mv);

    action el;
    el.type = action_type::erase_line;
    el.payload = uint32_t{1};
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
    write_bytes(g, "\x1B[2;1H");  // CUP to row 2, col 1 (0-based: 1, 0)
    write_bytes(g, "XXXXX");
    // Cursor is now at (1,5) → auto-wrapped to (2,0). Move back to row 1.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{1, 2};
    g.apply(mv);

    action el;
    el.type = action_type::erase_line;
    el.payload = uint32_t{2};
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
    mv.payload = cursor_pos{1, 3};
    g.apply(mv);

    action el;
    el.type = action_type::erase_line;
    el.payload = uint32_t{2};
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
    ed.payload = uint32_t{2};
    g.apply(ed);
    SUCCEED("erase_display on zero-size grid did not crash");
}

TEST_CASE("Grid — erase_line on zero-size grid does not crash", "[grid][erase][edge]") {
    terminal_grid g(0, 0);
    action el;
    el.type = action_type::erase_line;
    el.payload = uint32_t{2};
    g.apply(el);
    SUCCEED("erase_line on zero-size grid did not crash");
}

TEST_CASE("Grid — erase_display mode 3 treated as mode 2", "[grid][erase]") {
    terminal_grid g(3, 2);
    // Fill with 'X' using CUP.
    write_bytes(g, "\x1B[1;1HXXX");
    write_bytes(g, "\x1B[2;1HXXX");

    action ed;
    ed.type = action_type::erase_display;
    ed.payload = uint32_t{3};
    g.apply(ed);

    for (uint32_t r = 0; r < 2; ++r)
        for (uint32_t c = 0; c < 3; ++c)
            CHECK(g.cell(r, c).codepoint == U' ');
}

TEST_CASE("Grid — erase uses default colours", "[grid][erase]") {
    terminal_grid g(5, 3);
    // Set a cell with non-default colours via SGR, then erase.
    write_bytes(g, "\x1B[31m");  // SGR red fg
    g.write_char(U'Y');           // at (0,0) with red fg

    // Move back to (0,0) and erase the line.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{0, 0};
    g.apply(mv);

    action el;
    el.type = action_type::erase_line;
    el.payload = uint32_t{2};
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
    write_bytes(g, "\x1B[1;1HXXXXX");
    write_bytes(g, "\x1B[2;1HXXXXX");
    write_bytes(g, "\x1B[3;1HXXXX");   // 4 X's to avoid scroll
    // Move to (1, 2) and erase from cursor to end via escape sequence.
    write_bytes(g, "\x1B[2;3H");   // CUP to row 2, col 3 (0-based: 1, 2)
    write_bytes(g, "\x1B[0J");     // ED mode 0

    CHECK(g.cell(0, 0).codepoint == U'X');
    CHECK(g.cell(1, 2).codepoint == U' ');
    CHECK(g.cell(2, 4).codepoint == U' ');
}

// ===========================================================================
// Task 13 — Scroll region (DECSTBM)
// ===========================================================================

TEST_CASE("Grid — default scroll region is full screen", "[grid][scroll_region]") {
    terminal_grid g(10, 5);
    // Use CUP to place test content avoiding auto-wrap at the bottom.
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "row0");
    write_bytes(g, "\x1B[5;1H"); write_bytes(g, "row4");

    // Cursor ends after "row4" at (4, 4). Move to col 0 of bottom row.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{4, 0};
    g.apply(mv);
    CHECK(g.cursor_row() == 4);

    // newline at bottom margin scrolls.
    g.newline();
    CHECK(g.cursor_row() == 4);  // stays at bottom margin
    // Row 0 content pushed to scrollback. Check via scrolling back.
    (void)g.scroll_viewport(1);
    CHECK(g.cell(0, 0).codepoint == U'r');
}

TEST_CASE("Grid — set_scroll_region with valid params", "[grid][scroll_region]") {
    terminal_grid g(10, 10);
    g.set_scroll_region(3, 8);  // 1-based: rows 3-8 (0-based: 2-7)
    // Cursor should move to home (0, 0).
    CHECK(g.cursor_row() == 0);
    CHECK(g.cursor_col() == 0);
}

TEST_CASE("Grid — set_scroll_region top > bottom is ignored", "[grid][scroll_region]") {
    terminal_grid g(10, 10);
    // Fill with 'X' via CUP.
    write_bytes(g, "\x1B[1;1HXXXXXXXXXX");
    write_bytes(g, "\x1B[2;1HYYYYYYYYYY");
    // Try to set invalid region.
    g.set_scroll_region(8, 3);  // top > bottom — ignored
    // newline at bottom should scroll full screen (default region unchanged).
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{9, 0};
    g.apply(mv);
    g.newline();  // full-screen scroll
    // Row 0 should be 'Y', row 9 blank.
    CHECK(g.cell(0, 0).codepoint == U'Y');
    CHECK(g.cell(9, 0).codepoint == U' ');
}

TEST_CASE("Grid — set_scroll_region with top > rows is ignored", "[grid][scroll_region]") {
    terminal_grid g(10, 5);

    // Move cursor away from home so we can verify the call is a no-op.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{3, 7};
    g.apply(mv);
    CHECK(g.cursor_row() == 3);
    CHECK(g.cursor_col() == 7);

    // top=99 clamps to 5, bottom=100 clamps to 5 → top >= bottom → ignored per VT100.
    g.set_scroll_region(99, 100);

    // Cursor was NOT moved to home (the call was ignored).
    CHECK(g.cursor_row() == 3);
    CHECK(g.cursor_col() == 7);
}

TEST_CASE("Grid — set_scroll_region bottom > rows clamped", "[grid][scroll_region]") {
    terminal_grid g(10, 5);
    g.set_scroll_region(1, 999);  // bottom clamped to 5 → region 1-5, cursor home
    // Cursor at (0,0). Write on row 0 via CUP.
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "HEADER");
    // Move cursor to bottom.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{4, 0};
    g.apply(mv);
    g.newline();  // should scroll full screen (region = 1-5)
    CHECK(g.cursor_row() == 4);
    // Row 0 went to scrollback — check via scrolling back.
    (void)g.scroll_viewport(1);
    CHECK(g.cell(0, 0).codepoint == U'H');
}

TEST_CASE("Grid — set_scroll_region 0;0 resets to full screen", "[grid][scroll_region]") {
    terminal_grid g(10, 10);
    // First set a sub-region.
    g.set_scroll_region(3, 8);
    // Then reset.
    g.set_scroll_region(0, 0);
    // Now newline at bottom should scroll full screen.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{9, 0};
    g.apply(mv);
    g.write_char(U'X');
    g.newline();  // scroll full screen
    // Row 0 scrolled into scrollback, row 9 blank.
    CHECK(g.cursor_row() == 9);
}

// ===========================================================================
// Task 13 — newline with scroll region
// ===========================================================================

TEST_CASE("Grid — newline at bottom margin scrolls region, header stays", "[grid][scroll_region][newline]") {
    terminal_grid g(10, 5);
    // Fill with labels using CUP. Write fewer chars to avoid auto-wrap.
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "HDR");
    write_bytes(g, "\x1B[2;1H"); write_bytes(g, "r1");
    write_bytes(g, "\x1B[3;1H"); write_bytes(g, "r2");
    write_bytes(g, "\x1B[4;1H"); write_bytes(g, "r3");
    write_bytes(g, "\x1B[5;1H"); write_bytes(g, "r4");

    // Set scroll region to rows 2-5 (0-based: 1-4).
    g.set_scroll_region(2, 5);
    // Cursor is now at home (0,0).

    // Move to bottom margin.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{4, 0};
    g.apply(mv);

    // newline at bottom margin should scroll the region, NOT the header.
    g.newline();

    // Header row 0 should be unchanged.
    CHECK(g.cell(0, 0).codepoint == U'H');
    // Row 1 (first row of scroll region) should now have old row 2 content.
    CHECK(g.cell(1, 0).codepoint == U'r');
    CHECK(g.cell(1, 1).codepoint == U'2');
    // Row 3 should have old row 4 content.
    CHECK(g.cell(3, 0).codepoint == U'r');
    CHECK(g.cell(3, 1).codepoint == U'4');
    // Bottom row of region (row 4) should be blank.
    CHECK(g.cell(4, 0).codepoint == U' ');
    // Cursor stays at bottom margin.
    CHECK(g.cursor_row() == 4);
}

TEST_CASE("Grid — newline above bottom margin just advances", "[grid][scroll_region][newline]") {
    terminal_grid g(10, 5);
    g.set_scroll_region(1, 4);  // rows 1-4 (0-based: 0-3)
    // Cursor is at home (0,0) after set_scroll_region.
    g.newline();  // cursor was at (0,0), which is the bottom margin!
    // Actually no — bottom margin is 3 (0-based). Cursor at 0 < 3.
    // newline: cursor was at 0, which is < scroll_bottom_(3), so just advance to 1.
    CHECK(g.cursor_row() == 1);
    CHECK(g.cursor_col() == 0);
}

// ===========================================================================
// Task 13 — write_char auto-wrap with scroll region
// ===========================================================================

TEST_CASE("Grid — write_char auto-wrap respects scroll region", "[grid][scroll_region][write_char]") {
    terminal_grid g(3, 5);  // 5 rows to avoid bottom-row auto-scroll
    g.set_scroll_region(2, 3);  // rows 2-3 (0-based: 1-2)
    // Cursor is at home (0,0).

    // Write header row.
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "HDR");  // fills row 0

    // Move to row 1, start writing.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{1, 0};
    g.apply(mv);

    // Write data in the scroll region. "abc" fills row 1, wraps to row 2.
    g.write_char(U'a'); g.write_char(U'b'); g.write_char(U'c');  // wraps to row 2
    g.write_char(U'd'); g.write_char(U'e'); g.write_char(U'f');  // wraps — cursor was at (2,3) → scroll_up

    // Header row should be untouched.
    CHECK(g.cell(0, 0).codepoint == U'H');
    CHECK(g.cell(0, 1).codepoint == U'D');
    CHECK(g.cell(0, 2).codepoint == U'R');
}

// ===========================================================================
// Task 13 — IL: Insert Lines
// ===========================================================================

TEST_CASE("Grid — IL inserts blank line at cursor within region", "[grid][il]") {
    terminal_grid g(5, 5);
    // Fill with letters using CUP + exactly 4 write_char calls to avoid
    // auto-wrap at the bottom row triggering scroll.
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "AAAA");
    write_bytes(g, "\x1B[2;1H"); write_bytes(g, "BBBB");
    write_bytes(g, "\x1B[3;1H"); write_bytes(g, "CCCC");
    write_bytes(g, "\x1B[4;1H"); write_bytes(g, "DDDD");
    write_bytes(g, "\x1B[5;1H"); write_bytes(g, "EEEE");

    // Move cursor to row 2 (0-based: 1).
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{1, 2};
    g.apply(mv);

    // Insert 2 lines.
    g.insert_lines(2);

    // Row 0 unchanged.
    CHECK(g.cell(0, 0).codepoint == U'A');
    // Rows 1-2 should be blank (inserted).
    CHECK(g.cell(1, 0).codepoint == U' ');
    CHECK(g.cell(2, 0).codepoint == U' ');
    // Row 3 should have old row 1 content (B → shifted to row 3).
    CHECK(g.cell(3, 0).codepoint == U'B');
    // Row 4 should have old row 2 content (C → shifted to row 4).
    CHECK(g.cell(4, 0).codepoint == U'C');
    // D and E are lost.
    // Cursor column reset.
    CHECK(g.cursor_col() == 0);
    CHECK(g.cursor_row() == 1);  // row unchanged
}

TEST_CASE("Grid — IL count clamped to region boundary", "[grid][il]") {
    terminal_grid g(5, 5);
    // Fill rows 2-4 (0-based: 1-3).
    write_bytes(g, "\x1B[2;1H"); write_bytes(g, "BBBBB");
    write_bytes(g, "\x1B[3;1H"); write_bytes(g, "CCCCC");
    write_bytes(g, "\x1B[4;1H"); write_bytes(g, "DDDDD");

    // Set scroll region to rows 2-4 (0-based: 1-3).
    g.set_scroll_region(2, 4);
    // Cursor is now at (0,0). Move to row 2.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{2, 0};  // 0-based row 2 (third visible row)
    g.apply(mv);

    // Only 2 rows remain in the region below cursor (rows 2 and 3).
    // Try to insert 5 lines — should be clamped to 2.
    g.insert_lines(5);

    // Rows 2-3 should be blank.
    CHECK(g.cell(2, 0).codepoint == U' ');
    CHECK(g.cell(3, 0).codepoint == U' ');
    // B, C, D shifted off the bottom, lost.
}

TEST_CASE("Grid — IL ignored when cursor outside scroll region (above)", "[grid][il][edge]") {
    terminal_grid g(5, 5);
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "AAAAA");
    write_bytes(g, "\x1B[2;1H"); write_bytes(g, "BBBBB");

    // Set scroll region to rows 3-5 (0-based: 2-4).
    g.set_scroll_region(3, 5);
    // Cursor is at (0,0) — above scroll region.
    CHECK(g.cursor_row() == 0);

    g.insert_lines(1);  // should be ignored

    // Data unchanged.
    CHECK(g.cell(0, 0).codepoint == U'A');
    CHECK(g.cell(1, 0).codepoint == U'B');
}

TEST_CASE("Grid — IL ignored when cursor outside scroll region (below)", "[grid][il][edge]") {
    terminal_grid g(5, 5);
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "AAAAA");

    // Set scroll region to rows 1-2 (0-based: 0-1).
    g.set_scroll_region(1, 2);
    // Cursor is at (0,0) — inside region.
    // Move cursor below region.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{3, 0};
    g.apply(mv);

    g.insert_lines(1);  // should be ignored (row 3 > scroll_bottom_ 1)

    // Data unchanged.
    CHECK(g.cell(0, 0).codepoint == U'A');
}

TEST_CASE("Grid — IL cursor col resets to 0", "[grid][il]") {
    terminal_grid g(10, 5);
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{2, 5};
    g.apply(mv);

    g.insert_lines(1);
    CHECK(g.cursor_col() == 0);
}

// ===========================================================================
// Task 13 — DL: Delete Lines
// ===========================================================================

TEST_CASE("Grid — DL deletes line at cursor within region", "[grid][dl]") {
    terminal_grid g(5, 5);
    // Fill with letters using CUP to avoid auto-wrap at bottom.
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "AAAA");
    write_bytes(g, "\x1B[2;1H"); write_bytes(g, "BBBB");
    write_bytes(g, "\x1B[3;1H"); write_bytes(g, "CCCC");
    write_bytes(g, "\x1B[4;1H"); write_bytes(g, "DDDD");
    write_bytes(g, "\x1B[5;1H"); write_bytes(g, "EEEE");

    // Move cursor to row 1 (0-based).
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{1, 2};
    g.apply(mv);

    // Delete 2 lines starting at row 1 (rows 1 and 2 deleted).
    g.delete_lines(2);

    // Row 0 unchanged.
    CHECK(g.cell(0, 0).codepoint == U'A');
    // Row 1 should have D (old row 3 content shifted up).
    CHECK(g.cell(1, 0).codepoint == U'D');
    // Row 2 should have E (old row 4 content shifted up).
    CHECK(g.cell(2, 0).codepoint == U'E');
    // Row 3 should be blank.
    CHECK(g.cell(3, 0).codepoint == U' ');
    // Row 4 should be blank.
    CHECK(g.cell(4, 0).codepoint == U' ');
    // Cursor column reset.
    CHECK(g.cursor_col() == 0);
}

TEST_CASE("Grid — DL ignored when cursor outside scroll region", "[grid][dl][edge]") {
    terminal_grid g(5, 5);
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "AAAAA");
    write_bytes(g, "\x1B[3;1H"); write_bytes(g, "CCCCC");

    // Set scroll region to rows 1-2 (0-based: 0-1).
    g.set_scroll_region(1, 2);
    // Cursor is at (0,0) — inside region.
    // Move cursor below region.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{3, 0};
    g.apply(mv);

    g.delete_lines(1);  // should be ignored

    CHECK(g.cell(0, 0).codepoint == U'A');
    CHECK(g.cell(2, 0).codepoint == U'C');
}

TEST_CASE("Grid — DL cursor col resets to 0", "[grid][dl]") {
    terminal_grid g(10, 5);
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{2, 5};
    g.apply(mv);

    g.delete_lines(1);
    CHECK(g.cursor_col() == 0);
}

// ===========================================================================
// Task 13 — SU: Scroll Up
// ===========================================================================

TEST_CASE("Grid — SU full screen pushes to scrollback", "[grid][su]") {
    terminal_grid g(5, 3);
    // Use CUP to avoid auto-wrap at bottom.
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "AAAA");
    write_bytes(g, "\x1B[2;1H"); write_bytes(g, "BBBB");
    write_bytes(g, "\x1B[3;1H"); write_bytes(g, "CCCC");

    g.scroll_page_up(1);

    // Row 0 should be B, row 1 should be C.
    CHECK(g.cell(0, 0).codepoint == U'B');
    CHECK(g.cell(1, 0).codepoint == U'C');
    // Row 2 should be blank.
    CHECK(g.cell(2, 0).codepoint == U' ');
    // Row A should be in scrollback. is_following_output only changes when
    // user manually scrolls; scrollback_count increased but viewport unchanged.
    CHECK(g.is_following_output() == true);
    // Verify scrollback has Row A by scrolling back.
    (void)g.scroll_viewport(1);
    CHECK(g.cell(0, 0).codepoint == U'A');
}

TEST_CASE("Grid — SU sub-region does not affect scrollback", "[grid][su]") {
    terminal_grid g(5, 5);
    // Use CUP to avoid auto-wrap at bottom.
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "AAAA");
    write_bytes(g, "\x1B[2;1H"); write_bytes(g, "BBBB");
    write_bytes(g, "\x1B[3;1H"); write_bytes(g, "CCCC");
    write_bytes(g, "\x1B[4;1H"); write_bytes(g, "DDDD");
    write_bytes(g, "\x1B[5;1H"); write_bytes(g, "EEEE");

    // Set scroll region to rows 2-4 (0-based: 1-3).
    g.set_scroll_region(2, 4);
    // Cursor is now at home (0,0).

    g.scroll_page_up(1);

    // Row 0 (header, outside region) unchanged.
    CHECK(g.cell(0, 0).codepoint == U'A');
    // Row 1 (first of region) should now have C (was row 2).
    CHECK(g.cell(1, 0).codepoint == U'C');
    // Row 2 should have D (was row 3).
    CHECK(g.cell(2, 0).codepoint == U'D');
    // Row 3 should be blank (bottom of region).
    CHECK(g.cell(3, 0).codepoint == U' ');
    // Row 4 (below region) unchanged.
    CHECK(g.cell(4, 0).codepoint == U'E');
}

// ===========================================================================
// Task 13 — SD: Scroll Down
// ===========================================================================

TEST_CASE("Grid — SD full screen inserts blanks at top", "[grid][sd]") {
    terminal_grid g(5, 3);
    // Use CUP to avoid auto-wrap at bottom.
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "AAAA");
    write_bytes(g, "\x1B[2;1H"); write_bytes(g, "BBBB");
    write_bytes(g, "\x1B[3;1H"); write_bytes(g, "CCCC");

    g.scroll_page_down(1);

    // Row 0 should be blank.
    CHECK(g.cell(0, 0).codepoint == U' ');
    // Row 1 should have A.
    CHECK(g.cell(1, 0).codepoint == U'A');
    // Row 2 should have B.
    CHECK(g.cell(2, 0).codepoint == U'B');
    // C should be lost.
}

TEST_CASE("Grid — SD sub-region does not affect outside rows", "[grid][sd]") {
    terminal_grid g(5, 5);
    // Use CUP to avoid auto-wrap at bottom.
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "AAAA");
    write_bytes(g, "\x1B[2;1H"); write_bytes(g, "BBBB");
    write_bytes(g, "\x1B[3;1H"); write_bytes(g, "CCCC");
    write_bytes(g, "\x1B[4;1H"); write_bytes(g, "DDDD");
    write_bytes(g, "\x1B[5;1H"); write_bytes(g, "EEEE");

    // Set scroll region to rows 2-4 (0-based: 1-3).
    g.set_scroll_region(2, 4);
    // Cursor is now at home (0,0).

    g.scroll_page_down(1);

    // Row 0 (header, outside region) unchanged.
    CHECK(g.cell(0, 0).codepoint == U'A');
    // Row 1 (first of region) should be blank.
    CHECK(g.cell(1, 0).codepoint == U' ');
    // Row 2 should have old row 1 content (B).
    CHECK(g.cell(2, 0).codepoint == U'B');
    // Row 3 should have old row 2 content (C).
    CHECK(g.cell(3, 0).codepoint == U'C');
    // Row 4 (below region) unchanged.
    CHECK(g.cell(4, 0).codepoint == U'E');
}

// ===========================================================================
// Task 13 — Edge cases
// ===========================================================================

TEST_CASE("Grid — IL/DL/SU/SD on zero-size grid does not crash", "[grid][lineops][edge]") {
    terminal_grid g(0, 0);
    g.insert_lines(1);
    g.delete_lines(1);
    g.scroll_page_up(1);
    g.scroll_page_down(1);
    g.set_scroll_region(1, 1);
    SUCCEED("line operations on zero-size grid did not crash");
}

TEST_CASE("Grid — scroll region preserved across resize", "[grid][scroll_region][resize]") {
    terminal_grid g(10, 10);
    g.set_scroll_region(3, 8);
    g.resize(20, 5);
    // Resize resets scroll region to full screen.
    // So newline at bottom should scroll full screen.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{4, 0};
    g.apply(mv);
    g.write_char(U'X');
    g.newline();
    CHECK(g.cursor_row() == 4);  // stays at bottom margin
}

TEST_CASE("Grid — integration DECSTBM + newline via write_bytes", "[grid][scroll_region][integration]") {
    terminal_grid g(10, 8);  // extra rows to avoid auto-wrap issues
    write_bytes(g, "\x1B[1;1H"); write_bytes(g, "HEADER");
    // Set scroll region rows 2-5 (0-based: 1-4)
    write_bytes(g, "\x1B[2;5r");
    // Cursor should be at home (0,0). Move to row 2 and write.
    write_bytes(g, "\x1B[2;1H");
    // Fill the scroll region (rows 1-4) with 4 lines of data.
    write_bytes(g, "r1"); g.newline();
    write_bytes(g, "r2"); g.newline();
    write_bytes(g, "r3"); g.newline();
    write_bytes(g, "r4");  // at bottom margin, row 4
    g.newline();           // triggers scroll within region [1,4]

    // Header untouched.
    CHECK(g.cell(0, 0).codepoint == U'H');
    CHECK(g.cell(0, 1).codepoint == U'E');
    // Scroll region scrolled: r1 shifted up, row 1 now has r2.
    CHECK(g.cell(1, 0).codepoint == U'r');
    CHECK(g.cell(1, 1).codepoint == U'2');
}

// ===========================================================================
// Task 14 — ICH (Insert Characters)
// ===========================================================================

TEST_CASE("Grid — ICH inserts blanks and shifts cells right", "[grid][task14][ich]") {
    terminal_grid g(5, 3);
    // Fill row 0 with "ABCDE".
    write_bytes(g, "ABCDE");

    // Move cursor to col 1.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{0, 1};
    g.apply(mv);

    // Insert 2 blank cells.
    g.insert_chars(2);

    // Row should be: A . . B C  (D, E lost)
    CHECK(g.cell(0, 0).codepoint == U'A');
    CHECK(g.cell(0, 1).codepoint == U' ');
    CHECK(g.cell(0, 2).codepoint == U' ');
    CHECK(g.cell(0, 3).codepoint == U'B');
    CHECK(g.cell(0, 4).codepoint == U'C');
    // Cursor unchanged.
    CHECK(g.cursor_col() == 1);
    CHECK(g.cursor_row() == 0);
}

TEST_CASE("Grid — ICH count clamped to remaining columns", "[grid][task14][ich]") {
    terminal_grid g(5, 3);
    write_bytes(g, "ABCDE");

    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{0, 3};
    g.apply(mv);

    // Only 2 columns remain. Request 10.
    g.insert_chars(10);

    // Everything from col 3 onward should be blank.
    CHECK(g.cell(0, 0).codepoint == U'A');
    CHECK(g.cell(0, 1).codepoint == U'B');
    CHECK(g.cell(0, 2).codepoint == U'C');
    CHECK(g.cell(0, 3).codepoint == U' ');
    CHECK(g.cell(0, 4).codepoint == U' ');
    // Cursor unchanged.
    CHECK(g.cursor_col() == 3);
}

TEST_CASE("Grid — ICH at last column blanks that cell", "[grid][task14][ich]") {
    terminal_grid g(5, 3);
    write_bytes(g, "ABCDE");

    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{0, 4};
    g.apply(mv);

    g.insert_chars(1);

    // Only cell 4 is blanked (E shifted right off the edge).
    CHECK(g.cell(0, 0).codepoint == U'A');
    CHECK(g.cell(0, 1).codepoint == U'B');
    CHECK(g.cell(0, 2).codepoint == U'C');
    CHECK(g.cell(0, 3).codepoint == U'D');
    CHECK(g.cell(0, 4).codepoint == U' ');
    CHECK(g.cursor_col() == 4);
}

TEST_CASE("Grid — ICH on zero-size grid does nothing", "[grid][task14][ich]") {
    terminal_grid g(0, 0);
    // Should not crash.
    g.insert_chars(3);
    CHECK(g.cols() == 0);
    CHECK(g.rows() == 0);
}

TEST_CASE("Grid — ICH cursor unchanged", "[grid][task14][ich]") {
    terminal_grid g(10, 5);
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{2, 3};
    g.apply(mv);

    g.insert_chars(2);

    CHECK(g.cursor_col() == 3);
    CHECK(g.cursor_row() == 2);
}

// ===========================================================================
// Task 14 — DCH (Delete Characters)
// ===========================================================================

TEST_CASE("Grid — DCH deletes cells and shifts left", "[grid][task14][dch]") {
    terminal_grid g(5, 3);
    // Fill row 0 with "ABCDE".
    write_bytes(g, "ABCDE");

    // Move cursor to col 1.
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{0, 1};
    g.apply(mv);

    // Delete 2 cells.
    g.delete_chars(2);

    // Row should be: A D E . .
    CHECK(g.cell(0, 0).codepoint == U'A');
    CHECK(g.cell(0, 1).codepoint == U'D');
    CHECK(g.cell(0, 2).codepoint == U'E');
    CHECK(g.cell(0, 3).codepoint == U' ');
    CHECK(g.cell(0, 4).codepoint == U' ');
    // Cursor unchanged.
    CHECK(g.cursor_col() == 1);
    CHECK(g.cursor_row() == 0);
}

TEST_CASE("Grid — DCH count clamped to remaining columns", "[grid][task14][dch]") {
    terminal_grid g(5, 3);
    write_bytes(g, "ABCDE");

    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{0, 3};
    g.apply(mv);

    // Only 2 columns remain. Request 10.
    g.delete_chars(10);

    // Everything from col 3 onward should be blank.
    CHECK(g.cell(0, 0).codepoint == U'A');
    CHECK(g.cell(0, 1).codepoint == U'B');
    CHECK(g.cell(0, 2).codepoint == U'C');
    CHECK(g.cell(0, 3).codepoint == U' ');
    CHECK(g.cell(0, 4).codepoint == U' ');
    // Cursor unchanged.
    CHECK(g.cursor_col() == 3);
}

TEST_CASE("Grid — DCH at last column blanks that cell", "[grid][task14][dch]") {
    terminal_grid g(5, 3);
    write_bytes(g, "ABCDE");

    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{0, 4};
    g.apply(mv);

    g.delete_chars(1);

    // Only cell 4 is blanked (deleted, nothing to shift in).
    CHECK(g.cell(0, 0).codepoint == U'A');
    CHECK(g.cell(0, 1).codepoint == U'B');
    CHECK(g.cell(0, 2).codepoint == U'C');
    CHECK(g.cell(0, 3).codepoint == U'D');
    CHECK(g.cell(0, 4).codepoint == U' ');
    CHECK(g.cursor_col() == 4);
}

TEST_CASE("Grid — DCH cursor unchanged", "[grid][task14][dch]") {
    terminal_grid g(10, 5);
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{2, 3};
    g.apply(mv);

    g.delete_chars(2);

    CHECK(g.cursor_col() == 3);
    CHECK(g.cursor_row() == 2);
}

// ===========================================================================
// Task 14 — ECH (Erase Characters)
// ===========================================================================

TEST_CASE("Grid — ECH blanks cells in place", "[grid][task14][ech]") {
    terminal_grid g(5, 3);
    write_bytes(g, "ABCDE");

    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{0, 1};
    g.apply(mv);

    // Erase 3 cells.
    g.erase_chars(3);

    // Row should be: A . . . E
    CHECK(g.cell(0, 0).codepoint == U'A');
    CHECK(g.cell(0, 1).codepoint == U' ');
    CHECK(g.cell(0, 2).codepoint == U' ');
    CHECK(g.cell(0, 3).codepoint == U' ');
    CHECK(g.cell(0, 4).codepoint == U'E');
    // Cursor unchanged.
    CHECK(g.cursor_col() == 1);
    CHECK(g.cursor_row() == 0);
}

TEST_CASE("Grid — ECH count clamped to remaining columns", "[grid][task14][ech]") {
    terminal_grid g(5, 3);
    write_bytes(g, "ABCDE");

    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{0, 3};
    g.apply(mv);

    // Only 2 columns remain. Request 10.
    g.erase_chars(10);

    // Everything from col 3 onward should be blank.
    CHECK(g.cell(0, 0).codepoint == U'A');
    CHECK(g.cell(0, 1).codepoint == U'B');
    CHECK(g.cell(0, 2).codepoint == U'C');
    CHECK(g.cell(0, 3).codepoint == U' ');
    CHECK(g.cell(0, 4).codepoint == U' ');
    // Cursor unchanged.
    CHECK(g.cursor_col() == 3);
}

TEST_CASE("Grid — ECH cursor unchanged", "[grid][task14][ech]") {
    terminal_grid g(10, 5);
    action mv;
    mv.type = action_type::move_cursor;
    mv.payload = cursor_pos{2, 3};
    g.apply(mv);

    g.erase_chars(3);

    CHECK(g.cursor_col() == 3);
    CHECK(g.cursor_row() == 2);
}

// ===========================================================================
// Task 15 — Wide characters
// ===========================================================================

TEST_CASE("Grid — wide char occupies two cells", "[grid][task15][wide]") {
    terminal_grid g(10, 5);
    g.write_char(0x4E2D);  // 中 (CJK, width 2)
    // Cell (0,0) = 中 with wide_lead kind
    CHECK(g.cell(0, 0).codepoint == 0x4E2D);
    CHECK(g.cell(0, 0).kind == cell_kind::wide_lead);
    // Cell (0,1) = continuation
    CHECK(g.cell(0, 1).kind == cell_kind::wide_tail);
    // Cursor at column 2
    CHECK(g.cursor_col() == 2);
    CHECK(g.cursor_row() == 0);
}

TEST_CASE("Grid — wide char at last column wraps", "[grid][task15][wide]") {
    // 5-column grid, fill first 4 cols, then write wide char at col 4
    // (col 4 is last column) → should wrap to row 1, cols 0-1
    terminal_grid g(5, 5);
    g.write_char(U'A'); g.write_char(U'A'); g.write_char(U'A'); g.write_char(U'A');
    CHECK(g.cursor_col() == 4);
    CHECK(g.cursor_row() == 0);

    g.write_char(0x4E2D);  // wide char at last column → wrap
    CHECK(g.cursor_row() == 1);
    CHECK(g.cursor_col() == 2);  // wide char at cols 0-1, cursor now at 2
    CHECK(g.cell(1, 0).codepoint == 0x4E2D);
    CHECK(g.cell(1, 0).kind == cell_kind::wide_lead);
    CHECK(g.cell(1, 1).kind == cell_kind::wide_tail);
}

TEST_CASE("Grid — wide char at second-to-last column fits", "[grid][task15][wide]") {
    // 5-column grid, fill 3 cols, wide char at col 3 (second-to-last) → fits
    terminal_grid g(5, 5);
    g.write_char(U'A'); g.write_char(U'A'); g.write_char(U'A');  // cols 0-2
    CHECK(g.cursor_col() == 3);

    g.write_char(0x4E2D);  // wide char at col 3, fits at cols 3-4
    CHECK(g.cell(0, 3).codepoint == 0x4E2D);
    CHECK(g.cell(0, 3).kind == cell_kind::wide_lead);
    CHECK(g.cell(0, 4).kind == cell_kind::wide_tail);
    // After writing 2 cells, cursor goes to col 5 which auto-wraps to row 1.
    CHECK(g.cursor_row() == 1);
    CHECK(g.cursor_col() == 0);
}

TEST_CASE("Grid — normal char after wide char placed correctly", "[grid][task15][wide]") {
    terminal_grid g(10, 5);
    g.write_char(0x4E2D);  // wide, cols 0-1
    g.write_char(U'A');    // col 2
    CHECK(g.cell(0, 2).codepoint == U'A');
    CHECK(g.cursor_col() == 3);
}

TEST_CASE("Grid — render_cells marks wide and continuation", "[grid][task15][wide]") {
    terminal_grid g(10, 5);
    g.write_char(0x4E2D);
    auto cells = g.render_cells();
    // Cell 0: codepoint = 0x4E2D, kind is wide_lead
    CHECK(cells[0].codepoint == 0x4E2D);
    CHECK(cells[0].kind == static_cast<uint8_t>(cell_kind::wide_lead));
    // Cell 1: continuation, kind is wide_tail
    CHECK(cells[1].kind == static_cast<uint8_t>(cell_kind::wide_tail));
}

TEST_CASE("Grid — wide char fg/bg colours propagated to both cells", "[grid][task15][wide]") {
    terminal_grid g(10, 5);
    // Set red fg via SGR then write a wide char.
    write_bytes(g, "\x1B[31m");  // red fg
    g.write_char(0x4E2D);
    // Both cells should have the same fg.
    CHECK(g.cell(0, 0).fg.r == g.cell(0, 1).fg.r);
    CHECK(g.cell(0, 0).fg.g == g.cell(0, 1).fg.g);
    CHECK(g.cell(0, 0).fg.b == g.cell(0, 1).fg.b);
}

// ===========================================================================
// Task 15 — Combining characters (NFC pre-composition)
// ===========================================================================

TEST_CASE("Grid — combining acute after 'e' composes to é", "[grid][task15][combining]") {
    terminal_grid g(10, 5);
    g.write_char(U'e');
    g.write_char(0x0301);  // combining acute accent
    // Cursor stays at col 1 (combining char is zero-width)
    CHECK(g.cursor_col() == 1);
    // Cell (0,0) should now be é (U+00E9)
    CHECK(g.cell(0, 0).codepoint == 0x00E9);
}

TEST_CASE("Grid — combining char at column 0 is width 1", "[grid][task15][combining]") {
    terminal_grid g(10, 5);
    g.write_char(0x0301);  // combining acute at col 0
    CHECK(g.cursor_col() == 1);  // treated as width 1
    CHECK(g.cell(0, 0).codepoint == 0x0301);
}

TEST_CASE("Grid — combining char after space is width 1", "[grid][task15][combining]") {
    terminal_grid g(10, 5);
    g.write_char(U' ');  // space
    g.write_char(0x0301);  // combining acute
    CHECK(g.cursor_col() == 2);  // treated as width 1
    CHECK(g.cell(0, 1).codepoint == 0x0301);
}

TEST_CASE("Grid — combining char after wide_tail is width 1", "[grid][task15][combining]") {
    terminal_grid g(10, 5);
    g.write_char(0x4E2D);  // wide, cols 0-1; col 1 is wide_tail
    // Write combining at col 2 (advances from col 2 normally).
    g.write_char(0x0301);
    // Should be width 1 at col 2 (not composed with wide_tail at col 1).
    CHECK(g.cursor_col() == 3);
    CHECK(g.cell(0, 2).codepoint == 0x0301);
}

// ===========================================================================
// Task 15 — wcwidth unit tests
// ===========================================================================

TEST_CASE("wcwidth — ASCII returns 1", "[task15][wcwidth]") {
    CHECK(wcwidth(U'A') == 1);
    CHECK(wcwidth(U' ') == 1);
    CHECK(wcwidth(U'0') == 1);
}

TEST_CASE("wcwidth — CJK returns 2", "[task15][wcwidth]") {
    CHECK(wcwidth(0x4E2D) == 2);  // 中
    CHECK(wcwidth(0x6587) == 2);  // 文
    CHECK(wcwidth(0x3042) == 2);  // あ (Hiragana)
    CHECK(wcwidth(0xAC00) == 2);  // 가 (Hangul)
}

TEST_CASE("wcwidth — fullwidth returns 2", "[task15][wcwidth]") {
    CHECK(wcwidth(0xFF01) == 2);  // ！ fullwidth exclamation
    CHECK(wcwidth(0xFF21) == 2);  // Ａ fullwidth A
}

TEST_CASE("wcwidth — combining returns 0", "[task15][wcwidth]") {
    CHECK(wcwidth(0x0301) == 0);  // combining acute
    CHECK(wcwidth(0x0300) == 0);  // combining grave
}

TEST_CASE("wcwidth — control returns -1", "[task15][wcwidth]") {
    CHECK(wcwidth(0x0000) == -1);
    CHECK(wcwidth(0x001F) == -1);
    CHECK(wcwidth(0x007F) == -1);
}

TEST_CASE("wcwidth — emoji returns 2", "[task15][wcwidth]") {
    CHECK(wcwidth(0x1F600) == 2);  // 😀
}
