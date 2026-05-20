# TASK-2: Static text rendering

## Goal

Launch `betty.exe` → the window displays the hardcoded text "betty" in Catppuccin Mocha foreground (`#cdd6f4`), rendered with Consolas at 14 pt. Text is positioned at the top-left of the window. Font metrics (cell width, cell height) are computed and verified.

---

## Decisions from discussion

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | Glyph atlas approach (pre-render glyphs to a texture, draw quads) | Aligns with per-cell rendering needed in Tasks 3–5 |
| 2 | Runtime HLSL compilation (`D3DCompile`) | Keeps CMake simple; no build rules for shaders |
| 3 | New `text.hpp` / `text.cpp` file pair | Separates text concerns from `gfx.hpp` / `gfx.cpp` |
| 4 | `IDWriteGlyphRunAnalysis` for rasterization | No Direct2D dependency; alpha-only bitmaps via `CreateAlphaTexture` |
| 5 | Pre-bake ASCII 0–127 at startup | Simple, deterministic; handles Task 2 trivially |
| 6 | Font metrics as methods on renderer type | Fits existing pattern; queried post-creation |
| 7 | HLSL as raw string literals in `.cpp` | Zero build-system changes; lives alongside usage |
| 8 | Type name `glyph_renderer` | Describes what it does, not the underlying tech |
| 9 | Constant buffer with cell coordinates in shader | Aligns with cell-grid thinking needed in Tasks 4+ |
| 10 | Pre-baked white RGBA atlas; defer color to Task 6 | Simpler shader for now; per-cell colors come later |
| 11 | Text color: Catppuccin Mocha foreground `#cdd6f4` | Matches eventual terminal text color |

---

## 1. Glyph renderer architecture

```
┌─────────────────────────────────────────────┐
│                 glyph_renderer              │
│                                             │
│  ┌──────────┐   ┌───────────────┐           │
│  │ DWrite   │──►│ Glyph Atlas   │           │
│  │ Factory  │   │ (D3D texture) │           │
│  │ Format   │   │ 128 glyphs    │           │
│  └──────────┘   │ 16×8 grid     │           │
│                 └───────┬───────┘           │
│                         │                   │
│  ┌──────────┐   ┌───────▼───────┐           │
│  │ VS + PS  │◄──│  Quad Buffer  │           │
│  │ (HLSL)   │   │  (per-frame)  │           │
│  └────┬─────┘   └───────────────┘           │
│       │                                     │
│  ┌────▼─────┐                               │
│  │ Constant │  cell_width, cell_height      │
│  │ Buffer   │  [future: per-cell colors]    │
│  └──────────┘                               │
└─────────────────────────────────────────────┘
```

- **DWrite factory & text format**: Created once at init. `IDWriteFactory` + `IDWriteTextFormat` (Consolas, 14 pt).
- **Glyph atlas**: Pre-baked `ID3D11Texture2D` (RGBA, 16×8 glyph grid). Built at init time and never modified.
- **Shader resource view**: Bound to the glyph atlas for sampling in the pixel shader.
- **Constant buffer**: Contains `cell_width`, `cell_height`, `inv_texture_width`, `inv_texture_height`. The vertex shader translates cell coordinates → NDC.
- **Dynamic vertex buffer**: Updated each frame with a quad (4 vertices) for each glyph. Usage `D3D11_USAGE_DYNAMIC` with `D3D11_CPU_ACCESS_WRITE`.
- **Vertex + pixel shaders**: Compiled at runtime via `D3DCompile`, stored as `ID3D11VertexShader` / `ID3D11PixelShader`.
- **Input layout**: Position (2 floats), UV (2 floats) — 16 bytes per vertex. The vertex shader input receives cell+glyph position and atlas UV; both are packed into the same vertex format for simplicity.

### 1.1 Per-frame render

Each frame `glyph_renderer::draw(device)`:
1. Map the dynamic vertex buffer with `D3D11_MAP_WRITE_DISCARD`.
2. For each glyph in the frame's text ("betty"), write 4 vertices:
   - Positions: `(col * cell_width, row * cell_height)` → top-left, top-right, bottom-right, bottom-left
   - UVs: Atlas coordinates for the glyph's slot
3. Unmap buffer.
4. Set up pipeline: vertex shader, pixel shader, input layout, constant buffer, SRV.
5. Draw 6 indices (2 triangles) per glyph via `DrawIndexed`.
6. Device state is restored to defaults after (or the caller ensures proper setup).

