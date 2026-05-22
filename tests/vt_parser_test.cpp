#include <catch2/catch_test_macros.hpp>
#include "terminal/vt_parser.hpp"

using namespace betty::terminal;

// Helper: feed a sequence of bytes to the parser, returning the last
// non-empty result (or empty if the sequence produces no actions).
// Useful for one-shot sequences where we expect exactly one action at the end.
static auto parse_sequence(vt_parser &p, std::string_view bytes)
    -> std::vector<action>
{
    std::vector<action> result;
    for (auto const b : bytes) {
        auto v = p.parse(static_cast<unsigned char>(b));
        if (!v.empty()) result = std::move(v);
    }
    return result;
}

// Convenience overload that creates a fresh parser.
static auto parse_sequence(std::string_view bytes)
    -> std::vector<action>
{
    vt_parser p;
    return parse_sequence(p, bytes);
}

// ===========================================================================
// 5a — Ground state: printable characters & C0 controls
// ===========================================================================

TEST_CASE("Ground state — carriage return", "[ground]") {
    vt_parser p;
    auto const v = p.parse('\r');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::carriage_return);
}

TEST_CASE("Ground state — newline", "[ground]") {
    vt_parser p;
    auto const v = p.parse('\n');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::newline);
}

TEST_CASE("Ground state — printable character", "[ground]") {
    vt_parser p;
    auto const v = p.parse('A');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::write_char);
    CHECK(v[0].codepoint == 65);
}

TEST_CASE("Ground state — two printable characters", "[ground]") {
    vt_parser p;
    auto const v1 = p.parse('z');
    REQUIRE(v1.size() == 1);
    CHECK(v1[0].type == action_type::write_char);
    CHECK(v1[0].codepoint == 122);

    auto const v2 = p.parse('!');
    REQUIRE(v2.size() == 1);
    CHECK(v2[0].type == action_type::write_char);
    CHECK(v2[0].codepoint == 33);
}

TEST_CASE("Ground state — C0 controls silently ignored", "[ground]") {
    vt_parser p;
    // SOH (0x01), BS (0x08), SUB (0x1A)
    CHECK(p.parse(0x01).empty());
    CHECK(p.parse(0x08).empty());
    CHECK(p.parse(0x1A).empty());

    // Parser should still be in ground state — printable chars work.
    auto const v = p.parse('A');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::write_char);
}

// ===========================================================================
// 5b — Escape state
// ===========================================================================

TEST_CASE("Escape state — ESC [ enters CSI entry", "[escape]") {
    vt_parser p;
    CHECK(p.parse(0x1B).empty());   // ESC
    CHECK(p.parse('[').empty());     // '[' — still no action
}

TEST_CASE("Escape state — DECSC (ESC 7) saves cursor, no action", "[escape]") {
    vt_parser p;
    CHECK(p.parse(0x1B).empty());
    CHECK(p.parse('7').empty());

    // State should have returned to ground.
    auto const v = p.parse('A');
    REQUIRE(v.size() == 1);
    CHECK(v[0].codepoint == 65);
}

TEST_CASE("Escape state — DECRC (ESC 8) restores cursor to default (0,0)", "[escape]") {
    vt_parser p;
    CHECK(p.parse(0x1B).empty());
    auto const v = p.parse('8');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
    CHECK(v[0].row == 0);
    CHECK(v[0].col == 0);
}

TEST_CASE("Escape state — unknown ESC sequence discarded, state recovers", "[escape]") {
    vt_parser p;
    CHECK(p.parse(0x1B).empty());
    CHECK(p.parse('X').empty());

    // State should have returned to ground.
    auto const v = p.parse('A');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::write_char);
    CHECK(v[0].codepoint == 65);
}

// ===========================================================================
// 5c — CSI relative cursor movement: CUU (A), CUD (B), CUF (C), CUB (D)
// ===========================================================================

