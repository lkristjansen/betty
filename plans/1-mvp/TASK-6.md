# Task 6 — SGR Colours

## Summary of design decisions

| Decision | Choice |
|----------|--------|
| Color type | `struct { uint8_t r, g, b; uint8_t flags; }` — 4 bytes, bit 0 = `is_default` |
| Rendering | Single interleaved vertex buffer; UV discriminator (negative U = solid-colour bg quad, positive U = alpha-textured fg quad) |
| 256-colour | Included |
| Catppuccin palette | 16 ANSI slots mapped as documented below |
| Bold-as-bright | **Not** implemented (SGR 1 does not affect colour) |
| SGR reset | Stateful only — resets the "current pen" for future writes, existing cells unchanged |
| Non-colour SGR (bold, italic, underline, …) | Parsed and silently ignored for now; deferred to Task 12 |

---

## Step 1 — Define colour types and Catppuccin Mocha palettes

**File:** `src/terminal/grid.hpp`

### 1a. Define `rgb_color`

```cpp
struct rgb_color {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t flags = 0;  // bit 0: is_default (1 = use terminal default colour)
};

// Sentinel constructors
constexpr rgb_color default_fg() { return {0, 0, 0, 1}; }
constexpr rgb_color default_bg() { return {0, 0, 0, 1}; }
```

### 1b. Define the 16-colour Catppuccin Mocha ANSI palette

```cpp
inline constexpr std::array<rgb_color, 16> catppuccin_palette = {{
  /*  0 */ {0x45, 0x47, 0x5a, 0},  // surface1
  /*  1 */ {0xf3, 0x8b, 0xa8, 0},  // red
  /*  2 */ {0xa6, 0xe3, 0xa1, 0},  // green
  /*  3 */ {0xf9, 0xe2, 0xaf, 0},  // yellow
  /*  4 */ {0x89, 0xb4, 0xfa, 0},  // blue
  /*  5 */ {0xcb, 0xa6, 0xf7, 0},  // mauve
  /*  6 */ {0x94, 0xe2, 0xd5, 0},  // teal
  /*  7 */ {0xba, 0xc2, 0xde, 0},  // subtext1
  /*  8 */ {0x58, 0x5b, 0x70, 0},  // surface2
  /*  9 */ {0xf3, 0x8b, 0xa8, 0},  // red (same as 1)
  /* 10 */ {0xa6, 0xe3, 0xa1, 0},  // green (same as 2)
  /* 11 */ {0xf9, 0xe2, 0xaf, 0},  // yellow (same as 3)
  /* 12 */ {0x89, 0xb4, 0xfa, 0},  // blue (same as 4)
  /* 13 */ {0xcb, 0xa6, 0xf7, 0},  // mauve (same as 5)
  /* 14 */ {0x94, 0xe2, 0xd5, 0},  // teal (same as 6)
  /* 15 */ {0xa6, 0xad, 0xc8, 0},  // subtext0
}};

// Default terminal colours
inline constexpr rgb_color k_default_fg_color = {0xcd, 0xd6, 0xf4, 0};  // text
inline constexpr rgb_color k_default_bg_color = {0x1e, 0x1e, 0x2e, 0};  // base (for reference only — default bg = transparent)
```

### 1c. 256-colour lookup function

```cpp
constexpr rgb_color xterm_256_color(uint8_t index) {
  if (index < 16) return catppuccin_palette[index];
  if (index < 232) {
    index -= 16;
    uint8_t r = (index / 36) * 51;
    uint8_t g = ((index / 6) % 6) * 51;
    uint8_t b = (index % 6) * 51;
    return {r, g, b, 0};
  }
  uint8_t gray = (index - 232) * 10 + 8;
  return {gray, gray, gray, 0};
}
```

### 1d. Add colour fields to `grid_cell`

```cpp
struct grid_cell {
  char32_t codepoint = U' ';
  rgb_color fg = default_fg();
  rgb_color bg = default_bg();
};
```