**Coordinate convention**: The vertex shader receives cell-relative positions in pixel space (`[0, window_width]`, `[0, window_height]`) and transforms to NDC `[-1, 1]` using the window dimensions from the constant buffer. Later (Task 10) the window dimensions will update dynamically; for now they're fixed at `default_window_size`.

---

## 2. Glyph atlas construction

### 2.1 Atlas layout

```
┌─────────────────────────────────────┐
│ 0   1   2   3  ...  15              │  ← 16 columns
│ 16  17  18  19 ...  31             │
│ ...                                 │
│ 112 113 114 115 ... 127            │  ← 8 rows
└─────────────────────────────────────┘

codepoint 0x20 (space)   → slot 0
codepoint 0x21 (!)        → slot 1
...
codepoint 'b' (0x62)      → slot 0x62
codepoint 0x7F (DEL)      → slot 127 (rendered as empty / box)
```

- **Per-glyph slot size**: `(cell_width + padding) × (cell_height + padding)`, where padding is 1 px on each side to prevent bleeding.
- **Atlas texture size**: `16 * slot_width` × `8 * slot_height`. For Consolas 14 pt this is roughly `16 * 10 × 8 * 20` = `160 × 160` px, but we compute exactly at runtime.
- **Texture format**: `DXGI_FORMAT_R8G8B8A8_UNORM`, bound as `D3D11_USAGE_DEFAULT` with no CPU access (uploaded once via staging).
- **Shader resource view**: Bound with `D3D11_FILTER_MIN_MAG_MIP_LINEAR` for bilinear filtering.

### 2.2 Glyph rasterization algorithm

