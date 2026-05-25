#include <catch2/catch_test_macros.hpp>
#include "util/utf8.hpp"

using namespace betty::util;

// ===========================================================================
// UTF-8 encoding
// ===========================================================================

TEST_CASE("UTF-8 — ASCII (1 byte)", "[utf8]") {
    CHECK(utf8_encode(0x41) == "A");            // 'A'
    CHECK(utf8_encode(0x20) == " ");            // space
    CHECK(utf8_encode(0x7F) == "\x7F");         // DEL (boundary)
}

TEST_CASE("UTF-8 — Danish lowercase (2 byte)", "[utf8]") {
    CHECK(utf8_encode(0xE6) == "\xC3\xA6");     // æ
    CHECK(utf8_encode(0xF8) == "\xC3\xB8");     // ø
    CHECK(utf8_encode(0xE5) == "\xC3\xA5");     // å
}

TEST_CASE("UTF-8 — Danish uppercase (2 byte)", "[utf8]") {
    CHECK(utf8_encode(0xC6) == "\xC3\x86");     // Æ
    CHECK(utf8_encode(0xD8) == "\xC3\x98");     // Ø
    CHECK(utf8_encode(0xC5) == "\xC3\x85");     // Å
}

TEST_CASE("UTF-8 — 2-byte boundary values", "[utf8]") {
    CHECK(utf8_encode(0x80) == "\xC2\x80");     // U+0080 (first 2-byte)
    CHECK(utf8_encode(0x7FF) == "\xDF\xBF");    // U+07FF (last 2-byte)
}

TEST_CASE("UTF-8 — 3-byte sequences", "[utf8]") {
    CHECK(utf8_encode(0x20AC) == "\xE2\x82\xAC");   // €  (U+20AC)
    CHECK(utf8_encode(0x0800) == "\xE0\xA0\x80");   // U+0800 (first 3-byte)
    CHECK(utf8_encode(0xFFFF) == "\xEF\xBF\xBF");   // U+FFFF (last 3-byte)
}

TEST_CASE("UTF-8 — 4-byte sequences", "[utf8]") {
    CHECK(utf8_encode(0x1F600) == "\xF0\x9F\x98\x80");  // 😀 (U+1F600)
    CHECK(utf8_encode(0x10000) == "\xF0\x90\x80\x80");  // U+10000 (first 4-byte)
    CHECK(utf8_encode(0x10FFFF) == "\xF4\x8F\xBF\xBF"); // U+10FFFF (last valid)
}

TEST_CASE("UTF-8 — invalid codepoints", "[utf8]") {
    // Surrogates
    CHECK(utf8_encode(0xD800).empty());
    CHECK(utf8_encode(0xDFFF).empty());
    // Mid-surrogate
    CHECK(utf8_encode(0xDC00).empty());
    // Beyond U+10FFFF
    CHECK(utf8_encode(0x110000).empty());
    CHECK(utf8_encode(0xFFFFFF).empty());
}

TEST_CASE("UTF-8 — null character", "[utf8]") {
    // U+0000 null — valid Unicode, encodes as single 0x00 byte
    CHECK(utf8_encode(0x00) == std::string("\0", 1));
}
