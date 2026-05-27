# TASK-16: Add 8px Terminal Padding

## Problem
Glyphs at row 0, col 0 render at pixel (0,0) — the left and top edges of characters touch the window border. The bottom and right edges also have no margin.

## Solution
Add 8px of uniform padding on all four sides. Two changes work together:

1. **Grid sizing**: subtract `2 × padding` from the client area before computing cols/rows. This ensures the grid fits within the padded region.
2. **Rendering offset**: shift all `draw_grid()` pixel positions by `+padding` so the grid is visually inset from the window edges.

The padding area naturally shows the clear color (Catppuccin Mocha base `#1e1e2e`), which is already what `begin_frame()` fills the swap chain with.

## Design Decisions
| Decision | Choice |
|----------|--------|
| Padding shape | Uniform on all 4 sides |
| Padding behavior on resize | Fixed 8px — grid shrinks/grows to fit remaining area |
| Grid sizing owner | Application layer (`application.cpp`) |
| Rendering offset | Passed from app → renderer_context → glyph_renderer |
| Padding value | Hardcoded constant |
| Padding fill | Background clear color (no extra work needed) |
| Min window size | Increased to account for padding |
| Edge case (window < padding) | Prevented by minimum window size |

## Files to Modify

### 1. `src/platform/types.hpp`
- Add `inline constexpr uint32_t k_padding_px = 8;`

### 2. `src/platform/text.hpp`
- Add `uint32_t padding` parameter to `glyph_renderer::draw_grid()`

### 3. `src/platform/text.cpp`
- In `draw_grid()`: offset every `x0`, `y0`, `x1`, `y1` by `+padding`
- Also offset underline and strikethrough decoration positions

### 4. `src/platform/renderer_context.hpp` and `renderer_context.cpp`
- `draw_grid()`: add `uint32_t padding` parameter, forward to glyph_renderer

### 5. `src/application.cpp`
- `make_application()`: compute cols/rows using `(client_size - 2*padding) / cell_size`
- `make_application()`: set min window size with padding included
- `on_resize()`: compute cols/rows using `(client_size - 2*padding) / cell_size`
- `run()`: pass `k_padding_px` to `renderer_ctx_.draw_grid()`

## What Does NOT Change
- Swap chain size (still full client area)
- `begin_frame()` / ClearRenderTargetView (padding area shows clear color naturally)
- Vertex shader, constant buffer, viewport
- Scrollback, cursor, input handling, shell integration
- `draw()` and `draw_text()` (debug rendering paths)

## Acceptance Criteria
1. Launch betty — glyphs at all four edges have visible space between them and the window border
2. Resize the window — padding remains 8px, grid rows/cols adjust correctly
3. Window cannot be shrunk below the point where at least 1 col × 1 row fits with padding
4. The padding area matches the terminal background color (no visible seam)
5. Scrollback and cursor behavior are unchanged
