#include <catch2/catch_test_macros.hpp>
#include "terminal/vt_parser.hpp"

using namespace betty::terminal;

// Helper: feed a sequence of bytes to the parser, returning only the
// final result.  Useful for one-shot sequences where we expect exactly
// one action at the end.
static auto parse_sequence(vt_parser &p, std::string_view bytes)
    -> std::optional<action>
{
    std::optional<action> result;
    for (auto const b : bytes) {
        result = p.parse(static_cast<unsigned char>(b));
    }
    return result;
}

// Convenience overload that creates a fresh parser.
static auto parse_sequence(std::string_view bytes)
    -> std::optional<action>
{
    vt_parser p;
    return parse_sequence(p, bytes);
}

// ===========================================================================
// 5a — Ground state: printable characters & C0 controls
// ===========================================================================

TEST_CASE("Ground state — carriage return", "[ground]") {
    vt_parser p;
    auto const a = p.parse('\r');
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::carriage_return);
}

TEST_CASE("Ground state — newline", "[ground]") {
    vt_parser p;
    auto const a = p.parse('\n');
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::newline);
}

TEST_CASE("Ground state — printable character", "[ground]") {
    vt_parser p;
    auto const a = p.parse('A');
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::write_char);
    CHECK(a->codepoint == 65);
}

TEST_CASE("Ground state — two printable characters", "[ground]") {
    vt_parser p;
    auto const a1 = p.parse('z');
    REQUIRE(a1.has_value());
    CHECK(a1->type == action_type::write_char);
    CHECK(a1->codepoint == 122);

    auto const a2 = p.parse('!');
    REQUIRE(a2.has_value());
    CHECK(a2->type == action_type::write_char);
    CHECK(a2->codepoint == 33);
}

TEST_CASE("Ground state — C0 controls silently ignored", "[ground]") {
    vt_parser p;
    // SOH (0x01), BS (0x08), SUB (0x1A)
    CHECK_FALSE(p.parse(0x01).has_value());
    CHECK_FALSE(p.parse(0x08).has_value());
    CHECK_FALSE(p.parse(0x1A).has_value());

    // Parser should still be in ground state — printable chars work.
    auto const a = p.parse('A');
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::write_char);
}

// ===========================================================================
// 5b — Escape state
// ===========================================================================

TEST_CASE("Escape state — ESC [ enters CSI entry", "[escape]") {
    vt_parser p;
    CHECK_FALSE(p.parse(0x1B).has_value());   // ESC
    CHECK_FALSE(p.parse('[').has_value());    // '[' — still no action
}

TEST_CASE("Escape state — DECSC (ESC 7) saves cursor, no action", "[escape]") {
    vt_parser p;
    CHECK_FALSE(p.parse(0x1B).has_value());
    CHECK_FALSE(p.parse('7').has_value());

    // State should have returned to ground.
    auto const a = p.parse('A');
    REQUIRE(a.has_value());
    CHECK(a->codepoint == 65);
}

TEST_CASE("Escape state — DECRC (ESC 8) restores cursor to default (0,0)", "[escape]") {
    vt_parser p;
    CHECK_FALSE(p.parse(0x1B).has_value());
    auto const a = p.parse('8');
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor);
    CHECK(a->row == 0);
    CHECK(a->col == 0);
}

TEST_CASE("Escape state — unknown ESC sequence discarded, state recovers", "[escape]") {
    vt_parser p;
    CHECK_FALSE(p.parse(0x1B).has_value());
    CHECK_FALSE(p.parse('X').has_value());

    // State should have returned to ground.
    auto const a = p.parse('A');
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::write_char);
    CHECK(a->codepoint == 65);
}

// ===========================================================================
// 5c — CSI relative cursor movement: CUU (A), CUD (B), CUF (C), CUB (D)
// ===========================================================================

TEST_CASE("CSI CUU — no param defaults to count=1", "[csi][cuu]") {
    auto const a = parse_sequence("\x1B[A");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor_up);
    CHECK(a->count == 1);
}

