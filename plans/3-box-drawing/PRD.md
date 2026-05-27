# Box-Drawing & Block Element Vector Rendering — PRD

## Problem

When Betty renders characters like `─`, `│`, `┌`, `├`, or `-`, `=`, `_`, it rasterizes them from the font's glyph atlas. Because fonts place the ink for these characters within glyph bounds that don't fill the entire cell, gaps appear between consecutive characters. A line of `─────────` renders with visible seams at each cell boundary, and corner-junction characters like `┌` have misaligned strokes.

Windows Terminal solves this by rendering box-drawing and block-element characters with **vector primitives** (axis-aligned rectangles) drawn at exact pixel positions, ensuring strokes connect perfectly at cell boundaries regardless of font metrics.

## Goal

Replace the current bitmap-stretching hack (which was applied to ASCII `-`, `=`, `_`) with a proper vector renderer for Unicode box-drawing characters (U+2500–U+257F) and block elements (U+2580–U+259F). These characters will be rendered as solid-color rectangles using the existing background-quad rendering path — no glyph texture lookup needed.

## Scope

### In Scope — Unicode Box-Drawing & Block Elements

**Box-drawing characters (U+2500–U+257F):** Horizontal lines, vertical lines, corners, tees, crosses, dashes, and diagonals. These all compose from up to 4 directional strokes (up, down, left, right) plus optional dots/dashes.

**Block elements (U+2580–U+259F):** Full block, half blocks, quadrants, left/right blocks of various fractions. These define rectangles that tile the cell in predictable ways.

### Out of Scope

- ASCII characters (`-`, `=`, `_`) — revert to normal font rasterization (no stretching).
- Diagonal box-drawing characters (U+2571–U+2572) — these need line rendering at angles, which is more complex; leave as font glyphs for now.
- Graphic characters outside U+2500–U+259F — no changes.
- Braille patterns (U+2800–U+28FF) — possible future enhancement.

## Design

### Core Idea

Each box-drawing or block-element character can be decomposed into a set of **filled rectangles** relative to the cell boundary. These rectangles are drawn as solid-color background quads (using the existing `emit_bg_quad` path) in the foreground color — no texture sampling, no atlas lookup.

For box-drawing characters, each character specifies which of 4 directional arms are present (up, down, left, right) and whether those arms are light, heavy, or double. Each arm is a thin rectangle that starts at the cell center and extends to the cell edge. A central square fills the intersection.

For block elements, each character maps directly to 1–2 rectangles covering the specified fraction of the cell.

### Rendering Integration

In `draw_grid()`, before the glyph-lookup path, check if `cell.codepoint` falls in the box-drawing or block-element range. If so:
1. Emit the background quad as usual.
2. Look up the decomposition for that codepoint.
3. Emit foreground-colored quads for each rectangle in the decomposition.
4. Skip the glyph-texture emission for that cell entirely (no atlas lookup, no bold double-draw).

This keeps the change confined to the render loop in `text.cpp` — no new shaders, no new textures, nochanges to the grid or parser.

### Sub-pixel Alignment

All rectangle coordinates are computed from the cell's integer pixel bounds. Stroke positions and thicknesses are derived from `cell_width` and `cell_height` at renderer creation time, ensuring consistent alignment across cells.

### Line Thickness

- **Light** strokes: 1px (or 2px on high-DPI where cell dimensions are large enough)
- **Heavy** strokes: 2× light thickness
- **Double** lines: two parallel strokes separated by a gap (≈1px gap)

## Implementation Plan

See [TASKS.md](TASKS.md) for the task breakdown.