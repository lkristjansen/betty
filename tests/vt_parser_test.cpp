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

TEST_CASE("Escape state — DECSC (ESC 7) emits save_cursor action", "[escape]") {
    vt_parser p;
    CHECK(p.parse(0x1B).empty());
    auto const v = p.parse('7');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::save_cursor);

    // State should have returned to ground.
    auto const v2 = p.parse('A');
    REQUIRE(v2.size() == 1);
    CHECK(v2[0].codepoint == 65);
}

TEST_CASE("Escape state — DECRC (ESC 8) emits restore_cursor action", "[escape]") {
    vt_parser p;
    CHECK(p.parse(0x1B).empty());
    auto const v = p.parse('8');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::restore_cursor);
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

TEST_CASE("DECSC/DECRC — save then restore emits correct action types", "[dec]") {
    vt_parser p;
    CHECK(p.parse(0x1B).empty());
    auto const v_save = p.parse('7');
    REQUIRE(v_save.size() == 1);
    CHECK(v_save[0].type == action_type::save_cursor);

    CHECK(p.parse(0x1B).empty());
    auto const v_restore = p.parse('8');
    REQUIRE(v_restore.size() == 1);
    CHECK(v_restore[0].type == action_type::restore_cursor);
}

TEST_CASE("DECSC/DECRC — parser emits save/restore actions in correct order", "[dec]") {
    vt_parser p;

    // DECSC — save
    CHECK(p.parse(0x1B).empty());
    auto const v1 = p.parse('7');
    REQUIRE(v1.size() == 1);
    CHECK(v1[0].type == action_type::save_cursor);

    // DECRC — restore
    CHECK(p.parse(0x1B).empty());
    auto const v2 = p.parse('8');
    REQUIRE(v2.size() == 1);
    CHECK(v2[0].type == action_type::restore_cursor);
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

// ===========================================================================
// Task 8 — ED: Erase in Display (CSI Ps J)
// ===========================================================================

TEST_CASE("CSI ED — no param defaults to mode 0", "[csi][ed]") {
    auto const v = parse_sequence("\x1B[J");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_display);
    CHECK(v[0].count == 0);
}

TEST_CASE("CSI ED — mode 0 (erase to end)", "[csi][ed]") {
    auto const v = parse_sequence("\x1B[0J");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_display);
    CHECK(v[0].count == 0);
}

TEST_CASE("CSI ED — mode 1 (erase from beginning)", "[csi][ed]") {
    auto const v = parse_sequence("\x1B[1J");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_display);
    CHECK(v[0].count == 1);
}

TEST_CASE("CSI ED — mode 2 (erase entire display)", "[csi][ed]") {
    auto const v = parse_sequence("\x1B[2J");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_display);
    CHECK(v[0].count == 2);
}

TEST_CASE("CSI ED — mode 3 (erase display + scrollback)", "[csi][ed]") {
    auto const v = parse_sequence("\x1B[3J");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_display);
    CHECK(v[0].count == 3);
}

TEST_CASE("CSI ED — missing first param defaults to 0", "[csi][ed]") {
    auto const v = parse_sequence("\x1B[;J");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_display);
    CHECK(v[0].count == 0);
}

// ===========================================================================
// Task 8 — EL: Erase in Line (CSI Ps K)
// ===========================================================================

TEST_CASE("CSI EL — no param defaults to mode 0", "[csi][el]") {
    auto const v = parse_sequence("\x1B[K");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_line);
    CHECK(v[0].count == 0);
}

TEST_CASE("CSI EL — mode 0 (erase to end of line)", "[csi][el]") {
    auto const v = parse_sequence("\x1B[0K");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_line);
    CHECK(v[0].count == 0);
}

TEST_CASE("CSI EL — mode 1 (erase from beginning of line)", "[csi][el]") {
    auto const v = parse_sequence("\x1B[1K");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_line);
    CHECK(v[0].count == 1);
}

TEST_CASE("CSI EL — mode 2 (erase entire line)", "[csi][el]") {
    auto const v = parse_sequence("\x1B[2K");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_line);
    CHECK(v[0].count == 2);
}

TEST_CASE("CSI EL — missing first param defaults to 0", "[csi][el]") {
    auto const v = parse_sequence("\x1B[;K");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_line);
    CHECK(v[0].count == 0);
}

// ===========================================================================
// Task 8 — ED/EL integration: parser recovers after erase
// ===========================================================================

TEST_CASE("CSI ED/EL — parser returns to ground after erase", "[csi][ed][el]") {
    vt_parser p;
    parse_sequence(p, "\x1B[2J");
    auto const v = p.parse('A');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::write_char);
}