For each codepoint 0x00–0x7F:
1. Create an `IDWriteTextLayout` with the single character.
2. Query glyph indices and advances via `IDWriteFontFace::GetGlyphIndicesW` and `GetDesignGlyphAdvances`.
3. Create an `IDWriteGlyphRunAnalysis`:
   - `renderingMode = DWRITE_RENDERING_MODE_CLEARTYPE_GDI_CLASSIC` or `DWRITE_RENDERING_MODE_ALIASED` (we want grayscale anti-aliasing, not ClearType since we're pre-baking white glyphs on transparent background).
   - `measuringMode = DWRITE_MEASURING_MODE_NATURAL`
   - `transform` = identity, `pixelsPerDip` = 1.0 (we don't handle DPI yet).
4. Call `GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds)` or `DWRITE_TEXTURE_ALIASED_1x1` — get the bounding rectangle.
5. `CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &bounds, buffer, bufferSize)` — get the alpha bitmap.
6. Copy alpha values into the atlas texture: for each texel in the alpha bitmap, write `(255, 255, 255, alpha)` into the RGBA atlas at the glyph's slot position.

### 2.3 Font metrics

After the `IDWriteTextFormat` is created:
- Retrieve `IDWriteFontFace` from the format.
- Get `DWRITE_FONT_METRICS` — `designUnitsPerEm`, `ascent`, `descent`, `lineGap`, `capHeight`, etc.
- Compute cell dimensions:
  ```
  cell_width  = designUnitsPerEm  // or a fixed value from GetDesignGlyphAdvances for 'M' / 'W'
  cell_height = ascent + descent + lineGap  // in design units, converted to pixels
  ```
  …all converted from design units to pixels via `pixelsPerDip` and `fontSize`.
- **Better approach**: Since Consolas is monospace, we can use `IDWriteTextLayout` for a known character (e.g., 'W' which is typically widest) via `GetMetrics().width`, and use `GetMetrics().height` for cell height. Or simply use `IDWriteTextLayout` with the full printable ASCII range to find max width. For Consolas at 14 pt, all characters have the same advance width.
- Store `cell_width` and `cell_height` as `uint32_t` on the renderer.
- Add a `static_assert` or runtime check: `cell_width > 0 && cell_height > 0`.

---

## 3. File changes

### 3.1 New files

| File | Purpose |
|------|---------|
| `src/platform/text.hpp` | Public API: `glyph_renderer` type, `make_glyph_renderer`, font metrics accessors. No DWrite/D3D includes. |
| `src/platform/text.cpp` | Implementation: DWrite init, glyph atlas construction, vertex/index buffer setup, shader compilation, draw loop. Includes `<dwrite.h>`, `<dwrite_2.h>`, `<d3dcompiler.h>`. |

### 3.2 Modified files

| File | Change |
|------|--------|
| `src/CMakeLists.txt` | No changes (platform is a static lib, `text.cpp` is compiled into it). |
| `src/platform/CMakeLists.txt` | Add `text.cpp` to the library sources. Add `dwrite.lib` and `d3dcompiler.lib` to linker inputs. |
| `src/main.cpp` | Add `#include "platform/text.hpp"`. Create `glyph_renderer` after device. Call `renderer.draw(device)` each frame between clear and present. |

No changes to `gfx.hpp`, `gfx.cpp`, `window.hpp`, `window.cpp`, `types.hpp`, or `error.hpp`.

---

## 4. Public API design (`text.hpp`)

```cpp
#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <system_error>
#include "types.hpp"

namespace betty::platform {

// Opaque forward declarations from gfx.hpp
struct d3d_device;
struct d3d_render_target_view;

// --- glyph_renderer ---------------------------------------------------------

// Renders monospace glyphs from a pre-baked texture atlas.
// All glyphs are white; color is deferred to Task 6.
struct glyph_renderer {
  // Accessors for computed font metrics (available after successful creation).
  auto cell_width() const -> uint32_t;
  auto cell_height() const -> uint32_t;

  // Draw the given text at cell position (col=0, row=0).
  // Rebinds the RTV before drawing (the caller must pass the current RTV).
  auto draw(d3d_device const& device, d3d_render_target_view const& rtv, std::string_view text) const -> void;

  glyph_renderer() = default;
  ~glyph_renderer();
  glyph_renderer(glyph_renderer&&) noexcept;
  glyph_renderer& operator=(glyph_renderer&&) noexcept;
  glyph_renderer(glyph_renderer const&) = delete;
  glyph_renderer& operator=(glyph_renderer const&) = delete;

private:
  struct impl;
  std::unique_ptr<impl> impl_;

  friend auto make_glyph_renderer(d3d_device const&, window_dimensions const&)
    -> std::expected<glyph_renderer, std::error_code>;
};

// --- factory ----------------------------------------------------------------

// Creates the glyph renderer. The device is needed for texture/buffer creation.
// The window dimensions are used for the constant buffer's orthographic transform.
auto make_glyph_renderer(d3d_device const& device, window_dimensions const& window_size)
  -> std::expected<glyph_renderer, std::error_code>;

} // namespace betty::platform
```

Design notes:
- `draw()` takes `std::string_view` for the text and rebinds the RTV via friendship. For Task 2, only ASCII is supported. Non-ASCII codepoints are silently skipped (no crash).
- `draw()` maps the dynamic vertex buffer, writes quads, binds pipeline state including RTV, draws, and unmaps — all in one call. This is simple and correct for the single "betty" string in Task 2. In later tasks, we may batch multiple draw calls or pass a full grid.
- `make_glyph_renderer` takes window dimensions so the vertex shader knows the orthographic transform. In Task 10, we'll add a resize method.
- `cell_width()` / `cell_height()` are available after construction. They're const methods since metrics don't change after init (single font, single size).

---

## 5. Vertex format and buffer layout

### 5.1 Vertex structure

```cpp
struct glyph_vertex {
  float x, y;       // pixel position (top-left origin)
  float u, v;       // texture coordinates (0..1)
};
```

6 vertices per glyph (2 triangles, indexed):
- Triangle 1: top-left, top-right, bottom-right
- Triangle 2: top-left, bottom-right, bottom-left

The index buffer is **static** — built once with `[0, 1, 2, 0, 2, 3]` repeated for each glyph slot. In Task 2 we only render "betty" (5 chars), but we allocate for a safe maximum (e.g., 256 indices / 64 glyphs per frame) to avoid resizing. Index format: `DXGI_FORMAT_R16_UINT`.

### 5.2 Dynamic vertex buffer

- Usage: `D3D11_USAGE_DYNAMIC`, CPU access: `D3D11_CPU_ACCESS_WRITE`
- Size: enough for 64 glyphs per frame (64 × 4 vertices × 16 bytes = 4096 bytes). Overkill for Task 2, room for Task 3+.
- Mapped with `D3D11_MAP_WRITE_DISCARD` each frame.

### 5.3 Constant buffer

```cpp
struct glyph_constants {
  float window_width;    // in pixels
  float window_height;   // in pixels
  float cell_width;      // in pixels
  float cell_height;     // in pixels
  float inv_tex_width;   // 1.0 / atlas_width
  float inv_tex_height;  // 1.0 / atlas_height
};
```

Padded to 16-byte alignment (3 × `float4`). Created with `D3D11_USAGE_DEFAULT`.

---

## 6. Shader design

### 6.1 Vertex shader

```hlsl
cbuffer Constants : register(b0) {
  float2 window_size;        // window_width, window_height
  float2 cell_size;          // cell_width, cell_height
  float2 inv_tex_size;       // 1/atlas_width, 1/atlas_height
};

struct VS_INPUT {
  float2 position : POSITION;   // pixel position, top-left origin
  float2 uv       : TEXCOORD;
};

struct VS_OUTPUT {
  float4 position : SV_POSITION;
  float2 uv       : TEXCOORD;
};

VS_OUTPUT main(VS_INPUT input) {
  VS_OUTPUT output;
  // Convert pixel position (top-left origin) to NDC [-1, 1] (top-left is -1,1)
  output.position.x = (input.position.x / window_size.x) * 2.0 - 1.0;
  output.position.y = 1.0 - (input.position.y / window_size.y) * 2.0;
  output.position.zw = float2(0.0, 1.0);
  output.uv = input.uv;
  return output;
}
```

### 6.2 Pixel shader

```hlsl
Texture2D<float4> glyph_atlas : register(t0);
SamplerState linear_sampler : register(s0);

struct VS_OUTPUT {
  float4 position : SV_POSITION;
  float2 uv       : TEXCOORD;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
  return glyph_atlas.Sample(linear_sampler, input.uv);
}
```

Since glyphs are pre-baked as white RGBA, the sampled color is `(1, 1, 1, alpha)`. The background naturally shows through where alpha < 1. This gives the correct Catppuccin Mocha foreground appearance because the window is already cleared to `mocha_base` — wait, that's wrong. The window is cleared to the background color (`#1e1e2e`), and we're drawing white text. But we said we want the text to be in foreground color (`#cdd6f4`).

With the "defer color" decision and pre-baked white glyphs, we'd need a tint uniform to get `#cdd6f4`. But the decision was to defer color. So we have two options:
1. Pre-bake the atlas with `#cdd6f4` tint baked into the alpha values (i.e., RGBA = `(205, 214, 244, alpha)` instead of `(255, 255, 255, alpha)`).
2. Add a color constant buffer now and use it in the pixel shader.

Given the "defer color" decision, option 1 (pre-bake with the target color) is simplest for Task 2. We'll add a color parameter to `make_glyph_renderer` or just hardcode `#cdd6f4` in the rasterization step.

Let me adjust the rasterization: when copying alpha into the atlas, write `(0xCD, 0xD6, 0xF4, alpha)` instead of white. The pixel shader remains unchanged — it just samples and outputs. When Task 6 arrives, we change the atlas to alpha-only and add a foreground color constant buffer.

Actually, even simpler: let's just accept that for Task 2, the text will be white (which still shows up against the dark background), and add proper foreground color in Task 6. No — the user explicitly chose `#cdd6f4` in Q11.

**Revised plan**: Pre-bake the atlas with `#cdd6f4` tint. Atlas pixel = `rgba(0xCD/255, 0xD6/255, 0xF4/255, alpha)`. The pixel shader samples and outputs directly. When Task 6 arrives, we'll switch to alpha-only atlas and add a foreground color constant/uniform.

---

## 7. Glyph rasterization details

### 7.1 `IDWriteGlyphRunAnalysis` setup

```cpp
// 1. Create IDWriteTextFormat
//    fontFamily = "Consolas"
//    fontSize = 14.0f (in DIPs)
//    fontWeight = DWRITE_FONT_WEIGHT_NORMAL
//    fontStyle = DWRITE_FONT_STYLE_NORMAL
//    fontStretch = DWRITE_FONT_STRETCH_NORMAL

// 2. For each codepoint in 0x00..0x7F:
//    a. Create IDWriteTextLayout with the character
//    b. Get glyph count from layout metrics
//    c. Allocate buffers: UINT16 glyph_indices[count], FLOAT advances[count], DWRITE_GLYPH_OFFSET offsets[count]
//    d. Call layout->GetClusterMetrics(nullptr, 0, &cluster_count)
//       Or better: use IDWriteFontFace::GetGlyphIndices(&codepoint, 1, &glyph_index)
//    e. Call fontFace->GetDesignGlyphAdvances(1, &glyph_index, &advance, is_sideways=false)
//    f. Create IDWriteGlyphRunAnalysis:
//       dwriteFactory->CreateGlyphRunAnalysis(
//           &glyph_run,           // DWRITE_GLYPH_RUN with fontFace, fontEmSize, glyphIndices, advances, offsets
//           1.0f,                 // pixelsPerDip
//           nullptr,              // transform (identity)
//           DWRITE_RENDERING_MODE_CLEARTYPE_GDI_CLASSIC,  // produces grayscale AA
//           DWRITE_MEASURING_MODE_NATURAL,
//           0.0f, 0.0f,          // originX, originY
//           &glyph_analysis
//       );
//    g. RECT bounds;
//       glyph_analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
//    h. UINT32 buffer_size = (bounds.right - bounds.left) * (bounds.bottom - bounds.top);
//    i. std::vector<BYTE> alpha_buffer(buffer_size);
//    j. glyph_analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, alpha_buffer.data(), buffer_size);
//    k. Copy into atlas: for each pixel (bx, by) in alpha buffer:
//         atlas[(slot_y + by) * atlas_width + (slot_x + bx)] = { 0xCD, 0xD6, 0xF4, alpha_buffer[by * bounds_width + bx] }
```

### 7.2 Font metrics computation

For Consolas at 14 pt, we compute `cell_width` and `cell_height`:

```cpp
// Retrieve from IDWriteTextLayout for a representative character
IDWriteTextLayout* layout;
dwrite_factory->CreateTextLayout(
    L"W", 1,      // 'W' is typically the widest ASCII char
    text_format,
    FLT_MAX,      // maxWidth
    FLT_MAX,      // maxHeight
    &layout
);

DWRITE_TEXT_METRICS metrics;
layout->GetMetrics(&metrics);
cell_width = static_cast<uint32_t>(std::ceil(metrics.width));
cell_height = static_cast<uint32_t>(std::ceil(metrics.height));

// For Consolas 14 pt at 96 DPI (default), expect ~8-9 px width, ~19-20 px height.
```

We could also use `IDWriteTextLayout` with the full alphabet to validate that all ASCII chars fit within these bounds (monospace guarantee).

---

## 8. Implementation steps

### Step 1: Update CMake (`src/platform/CMakeLists.txt`)

Add to library sources:
```cmake
text.cpp
```

Add to linker inputs:
```cmake
find_library(DWRITE_LIB dwrite REQUIRED)
find_library(D3DCOMPILER_LIB d3dcompiler REQUIRED)

target_link_libraries(platform PUBLIC
    ${D3D11_LIB}
    ${DXGI_LIB}
    ${DWRITE_LIB}
    ${D3DCOMPILER_LIB}
    user32
    gdi32
)
```

### Step 2: Create `text.hpp`

Public API as described in Section 4. Forward-declares `d3d_device` from `gfx.hpp`.

### Step 3: Create `text.cpp` — PIMPL and structure

Define `glyph_renderer::impl`:
```cpp
struct glyph_renderer::impl {
  // DWrite
  ComPtr<IDWriteFactory> dwrite_factory;
  ComPtr<IDWriteTextFormat> text_format;

  // Font metrics
  uint32_t cell_width;
  uint32_t cell_height;

  // Glyph atlas
  ComPtr<ID3D11Texture2D> atlas_texture;
  ComPtr<ID3D11ShaderResourceView> atlas_srv;
  uint32_t atlas_width;
  uint32_t atlas_height;
  std::array<glyph_slot, 128> glyph_slots;  // per-codepoint UV info

  // D3D pipeline state
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  ComPtr<ID3D11InputLayout> input_layout;
  ComPtr<ID3D11Buffer> constant_buffer;
  ComPtr<ID3D11Buffer> vertex_buffer;
  ComPtr<ID3D11Buffer> index_buffer;
  ComPtr<ID3D11SamplerState> sampler_state;
  ComPtr<ID3D11BlendState> blend_state;      // alpha blending
  ComPtr<ID3D11DepthStencilState> depth_state; // depth/stencil disabled

  // Window dimensions (for constant buffer)
  uint32_t window_width;
  uint32_t window_height;
};
```

Per-glyph slot info:
```cpp
struct glyph_slot {
  uint16_t atlas_x;   // pixel offset in atlas
  uint16_t atlas_y;
  uint16_t width;     // pixel width in atlas
  uint16_t height;
  float u0, v0;       // normalized UVs
  float u1, v1;
};
```

### Step 4: Implement `make_glyph_renderer()`

1. **Create DWrite factory**: `DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, IID_PPV_ARGS(&factory))`.
2. **Create text format**: `factory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-US", &format)`.
3. **Compute font metrics**: As described in §7.2.
4. **Determine atlas layout**: Compute `slot_width = cell_width + 2`, `slot_height = cell_height + 2`. `atlas_width = 16 * slot_width`, `atlas_height = 8 * slot_height`.
5. **Create atlas texture**: `ID3D11Texture2D` with `DXGI_FORMAT_R8G8B8A8_UNORM`, size = `(atlas_width, atlas_height)`, `Usage = D3D11_USAGE_DEFAULT`, `BindFlags = D3D11_BIND_SHADER_RESOURCE`.
6. **Create staging texture**: Same dimensions, `Usage = D3D11_USAGE_STAGING`, `CPUAccessFlags = D3D11_CPU_ACCESS_WRITE`.
7. **Rasterize glyphs**: Loop over codepoints 0x00–0x7F. For each:
   - Get glyph index from `IDWriteFontFace`.
   - Create `IDWriteGlyphRunAnalysis`.
   - Get alpha texture via `CreateAlphaTexture`.
   - Copy into staging texture at the slot position, writing RGBA = `(0xCD, 0xD6, 0xF4, alpha)`.
8. **Copy staging → atlas**: `device.context->CopySubresourceRegion(atlas, 0, 0, 0, 0, staging, 0, &box)`.
   Or use `UpdateSubresource` with the staging data directly on the atlas texture if using `D3D11_USAGE_DEFAULT` — actually `UpdateSubresource` works directly on `DEFAULT` textures. So we can skip the staging texture and just call `context->UpdateSubresource(atlas, 0, &box, data, row_pitch, 0)` for each glyph's slot.
9. **Create SRV**: `device->CreateShaderResourceView(atlas, &srv_desc, &srv)`.
10. **Compile shaders**: `D3DCompile(source, length, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs_blob, &error_blob)`. Same for pixel shader with `"ps_5_0"`.
11. **Create shader objects**: `device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vs)` and similarly for pixel shader.
12. **Create input layout**: From `D3D11_INPUT_ELEMENT_DESC` array matching `glyph_vertex`.
13. **Create constant buffer**: `D3D11_BUFFER_DESC` with `ByteWidth = sizeof(glyph_constants)`, `Usage = D3D11_USAGE_DEFAULT`, `BindFlags = D3D11_BIND_CONSTANT_BUFFER`. Fill and upload initial data.
14. **Create dynamic vertex buffer**: `ByteWidth = max_quads * 4 * sizeof(glyph_vertex)`, `Usage = D3D11_USAGE_DYNAMIC`, `CPUAccessFlags = D3D11_CPU_ACCESS_WRITE`, `BindFlags = D3D11_BIND_VERTEX_BUFFER`.
15. **Create static index buffer**: `ByteWidth = max_quads * 6 * sizeof(uint16_t)`, `Usage = D3D11_USAGE_DEFAULT`. Fill with `[0,1,2, 0,2,3]` pattern.
16. **Create sampler state**: `D3D11_FILTER_MIN_MAG_MIP_LINEAR`, `AddressU/V/W = D3D11_TEXTURE_ADDRESS_CLAMP`.
17. **Create blend state**: `D3D11_BLEND_SRC_ALPHA` / `D3D11_BLEND_INV_SRC_ALPHA` for alpha blending. Render target write mask = `D3D11_COLOR_WRITE_ENABLE_ALL`.
18. **Create depth-stencil state**: Depth test disabled (`DepthEnable = FALSE`), stencil disabled (`StencilEnable = FALSE`).
19. Wrap in `glyph_renderer`, return.

### Step 5: Implement `glyph_renderer::draw()`

```cpp
void glyph_renderer::draw(d3d_device const& device, d3d_render_target_view const& rtv, std::string_view text) const {
  auto* context = device.impl_->context;

  // 1. Map vertex buffer
  D3D11_MAPPED_SUBRESOURCE mapped{};
  context->Map(impl_->vertex_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

  auto* vertices = static_cast<glyph_vertex*>(mapped.pData);
  uint32_t col = 0;
  uint32_t quad_count = 0;

  for (size_t i = 0; i < text.size(); ++i) {
    uint8_t cp = static_cast<uint8_t>(text[i]);
    if (cp > 127) continue;  // non-ASCII skipped for now

    auto& slot = impl_->glyph_slots[cp];
    float x0 = col * impl_->cell_width;
    float y0 = 0.0f;  // row 0
    float x1 = x0 + impl_->cell_width;
    float y1 = y0 + impl_->cell_height;

    // Top-left
    vertices[quad_count * 4 + 0] = { x0, y0, slot.u0, slot.v0 };
    // Top-right
    vertices[quad_count * 4 + 1] = { x1, y0, slot.u1, slot.v0 };
    // Bottom-right
    vertices[quad_count * 4 + 2] = { x1, y1, slot.u1, slot.v1 };
    // Bottom-left
    vertices[quad_count * 4 + 3] = { x0, y1, slot.u0, slot.v1 };

    ++col;
    ++quad_count;
  }

  context->Unmap(impl_->vertex_buffer.Get(), 0);

  if (quad_count == 0) return;

  // 2. Bind RTV and set pipeline state
  context->OMSetRenderTargets(1, rtv.impl_->rtv.GetAddressOf(), nullptr);
  context->OMSetBlendState(impl_->blend_state.Get(), nullptr, 0xFFFFFFFF);
  context->OMSetDepthStencilState(impl_->depth_state.Get(), 0);

  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->IASetInputLayout(impl_->input_layout.Get());

  UINT stride = sizeof(glyph_vertex);
  UINT offset = 0;
  context->IASetVertexBuffers(0, 1, impl_->vertex_buffer.GetAddressOf(), &stride, &offset);
  context->IASetIndexBuffer(impl_->index_buffer.Get(), DXGI_FORMAT_R16_UINT, 0);

  context->VSSetShader(impl_->vertex_shader.Get(), nullptr, 0);
  context->VSSetConstantBuffers(0, 1, impl_->constant_buffer.GetAddressOf());
  context->PSSetShader(impl_->pixel_shader.Get(), nullptr, 0);
  context->PSSetShaderResources(0, 1, impl_->atlas_srv.GetAddressOf());
  context->PSSetSamplers(0, 1, impl_->sampler_state.GetAddressOf());

  // 3. Draw
  context->DrawIndexed(quad_count * 6, 0, 0);
}
```

RTV access: `draw()` accesses `rtv.impl_->rtv` via friendship granted in `d3d_render_target_view`. See §16 for the updated friendship diagram.

### Step 6: Update `main.cpp`

```cpp
#include "platform/text.hpp"

// ...

int main() {
  // ... (existing setup: window, device, swap chain, rtv) ...

  // 5. Create glyph renderer
  auto renderer_result = platform::make_glyph_renderer(
    device,
    platform::window_dimensions{ platform::default_window_size.width, platform::default_window_size.height }
  );
  if (!renderer_result) {
    log_error(renderer_result.error(), "create glyph renderer");
    return 1;
  }
  auto& renderer = *renderer_result;

  // 6. Message loop
  while (platform::dispatch_pending_messages()) {
    device.clear(rtv, platform::mocha_base);
    renderer.draw(device, rtv, "betty");
    auto present_result = swap_chain.present();
    if (!present_result) {
      log_error(present_result.error(), "present");
      return 1;
    }
  }

  // Note: device.clear() binds the RTV, then renderer.draw() rebinds it
  // before drawing. This is intentional — no ordering dependency between
  // clear() and draw().

  return 0;
}
```



---

## 9. Error handling

Follows the existing pattern from Task 1:
- `make_glyph_renderer()` returns `std::expected<glyph_renderer, std::error_code>`.
- DWrite failures: `HRESULT` codes mapped via `make_d3d_error()` (same `HRESULT`-based category used for D3D, since DWrite also uses HRESULT).
- `D3DCompile` failures: HRESULT from the compiler, mapped the same way. Compiler error messages go to stderr via `OutputDebugStringW` or are discarded (not user-visible since this is a `WINDOWS` subsystem app).
- Invalid codepoints in `draw()` are silently skipped — no crash, no corruption.
- Atlas slot out-of-bounds: handled by the `if (cp > 127) continue` guard.

---

## 10. Integration order (what gets built first)

1. CMake changes — add `dwrite.lib`, `d3dcompiler.lib`, `text.cpp`.
2. Create `text.hpp` with the full public API.
3. Stub `text.cpp` with PIMPL definition, `make_glyph_renderer()` returning an error, and empty `draw()`.
4. Update `main.cpp` with the renderer creation and draw call. **This won't render text yet**, but verifies the compilation and linking work.
5. Implement `make_glyph_renderer()` incrementally:
   a. DWrite factory + text format creation → verify no crash.
   b. Font metrics computation → print to debug output, verify ~8–9 px width, ~19–20 px height.
   c. Shader compilation → verify shaders compile without errors.
   d. Atlas texture creation → verify texture dimensions are reasonable.
   e. Glyph rasterization → verify alpha values for each glyph.
   f. Buffer creation → verify vertex/index/constant buffers allocate.
   g. Full pipeline setup → verify no D3D debug layer warnings.
6. Implement `draw()`.
7. Full integration test.

---

## 11. Vertex shader design decision: cell vs pixel coordinates

Revisiting Q9 — we chose constant buffer with cell coordinates. But since we're pre-computing pixel positions in `draw()` (cell positions × cell dimensions), the vertex shader receives **pixel positions** (not cell indices). This is a pragmatic hybrid: the C++ side computes pixel positions from cells (simple multiplication), and the shader transforms pixel→NDC. The constant buffer stores `window_width` and `window_height` for the transform.

In Task 4+ when we have a full grid, we can change to sending cell indices and letting the shader multiply by cell dimensions — but that's a future optimization. For Task 2, pixel positions in the vertex buffer are simplest.

---

## 12. Testing / verification

Manual:
1. Launch `betty.exe`.
2. Confirm the word "betty" appears in the top-left corner of the window.
3. Confirm the text color matches `#cdd6f4` (light lavender).
4. Confirm the text is sharp and well-rendered (no blurry edges, no artifacts).
5. Confirm the window background remains `#1e1e2e`.
6. Close the window — clean exit, no D3D debug layer warnings.
7. Debug build: check Visual Studio Output window for D3D11 debug messages — should be clean.

Compile-time:
8. `main.cpp` must not need `#include <dwrite.h>`, `<d3dcompiler.h>`, or `<d3d11.h>`.
9. `text.hpp` must not expose DWrite, D3D, or HLSL types — only types from `types.hpp` and opaque references.

---

## 13. Dependencies

```
TASK-1 ──► TASK-2 ──► TASK-3
```

Task 2 depends on the D3D11 device and swap chain from Task 1. The glyph renderer is a peer to the existing graphics types and does not modify them.

---

## 14. Files to create / change (summary)

| File | Action | Purpose |
|------|--------|---------|
| `src/platform/CMakeLists.txt` | **Modify** | Add `text.cpp`, `dwrite.lib`, `d3dcompiler.lib` |
| `src/platform/text.hpp` | **Create** | Public API: `glyph_renderer`, `make_glyph_renderer` |
| `src/platform/text.cpp` | **Create** | Full implementation (DWrite, atlas, shaders, buffers) |
| `src/main.cpp` | **Modify** | Create renderer, call `draw()` each frame |

No changes to `gfx.hpp`, `gfx.cpp`, `window.hpp`, `window.cpp`, `types.hpp`, `error.hpp`, or any other files.

---

## 15. Decisions deferred to later tasks

| # | Decision | Deferred to |
|---|----------|-------------|
| 1 | `draw()` with `row` parameter (currently draws at row 0 only) | Task 3 (live shell I/O) |
| 2 | `resize()` method for window dimension changes | Task 10 (window resizing) |
| 3 | Per-cell foreground colors in the shader | Task 6 (SGR colours) |
| 4 | Non-ASCII glyph rasterization (currently 0x00–0x7F only) | Task 15 (Unicode + wide characters) |

---

## 16. Friendship diagram (extended from TASK-1)

```
                    ┌──────────────────┐
                    │   d3d_device     │
                    │  - impl (PIMPL)  │
                    │  + clear()       │
                    └──┬───────────┬───┘
                       │ friend    │ friend
                       ▼           ▼
              make_device    glyph_renderer
                  (existing)       │
                                   │ friend
                                   ▼
                    ┌──────────────────────────┐
                    │ d3d_render_target_view   │
                    │  - impl (PIMPL)          │
                    └──────────────────────────┘
                                   ▲
                                   │ friend
                                   │
                        glyph_renderer
                   (accesses rtv.impl_->rtv
                    for OMSetRenderTargets)
```

Changes from Task 1:
- `glyph_renderer` is a friend of `d3d_device` (needs `impl_->context` for buffer mapping, shader binding, drawing).
- `glyph_renderer` is a friend of `d3d_render_target_view` (needs `impl_->rtv` for `OMSetRenderTargets`).
- `gfx.hpp` adds a forward declaration: `struct glyph_renderer;` before the `d3d_render_target_view` definition.
