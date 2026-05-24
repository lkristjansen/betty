#include <catch2/catch_test_macros.hpp>
#include "terminal/scrollback_buffer.hpp"

using namespace betty::terminal;

// ===========================================================================
// Construction
// ===========================================================================

TEST_CASE("scrollback_buffer — construction sets dimensions", "[scrollback_buffer]") {
    scrollback_buffer buf(80, 24, 10000);
    CHECK(buf.cols() == 80);
    CHECK(buf.rows() == 24);
    CHECK(buf.scrollback_count() == 0);
    CHECK(buf.is_following_output());
}

TEST_CASE("scrollback_buffer — all cells initialised to space", "[scrollback_buffer]") {
    scrollback_buffer buf(5, 3, 10);
    for (uint32_t r = 0; r < 3; ++r) {
        auto row = buf.active_row(r);
        for (uint32_t c = 0; c < 5; ++c) {
            CHECK(row[c].codepoint == U' ');
        }
    }
}

// ===========================================================================
// active_row / rendered_row
// ===========================================================================

TEST_CASE("scrollback_buffer — active_row returns mutable span", "[scrollback_buffer]") {
    scrollback_buffer buf(5, 3, 10);
    auto row = buf.active_row(1);
    row[2] = grid_cell{U'X', {}, {}, cell_attr::none};
    CHECK(buf.active_row(1)[2].codepoint == U'X');
}

TEST_CASE("scrollback_buffer — rendered_row matches active_row when not scrolled", "[scrollback_buffer]") {
    scrollback_buffer buf(5, 3, 10);
    auto active = buf.active_row(0);
    active[0] = grid_cell{U'A', {}, {}, cell_attr::none};

    auto rendered = buf.rendered_row(0);
    CHECK(rendered[0].codepoint == U'A');
}

// ===========================================================================
// push_scrollback
// ===========================================================================

TEST_CASE("scrollback_buffer — push_scrollback shifts content", "[scrollback_buffer]") {
    scrollback_buffer buf(3, 2, 10);
    buf.active_row(0)[0] = grid_cell{U'A', {}, {}, cell_attr::none};
    buf.active_row(1)[0] = grid_cell{U'B', {}, {}, cell_attr::none};

    buf.push_scrollback();

    CHECK(buf.scrollback_count() == 1);
    // Old row 0 is now scrollback. Visible row 0 is old row 1.
    CHECK(buf.active_row(0)[0].codepoint == U'B');
    // New bottom row is blank.
    CHECK(buf.active_row(1)[0].codepoint == U' ');
}

TEST_CASE("scrollback_buffer — push_scrollback wraps at max_scrollback", "[scrollback_buffer]") {
    scrollback_buffer buf(3, 2, 2);
    buf.active_row(0)[0] = grid_cell{U'A', {}, {}, cell_attr::none};
    buf.active_row(1)[0] = grid_cell{U'B', {}, {}, cell_attr::none};

    buf.push_scrollback(); // count = 1
    buf.push_scrollback(); // count = 2
    buf.push_scrollback(); // count stays 2, head rotates

    CHECK(buf.scrollback_count() == 2);
    // After 3 pushes on a 2-row visible grid: oldest visible row went into
    // scrollback, then got dropped when max reached.
}

// ===========================================================================
// scroll_viewport
// ===========================================================================

TEST_CASE("scrollback_buffer — scroll_viewport moves visible window", "[scrollback_buffer]") {
    scrollback_buffer buf(3, 2, 10);
    buf.active_row(0)[0] = grid_cell{U'A', {}, {}, cell_attr::none};
    buf.active_row(1)[0] = grid_cell{U'B', {}, {}, cell_attr::none};
    buf.push_scrollback(); // A is now in scrollback

    CHECK(buf.is_following_output());
    auto offset = buf.scroll_viewport(1);
    CHECK(offset == 1);
    CHECK(!buf.is_following_output());

    // rendered_row(0) should now show the scrollback row (A).
    auto row = buf.rendered_row(0);
    CHECK(row[0].codepoint == U'A');
}

TEST_CASE("scrollback_buffer — scroll_viewport clamps to scrollback_count", "[scrollback_buffer]") {
    scrollback_buffer buf(3, 2, 10);
    buf.push_scrollback();
    auto offset = buf.scroll_viewport(5);
    CHECK(offset == 1);
}

TEST_CASE("scrollback_buffer — scroll_viewport forward clamps to 0", "[scrollback_buffer]") {
    scrollback_buffer buf(3, 2, 10);
    buf.push_scrollback();
    (void)buf.scroll_viewport(1);
    auto offset = buf.scroll_viewport(-5);
    CHECK(offset == 0);
    CHECK(buf.is_following_output());
}

// ===========================================================================
// clear_all
// ===========================================================================

TEST_CASE("scrollback_buffer — clear_all resets everything", "[scrollback_buffer]") {
    scrollback_buffer buf(3, 2, 10);
    buf.active_row(0)[0] = grid_cell{U'X', {}, {}, cell_attr::none};
    buf.push_scrollback();
    (void)buf.scroll_viewport(1);

    buf.clear_all();
    CHECK(buf.scrollback_count() == 0);
    CHECK(buf.viewport_scroll() == 0);
    CHECK(buf.is_following_output());
    CHECK(buf.active_row(0)[0].codepoint == U' ');
}