TEST_CASE("CSI ED — with intermediate byte", "[csi][ed]") {
    auto const v = parse_sequence("\x1B[?J");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_display);
    CHECK(v[0].count == 0);
}

TEST_CASE("CSI EL — with intermediate byte", "[csi][el]") {
    auto const v = parse_sequence("\x1B[?K");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_line);
    CHECK(v[0].count == 0);
}

// ===========================================================================
// Task 9 — OSC window title sequences
// ===========================================================================

TEST_CASE("OSC — OSC 0 with BEL terminator sets window title", "[osc][bel]") {
    auto const v = parse_sequence("\x1B]0;hello\x07");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_window_title);
    CHECK(v[0].title == "hello");
}

TEST_CASE("OSC — OSC 2 with ST terminator sets window title", "[osc][st]") {
    auto const v = parse_sequence("\x1B]2;world\x1B\\");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_window_title);
    CHECK(v[0].title == "world");
}

TEST_CASE("OSC — OSC 0 with ESC BEL terminator sets window title", "[osc][st-esc-bel-seq]") {
    auto const v = parse_sequence("\x1B]0;pi - project\x1B\x07");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_window_title);
    CHECK(v[0].title == "pi - project");
}

TEST_CASE("OSC — OSC 1 treated as window title", "[osc][osc1]") {
    auto const v = parse_sequence("\x1B]1;test\x07");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_window_title);
    CHECK(v[0].title == "test");
}

TEST_CASE("OSC — empty title ignored", "[osc][empty]") {
    auto const v = parse_sequence("\x1B]0;\x07");
    CHECK(v.empty());
}

TEST_CASE("OSC — missing semicolon ignored", "[osc][no-semi]") {
    auto const v = parse_sequence("\x1B]garbage\x07");
    CHECK(v.empty());
}

TEST_CASE("OSC — unrecognized OSC number ignored", "[osc][unknown-ps]") {
    auto const v = parse_sequence("\x1B]4;data\x07");
    CHECK(v.empty());
}

TEST_CASE("OSC — title truncated to 255 characters", "[osc][truncate]") {
    std::string long_title(300, 'x');
    std::string seq = "\x1B]0;";
    seq += long_title;
    seq += "\x07";
    auto const v = parse_sequence(seq);
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_window_title);
    CHECK(v[0].title.size() == 255);
    CHECK(v[0].title == std::string(255, 'x'));
}

TEST_CASE("OSC — UTF-8 title bytes passed through", "[osc][unicode]") {
    // "café" = 5 bytes in UTF-8 (é = 0xC3 0xA9)
    auto const v = parse_sequence("\x1B]0;caf\xC3\xA9\x07");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_window_title);
    CHECK(v[0].title == "caf\xC3\xA9");
}

TEST_CASE("OSC — buffer limit 1024 bytes", "[osc][buffer-limit]") {
    std::string huge(1100, 'y');
    std::string seq = "\x1B]0;";
    seq += huge;
    seq += "\x07";
    auto const v = parse_sequence(seq);
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_window_title);
    // Title text before BEL is truncated to 1024 bytes (minus "0;"), but
    // then the title extraction further truncates to 255 chars.
    CHECK(v[0].title.size() == 255);
}

TEST_CASE("OSC — ESC inside OSC not followed by backslash restarts escape", "[osc][st-esc-other]") {
    vt_parser p;
    // Start OSC, feed ESC, then 'x' — should discard OSC and process ESC x as unknown escape
    CHECK(p.parse('\x1B').empty());
    CHECK(p.parse(']').empty());
    CHECK(p.parse('0').empty());
    CHECK(p.parse(';').empty());
    CHECK(p.parse('a').empty());
    CHECK(p.parse('\x1B').empty());  // ESC inside OSC
    // 'x' should be processed in escape state — unknown, returns to ground
    CHECK(p.parse('x').empty());
    // Should be back in ground
    auto const v = p.parse('A');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::write_char);
}

TEST_CASE("OSC — ESC [ inside OSC starts new CSI", "[osc][st-esc-csi]") {
    vt_parser p;
    CHECK(p.parse('\x1B').empty());
    CHECK(p.parse(']').empty());
    CHECK(p.parse('g').empty());
    CHECK(p.parse('\x1B').empty());  // ESC inside OSC
    // '[' starts new CSI — discard OSC, enter CSI entry
    CHECK(p.parse('[').empty());
    auto const v = p.parse('H');     // CUP — move cursor to 0,0
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::move_cursor);
}

