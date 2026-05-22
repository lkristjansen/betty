# TASK-5: Unit Test Suite for `vt_parser` with Catch2

## Design Decisions

| Decision | Choice |
|---|---|
| Catch2 acquisition | CMake `FetchContent` (v3.12.0) |
| Link strategy | Link against `terminal` static library |
| Test organization | Single test executable, one `.cpp` per source under test |
| Private state access | Black-box only — test via public `parse()` interface |
| CTest | `enable_testing()` + `add_test()`, with dedicated `test-debug` preset |
| Directory | `tests/` at repository root |

---

## Step 1 — Create `tests/` directory structure

```
tests/
├── CMakeLists.txt
└── vt_parser_test.cpp
```

---

## Step 2 — Add test gating + FetchContent for Catch2 to root `CMakeLists.txt`

In `./CMakeLists.txt`, after `project(…)` and before `add_subdirectory(src)`, add:

```cmake
option(BUILD_TESTING "Build unit tests" OFF)

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.12.0
    )
    FetchContent_MakeAvailable(Catch2)
    enable_testing()
    add_subdirectory(tests)
endif()
```

---

## Step 3 — Create `tests/CMakeLists.txt`

```cmake
add_executable(betty_tests
    vt_parser_test.cpp
)

target_link_libraries(betty_tests PRIVATE
    terminal
    Catch2::Catch2WithMain
)

target_compile_features(betty_tests PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(betty_tests PRIVATE /W4 /WX /utf-8)
endif()

add_test(NAME betty_tests COMMAND betty_tests)
```

---

## Step 4 — Add `test-debug` preset to `CMakePresets.json`

### Configure preset (add to `configurePresets` array)

```json
{
    "name": "test-debug",
    "displayName": "Test Debug",
    "inherits": "debug",
    "cacheVariables": {
        "BUILD_TESTING": "ON"
    }
}
```

### Build preset (add to `buildPresets` array)

```json
{
    "name": "test-debug",
    "displayName": "Build Test Debug",
    "configurePreset": "test-debug",
    "configuration": "Debug"
}
```

---

## Step 5 — Write `tests/vt_parser_test.cpp`

All tests are **black-box** — they only call the public `parse(unsigned char)` method. No modifications to production code.

### 5a — Ground state: printable characters & C0 controls

| # | Test name | Input | Expected |
|---|---|---|---|
| 1 | `\r` produces `carriage_return` | `parse('\r')` | `action{carriage_return}` |
| 2 | `\n` produces `newline` | `parse('\n')` | `action{newline}` |
| 3 | Printable char produces `write_char` | `parse('A')` | `action{write_char, codepoint=65}` |
| 4 | Two consecutive printable chars | `parse('z')` → `parse('!')` | Two `write_char` actions with codepoints `122`, `33` |
| 5 | C0 control 0x01 (SOH) silently ignored | `parse(0x01)` | `std::nullopt` |
| 6 | C0 control 0x08 (BS) silently ignored | `parse(0x08)` | `std::nullopt` |
| 7 | C0 control 0x1A (SUB) silently ignored | `parse(0x1A)` | `std::nullopt` |

### 5b — Escape state

| # | Test name | Input bytes | Expected |
|---|---|---|---|
| 8 | ESC `[` enters CSI — no action yet | `parse(0x1B)`, `parse('[')` | Both `nullopt` |
| 9 | DECSC (ESC 7) saves cursor, returns to ground, no action | `parse(0x1B)`, `parse('7')` | Both `nullopt` |
| 10 | DECRC (ESC 8) restores cursor to default (0,0) | `parse(0x1B)`, `parse('8')` | Second → `move_cursor{row=0, col=0}` |
| 11 | Unknown ESC sequence discarded, state recovers | `parse(0x1B)`, `parse('X')`, `parse('A')` | First two `nullopt`, third → `write_char('A')` |

### 5c — CSI relative cursor movement: CUU (A), CUD (B), CUF (C), CUB (D)

