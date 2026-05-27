# Task 6: Visual validation

## Goal

Manually verify that box-drawing and block-element characters render correctly in betty.

## Test Commands

Run these in the betty terminal and inspect the output:

### 6.1: Horizontal line
```bash
python -c "print('\u2500' * 60)"
```
**Expected:** A single unbroken horizontal line spanning 60 cells with no gaps at cell boundaries.

### 6.2: Vertical line
```bash
python -c "print('\n'.join(['\u2502'] * 20))"
```
**Expected:** A single unbroken vertical line with no gaps between rows.

### 6.3: Box corners and tees
```bash
python -c "print('\u250c\u2500\u2500\u2500\u2510\n\u2502   \u2502\n\u2514\u2500\u2500\u2500\u2518')"
```
**Expected:** A small rectangle with connected corners, no gaps.

### 6.4: Full cross
```bash
python -c "print('\u253c')"
```
**Expected:** A plus sign with arms touching all four cell edges.

### 6.5: Heavy and double lines
```bash
python -c "print('\u2501\u2501\u2501\u2501\n\u2503   \u2503\n\u2550\u2550\u2550\u2550\n\u2551   \u2551')"
```
**Expected:** Heavy lines (`━┃`) and double lines (`═║`) render with correct thickness and alignment.

### 6.6: Block elements
```bash
python -c "print('\u2588\u2588\u2588\n\u2593\u2592\u2591\n\u258c\u2580\u2590')"
```
**Expected:** `█` fills cells completely. `▓▒░` fall back to font rendering. `▌▀▐` show half/quarter blocks.

### 6.7: Normal ASCII characters unchanged
```bash
python -c "print('--- hello ---\n===\n___')"
```
**Expected:** Dashes, equals, underscores render normally from font glyphs (no stretching, no gaps expected).

### 6.8: htop or similar TUI
Run `htop` (if available) or any TUI app that uses box-drawing characters.
**Expected:** All borders and lines connect without visible seams.

## Success Criteria

- All horizontal and vertical box-drawing lines are visually seamless.
- Corners and tees connect properly.
- Block elements fill the correct fractions of their cells.
- ASCII characters (`-`, `=`, `_`) render normally from the font.
- No visual artifacts or rendering regressions compared to the pre-change baseline.