# Task 5: Tests for box_drawing data

## Goal

Add unit tests to verify the box-drawing data tables and lookup functions are correct.

## Test Cases

Create `tests/box_drawing_tests.cpp` using Catch2 (already in the project):

### 5.1: is_box_drawing_or_block()

| Input | Expected | Reason |
|-------|----------|--------|
| U+2500 `─` | true | Light horizontal — in range |
| U+253C `┼` | true | Light cross — in range |
| U+2588 `█` | true | Full block — in range |
| U+2595 `▕` | true | Right 1/8 — in range |
| U+2571 `╱` | false | Diagonal — excluded |
| U+2572 `╲` | false | Diagonal — excluded |
| U+2591 `░` | false | Light shade — excluded |
| U+2592 `▒` | false | Medium shade — excluded |
| U+2593 `▓` | false | Dark shade — excluded |
| U+002D `-` | false | ASCII hyphen |
| U+0041 `A` | false | Latin letter |
| U+1F600 😀 | false | Emoji |

### 5.2: get_box_drawing_rects() — Box Drawing

| Codepoint | Check |
|-----------|-------|
| U+2500 `─` | Returns 3 rects (left arm, right arm, center). Center connects the two arms. |
| U+2502 `│` | Returns 3 rects (up arm, down arm, center). |
| U+250C `┌` | Returns 3 rects (right arm, down arm, center). No up arm, no left arm. |
| U+253C `┼` | Returns 5 rects (4 arms + center). |
| U+2571 `╱` | Returns 0 (diagonal, falls back to font). |
| U+2504 `┄` | Returns 0 (dashed, falls back to font). |

### 5.3: get_box_drawing_rects() — Block Elements

| Codepoint | Expected result |
|-----------|----------------|
| U+2588 `█` | Single rect `{0, 0, 1, 1}` |
| U+2580 `▀` | Single rect `{0, 0, 1, 0.5}` |
| U+2584 `▄` | Single rect `{0, 0.5, 1, 1}` |
| U+258C `▌` | Single rect `{0, 0, 0.5, 1}` |
| U+2598 `▘` | Single rect `{0, 0, 0.5, 0.5}` |
| U+259A `▚` | Two rects (upper-left + lower-right quadrants) |

### 5.4: Rectangle validity

For every codepoint that returns > 0 rects, verify:
- `left < right`
- `top < bottom`
- All values are in [0.0, 1.0]

## File

- `tests/box_drawing_tests.cpp`
- Update `tests/CMakeLists.txt` to include the new test file

## Dependencies

Task 2 must be complete (the module being tested must exist).