| # | Test name | Input bytes | Expected |
|---|---|---|---|
| 12 | CUU — no param defaults to count=1 | `0x1B`, `[`, `A` | `move_cursor_up{count=1}` |
| 13 | CUU — explicit count | `0x1B`, `[`, `5`, `A` | `move_cursor_up{count=5}` |
| 14 | CUU — param 0 defaulted to 1 | `0x1B`, `[`, `0`, `A` | `move_cursor_up{count=1}` |
| 15 | CUD | `0x1B`, `[`, `3`, `B` | `move_cursor_down{count=3}` |
| 16 | CUF — multi-digit param | `0x1B`, `[`, `1`, `0`, `C` | `move_cursor_forward{count=10}` |
| 17 | CUB | `0x1B`, `[`, `4`, `D` | `move_cursor_back{count=4}` |

### 5d — CSI absolute cursor positioning: CUP (H), HVP (f)

| # | Test name | Input bytes | Expected |
|---|---|---|---|
| 18 | CUP — no params defaults to 1;1 (0-based: row=0, col=0) | `0x1B`, `[`, `H` | `move_cursor{row=0, col=0}` |
| 19 | CUP — row only (col defaults) | `0x1B`, `[`, `5`, `H` | `move_cursor{row=4, col=0}` |
| 20 | CUP — row and col | `0x1B`, `[`, `5`, `;`, `1`, `0`, `H` | `move_cursor{row=4, col=9}` |
| 21 | CUP — param 0 treated as 1 | `0x1B`, `[`, `0`, `;`, `0`, `H` | `move_cursor{row=0, col=0}` |
| 22 | HVP (`f`) — same semantics as CUP | `0x1B`, `[`, `8`, `;`, `3`, `f` | `move_cursor{row=7, col=2}` |
| 23 | CUP — empty first param defaults | `0x1B`, `[`, `;`, `1`, `0`, `H` | `move_cursor{row=0, col=9}` |
| 24 | CUP — empty second param defaults | `0x1B`, `[`, `5`, `;`, `H` | `move_cursor{row=4, col=0}` |

### 5e — Incremental byte feeding

| # | Test name | Sequence | Expected |
|---|---|---|---|
| 25 | CUP fed byte-by-byte | ESC `[` `1` `2` `;` `3` `4` `H` | First 7 → nullopt, 8th → `move_cursor{row=11, col=33}` |
| 26 | State resets after complete CSI | ESC `[` `H` `X` | ESC, `[`, `H` → nullopt, `nullopt`, `move_cursor(0,0)`; then `parse('X')` → `write_char('X')` |

### 5f — DECSC / DECRC round-trip

| # | Test name | Sequence | Expected |
|---|---|---|---|
| 27 | Save → restore defaults | ESC 7, ESC 8 | Restore → `move_cursor{row=0, col=0}` |
| 28 | Move → save → move → restore | CUP(5,10), ESC 7, CUP(3,3), ESC 8 | Final ESC 8 → `move_cursor{row=4, col=9}` |

### 5g — Invalid sequences don't corrupt state

| # | Test name | Sequence | Expected |
|---|---|---|---|
| 29 | Unknown CSI final byte discarded | ESC `[` `Z`, then `A` | CSI → nullopt; `A` → `write_char('A')` |
| 30 | Garbage byte during CSI params resets | ESC `[` `5` `0x01` | CSI discarded, state → ground |
| 31 | Garbage byte during escape resets | ESC `!` | Discarded, state → ground |
| 32 | Back-to-back valid sequences | ESC `[` `3` `A`, ESC `[` `5` `;` `2` `H`, `\n` | Three distinct correct actions in sequence |

### 5h — CSI intermediate bytes

| # | Test name | Input | Expected |
|---|---|---|---|
| 33 | Single intermediate byte before final | ESC `[` `?` `H` | `move_cursor{row=0, col=0}` |
| 34 | Multiple intermediate bytes | ESC `[` `?` `$` `H` | `move_cursor{row=0, col=0}` |

---

## Step 6 — Build and run

```bash
# Configure
cmake --preset test-debug

# Build the test target
cmake --build --preset test-debug --target betty_tests

# Run via CTest
cd build/debug && ctest --output-on-failure

# Or run the test executable directly (for verbose Catch2 output)
./build/debug/tests/betty_tests.exe -v
```

---

## Summary

- **6 steps**, zero changes to production code
- **34 test cases** across 8 behavior areas
- All tests are black-box — they exercise only the public `parse()` method
- Covers: every state, every sequence type, parameter edge cases, error recovery, DECSC/DECRC round-trip, intermediate bytes, incremental byte feeding, and state corruption resistance