TEST_CASE("OSC — ESC BEL inside OSC terminates (ECMA-48 string terminator)", "[osc][st-esc-bel]") {
    vt_parser p;
    // Feed OSC 0;title, then ESC then BEL — should terminate and set title
    CHECK(p.parse('\x1B').empty());
    CHECK(p.parse(']').empty());
    CHECK(p.parse('2').empty());
    CHECK(p.parse(';').empty());
    CHECK(p.parse('t').empty());
    CHECK(p.parse('i').empty());
    CHECK(p.parse('t').empty());
    CHECK(p.parse('l').empty());
    CHECK(p.parse('e').empty());
    CHECK(p.parse('\x1B').empty());  // ESC inside OSC → osc_esc
    auto const v = p.parse('\x07');  // BEL → terminates OSC
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_window_title);
    CHECK(v[0].title == "title");
}

TEST_CASE("OSC — parser returns to ground after OSC", "[osc][recovery]") {
    vt_parser p;
    parse_sequence(p, "\x1B]0;hi\x07");
    auto const v = p.parse('A');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::write_char);
    CHECK(v[0].codepoint == 65);
}

TEST_CASE("OSC — byte-by-byte feeding", "[osc][multi-byte]") {
    vt_parser p;
    CHECK(p.parse('\x1B').empty());
    CHECK(p.parse(']').empty());
    CHECK(p.parse('0').empty());
    CHECK(p.parse(';').empty());
    CHECK(p.parse('x').empty());
    auto const v = p.parse('\x07');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_window_title);
    CHECK(v[0].title == "x");
}

// ===========================================================================
// Task 13 — IL: Insert Lines (CSI Ps L)
// ===========================================================================

TEST_CASE("CSI IL — no param defaults to count=1", "[csi][il]") {
    auto const v = parse_sequence("\x1B[L");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::insert_lines);
    CHECK(v[0].count == 1);
}

TEST_CASE("CSI IL — explicit count", "[csi][il]") {
    auto const v = parse_sequence("\x1B[5L");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::insert_lines);
    CHECK(v[0].count == 5);
}

TEST_CASE("CSI IL — zero param defaulted to 1", "[csi][il]") {
    auto const v = parse_sequence("\x1B[0L");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::insert_lines);
    CHECK(v[0].count == 1);
}

TEST_CASE("CSI IL — with intermediate byte after param", "[csi][il]") {
    auto const v = parse_sequence("\x1B[3 L");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::insert_lines);
    CHECK(v[0].count == 3);
}

// ===========================================================================
// Task 13 — DL: Delete Lines (CSI Ps M)
// ===========================================================================

TEST_CASE("CSI DL — no param defaults to count=1", "[csi][dl]") {
    auto const v = parse_sequence("\x1B[M");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::delete_lines);
    CHECK(v[0].count == 1);
}

TEST_CASE("CSI DL — explicit count", "[csi][dl]") {
    auto const v = parse_sequence("\x1B[3M");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::delete_lines);
    CHECK(v[0].count == 3);
}

TEST_CASE("CSI DL — zero param defaulted to 1", "[csi][dl]") {
    auto const v = parse_sequence("\x1B[0M");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::delete_lines);
    CHECK(v[0].count == 1);
}

// ===========================================================================
// Task 13 — SU: Scroll Up (CSI Ps S)
// ===========================================================================

TEST_CASE("CSI SU — no param defaults to count=1", "[csi][su]") {
    auto const v = parse_sequence("\x1B[S");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::scroll_up_page);
    CHECK(v[0].count == 1);
}

TEST_CASE("CSI SU — explicit count", "[csi][su]") {
    auto const v = parse_sequence("\x1B[4S");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::scroll_up_page);
    CHECK(v[0].count == 4);
}

// ===========================================================================
// Task 13 — SD: Scroll Down (CSI Ps T)
// ===========================================================================

TEST_CASE("CSI SD — no param defaults to count=1", "[csi][sd]") {
    auto const v = parse_sequence("\x1B[T");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::scroll_down_page);
    CHECK(v[0].count == 1);
}

TEST_CASE("CSI SD — explicit count", "[csi][sd]") {
    auto const v = parse_sequence("\x1B[2T");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::scroll_down_page);
    CHECK(v[0].count == 2);
}

// ===========================================================================
// Task 13 — DECSTBM: Set Scrolling Region (CSI Ps ; Ps r)
// ===========================================================================

TEST_CASE("CSI DECSTBM — set region", "[csi][decstbm]") {
    auto const v = parse_sequence("\x1B[3;10r");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_scroll_region);
    CHECK(v[0].row == 3);
    CHECK(v[0].col == 10);
}

TEST_CASE("CSI DECSTBM — reset with 0;0", "[csi][decstbm]") {
    auto const v = parse_sequence("\x1B[0;0r");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_scroll_region);
    CHECK(v[0].row == 0);
    CHECK(v[0].col == 0);
}