TEST_CASE("CSI CUU — no param defaults to count=1", "[csi][cuu]") {
    auto const v = parse_sequence("\x1B[A");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor_up);
    CHECK(v[0].count == 1);
}

TEST_CASE("CSI CUU — explicit count", "[csi][cuu]") {
    auto const v = parse_sequence("\x1B[5A");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor_up);
    CHECK(v[0].count == 5);
}

TEST_CASE("CSI CUU — param 0 defaulted to 1", "[csi][cuu]") {
    auto const v = parse_sequence("\x1B[0A");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor_up);
    CHECK(v[0].count == 1);
}

TEST_CASE("CSI CUD", "[csi][cud]") {
    auto const v = parse_sequence("\x1B[3B");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor_down);
    CHECK(v[0].count == 3);
}

TEST_CASE("CSI CUF — multi-digit param", "[csi][cuf]") {
    auto const v = parse_sequence("\x1B[10C");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor_forward);
    CHECK(v[0].count == 10);
}

TEST_CASE("CSI CUB", "[csi][cub]") {
    auto const v = parse_sequence("\x1B[4D");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor_back);
    CHECK(v[0].count == 4);
}

// ===========================================================================
// 5d — CSI absolute cursor positioning: CUP (H), HVP (f)
// ===========================================================================

TEST_CASE("CSI CUP — no params defaults to 1;1", "[csi][cup]") {
    auto const v = parse_sequence("\x1B[H");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
    CHECK(v[0].row == 0);
    CHECK(v[0].col == 0);
}

TEST_CASE("CSI CUP — row only", "[csi][cup]") {
    auto const v = parse_sequence("\x1B[5H");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
    CHECK(v[0].row == 4);
    CHECK(v[0].col == 0);
}

TEST_CASE("CSI CUP — row and col", "[csi][cup]") {
    auto const v = parse_sequence("\x1B[5;10H");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
    CHECK(v[0].row == 4);
    CHECK(v[0].col == 9);
}

TEST_CASE("CSI CUP — param 0 treated as 1", "[csi][cup]") {
    auto const v = parse_sequence("\x1B[0;0H");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
    CHECK(v[0].row == 0);
    CHECK(v[0].col == 0);
}

TEST_CASE("CSI HVP (f) — same semantics as CUP", "[csi][hvp]") {
    auto const v = parse_sequence("\x1B[8;3f");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
    CHECK(v[0].row == 7);
    CHECK(v[0].col == 2);
}

TEST_CASE("CSI CUP — empty first param defaults", "[csi][cup]") {
    auto const v = parse_sequence("\x1B[;10H");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
    CHECK(v[0].row == 0);
    CHECK(v[0].col == 9);
}

TEST_CASE("CSI CUP — empty second param defaults", "[csi][cup]") {
    auto const v = parse_sequence("\x1B[5;H");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
    CHECK(v[0].row == 4);
    CHECK(v[0].col == 0);
}

// ===========================================================================
// 5e — Incremental byte feeding
// ===========================================================================

TEST_CASE("Incremental — CUP fed byte-by-byte", "[incremental]") {
    vt_parser p;
    CHECK(p.parse(0x1B).empty());  // ESC
    CHECK(p.parse('[').empty());   // [
    CHECK(p.parse('1').empty());   // 1
    CHECK(p.parse('2').empty());   // 2
    CHECK(p.parse(';').empty());   // ;
    CHECK(p.parse('3').empty());   // 3
    CHECK(p.parse('4').empty());   // 4
    auto const v = p.parse('H');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
    CHECK(v[0].row == 11);
    CHECK(v[0].col == 33);
}

TEST_CASE("Incremental — state resets after complete CSI", "[incremental]") {
    vt_parser p;

    // Feed ESC [ H
    CHECK(p.parse(0x1B).empty());
    CHECK(p.parse('[').empty());
    auto const v1 = p.parse('H');
    REQUIRE(v1.size() == 1);
    CHECK(v1[0].type == action_type::move_cursor);

    // Next byte should be treated as ground state.
    auto const v2 = p.parse('X');
    REQUIRE(v2.size() == 1);
    CHECK(v2[0].type == action_type::write_char);
    CHECK(v2[0].codepoint == 88);
}

