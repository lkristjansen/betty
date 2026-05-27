#include <catch2/catch_test_macros.hpp>
#include "platform/box_drawing.hpp"

using betty::platform::cell_rect;
using betty::platform::is_box_drawing_or_block;
using betty::platform::get_box_drawing_rects;

// ===========================================================================
// 5.1  is_box_drawing_or_block()
// ===========================================================================

TEST_CASE("is_box_drawing_or_block returns true for box-drawing characters", "[box_drawing]") {
  CHECK(is_box_drawing_or_block(U'\u2500'));  // light horizontal
  CHECK(is_box_drawing_or_block(U'\u253C'));  // light cross
  CHECK(is_box_drawing_or_block(U'\u2550'));  // double horizontal
  CHECK(is_box_drawing_or_block(U'\u256C'));  // double cross
  CHECK(is_box_drawing_or_block(U'\u2574'));  // left half line
  CHECK(is_box_drawing_or_block(U'\u257F'));  // mixed half line
}

TEST_CASE("is_box_drawing_or_block returns true for block elements", "[box_drawing]") {
  CHECK(is_box_drawing_or_block(U'\u2580'));  // upper half
  CHECK(is_box_drawing_or_block(U'\u2588'));  // full block
  CHECK(is_box_drawing_or_block(U'\u2590'));  // right half
  CHECK(is_box_drawing_or_block(U'\u2595'));  // right 1/8
  CHECK(is_box_drawing_or_block(U'\u2596'));  // quadrant
  CHECK(is_box_drawing_or_block(U'\u259F'));  // quadrant combination
}

TEST_CASE("is_box_drawing_or_block returns false for excluded characters", "[box_drawing]") {
  CHECK_FALSE(is_box_drawing_or_block(U'\u2571'));  // diagonal
  CHECK_FALSE(is_box_drawing_or_block(U'\u2572'));  // diagonal
  CHECK_FALSE(is_box_drawing_or_block(U'\u2573'));  // diagonal cross
  CHECK_FALSE(is_box_drawing_or_block(U'\u2504'));  // dashed
  CHECK_FALSE(is_box_drawing_or_block(U'\u2508'));  // dashed
  CHECK_FALSE(is_box_drawing_or_block(U'\u256D'));  // arc
  CHECK_FALSE(is_box_drawing_or_block(U'\u2591'));  // light shade
  CHECK_FALSE(is_box_drawing_or_block(U'\u2592'));  // medium shade
  CHECK_FALSE(is_box_drawing_or_block(U'\u2593'));  // dark shade
}

TEST_CASE("is_box_drawing_or_block returns false for non-box characters", "[box_drawing]") {
  CHECK_FALSE(is_box_drawing_or_block(U'-'));
  CHECK_FALSE(is_box_drawing_or_block(U'A'));
  CHECK_FALSE(is_box_drawing_or_block(U' '));
  CHECK_FALSE(is_box_drawing_or_block(0));
  CHECK_FALSE(is_box_drawing_or_block(0x1F600));  // emoji
}

// ===========================================================================
// 5.2  get_box_drawing_rects() — Box Drawing
// ===========================================================================

TEST_CASE("U+2500 light horizontal returns 3 rects", "[box_drawing]") {
  cell_rect rects[8];
  uint32_t const count = get_box_drawing_rects(U'\u2500', rects, 8);
  REQUIRE(count == 3);
  // Left arm: extends from left edge to centre
  // Right arm: extends from centre to right edge
  // Centre: small square at centre
}

TEST_CASE("U+2502 light vertical returns 3 rects", "[box_drawing]") {
  cell_rect rects[8];
  uint32_t const count = get_box_drawing_rects(U'\u2502', rects, 8);
  REQUIRE(count == 3);
}

TEST_CASE("U+250C light corner returns 3 rects", "[box_drawing]") {
  cell_rect rects[8];
  uint32_t const count = get_box_drawing_rects(U'\u250C', rects, 8);
  REQUIRE(count == 3);
  // Down arm + right arm + centre
}

TEST_CASE("U+253C light cross returns 5 rects", "[box_drawing]") {
  cell_rect rects[8];
  uint32_t const count = get_box_drawing_rects(U'\u253C', rects, 8);
  REQUIRE(count == 5);
  // 4 arms + centre
}

TEST_CASE("U+2550 double horizontal returns 4 rects", "[box_drawing]") {
  cell_rect rects[8];
  uint32_t const count = get_box_drawing_rects(U'\u2550', rects, 8);
  REQUIRE(count == 4);
  // Two left-arm strokes + two right-arm strokes (no centre for double)
}

TEST_CASE("U+256C double cross returns 8 rects", "[box_drawing]") {
  cell_rect rects[8];
  uint32_t const count = get_box_drawing_rects(U'\u256C', rects, 8);
  REQUIRE(count == 8);
  // 4 arms × 2 double strokes
}

TEST_CASE("U+2574 half line returns 1 rect", "[box_drawing]") {
  cell_rect rects[8];
  uint32_t const count = get_box_drawing_rects(U'\u2574', rects, 8);
  REQUIRE(count == 1);
  // Single left arm
}