TEST_CASE("CSI CUU — explicit count", "[csi][cuu]") {
    auto const a = parse_sequence("\x1B[5A");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor_up);
    CHECK(a->count == 5);
}

TEST_CASE("CSI CUU — param 0 defaulted to 1", "[csi][cuu]") {
    auto const a = parse_sequence("\x1B[0A");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor_up);
    CHECK(a->count == 1);
}

TEST_CASE("CSI CUD", "[csi][cud]") {
    auto const a = parse_sequence("\x1B[3B");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor_down);
    CHECK(a->count == 3);
}

TEST_CASE("CSI CUF — multi-digit param", "[csi][cuf]") {
    auto const a = parse_sequence("\x1B[10C");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor_forward);
    CHECK(a->count == 10);
}

TEST_CASE("CSI CUB", "[csi][cub]") {
    auto const a = parse_sequence("\x1B[4D");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor_back);
    CHECK(a->count == 4);
}

// ===========================================================================
// 5d — CSI absolute cursor positioning: CUP (H), HVP (f)
// ===========================================================================

TEST_CASE("CSI CUP — no params defaults to 1;1", "[csi][cup]") {
    auto const a = parse_sequence("\x1B[H");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor);
    CHECK(a->row == 0);
    CHECK(a->col == 0);
}

TEST_CASE("CSI CUP — row only", "[csi][cup]") {
    auto const a = parse_sequence("\x1B[5H");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor);
    CHECK(a->row == 4);
    CHECK(a->col == 0);
}

TEST_CASE("CSI CUP — row and col", "[csi][cup]") {
    auto const a = parse_sequence("\x1B[5;10H");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor);
    CHECK(a->row == 4);
    CHECK(a->col == 9);
}

TEST_CASE("CSI CUP — param 0 treated as 1", "[csi][cup]") {
    auto const a = parse_sequence("\x1B[0;0H");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor);
    CHECK(a->row == 0);
    CHECK(a->col == 0);
}

TEST_CASE("CSI HVP (f) — same semantics as CUP", "[csi][hvp]") {
    auto const a = parse_sequence("\x1B[8;3f");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor);
    CHECK(a->row == 7);
    CHECK(a->col == 2);
}

TEST_CASE("CSI CUP — empty first param defaults", "[csi][cup]") {
    auto const a = parse_sequence("\x1B[;10H");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor);
    CHECK(a->row == 0);
    CHECK(a->col == 9);
}

TEST_CASE("CSI CUP — empty second param defaults", "[csi][cup]") {
    auto const a = parse_sequence("\x1B[5;H");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor);
    CHECK(a->row == 4);
    CHECK(a->col == 0);
}

// ===========================================================================
// 5e — Incremental byte feeding
// ===========================================================================

TEST_CASE("Incremental — CUP fed byte-by-byte", "[incremental]") {
    vt_parser p;
    CHECK_FALSE(p.parse(0x1B).has_value());  // ESC
    CHECK_FALSE(p.parse('[').has_value());   // [
    CHECK_FALSE(p.parse('1').has_value());   // 1
    CHECK_FALSE(p.parse('2').has_value());   // 2
    CHECK_FALSE(p.parse(';').has_value());   // ;
    CHECK_FALSE(p.parse('3').has_value());   // 3
    CHECK_FALSE(p.parse('4').has_value());   // 4
    auto const a = p.parse('H');
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor);
    CHECK(a->row == 11);
    CHECK(a->col == 33);
}

TEST_CASE("Incremental — state resets after complete CSI", "[incremental]") {
    vt_parser p;

    // Feed ESC [ H
    CHECK_FALSE(p.parse(0x1B).has_value());
    CHECK_FALSE(p.parse('[').has_value());
    auto const a1 = p.parse('H');
    REQUIRE(a1.has_value());
    CHECK(a1->type == action_type::move_cursor);

    // Next byte should be treated as ground state.
    auto const a2 = p.parse('X');
    REQUIRE(a2.has_value());
    CHECK(a2->type == action_type::write_char);
    CHECK(a2->codepoint == 88);
}