// ===========================================================================
// 5f — DECSC / DECRC round-trip
// ===========================================================================

TEST_CASE("DECSC/DECRC — save then restore defaults", "[dec]") {
    vt_parser p;
    CHECK(p.parse(0x1B).empty());
    CHECK(p.parse('7').empty());   // DECSC

    CHECK(p.parse(0x1B).empty());
    auto const v = p.parse('8');   // DECRC
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
    CHECK(v[0].row == 0);
    CHECK(v[0].col == 0);
}

TEST_CASE("DECSC/DECRC — move, save, move, restore", "[dec]") {
    vt_parser p;

    // CUP to row 5, col 10 (0-based: 4, 9)
    parse_sequence(p, "\x1B[5;10H");

    // DECSC — save
    CHECK(p.parse(0x1B).empty());
    CHECK(p.parse('7').empty());

    // CUP to row 3, col 3 (0-based: 2, 2)
    parse_sequence(p, "\x1B[3;3H");

    // DECRC — restore
    CHECK(p.parse(0x1B).empty());
    auto const v = p.parse('8');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
    CHECK(v[0].row == 4);
    CHECK(v[0].col == 9);
}

// ===========================================================================
// 5g — Invalid sequences don't corrupt state
// ===========================================================================

TEST_CASE("Invalid — unknown CSI final byte discarded", "[invalid]") {
    vt_parser p;
    parse_sequence(p, "\x1B[Z");   // Z is not a recognized final byte
    auto const v = p.parse('A');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::write_char);
}

TEST_CASE("Invalid — garbage byte during CSI params resets", "[invalid]") {
    vt_parser p;
    CHECK(p.parse(0x1B).empty());
    CHECK(p.parse('[').empty());
    CHECK(p.parse('5').empty());
    CHECK(p.parse(0x01).empty());  // SOH — garbage, should reset CSI

    // Should be back in ground.
    auto const v = p.parse('A');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::write_char);
}

TEST_CASE("Invalid — garbage byte during escape resets", "[invalid]") {
    vt_parser p;
    CHECK(p.parse(0x1B).empty());
    CHECK(p.parse('!').empty());   // '!' is not a recognized escape sequence

    // Should be back in ground.
    auto const v = p.parse('B');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::write_char);
}

TEST_CASE("Invalid — back-to-back valid sequences", "[invalid]") {
    vt_parser p;

    // CUU(3)
    {
        auto const v = parse_sequence(p, "\x1B[3A");
        REQUIRE(v.size() == 1);
        CHECK(v[0].type == action_type::move_cursor_up);
        CHECK(v[0].count == 3);
    }

    // CUP(5, 2)
    {
        auto const v = parse_sequence(p, "\x1B[5;2H");
        REQUIRE(v.size() == 1);
        CHECK(v[0].type == action_type::move_cursor);
        CHECK(v[0].row == 4);
        CHECK(v[0].col == 1);
    }

    // Newline
    {
        auto const v = p.parse('\n');
        REQUIRE(v.size() == 1);
        CHECK(v[0].type == action_type::newline);
    }
}

// ===========================================================================
// 5h — CSI intermediate bytes
// ===========================================================================

TEST_CASE("CSI intermediate — single intermediate byte before final", "[csi][intermediate]") {
    auto const v = parse_sequence("\x1B[?H");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
    CHECK(v[0].row == 0);
    CHECK(v[0].col == 0);
}

TEST_CASE("CSI intermediate — multiple intermediate bytes", "[csi][intermediate]") {
    auto const v = parse_sequence("\x1B[?$H");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
    CHECK(v[0].row == 0);
    CHECK(v[0].col == 0);
}