---

## Step 2 — Extend the action system

**File:** `src/terminal/vt_parser.hpp`

### 2a. Add action types

```cpp
enum class action_type : uint8_t {
  // ... existing ...
  sgr_reset,    // reset fg/bg to defaults (SGR 0 / 39 / 49)
  sgr_set_fg,   // set foreground colour
  sgr_set_bg,   // set background colour
};
```

### 2b. Add colour payload to `action`

```cpp
struct action {
  action_type type = action_type::write_char;
  char32_t codepoint = 0;
  uint32_t count     = 1;
  uint32_t row       = 0;
  uint32_t col       = 0;
  rgb_color color{};  // payload for sgr_set_fg / sgr_set_bg
};
```

---

## Step 3 — Parse SGR sequences in the VT parser

**File:** `src/terminal/vt_parser.cpp`

### 3a. Add `m` to the dispatch switch

In `vt_parser::dispatch()`, add a case for `'m'`:

```
case 'm': → parse SGR parameters → return action{ sgr_set_fg / sgr_set_bg / sgr_reset }
```

### 3b. SGR parameter parsing logic

The CSI `m` sequence carries a semicolon-separated list of parameters. Examples:

| Input | Meaning |
|-------|---------|
| `0` | Reset all |
| `31` | Set fg to ANSI 1 (red) |
| `44` | Set bg to ANSI 4 (blue) |
| `31;44` | Set both |
| `38;5;196` | Set fg to 256-colour index 196 |
| `48;2;128;64;32` | Set bg to true-colour (128, 64, 32) |

Parsing algorithm:

1. Split the accumulated `param_buffer_` on `;` into individual tokens.
2. Walk tokens left to right.
3. For each token:
   - If `0`: emit `sgr_reset`, skip remaining params.
   - If `30-37`: emit `sgr_set_fg` with `catppuccin_palette[n - 30]`.
   - If `40-47`: emit `sgr_set_bg` with `catppuccin_palette[n - 40]`.
   - If `90-97`: emit `sgr_set_fg` with `catppuccin_palette[(n - 90) + 8]`.
   - If `100-107`: emit `sgr_set_bg` with `catppuccin_palette[(n - 100) + 8]`.
   - If `38` (extended fg): look ahead at next token:
     - `2` → true colour — consume R, G, B from next 3 tokens → emit `sgr_set_fg`.
     - `5` → 256-colour — consume N from next token → emit `sgr_set_fg` with `xterm_256_color(N)`.
   - If `48` (extended bg): same logic as `38`, but emits `sgr_set_bg`.
   - If `39`: emit `sgr_set_fg` with `default_fg()`.
   - If `49`: emit `sgr_set_bg` with `default_bg()`.
   - Any other value (1-9, 21-29, etc.): silently ignore (for Task 12).

Since `dispatch` currently returns a single `std::optional<action>`, but SGR `m` can produce multiple actions (e.g., `31;44` → two colour sets), two approaches:

**Chosen approach: process all params inside dispatch and return only the last action.** The intermediate state (`current_fg_` / `current_bg_`) accumulates inside the grid, not the parser. Actually, a cleaner approach: have `dispatch` for `m` directly mutate the grid's state through a callback, or return a vector. 

**Simplest approach that works:** Make `dispatch` for `m` return only the *first* colour action (or reset), and leave the remaining params buffered... No, that's fragile.

**Better approach:** Change `vt_parser::parse()` to potentially return multiple actions via a callback. Or store pending SGR state in the parser and have `parse()` drain it. 

**Best approach for minimal churn:** Have the `m` handler in `dispatch` update a *parser-side* `pending_fg_` / `pending_bg_` and return a *sentinel* action `sgr_flush`. The grid's `apply()` then reads these parser-side fields. But the parser doesn't own the grid.