// ===========================================================================
// 5f — DECSC / DECRC round-trip
// ===========================================================================

TEST_CASE("DECSC/DECRC — save then restore defaults", "[dec]") {
    vt_parser p;
    CHECK_FALSE(p.parse(0x1B).has_value());
    CHECK_FALSE(p.parse('7').has_value());   // DECSC

    CHECK_FALSE(p.parse(0x1B).has_value());
    auto const a = p.parse('8');             // DECRC
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor);
    CHECK(a->row == 0);
    CHECK(a->col == 0);
}

TEST_CASE("DECSC/DECRC — move, save, move, restore", "[dec]") {
    vt_parser p;

    // CUP to row 5, col 10 (0-based: 4, 9)
    parse_sequence(p, "\x1B[5;10H");

    // DECSC — save
    CHECK_FALSE(p.parse(0x1B).has_value());
    CHECK_FALSE(p.parse('7').has_value());

    // CUP to row 3, col 3 (0-based: 2, 2)
    parse_sequence(p, "\x1B[3;3H");

    // DECRC — restore
    CHECK_FALSE(p.parse(0x1B).has_value());
    auto const a = p.parse('8');
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor);
    CHECK(a->row == 4);
    CHECK(a->col == 9);
}

// ===========================================================================
// 5g — Invalid sequences don't corrupt state
// ===========================================================================

TEST_CASE("Invalid — unknown CSI final byte discarded", "[invalid]") {
    vt_parser p;
    parse_sequence(p, "\x1B[Z");   // Z is not a recognized final byte
    auto const a = p.parse('A');
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::write_char);
}

TEST_CASE("Invalid — garbage byte during CSI params resets", "[invalid]") {
    vt_parser p;
    CHECK_FALSE(p.parse(0x1B).has_value());
    CHECK_FALSE(p.parse('[').has_value());
    CHECK_FALSE(p.parse('5').has_value());
    CHECK_FALSE(p.parse(0x01).has_value());  // SOH — garbage, should reset CSI

    // Should be back in ground.
    auto const a = p.parse('A');
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::write_char);
}

TEST_CASE("Invalid — garbage byte during escape resets", "[invalid]") {
    vt_parser p;
    CHECK_FALSE(p.parse(0x1B).has_value());
    CHECK_FALSE(p.parse('!').has_value());   // '!' is not a recognized escape sequence

    // Should be back in ground.
    auto const a = p.parse('B');
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::write_char);
}

TEST_CASE("Invalid — back-to-back valid sequences", "[invalid]") {
    vt_parser p;

    // CUU(3)
    {
        auto const a = parse_sequence(p, "\x1B[3A");
        REQUIRE(a.has_value());
        CHECK(a->type == action_type::move_cursor_up);
        CHECK(a->count == 3);
    }

    // CUP(5, 2)
    {
        auto const a = parse_sequence(p, "\x1B[5;2H");
        REQUIRE(a.has_value());
        CHECK(a->type == action_type::move_cursor);
        CHECK(a->row == 4);
        CHECK(a->col == 1);
    }

    // Newline
    {
        auto const a = p.parse('\n');
        REQUIRE(a.has_value());
        CHECK(a->type == action_type::newline);
    }
}

// ===========================================================================
// 5h — CSI intermediate bytes
// ===========================================================================

TEST_CASE("CSI intermediate — single intermediate byte before final", "[csi][intermediate]") {
    auto const a = parse_sequence("\x1B[?H");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor);
    CHECK(a->row == 0);
    CHECK(a->col == 0);
}

TEST_CASE("CSI intermediate — multiple intermediate bytes", "[csi][intermediate]") {
    auto const a = parse_sequence("\x1B[?$H");
    REQUIRE(a.has_value());
    CHECK(a->type == action_type::move_cursor);
    CHECK(a->row == 0);
    CHECK(a->col == 0);
}