TEST_CASE("CSI DECSTBM — no params emits defaults (0, 0)", "[csi][decstbm]") {
    auto const v = parse_sequence("\x1B[r");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_scroll_region);
    // split_params returns [0] for empty buffer; row=0, col=0
    CHECK(v[0].row == 0);
    CHECK(v[0].col == 0);
}

TEST_CASE("CSI DECSTBM — top only, bottom defaults to 0", "[csi][decstbm]") {
    auto const v = parse_sequence("\x1B[5;r");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_scroll_region);
    CHECK(v[0].row == 5);
    CHECK(v[0].col == 0);
}

TEST_CASE("CSI DECSTBM — bottom only", "[csi][decstbm]") {
    auto const v = parse_sequence("\x1B[;20r");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_scroll_region);
    CHECK(v[0].row == 0);
    CHECK(v[0].col == 20);
}

TEST_CASE("CSI DECSTBM — with intermediate byte after param", "[csi][decstbm]") {
    auto const v = parse_sequence("\x1B[5;10 r");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::set_scroll_region);
    CHECK(v[0].row == 5);
    CHECK(v[0].col == 10);
}

// ===========================================================================
// Task 13 — Parser recovery after new sequences
// ===========================================================================

TEST_CASE("CSI IL/DL/SU/SD/DECSTBM — parser returns to ground", "[csi][task13][recovery]") {
    vt_parser p;
    parse_sequence(p, "\x1B[L");
    auto const v = p.parse('A');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::write_char);

    parse_sequence(p, "\x1B[3M");
    auto const v2 = p.parse('B');
    REQUIRE(v2.size() == 1);
    CHECK(v2[0].type == action_type::write_char);

    parse_sequence(p, "\x1B[S");
    auto const v3 = p.parse('C');
    REQUIRE(v3.size() == 1);
    CHECK(v3[0].type == action_type::write_char);

    parse_sequence(p, "\x1B[2T");
    auto const v4 = p.parse('D');
    REQUIRE(v4.size() == 1);
    CHECK(v4[0].type == action_type::write_char);

    parse_sequence(p, "\x1B[1;24r");
    auto const v5 = p.parse('E');
    REQUIRE(v5.size() == 1);
    CHECK(v5[0].type == action_type::write_char);
}

// ===========================================================================
// Task 14 — ICH (Insert Characters): CSI Ps @
// ===========================================================================

TEST_CASE("CSI ICH — explicit count", "[csi][task14][ich]") {
    auto const v = parse_sequence("\x1B[3@");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::insert_chars);
    CHECK(v[0].count == 3);
}

TEST_CASE("CSI ICH — no params defaults to 1", "[csi][task14][ich]") {
    auto const v = parse_sequence("\x1B[@");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::insert_chars);
    CHECK(v[0].count == 1);
}

TEST_CASE("CSI ICH — zero count defaults to 1", "[csi][task14][ich]") {
    auto const v = parse_sequence("\x1B[0@");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::insert_chars);
    CHECK(v[0].count == 1);
}

// ===========================================================================
// Task 14 — DCH (Delete Characters): CSI Ps P
// ===========================================================================

TEST_CASE("CSI DCH — explicit count", "[csi][task14][dch]") {
    auto const v = parse_sequence("\x1B[2P");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::delete_chars);
    CHECK(v[0].count == 2);
}

TEST_CASE("CSI DCH — no params defaults to 1", "[csi][task14][dch]") {
    auto const v = parse_sequence("\x1B[P");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::delete_chars);
    CHECK(v[0].count == 1);
}

// ===========================================================================
// Task 14 — ECH (Erase Characters): CSI Ps X
// ===========================================================================

TEST_CASE("CSI ECH — explicit count", "[csi][task14][ech]") {
    auto const v = parse_sequence("\x1B[5X");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_chars);
    CHECK(v[0].count == 5);
}

TEST_CASE("CSI ECH — no params defaults to 1", "[csi][task14][ech]") {
    auto const v = parse_sequence("\x1B[X");
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::erase_chars);
    CHECK(v[0].count == 1);
}

// ===========================================================================
// Task 14 — Parser recovery after ICH/DCH/ECH
// ===========================================================================

TEST_CASE("CSI ICH/DCH/ECH — parser returns to ground", "[csi][task14][recovery]") {
    vt_parser p;
    parse_sequence(p, "\x1B[3@");
    auto const v = p.parse('A');
    REQUIRE(v.size() == 1);
    CHECK(v[0].type == action_type::write_char);

    parse_sequence(p, "\x1B[2P");
    auto const v2 = p.parse('B');
    REQUIRE(v2.size() == 1);
    CHECK(v2[0].type == action_type::write_char);

    parse_sequence(p, "\x1B[5X");
    auto const v3 = p.parse('C');
    REQUIRE(v3.size() == 1);
    CHECK(v3[0].type == action_type::write_char);
}