**Simplest approach:** Modify `dispatch` to accept a callback `void emit(action)` instead of returning `optional<action>`. This lets `m` emit multiple actions. The state machine in `parse()` calls the callback. This is a clean refactor.

Wait — let's reconsider. The current `parse()` signature is:

```cpp
auto parse(unsigned char byte) -> std::optional<action>;
```

This returns at most one action per byte — fine for cursor movement and character writes, but SGR `m` needs to emit multiple actions from a single final byte.

**Decision: Change `parse()` return type to `std::vector<action>`.** 

This is the simplest change:
- Return type changes from `std::optional<action>` to `std::vector<action>`.
- Existing cases wrap their single action in a vector.
- The `m` handler pushes multiple actions.
- `grid::write_bytes()` iterates over the vector.

This avoids callbacks, keeps the interface simple, and the heap allocation per parse byte is negligible since most parses return 0-1 actions and SGR sequences are infrequent.

### 3c. Implement token parsing

Add a helper to split the parameter buffer and iterate tokens:

```cpp
// Parse SGR 'm' command. Returns a vector of colour actions.
auto dispatch_sgr() -> std::vector<action>;
```

The token walker handles the lookahead for `38;2;…` and `48;2;…` sequences by storing params in a `std::vector<uint32_t>` and using an index cursor.

---

## Step 4 — Track SGR state in terminal_grid and apply to cells

**File:** `src/terminal/grid.cpp`

### 4a. Add SGR state members

```cpp
class terminal_grid {
  // ...
  rgb_color current_fg_ = default_fg();
  rgb_color current_bg_ = default_bg();
};
```

### 4b. Handle new actions in `apply()`

```cpp
case action_type::sgr_reset:
  current_fg_ = default_fg();
  current_bg_ = default_bg();
  break;
case action_type::sgr_set_fg:
  current_fg_ = a.color;
  break;
case action_type::sgr_set_bg:
  current_bg_ = a.color;
  break;
```

### 4c. Apply colours in `write_char()`

```cpp
void terminal_grid::write_char(char32_t cp) {
  if (cursor_col_ < cols_) {
    auto& cell = cells_[cursor_row_ * cols_ + cursor_col_];
    cell.codepoint = cp;
    cell.fg = current_fg_;
    cell.bg = current_bg_;
  }
  // ... cursor advance and scroll logic unchanged
}
```

---

## Step 5 — Switch atlas to alpha-only

**File:** `src/platform/text.cpp`

### 5a. Remove pre-baked RGBA from rasterization

Current code in `rasterize_glyph()` writes `(k_fg_r, k_fg_g, k_fg_b, alpha)`. Change to `(255, 255, 255, alpha)` — pure white glyphs on transparent background. The colour multiplication happens in the pixel shader.

Remove the constants:
```cpp
// Remove:
// inline constexpr uint8_t k_fg_r = 0xCD;
// inline constexpr uint8_t k_fg_g = 0xD6;
// inline constexpr uint8_t k_fg_b = 0xF4;
```

In `rasterize_glyph()`, change:
```cpp
staging_buffer[pixel_offset + 0] = 255;  // was k_fg_r
staging_buffer[pixel_offset + 1] = 255;  // was k_fg_g
staging_buffer[pixel_offset + 2] = 255;  // was k_fg_b
staging_buffer[pixel_offset + 3] = alpha;
```

### 5b. Update vertex format

```cpp
struct glyph_vertex {
  float x, y;    // pixel position (top-left origin)
  float u, v;    // texture coordinates (negative u → bg quad)
  float r, g, b; // colour (0–1, pre-normalized before upload)
  float pad;     // alignment to 32 bytes
};
static_assert(sizeof(glyph_vertex) == 32,
              "glyph_vertex must be 32 bytes for D3D11 alignment");
```

### 5c. Update input layout

