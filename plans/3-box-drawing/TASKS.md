# Box-Drawing & Block Element Vector Rendering — Task List

## Overview

Replace font-glyph rendering for Unicode box-drawing (U+2500–U+257F) and block element (U+2580–U+259F) characters with vector-rectangle rendering. Revert the ASCII stretch hack (`-`, `=`, `_`) since those aren't box-drawing characters.

---

## Task Summary

| Task | Description | Depends on | File |
|------|-------------|-----------|------|
| [1](task-1-revert-stretch-hack.md) | Revert ASCII stretch hack | — | text.cpp |
| [2](task-2-box-drawing-data.md) | Box-drawing / block-element lookup data | — | box_drawing.hpp/.cpp |
| [3](task-3-render-integration.md) | Integrate vector rendering into draw_grid() | 1, 2 | text.cpp, CMakeLists |
| [4](task-4-bold-handling.md) | Bold handling for vector-rendered chars | 3 | (no changes) |
| [5](task-5-tests.md) | Unit tests for box_drawing data | 2 | tests/ |
| [6](task-6-visual-validation.md) | Manual visual validation | 1–5 | (manual) |

## Execution Order

```
Task 1 (revert hack) ──┐
Task 2 (data tables) ──┼──► Task 3 (render integration) ──► Task 4 (bold, trivial) ──► Task 5 (tests) ──► Task 6 (visual validation)
```

Tasks 1 and 2 are independent and can be done in parallel. Task 3 depends on both. Tasks 4 and 5 depend on 3. Task 6 is manual.