TEST_CASE("Excluded characters return 0 rects", "[box_drawing]") {
  cell_rect rects[8];
  CHECK(get_box_drawing_rects(U'\u2571', rects, 8) == 0);  // diagonal
  CHECK(get_box_drawing_rects(U'\u2504', rects, 8) == 0);  // dashed
  CHECK(get_box_drawing_rects(U'\u256D', rects, 8) == 0);  // arc
}

// ===========================================================================
// 5.3  get_box_drawing_rects() — Block Elements
// ===========================================================================

TEST_CASE("U+2588 full block returns {0,0,1,1}", "[box_drawing]") {
  cell_rect rects[8];
  uint32_t const count = get_box_drawing_rects(U'\u2588', rects, 8);
  REQUIRE(count == 1);
  CHECK(rects[0].left   == 0.0f);
  CHECK(rects[0].top    == 0.0f);
  CHECK(rects[0].right  == 1.0f);
  CHECK(rects[0].bottom == 1.0f);
}

TEST_CASE("U+2580 upper half returns {0,0,1,0.5}", "[box_drawing]") {
  cell_rect rects[8];
  uint32_t const count = get_box_drawing_rects(U'\u2580', rects, 8);
  REQUIRE(count == 1);
  CHECK(rects[0].left   == 0.0f);
  CHECK(rects[0].top    == 0.0f);
  CHECK(rects[0].right  == 1.0f);
  CHECK(rects[0].bottom == 0.5f);
}

TEST_CASE("U+2584 lower half returns {0,0.5,1,1}", "[box_drawing]") {
  cell_rect rects[8];
  uint32_t const count = get_box_drawing_rects(U'\u2584', rects, 8);
  REQUIRE(count == 1);
  CHECK(rects[0].left   == 0.0f);
  CHECK(rects[0].top    == 0.5f);
  CHECK(rects[0].right  == 1.0f);
  CHECK(rects[0].bottom == 1.0f);
}

TEST_CASE("U+258C left half returns {0,0,0.5,1}", "[box_drawing]") {
  cell_rect rects[8];
  uint32_t const count = get_box_drawing_rects(U'\u258C', rects, 8);
  REQUIRE(count == 1);
  CHECK(rects[0].left   == 0.0f);
  CHECK(rects[0].top    == 0.0f);
  CHECK(rects[0].right  == 0.5f);
  CHECK(rects[0].bottom == 1.0f);
}

TEST_CASE("U+2598 upper-left quadrant", "[box_drawing]") {
  cell_rect rects[8];
  uint32_t const count = get_box_drawing_rects(U'\u2598', rects, 8);
  REQUIRE(count == 1);
  CHECK(rects[0].left   == 0.0f);
  CHECK(rects[0].top    == 0.0f);
  CHECK(rects[0].right  == 0.5f);
  CHECK(rects[0].bottom == 0.5f);
}

TEST_CASE("U+259A returns two rects (UL + LR)", "[box_drawing]") {
  cell_rect rects[8];
  uint32_t const count = get_box_drawing_rects(U'\u259A', rects, 8);
  REQUIRE(count == 2);
}

// ===========================================================================
// 5.4  Rectangle validity
// ===========================================================================

static void check_rect_valid(cell_rect const& r) {
  CHECK(r.left < r.right);
  CHECK(r.top < r.bottom);
  CHECK(r.left >= 0.0f);
  CHECK(r.left <= 1.0f);
  CHECK(r.right >= 0.0f);
  CHECK(r.right <= 1.0f);
  CHECK(r.top >= 0.0f);
  CHECK(r.top <= 1.0f);
  CHECK(r.bottom >= 0.0f);
  CHECK(r.bottom <= 1.0f);
}

TEST_CASE("All supported box-drawing characters produce valid rects", "[box_drawing]") {
  cell_rect rects[8];
  for (char32_t cp = 0x2500; cp <= 0x257F; ++cp) {
    if (!is_box_drawing_or_block(cp)) continue;
    uint32_t const count = get_box_drawing_rects(cp, rects, 8);
    REQUIRE(count > 0);
    for (uint32_t i = 0; i < count; ++i) {
      INFO("cp = U+" << std::hex << static_cast<uint32_t>(cp) << ", rect " << std::dec << i);
      check_rect_valid(rects[i]);
    }
  }
}

TEST_CASE("All supported block-element characters produce valid rects", "[box_drawing]") {
  cell_rect rects[8];
  for (char32_t cp = 0x2580; cp <= 0x259F; ++cp) {
    if (!is_box_drawing_or_block(cp)) continue;
    uint32_t const count = get_box_drawing_rects(cp, rects, 8);
    REQUIRE(count > 0);
    for (uint32_t i = 0; i < count; ++i) {
      INFO("cp = U+" << std::hex << static_cast<uint32_t>(cp) << ", rect " << std::dec << i);
      check_rect_valid(rects[i]);
    }
  }
}

TEST_CASE("get_box_drawing_rects handles small max_rects gracefully", "[box_drawing]") {
  cell_rect rects[1];
  // Should not crash; returns the number of rects that *would* be emitted.
  uint32_t const count = get_box_drawing_rects(U'\u253C', rects, 1);
  CHECK(count == 5);  // reports full count, only writes first rect
}