```cpp
D3D11_INPUT_ELEMENT_DESC layout[] = {
  { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,  0, 0,                             D3D11_INPUT_PER_VERTEX_DATA, 0 },
  { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,  0, D3D11_APPEND_ALIGNED_ELEMENT,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
  { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT,0, D3D11_APPEND_ALIGNED_ELEMENT,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
};
```

### 5d. Update vertex and pixel shaders

**Vertex shader** — pass colour through to PS:

```hlsl
cbuffer Constants : register(b0) {
  float2 window_size;
  float2 cell_size;
  float2 inv_tex_size;
};

struct VS_INPUT {
  float2 position : POSITION;
  float2 uv       : TEXCOORD;
  float3 color    : COLOR;
};

struct VS_OUTPUT {
  float4 position : SV_POSITION;
  float2 uv       : TEXCOORD;
  float3 color    : COLOR;
};

VS_OUTPUT main(VS_INPUT input) {
  VS_OUTPUT output;
  output.position.x = (input.position.x / window_size.x) * 2.0 - 1.0;
  output.position.y = 1.0 - (input.position.y / window_size.y) * 2.0;
  output.position.zw = float2(0.0, 1.0);
  output.uv = input.uv;
  output.color = input.color;
  return output;
}
```

**Pixel shader** — UV discriminator (negative u = bg quad, positive = fg glyph):

```hlsl
Texture2D<float4> glyph_atlas : register(t0);
SamplerState linear_sampler  : register(s0);

struct VS_OUTPUT {
  float4 position : SV_POSITION;
  float2 uv       : TEXCOORD;
  float3 color    : COLOR;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
  if (input.uv.x < 0.0) {
    // Background quad: solid colour with full alpha.
    return float4(input.color, 1.0);
  } else {
    // Foreground glyph: multiply atlas alpha by vertex colour.
    float alpha = glyph_atlas.Sample(linear_sampler, input.uv).a;
    return float4(input.color * alpha, alpha);
  }
}
```

Note: the atlas must be re-created (re-baked) with the new white-on-transparent rasterization, and the SRV format must remain `DXGI_FORMAT_R8G8B8A8_UNORM`.

---

## Step 6 — Update renderer for per-cell colours

**File:** `src/platform/text.cpp`

### 6a. Change `draw_grid()` signature

Currently `draw_grid()` takes `std::span<const char32_t>` — the codepoint-only array. It needs the full `grid_cell` data.

```cpp
[[nodiscard]] auto draw_grid(
  d3d_device const& device,
  d3d_render_target_view const& rtv,
  std::span<const grid_cell> cells,  // was std::span<const char32_t>
  uint32_t cols,
  uint32_t rows
) const -> std::expected<void, std::error_code>;
```

**File:** `src/platform/text.hpp` — update declaration.

### 6b. Implement per-cell quad emission

For each cell in the grid (row-major, within `draw_rows` × `draw_cols`):

```
if cell.bg is NOT default:
  emit a bg quad:
    - position: cell rect (x0, y0) → (x1, y1)
    - uv: (-1.0f, 0.0f, -1.0f, 0.0f)  // negative u = bg discriminator
    - color: cell.bg normalized to 0–1 floats

if cell.codepoint is NOT space (U' '):
  emit a fg quad:
    - position: same cell rect
    - uv: from glyph_slots[glyph_index] (normal UV)
    - color: resolved fg (if fg.is_default → k_default_fg_color, else cell.fg)
```

The colour resolution helper:

```cpp
rgb_color resolve_fg(rgb_color c) {
  if (c.flags & 1) return k_default_fg_color;  // is_default → terminal default
  return c;
}
rgb_color resolve_bg(rgb_color c) {
  // default bg → transparent (handled by skipping the bg quad entirely)
  return c;
}
```

### 6c. Quads per cell budget

With both bg + fg per cell, worst case is `max_rows × max_cols × 2` quads. At 18px font, the default 960×600 window is ~53 cols × 33 rows = 1,749 cells → 3,498 quads. Well within `k_max_glyphs_per_frame = 8192`.

