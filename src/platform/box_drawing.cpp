#include "box_drawing.hpp"
#include <array>
#include <cassert>

namespace betty::platform {

// ===========================================================================
// Arm-weight encoding for box-drawing characters (U+2500–U+257F)
// ===========================================================================

// Each codepoint in the box-drawing range maps to a single byte encoding
// the weight of its four directional arms:
//
//   Bits [1:0] — up weight
//   Bits [3:2] — down weight
//   Bits [5:4] — left weight
//   Bits [7:6] — right weight
//
// Weight values:
//   0 — none (arm absent)
//   1 — light  (stroke ≈ 1/8 of cell)
//   2 — heavy  (stroke ≈ 3/8 of cell)
//   3 — double (two parallel light strokes)
//
// A value of 0x00 means "not supported — fall through to font rendering".

namespace {
constexpr uint8_t k_wt_none   = 0;
constexpr uint8_t k_wt_light  = 1;
constexpr uint8_t k_wt_heavy  = 2;
constexpr uint8_t k_wt_double = 3;

// Pack four 2-bit arm weights into a single byte.
constexpr auto pack(uint8_t up, uint8_t down, uint8_t left, uint8_t right) -> uint8_t {
  return static_cast<uint8_t>(
      ((up)    & 0x03)       |
      (((down)  & 0x03) << 2) |
      (((left)  & 0x03) << 4) |
      (((right) & 0x03) << 6));
}

// Unpack helpers.
constexpr auto up_wt(uint8_t v)    -> uint8_t { return (v >> 0) & 0x03; }
constexpr auto down_wt(uint8_t v)  -> uint8_t { return (v >> 2) & 0x03; }
constexpr auto left_wt(uint8_t v)  -> uint8_t { return (v >> 4) & 0x03; }
constexpr auto right_wt(uint8_t v) -> uint8_t { return (v >> 6) & 0x03; }

// ── Box-Drawing lookup table (U+2500–U+257F) ──────────────────────────────
//
// Index = codepoint - 0x2500.  Each entry packs four arm weights.
// Entries set to 0x00 are excluded (fall through to font rendering):
//   · Dashed / dotted lines (U+2504–U+250B)
//   · Arcs / curved corners (U+256D–U+2570)
//   · Diagonals including cross (U+2571–U+2573)
//
// Notation below uses L/H/D for light/heavy/double and U/D/L/R for
// up/down/left/right.  E.g. "DL HR" = down light, right heavy.

constexpr std::array<uint8_t, 128> k_box_table = {
  // ── 0x00–0x03  Simple lines ─────────────────────────────────────────────
  // U+2500  ─   light horizontal          → LR + LL
  pack(0, 0, k_wt_light, k_wt_light),
  // U+2501  ━   heavy horizontal          → LR + LL
  pack(0, 0, k_wt_heavy, k_wt_heavy),
  // U+2502  │   light vertical            → UD + UL
  pack(k_wt_light, k_wt_light, 0, 0),
  // U+2503  ┃   heavy vertical            → UD + UL
  pack(k_wt_heavy, k_wt_heavy, 0, 0),

  // ── 0x04–0x0B  Dashed / dotted lines (skip) ────────────────────────────
  0, 0, 0, 0, 0, 0, 0, 0,

  // ── 0x0C–0x0F  Down-right corners ──────────────────────────────────────
  // U+250C  ┌   DL DR
  pack(0, k_wt_light, 0, k_wt_light),
  // U+250D  ┍   DL HR
  pack(0, k_wt_light, 0, k_wt_heavy),
  // U+250E  ┎   DH LR
  pack(0, k_wt_heavy, 0, k_wt_light),
  // U+250F  ┏   DH HR
  pack(0, k_wt_heavy, 0, k_wt_heavy),

  // ── 0x10–0x13  Down-left corners ───────────────────────────────────────
  // U+2510  ┐   DL LL
  pack(0, k_wt_light, k_wt_light, 0),
  // U+2511  ┑   DL HL
  pack(0, k_wt_light, k_wt_heavy, 0),
  // U+2512  ┒   DH LL
  pack(0, k_wt_heavy, k_wt_light, 0),
  // U+2513  ┓   DH HL
  pack(0, k_wt_heavy, k_wt_heavy, 0),

  // ── 0x14–0x17  Up-right corners ─────────────────────────────────────────
  // U+2514  └   UL UR
  pack(k_wt_light, 0, 0, k_wt_light),
  // U+2515  ┕   UL HR
  pack(k_wt_light, 0, 0, k_wt_heavy),
  // U+2516  ┖   UH LR
  pack(k_wt_heavy, 0, 0, k_wt_light),
  // U+2517  ┗   UH HR
  pack(k_wt_heavy, 0, 0, k_wt_heavy),

  // ── 0x18–0x1B  Up-left corners ──────────────────────────────────────────
  // U+2518  ┘   UL LL
  pack(k_wt_light, 0, k_wt_light, 0),
  // U+2519  ┙   UL HL
  pack(k_wt_light, 0, k_wt_heavy, 0),
  // U+251A  ┚   UH LL
  pack(k_wt_heavy, 0, k_wt_light, 0),
  // U+251B  ┛   UH HL
  pack(k_wt_heavy, 0, k_wt_heavy, 0),

  // ── 0x1C–0x23  Vertical-right tees ─────────────────────────────────────
  // U+251C  ├   UL DL LR
  pack(k_wt_light, k_wt_light, 0, k_wt_light),
  // U+251D  ┝   UL DL HR
  pack(k_wt_light, k_wt_light, 0, k_wt_heavy),
  // U+251E  ┞   UH DL LR
  pack(k_wt_heavy, k_wt_light, 0, k_wt_light),
  // U+251F  ┟   UL DH LR
  pack(k_wt_light, k_wt_heavy, 0, k_wt_light),
  // U+2520  ┠   UH DH LR
  pack(k_wt_heavy, k_wt_heavy, 0, k_wt_light),
  // U+2521  ┡   UL DH HR
  pack(k_wt_light, k_wt_heavy, 0, k_wt_heavy),
  // U+2522  ┢   UH DL HR
  pack(k_wt_heavy, k_wt_light, 0, k_wt_heavy),
  // U+2523  ┣   UH DH HR
  pack(k_wt_heavy, k_wt_heavy, 0, k_wt_heavy),

  // ── 0x24–0x2B  Vertical-left tees ──────────────────────────────────────
  // U+2524  ┤   UL DL LL
  pack(k_wt_light, k_wt_light, k_wt_light, 0),
  // U+2525  ┥   UL DL HL
  pack(k_wt_light, k_wt_light, k_wt_heavy, 0),
  // U+2526  ┦   UH DL LL
  pack(k_wt_heavy, k_wt_light, k_wt_light, 0),
  // U+2527  ┧   UL DH LL
  pack(k_wt_light, k_wt_heavy, k_wt_light, 0),
  // U+2528  ┨   UH DH LL
  pack(k_wt_heavy, k_wt_heavy, k_wt_light, 0),
  // U+2529  ┩   UL DH HL
  pack(k_wt_light, k_wt_heavy, k_wt_heavy, 0),
  // U+252A  ┪   UH DL HL
  pack(k_wt_heavy, k_wt_light, k_wt_heavy, 0),
  // U+252B  ┫   UH DH HL
  pack(k_wt_heavy, k_wt_heavy, k_wt_heavy, 0),

  // ── 0x2C–0x33  Horizontal-down tees ────────────────────────────────────
  // U+252C  ┬   DL LL LR
  pack(0, k_wt_light, k_wt_light, k_wt_light),
  // U+252D  ┭   DL LL HR
  pack(0, k_wt_light, k_wt_light, k_wt_heavy),
  // U+252E  ┮   DH LL LR
  pack(0, k_wt_heavy, k_wt_light, k_wt_light),
  // U+252F  ┯   DL HL LR  (left heavy, down+right light)
  pack(0, k_wt_light, k_wt_heavy, k_wt_light),
  // U+2530  ┰   DH HL LR
  pack(0, k_wt_heavy, k_wt_heavy, k_wt_light),
  // U+2531  ┱   DL HL HR
  pack(0, k_wt_light, k_wt_heavy, k_wt_heavy),
  // U+2532  ┲   DH LL HR
  pack(0, k_wt_heavy, k_wt_light, k_wt_heavy),
  // U+2533  ┳   DH HL HR
  pack(0, k_wt_heavy, k_wt_heavy, k_wt_heavy),

  // ── 0x34–0x3B  Horizontal-up tees ──────────────────────────────────────
  // U+2534  ┴   UL LL LR
  pack(k_wt_light, 0, k_wt_light, k_wt_light),
  // U+2535  ┵   UL LL HR
  pack(k_wt_light, 0, k_wt_light, k_wt_heavy),
  // U+2536  ┶   UH LL LR
  pack(k_wt_heavy, 0, k_wt_light, k_wt_light),
  // U+2537  ┷   UL HL LR
  pack(k_wt_light, 0, k_wt_heavy, k_wt_light),
  // U+2538  ┸   UH HL LR
  pack(k_wt_heavy, 0, k_wt_heavy, k_wt_light),
  // U+2539  ┹   UL HL HR
  pack(k_wt_light, 0, k_wt_heavy, k_wt_heavy),
  // U+253A  ┺   UH LL HR
  pack(k_wt_heavy, 0, k_wt_light, k_wt_heavy),
  // U+253B  ┻   UH HL HR
  pack(k_wt_heavy, 0, k_wt_heavy, k_wt_heavy),

  // ── 0x3C–0x4B  Crosses ─────────────────────────────────────────────────
  // U+253C  ┼   UL DL LL LR
  pack(k_wt_light, k_wt_light, k_wt_light, k_wt_light),
  // U+253D  ┽   UL DL LL HR
  pack(k_wt_light, k_wt_light, k_wt_light, k_wt_heavy),
  // U+253E  ┾   UH DL LL LR  (up heavy, rest light)
  pack(k_wt_heavy, k_wt_light, k_wt_light, k_wt_light),
  // U+253F  ┿   UL DH LL LR  (down heavy, rest light)
  pack(k_wt_light, k_wt_heavy, k_wt_light, k_wt_light),
  // U+2540  ╀   UL DL HL LR  (left heavy, rest light)
  pack(k_wt_light, k_wt_light, k_wt_heavy, k_wt_light),
  // U+2541  ╁   UL DL LL HR  (right heavy, rest light)
  pack(k_wt_light, k_wt_light, k_wt_light, k_wt_heavy),
  // U+2542  ╂   UH DH LL LR
  pack(k_wt_heavy, k_wt_heavy, k_wt_light, k_wt_light),
  // U+2543  ╃   UH DL HL LR  (up heavy, left heavy, rest light)
  pack(k_wt_heavy, k_wt_light, k_wt_heavy, k_wt_light),
  // U+2544  ╄   UH DL LL HR
  pack(k_wt_heavy, k_wt_light, k_wt_light, k_wt_heavy),
  // U+2545  ╅   UL DH HL LR
  pack(k_wt_light, k_wt_heavy, k_wt_heavy, k_wt_light),
  // U+2546  ╆   UL DH LL HR
  pack(k_wt_light, k_wt_heavy, k_wt_light, k_wt_heavy),
  // U+2547  ╇   UL DL HL HR
  pack(k_wt_light, k_wt_light, k_wt_heavy, k_wt_heavy),
  // U+2548  ╈   UH DH HL LR
  pack(k_wt_heavy, k_wt_heavy, k_wt_heavy, k_wt_light),
  // U+2549  ╉   UH DH LL HR
  pack(k_wt_heavy, k_wt_heavy, k_wt_light, k_wt_heavy),
  // U+254A  ╊   UH DL HL HR
  pack(k_wt_heavy, k_wt_light, k_wt_heavy, k_wt_heavy),
  // U+254B  ╋   UH DH HL HR
  pack(k_wt_heavy, k_wt_heavy, k_wt_heavy, k_wt_heavy),

  // ── 0x4C–0x4F  Reserved (skip) ─────────────────────────────────────────
  0, 0, 0, 0,

  // ── 0x50–0x53  Double / mixed double-single lines ──────────────────────
  // U+2550  ═   double horizontal       → LR + LL double
  pack(0, 0, k_wt_double, k_wt_double),
  // U+2551  ║   double vertical         → UD + UL double
  pack(k_wt_double, k_wt_double, 0, 0),
  // U+2552  ╒   DL double-R
  pack(0, k_wt_light, 0, k_wt_double),
  // U+2553  ╓   double-D LR
  pack(0, k_wt_double, 0, k_wt_light),

  // ── 0x54–0x57  Double corners (down-right, down-left) ──────────────────
  // U+2554  ╔   DD DR
  pack(0, k_wt_double, 0, k_wt_double),
  // U+2555  ╕   DL double-L
  pack(0, k_wt_light, k_wt_double, 0),
  // U+2556  ╖   double-D LL
  pack(0, k_wt_double, k_wt_light, 0),
  // U+2557  ╗   DD DL
  pack(0, k_wt_double, k_wt_double, 0),

  // ── 0x58–0x5B  Double corners (up-right, up-left) ──────────────────────
  // U+2558  ╘   UL double-R
  pack(k_wt_light, 0, 0, k_wt_double),
  // U+2559  ╙   double-U LR
  pack(k_wt_double, 0, 0, k_wt_light),
  // U+255A  ╚   UD UR
  pack(k_wt_double, 0, 0, k_wt_double),
  // U+255B  ╛   UL double-L
  pack(k_wt_light, 0, k_wt_double, 0),

  // ── 0x5C–0x5F  Double corners (up-left, reserved) ──────────────────────
  // U+255C  ╜   double-U LL
  pack(k_wt_double, 0, k_wt_light, 0),
  // U+255D  ╝   UD UL
  pack(k_wt_double, 0, k_wt_double, 0),
  // U+255E  ╞   UL DL double-R  (vertical light, right double)
  pack(k_wt_light, k_wt_light, 0, k_wt_double),
  // U+255F  ╟   double-U double-D LR  (vertical double, right light)
  pack(k_wt_double, k_wt_double, 0, k_wt_light),

  // ── 0x60–0x63  Double tees (vertical-right, vertical-left) ─────────────
  // U+2560  ╠   UD + DR  (vertical double, right double)
  pack(k_wt_double, k_wt_double, 0, k_wt_double),
  // U+2561  ╡   UL DL double-L
  pack(k_wt_light, k_wt_light, k_wt_double, 0),
  // U+2562  ╢   double-U double-D LL
  pack(k_wt_double, k_wt_double, k_wt_light, 0),
  // U+2563  ╣   UD + DL  (vertical double, left double)
  pack(k_wt_double, k_wt_double, k_wt_double, 0),

  // ── 0x64–0x67  Double tees (horizontal-down, horizontal-up) ────────────
  // U+2564  ╤   DL + LR double  (down light, horizontal double)
  pack(0, k_wt_light, k_wt_double, k_wt_double),
  // U+2565  ╥   DD + LR  (down double, horizontal light)
  pack(0, k_wt_double, k_wt_light, k_wt_light),
  // U+2566  ╦   DD + LR double  (horizontal+down, all double)
  pack(0, k_wt_double, k_wt_double, k_wt_double),
  // U+2567  ╧   UL + LR double  (up light, horizontal double)
  pack(k_wt_light, 0, k_wt_double, k_wt_double),

  // ── 0x68–0x6B  Double tees (horizontal-up), double cross ──────────────
  // U+2568  ╨   UD + LR  (up double, horizontal light)
  pack(k_wt_double, 0, k_wt_light, k_wt_light),
  // U+2569  ╩   UD + LR double  (horizontal+up, all double)
  pack(k_wt_double, 0, k_wt_double, k_wt_double),
  // U+256A  ╪   UL DL + LR double  (vertical light, horizontal double)
  pack(k_wt_light, k_wt_light, k_wt_double, k_wt_double),
  // U+256B  ╫   UD + LL LR  (vertical double, horizontal light)
  pack(k_wt_double, k_wt_double, k_wt_light, k_wt_light),

  // U+256C  ╬   all double cross
  pack(k_wt_double, k_wt_double, k_wt_double, k_wt_double),

  // ── 0x6D–0x70  Arcs / curved corners (skip) ────────────────────────────
  0, 0, 0, 0,

  // ── 0x71–0x73  Diagonals + X (skip) ────────────────────────────────────
  0, 0, 0,

  // ── 0x74–0x77  Half lines (light) ──────────────────────────────────────
  // U+2574  ╸   left half
  pack(0, 0, k_wt_light, 0),
  // U+2575  ╹   up half
  pack(k_wt_light, 0, 0, 0),
  // U+2576  ╺   right half
  pack(0, 0, 0, k_wt_light),
  // U+2577  ╻   down half
  pack(0, k_wt_light, 0, 0),

  // ── 0x78–0x7B  Half lines (heavy) ──────────────────────────────────────
  // U+2578  ╸   left heavy half
  pack(0, 0, k_wt_heavy, 0),
  // U+2579  ╹   up heavy half
  pack(k_wt_heavy, 0, 0, 0),
  // U+257A  ╺   right heavy half
  pack(0, 0, 0, k_wt_heavy),
  // U+257B  ╻   down heavy half
  pack(0, k_wt_heavy, 0, 0),

  // ── 0x7C–0x7F  Mixed-weight half lines ──────────────────────────────────
  // U+257C  ╼   left light, right heavy
  pack(0, 0, k_wt_light, k_wt_heavy),
  // U+257D  ╽   up light, down heavy
  pack(k_wt_light, k_wt_heavy, 0, 0),
  // U+257E  ╾   left heavy, right light
  pack(0, 0, k_wt_heavy, k_wt_light),
  // U+257F  ╿   up heavy, down light
  pack(k_wt_heavy, k_wt_light, 0, 0),
};

// ── Normalised geometry constants ─────────────────────────────────────────

// Light stroke: centred in cell, 1/8 cell thickness.
constexpr float k_light_half = 1.0f / 16.0f;  // half-thickness = 1/16
// Heavy stroke: 3/8 cell thickness.
constexpr float k_heavy_half = 3.0f / 16.0f;  // half-thickness = 3/16
// Double stroke: two 1/8 strokes offset by 3/16 from centre.
constexpr float k_double_inner_half = 3.0f / 16.0f; // inner edge of stroke
constexpr float k_double_outer_half = 5.0f / 16.0f; // outer edge of stroke

// Given a weight, compute the left/right bounds for a vertical stroke
// (or top/bottom bounds for a horizontal stroke), relative to the cell
// centre at 0.5.
constexpr void stroke_bounds(uint8_t wt, float& near_center, float& far_center) {
  switch (wt) {
    case k_wt_light:
      near_center = 0.5f - k_light_half;   // 7/16
      far_center  = 0.5f + k_light_half;   // 9/16
      break;
    case k_wt_heavy:
      near_center = 0.5f - k_heavy_half;   // 5/16
      far_center  = 0.5f + k_heavy_half;   // 11/16
      break;
    case k_wt_double:
      near_center = 0.5f - k_double_outer_half;  // 3/16
      far_center  = 0.5f + k_double_outer_half;  // 13/16
      break;
    default:
      near_center = 0.0f;
      far_center  = 0.0f;
      break;
  }
}

// Emit a single rectangle to `out` if there's room; return the number added.
inline uint32_t emit_rect(cell_rect* out, uint32_t idx, uint32_t max_rects,
                          float l, float t, float r, float b) {
  if (idx < max_rects) {
    out[idx] = { l, t, r, b };
  }
  return idx + 1;
}

// Emit one arm for a light or heavy stroke (1 rectangle).
uint32_t emit_arm(cell_rect* out, uint32_t idx, uint32_t max_rects,
                  uint8_t wt, char direction) {
  float near_c, far_c;
  stroke_bounds(wt, near_c, far_c);
  if (wt == k_wt_none) return idx;

  switch (direction) {
    case 'U':  // up arm: full width of stroke, top edge of cell
      return emit_rect(out, idx, max_rects, near_c, 0.0f, far_c, 0.5f);
    case 'D':  // down arm
      return emit_rect(out, idx, max_rects, near_c, 0.5f, far_c, 1.0f);
    case 'L':  // left arm
      return emit_rect(out, idx, max_rects, 0.0f, near_c, 0.5f, far_c);
    case 'R':  // right arm
      return emit_rect(out, idx, max_rects, 0.5f, near_c, 1.0f, far_c);
    default:
      return idx;
  }
}

// Emit two parallel strokes for a double-weight arm.
uint32_t emit_double_arm(cell_rect* out, uint32_t idx, uint32_t max_rects,
                         char direction) {
  switch (direction) {
    case 'U':  // two vertical strokes in upper half
      idx = emit_rect(out, idx, max_rects,
                       0.5f - k_double_outer_half, 0.0f,
                       0.5f - k_double_inner_half,  0.5f);
      idx = emit_rect(out, idx, max_rects,
                       0.5f + k_double_inner_half,  0.0f,
                       0.5f + k_double_outer_half,  0.5f);
      break;
    case 'D':  // two vertical strokes in lower half
      idx = emit_rect(out, idx, max_rects,
                       0.5f - k_double_outer_half, 0.5f,
                       0.5f - k_double_inner_half,  1.0f);
      idx = emit_rect(out, idx, max_rects,
                       0.5f + k_double_inner_half,  0.5f,
                       0.5f + k_double_outer_half,  1.0f);
      break;
    case 'L':  // two horizontal strokes in left half
      idx = emit_rect(out, idx, max_rects,
                       0.0f, 0.5f - k_double_outer_half,
                       0.5f, 0.5f - k_double_inner_half);
      idx = emit_rect(out, idx, max_rects,
                       0.0f, 0.5f + k_double_inner_half,
                       0.5f, 0.5f + k_double_outer_half);
      break;
    case 'R':  // two horizontal strokes in right half
      idx = emit_rect(out, idx, max_rects,
                       0.5f, 0.5f - k_double_outer_half,
                       1.0f, 0.5f - k_double_inner_half);
      idx = emit_rect(out, idx, max_rects,
                       0.5f, 0.5f + k_double_inner_half,
                       1.0f, 0.5f + k_double_outer_half);
      break;
  }
  return idx;
}

// Emit arms for one direction: one rect for light/heavy, two for double.
uint32_t emit_arm_maybe_double(cell_rect* out, uint32_t idx, uint32_t max_rects,
                                uint8_t wt, char direction) {
  if (wt == k_wt_none) return idx;
  if (wt == k_wt_double) {
    return emit_double_arm(out, idx, max_rects, direction);
  }
  return emit_arm(out, idx, max_rects, wt, direction);
}

// Emit the centre block for light or heavy strokes (only when at least
// 2 arms are present and both are the same weight).
uint32_t emit_center(cell_rect* out, uint32_t idx, uint32_t max_rects,
                     uint8_t wt) {
  float near_c, far_c;
  stroke_bounds(wt, near_c, far_c);
  return emit_rect(out, idx, max_rects, near_c, near_c, far_c, far_c);
}

} // anonymous namespace

// ===========================================================================
// Public API
// ===========================================================================

bool is_box_drawing_or_block(char32_t cp) noexcept {
  // Check box-drawing range (U+2500–U+257F).
  if (cp >= 0x2500 && cp <= 0x257F) {
    // Exclude dashes, arcs, diagonals (marked 0 in the table).
    return k_box_table[cp - 0x2500] != 0;
  }
  // Check block-element range (U+2580–U+259F).
  if (cp >= 0x2580 && cp <= 0x259F) {
    // Exclude shade characters (U+2591–U+2593).
    if (cp >= 0x2591 && cp <= 0x2593) return false;
    return true;
  }
  return false;
}

uint32_t get_box_drawing_rects(char32_t cp, cell_rect* out, uint32_t max_rects) noexcept {
  if (out == nullptr || max_rects == 0) return 0;

  // ── Box-drawing characters ────────────────────────────────────────────
  if (cp >= 0x2500 && cp <= 0x257F) {
    uint8_t const entry = k_box_table[cp - 0x2500];
    if (entry == 0) return 0;

    uint8_t const uw = up_wt(entry);
    uint8_t const dw = down_wt(entry);
    uint8_t const lw = left_wt(entry);
    uint8_t const rw = right_wt(entry);

    uint32_t idx = 0;

    // Emit directional arms (double arms produce two rects each).
    idx = emit_arm_maybe_double(out, idx, max_rects, uw, 'U');
    idx = emit_arm_maybe_double(out, idx, max_rects, dw, 'D');
    idx = emit_arm_maybe_double(out, idx, max_rects, lw, 'L');
    idx = emit_arm_maybe_double(out, idx, max_rects, rw, 'R');

    // Emit centre block for light/heavy characters with ≥ 2 arms.
    // Determine the centre weight: all non-double, non-none arms must
    // have the same weight for a centre to be meaningful.
    uint8_t center_wt = k_wt_none;
    uint32_t arm_count = 0;
    auto consider = [&](uint8_t w) {
      if (w != k_wt_none && w != k_wt_double) {
        if (center_wt == k_wt_none) center_wt = w;
        else if (center_wt != w) center_wt = 0xFF; // mixed weights → no centre
        ++arm_count;
      } else if (w == k_wt_double) {
        ++arm_count; // double arms don't get a separate centre
      }
    };
    consider(uw); consider(dw); consider(lw); consider(rw);

    if (arm_count >= 2 && center_wt != k_wt_none && center_wt != 0xFF) {
      idx = emit_center(out, idx, max_rects, center_wt);
    }

    return idx;
  }

  // ── Block-element characters ──────────────────────────────────────────
  if (cp >= 0x2580 && cp <= 0x259F) {
    // Shade characters excluded in is_box_drawing_or_block,
    // but guard here as well.
    if (cp >= 0x2591 && cp <= 0x2593) return 0;

    switch (cp) {
      // ── Upper/lower partial blocks ──────────────────────────────────
      case 0x2580: // ▀ upper half
        out[0] = { 0.0f, 0.0f, 1.0f, 0.5f };
        return 1;
      case 0x2581: // ▁ lower 1/8
        out[0] = { 0.0f, 0.875f, 1.0f, 1.0f };
        return 1;
      case 0x2582: // ▂ lower 1/4
        out[0] = { 0.0f, 0.75f, 1.0f, 1.0f };
        return 1;
      case 0x2583: // ▃ lower 3/8
        out[0] = { 0.0f, 0.625f, 1.0f, 1.0f };
        return 1;
      case 0x2584: // ▄ lower half
        out[0] = { 0.0f, 0.5f, 1.0f, 1.0f };
        return 1;
      case 0x2585: // ▅ lower 5/8
        out[0] = { 0.0f, 0.375f, 1.0f, 1.0f };
        return 1;
      case 0x2586: // ▆ lower 3/4
        out[0] = { 0.0f, 0.25f, 1.0f, 1.0f };
        return 1;
      case 0x2587: // ▇ lower 7/8
        out[0] = { 0.0f, 0.125f, 1.0f, 1.0f };
        return 1;
      case 0x2588: // █ full block
        out[0] = { 0.0f, 0.0f, 1.0f, 1.0f };
        return 1;

      // ── Left partial blocks ─────────────────────────────────────────
      case 0x2589: // ▉ left 7/8
        out[0] = { 0.0f, 0.0f, 0.875f, 1.0f };
        return 1;
      case 0x258A: // ▊ left 3/4
        out[0] = { 0.0f, 0.0f, 0.75f, 1.0f };
        return 1;
      case 0x258B: // ▋ left 5/8
        out[0] = { 0.0f, 0.0f, 0.625f, 1.0f };
        return 1;
      case 0x258C: // ▌ left half
        out[0] = { 0.0f, 0.0f, 0.5f, 1.0f };
        return 1;
      case 0x258D: // ▍ left 3/8
        out[0] = { 0.0f, 0.0f, 0.375f, 1.0f };
        return 1;
      case 0x258E: // ▎ left 1/4
        out[0] = { 0.0f, 0.0f, 0.25f, 1.0f };
        return 1;
      case 0x258F: // ▏ left 1/8
        out[0] = { 0.0f, 0.0f, 0.125f, 1.0f };
        return 1;

      // ── Right partial blocks ────────────────────────────────────────
      case 0x2590: // ▐ right half
        out[0] = { 0.5f, 0.0f, 1.0f, 1.0f };
        return 1;

      // ── Upper/lower small blocks ─────────────────────────────────────
      case 0x2594: // ▔ upper 1/8
        out[0] = { 0.0f, 0.0f, 1.0f, 0.125f };
        return 1;
      case 0x2595: // ▕ right 1/8
        out[0] = { 0.875f, 0.0f, 1.0f, 1.0f };
        return 1;

      // ── Quadrant blocks ──────────────────────────────────────────────
      case 0x2596: // ▖ lower left quadrant
        out[0] = { 0.0f, 0.5f, 0.5f, 1.0f };
        return 1;
      case 0x2597: // ▗ lower right quadrant
        out[0] = { 0.5f, 0.5f, 1.0f, 1.0f };
        return 1;
      case 0x2598: // ▘ upper left quadrant
        out[0] = { 0.0f, 0.0f, 0.5f, 0.5f };
        return 1;

      // ── Quadrant combinations (3 rects) ──────────────────────────────
      case 0x2599: // ▙ UL + LL + LR
        out[0] = { 0.0f, 0.0f, 0.5f, 0.5f };  // UL
        out[1] = { 0.0f, 0.5f, 0.5f, 1.0f };  // LL
        out[2] = { 0.5f, 0.5f, 1.0f, 1.0f };  // LR
        return 3;
      case 0x259B: // ▛ UL + UR + LL
        out[0] = { 0.0f, 0.0f, 0.5f, 0.5f };  // UL
        out[1] = { 0.5f, 0.0f, 1.0f, 0.5f };  // UR
        out[2] = { 0.0f, 0.5f, 0.5f, 1.0f };  // LL
        return 3;
      case 0x259C: // ▜ UL + UR + LR
        out[0] = { 0.0f, 0.0f, 0.5f, 0.5f };  // UL
        out[1] = { 0.5f, 0.0f, 1.0f, 0.5f };  // UR
        out[2] = { 0.5f, 0.5f, 1.0f, 1.0f };  // LR
        return 3;
      case 0x259F: // ▟ UR + LL + LR
        out[0] = { 0.5f, 0.0f, 1.0f, 0.5f };  // UR
        out[1] = { 0.0f, 0.5f, 0.5f, 1.0f };  // LL
        out[2] = { 0.5f, 0.5f, 1.0f, 1.0f };  // LR
        return 3;

      // ── Quadrant combinations (2 rects) ──────────────────────────────
      case 0x259A: // ▚ UL + LR
        out[0] = { 0.0f, 0.0f, 0.5f, 0.5f };  // UL
        out[1] = { 0.5f, 0.5f, 1.0f, 1.0f };  // LR
        return 2;
      case 0x259D: // ▌? Actually 0x259D is UPPER RIGHT QUADRANT
        out[0] = { 0.5f, 0.0f, 1.0f, 0.5f };
        return 1;
      case 0x259E: // ▞ UR + LL
        out[0] = { 0.5f, 0.0f, 1.0f, 0.5f };  // UR
        out[1] = { 0.0f, 0.5f, 0.5f, 1.0f };  // LL
        return 2;

      default:
        return 0;
    }
  }

  return 0;
}

} // namespace betty::platform
