#include <catch2/catch_test_macros.hpp>
#include "terminal/input_handler.hpp"

using namespace betty::terminal;

// ===========================================================================
// Printable ASCII — no longer handled by on_keydown
// ===========================================================================
// Printable characters (letters, digits, punctuation) are now delivered
// through WM_CHAR → write_char() and encoded as UTF-8.  on_keydown()
// returns empty for these — it only handles non-printable keys and
// Ctrl+letter combos.

TEST_CASE("Input — printable lowercase letter returns empty", "[input][printable]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::printable_a, false, false, false).empty());
}

TEST_CASE("Input — printable uppercase via vk_code returns empty", "[input][printable]") {
    input_handler h;
    CHECK(h.on_keydown(static_cast<vk_code>('A'), false, false, false).empty());
}

TEST_CASE("Input — printable digit returns empty", "[input][printable]") {
    input_handler h;
    CHECK(h.on_keydown(static_cast<vk_code>('0'), false, false, false).empty());
    CHECK(h.on_keydown(static_cast<vk_code>('9'), false, false, false).empty());
}

TEST_CASE("Input — printable punctuation returns empty", "[input][printable]") {
    input_handler h;
    CHECK(h.on_keydown(static_cast<vk_code>('.'), false, false, false).empty());
    CHECK(h.on_keydown(static_cast<vk_code>('/'), false, false, false).empty());
}

TEST_CASE("Input — uppercase Z returns empty", "[input][printable]") {
    input_handler h;
    CHECK(h.on_keydown(static_cast<vk_code>('Z'), false, false, false).empty());
}

// ===========================================================================
// Control keys (no modifiers)
// ===========================================================================

TEST_CASE("Input — enter returns CR", "[input][control]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::enter, false, false, false) == "\r");
}

TEST_CASE("Input — backspace returns DEL", "[input][control]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::backspace, false, false, false) == "\x7F");
}

TEST_CASE("Input — tab returns HT", "[input][control]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::tab, false, false, false) == "\t");
}

TEST_CASE("Input — escape returns ESC", "[input][control]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::escape, false, false, false) == "\x1B");
}

TEST_CASE("Input — space returns space", "[input][control]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::space, false, false, false) == " ");
}

// ===========================================================================
// Arrow keys
// ===========================================================================

TEST_CASE("Input — arrow_up returns CSI A", "[input][arrow]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::arrow_up, false, false, false) == "\x1B[A");
}

TEST_CASE("Input — arrow_down returns CSI B", "[input][arrow]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::arrow_down, false, false, false) == "\x1B[B");
}

TEST_CASE("Input — arrow_right returns CSI C", "[input][arrow]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::arrow_right, false, false, false) == "\x1B[C");
}

TEST_CASE("Input — arrow_left returns CSI D", "[input][arrow]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::arrow_left, false, false, false) == "\x1B[D");
}

// ===========================================================================
// Navigation keys
// ===========================================================================

TEST_CASE("Input — home returns CSI H", "[input][nav]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::home, false, false, false) == "\x1B[H");
}

TEST_CASE("Input — end returns CSI F", "[input][nav]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::end_, false, false, false) == "\x1B[F");
}

TEST_CASE("Input — page_up returns CSI 5~", "[input][nav]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::page_up, false, false, false) == "\x1B[5~");
}

TEST_CASE("Input — page_down returns CSI 6~", "[input][nav]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::page_down, false, false, false) == "\x1B[6~");
}

TEST_CASE("Input — delete returns CSI 3~", "[input][nav]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::delete_, false, false, false) == "\x1B[3~");
}

TEST_CASE("Input — insert returns CSI 2~", "[input][nav]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::insert_, false, false, false) == "\x1B[2~");
}

// ===========================================================================
// Function keys
// ===========================================================================

TEST_CASE("Input — F1 returns CSI OP", "[input][fn]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::f1, false, false, false) == "\x1B[OP");
}

TEST_CASE("Input — F4 returns CSI OS", "[input][fn]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::f4, false, false, false) == "\x1B[OS");
}

TEST_CASE("Input — F5 returns CSI 15~", "[input][fn]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::f5, false, false, false) == "\x1B[15~");
}

TEST_CASE("Input — F12 returns CSI 24~", "[input][fn]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::f12, false, false, false) == "\x1B[24~");
}

// ===========================================================================
// Ctrl modifier
// ===========================================================================

TEST_CASE("Input — Ctrl+A returns SOH (0x01)", "[input][ctrl]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::printable_a, true, false, false) == "\x01");
}

TEST_CASE("Input — Ctrl+Z returns SUB (0x1A)", "[input][ctrl]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::printable_z, true, false, false) == "\x1A");
}

TEST_CASE("Input — Ctrl+B returns 0x02", "[input][ctrl]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::printable_a, true, false, false) == "\x01");
    // Ctrl+B via the generic path
    auto a_val = static_cast<uint32_t>(vk_code::printable_a);
    auto b_val = a_val + 1;  // 'b'
    auto result = h.on_keydown(static_cast<vk_code>(b_val), true, false, false);
    CHECK(result == std::string(1, '\x02'));
}

TEST_CASE("Input — Ctrl+Enter falls through Ctrl block, still returns CR", "[input][ctrl][edge]") {
    input_handler h;
    // Ctrl+Enter — Ctrl block has no explicit match for enter,
    // and enter isn't in the a-z range, so it falls through to the
    // core keys switch where enter returns "\r".
    auto result = h.on_keydown(vk_code::enter, true, false, false);
    CHECK(result == "\r");
}

// ===========================================================================
// Unknown / unhandled keys
// ===========================================================================

TEST_CASE("Input — unknown vk_code returns empty", "[input][edge]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::unknown, false, false, false).empty());
}

TEST_CASE("Input — non-printable non-control returns empty", "[input][edge]") {
    input_handler h;
    // Use a value that doesn't match any switch case and isn't printable
    CHECK(h.on_keydown(static_cast<vk_code>(0x7F), false, false, false).empty());
}

// ===========================================================================
// Shift and alt modifiers (currently ignored — should still work)
// ===========================================================================

TEST_CASE("Input — shift is ignored, key still produces output", "[input][modifier]") {
    input_handler h;
    CHECK(h.on_keydown(vk_code::enter, false, true, false) == "\r");
}

TEST_CASE("Input — Alt prefixes keys with ESC", "[input][alt]") {
    input_handler h;
    // Alt+Up → ESC + CSI A
    CHECK(h.on_keydown(vk_code::arrow_up, false, false, true) == "\x1B\x1B[A");
    // Alt+Enter → ESC + CR
    CHECK(h.on_keydown(vk_code::enter, false, false, true) == "\x1B\r");
    // Alt+printable — returns empty (printable path removed; WM_CHAR handles this)
    CHECK(h.on_keydown(vk_code::printable_a, false, false, true).empty());
}

// ===========================================================================
// Edge: Ctrl+Alt combination
// ===========================================================================

TEST_CASE("Input — Ctrl+Alt letter sends ESC + ctrl-char", "[input][edge]") {
    input_handler h;
    // Ctrl+Alt+A: control char 0x01, prefixed with ESC
    auto result = h.on_keydown(vk_code::printable_a, true, false, true);
    CHECK(result == "\x1B\x01");
}