### 6d. Handle ASCII mapping

Non-ASCII codepoints (> 127) still map to `'?'` (index 63) since we only have an ASCII atlas. The atlas doesn't change size — still 128 glyphs.

---

## Step 7 — Update main.cpp integration

**File:** `src/main.cpp`

### 7a. Change codepoint extraction call

```cpp
// Old:
auto const cps = grid.codepoints();
if (!cps.empty()) {
  if (auto draw_result = renderer.draw_grid(device, rtv, cps, grid.cols(), grid.rows());
      !draw_result) { ... }
}

// New:
auto const cells = grid.cells();
if (!cells.empty()) {
  if (auto draw_result = renderer.draw_grid(device, rtv, cells, grid.cols(), grid.rows());
      !draw_result) { ... }
}
```

### 7b. Include `grid.hpp` for `grid_cell`

Already included. No new headers needed.

---

## Step 8 — Build, test, and verify

### Test cases

1. **`echo -e "\e[31mred text\e[0m"`** — should show "red text" in Catppuccin red, then revert to default.
2. **`echo -e "\e[44mblue background\e[0m"`** — blue background behind text.
3. **`echo -e "\e[38;2;255;128;0morange text\e[0m"`** — true-colour orange.
4. **`echo -e "\e[48;2;50;50;100mdark bg\e[0m"`** — true-colour background.
5. **`echo -e "\e[38;5;196mred via 256\e[0m"`** — 256-colour foreground.
6. **`Get-ChildItem`** (PowerShell `ls`) — should show default colourised output.
7. **`git diff`** (from a git repo) — diff hunks should be coloured.
8. **`ping -t localhost` then Ctrl+C** — no crash, colour state preserved/cleaned.
9. **`clear`** — doesn't clear colours yet (Task 8), but no crash.

### Potential issues to watch for

- **SGR sequences split across ConPTY reads**: The VT parser is byte-by-byte, so partial sequences are already handled by the state machine. The `m` handler must tolerate empty/broken parameter lists.
- **UV discriminator precision**: Negative zero vs positive zero — use `-1.0f` explicitly, not `-0.0f`.
- **Quad budget overflow**: Add a safety clamp — stop emitting quads if `quad_count >= k_max_glyphs_per_frame`, logging a warning.
- **Atlas format**: The atlas texture format doesn't change (still `R8G8B8A8_UNORM`), only the data written to it changes.
- **Background vs clear colour**: The window's base clear colour (`mocha_base`) is `#1e1e2e`. Default-bg cells render nothing (transparent), letting the clear colour show through — correct.

---

## Files changed

| File | Change summary |
|------|---------------|
| `src/terminal/grid.hpp` | Add `rgb_color`, `catppuccin_palette`, `xterm_256_color()`, `default_fg()`/`default_bg()`, colour fields to `grid_cell` |
| `src/terminal/grid.cpp` | Add SGR state members, handle `sgr_reset`/`sgr_set_fg`/`sgr_set_bg` in `apply()`, copy colours in `write_char()` |
| `src/terminal/vt_parser.hpp` | Add `sgr_reset`/`sgr_set_fg`/`sgr_set_bg` to `action_type`, add `rgb_color color` to `action`, change `parse()` return type to `std::vector<action>` |
| `src/terminal/vt_parser.cpp` | Add `dispatch_sgr()`, handle CSI `m` in dispatch, implement token-based SGR parameter parsing |
| `src/platform/text.hpp` | Update `draw_grid()` signature to `std::span<const grid_cell>`, declare new colour constants |
| `src/platform/text.cpp` | Update `glyph_vertex` (add colour), update shaders, update atlas rasterization to alpha-only, update input layout, re-implement `draw_grid()` with per-cell bg+fg quad emission |
| `src/main.cpp` | Change `codepoints()` → `cells()` call |