// ===========================================================================
// resize
// ===========================================================================

TEST_CASE("scrollback_buffer — resize same dims is no-op", "[scrollback_buffer]") {
    scrollback_buffer buf(5, 3, 10);
    buf.active_row(0)[0] = grid_cell{U'X', {}, {}, cell_attr::none};
    buf.resize(5, 3, 10);
    CHECK(buf.active_row(0)[0].codepoint == U'X');
}

TEST_CASE("scrollback_buffer — resize larger preserves content", "[scrollback_buffer]") {
    scrollback_buffer buf(3, 2, 10);
    buf.active_row(0)[0] = grid_cell{U'a', {}, {}, cell_attr::none};
    buf.active_row(0)[1] = grid_cell{U'b', {}, {}, cell_attr::none};
    buf.active_row(1)[0] = grid_cell{U'c', {}, {}, cell_attr::none};

    buf.resize(5, 4, 10);
    CHECK(buf.cols() == 5);
    CHECK(buf.rows() == 4);
    CHECK(buf.active_row(0)[0].codepoint == U'a');
    CHECK(buf.active_row(0)[1].codepoint == U'b');
    CHECK(buf.active_row(1)[0].codepoint == U'c');
    CHECK(buf.active_row(0)[3].codepoint == U' ');
    CHECK(buf.active_row(3)[0].codepoint == U' ');
}

TEST_CASE("scrollback_buffer — resize smaller reflows rows", "[scrollback_buffer]") {
    scrollback_buffer buf(5, 2, 10);
    // Row 0: a b c d e
    for (uint32_t c = 0; c < 5; ++c) {
        buf.active_row(0)[c] = grid_cell{static_cast<char32_t>(U'a' + c), {}, {}, cell_attr::none};
    }
    // Row 1: f g h i j
    for (uint32_t c = 0; c < 5; ++c) {
        buf.active_row(1)[c] = grid_cell{static_cast<char32_t>(U'f' + c), {}, {}, cell_attr::none};
    }

    buf.resize(3, 2, 10);
    // Row 0 should now be a b c (first 3 chars of old row 0)
    // Row 1 should be d e (next 2 chars)
    // Wait, that's only 2 visible rows and no scrollback. Let's trace:
    // old total logical = 2. After resize, new_logical_idx:
    // log_old=0: copy 3 cells (a,b,c) → new_phys=0, offset=0. new_logical_idx=1
    //            copy 2 cells (d,e) → new_phys=1, offset=3. new_logical_idx=2
    // log_old=1: copy 3 cells (f,g,h) → new_phys=0? No, new_logical_idx % new_capacity
    //            new_capacity = 2+10=12. new_logical_idx=2, new_phys=2, offset=6
    //            copy 2 cells (i,j) → new_logical_idx=3, new_phys=3, offset=9
    // Total new_logical_idx = 4. new_rows = 2. excess = 2.
    // scrollback_count = min(2, 10) = 2.
    // visible_base_logical = 2. rendered_row(0) = logical 2 = new_phys 2 = "fgh"
    // rendered_row(1) = logical 3 = new_phys 3 = "ij "
    // scrollback row 0 = logical 0 = "abc"
    // scrollback row 1 = logical 1 = "de "

    CHECK(buf.cols() == 3);
    CHECK(buf.rows() == 2);
    CHECK(buf.scrollback_count() == 2);

    // Check visible content
    CHECK(buf.active_row(0)[0].codepoint == U'f');
    CHECK(buf.active_row(1)[0].codepoint == U'i');

    // Check scrollback
    auto scrolled = buf.scroll_viewport(2);
    CHECK(scrolled == 2);
    CHECK(buf.rendered_row(0)[0].codepoint == U'a');
    CHECK(buf.rendered_row(1)[0].codepoint == U'd');
}

TEST_CASE("scrollback_buffer — resize to zero resets", "[scrollback_buffer]") {
    scrollback_buffer buf(10, 10, 10);
    buf.active_row(0)[0] = grid_cell{U'X', {}, {}, cell_attr::none};
    buf.resize(0, 0, 10);
    CHECK(buf.cols() == 0);
    CHECK(buf.rows() == 0);
    CHECK(buf.scrollback_count() == 0);
    CHECK(buf.viewport_scroll() == 0);
}

TEST_CASE("scrollback_buffer — resize clamps viewport_scroll", "[scrollback_buffer]") {
    scrollback_buffer buf(3, 5, 10);
    buf.push_scrollback(); // count = 1
    buf.push_scrollback(); // count = 2
    (void)buf.scroll_viewport(2);
    buf.resize(3, 2, 10);
    CHECK(buf.viewport_scroll() <= buf.scrollback_count());
}

// ===========================================================================
// visible_base_logical
// ===========================================================================

TEST_CASE("scrollback_buffer — visible_base_logical", "[scrollback_buffer]") {
    scrollback_buffer buf(3, 2, 10);
    buf.push_scrollback(); // count = 1
    buf.push_scrollback(); // count = 2
    CHECK(buf.visible_base_logical() == 2); // not scrolled
    (void)buf.scroll_viewport(1);
    CHECK(buf.visible_base_logical() == 1);
    (void)buf.scroll_viewport(1);
    CHECK(buf.visible_base_logical() == 0);
}
