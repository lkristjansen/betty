#include "text.hpp"
#include "gfx.hpp"
#include "gfx_impl.hpp"
#include "error.hpp"
#include "debug_print.hpp"
#include "terminal/types.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dwrite.h>
#include <dwrite_2.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace betty::platform {

// ===========================================================================
// Constants
// ===========================================================================

inline constexpr uint32_t k_font_size_px        = 18u;   // raster font size used everywhere
inline constexpr uint32_t k_max_glyphs_per_frame = 65536u; // safe for any reasonable terminal size
inline constexpr uint32_t k_vertices_per_quad    = 4u;
inline constexpr uint32_t k_indices_per_quad     = 6u;
inline constexpr uint32_t k_max_vertices = k_max_glyphs_per_frame * k_vertices_per_quad;
inline constexpr uint32_t k_max_indices  = k_max_glyphs_per_frame * k_indices_per_quad;
inline constexpr uint32_t k_atlas_cols   = 16u;
inline constexpr uint32_t k_atlas_rows   = 16u;  // 8 regular + 8 italic
inline constexpr uint32_t k_atlas_glyphs = 256u;  // ASCII 0–127, regular & italic
inline constexpr uint32_t k_glyph_padding = 1u;   // px each side

inline constexpr float    k_wide_cell_factor           = 2.0f;
inline constexpr float    k_normal_cell_factor         = 1.0f;
inline constexpr float    k_color_norm_div             = 255.0f;
inline constexpr float    k_faint_intensity            = 0.5f;
inline constexpr float    k_full_intensity             = 1.0f;
inline constexpr uint32_t k_ascii_max                  = 127;
inline constexpr uint32_t k_italic_slot_offset         = 128;
inline constexpr float    k_bold_offset_px             = 1.0f;
inline constexpr float    k_underline_thickness_px     = 2.0f;
inline constexpr float    k_strikethrough_position     = 0.4f;
inline constexpr float    k_strikethrough_thickness_px  = 2.0f;
inline constexpr uint32_t k_wide_col_advance           = 2;
inline constexpr uint32_t k_normal_col_advance         = 1;

// ===========================================================================
// Vertex and constant-buffer layouts
// ===========================================================================

// Discriminator for pixel shader texture selection.
// Must match the #define constants in k_pixel_shader_src.
enum class atlas_kind : uint32_t {
  none        = 0,  // background — solid colour, no texture sample
  static_atlas = 1,  // pre-baked ASCII atlas (slots 0–255)
  dyn_atlas    = 2,  // dynamic atlas for non-ASCII codepoints
};

struct glyph_vertex {
  float    x, y, u, v; // pixel position + UV (top-left origin)
  float    r, g, b;    // colour (0–1, pre-normalized)
  uint32_t tex_id;     // atlas_kind discriminator (replaces old pad field)
};
static_assert(sizeof(glyph_vertex) == 8 * sizeof(float),
              "glyph_vertex must be 32 bytes for D3D11_INPUT_ELEMENT_DESC");

struct glyph_constants {
  float window_width;
  float window_height;
  float cell_width;
  float cell_height;
  float inv_tex_width;
  float inv_tex_height;
  float _pad[2];  // pad to 32 bytes for D3D11 alignment
};
static_assert(sizeof(glyph_constants) == 32,
              "glyph_constants must be 32 bytes for D3D11 cbuffer alignment");

// Per-glyph slot metadata (atlas position + precomputed UVs).
struct glyph_slot {
  uint16_t atlas_x;
  uint16_t atlas_y;
  float    u0, v0;
  float    u1, v1;
};

// ===========================================================================
// HLSL shaders (raw string literals)
// ===========================================================================

namespace {

const char* k_vertex_shader_src = R"(
cbuffer Constants : register(b0) {
  float2 window_size;
  float2 cell_size;
  float2 inv_tex_size;
};

struct VS_INPUT {
  float2 position : POSITION;
  float2 uv       : TEXCOORD;
  float3 color    : COLOR;
  uint   tex_id   : TEXCOORD1;
};

struct VS_OUTPUT {
  float4 position : SV_POSITION;
  float2 uv       : TEXCOORD;
  float3 color    : COLOR;
  uint   tex_id   : TEXCOORD1;
};

VS_OUTPUT main(VS_INPUT input) {
  VS_OUTPUT output;
  // Convert pixel position (top-left origin) to NDC [-1, 1].
  // In pixel space, (0,0) is top-left. In NDC, (-1,1) is top-left.
  output.position.x = (input.position.x / window_size.x) * 2.0 - 1.0;
  output.position.y = 1.0 - (input.position.y / window_size.y) * 2.0;
  output.position.zw = float2(0.0, 1.0);
  output.uv = input.uv;
  output.color = input.color;
  output.tex_id = input.tex_id;
  return output;
}
)";

const char* k_pixel_shader_src = R"(
#define ATLAS_KIND_NONE         0u
#define ATLAS_KIND_STATIC_ATLAS 1u
#define ATLAS_KIND_DYN_ATLAS    2u

Texture2D<float4> glyph_atlas : register(t0);
Texture2D<float4> dyn_atlas   : register(t1);
SamplerState linear_sampler  : register(s0);

struct VS_OUTPUT {
  float4 position : SV_POSITION;
  float2 uv       : TEXCOORD;
  float3 color    : COLOR;
  uint   tex_id   : TEXCOORD1;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
  switch (input.tex_id) {
    case ATLAS_KIND_NONE: {
      // Background quad: solid colour (no texture sample).
      return float4(input.color, 1.0);
    }
    case ATLAS_KIND_DYN_ATLAS: {
      float alpha = dyn_atlas.Sample(linear_sampler, input.uv).a;
      return float4(input.color * alpha, alpha);
    }
    default: { // ATLAS_KIND_STATIC_ATLAS
      float alpha = glyph_atlas.Sample(linear_sampler, input.uv).a;
      return float4(input.color * alpha, alpha);
    }
  }
}
)";

} // anonymous namespace

// ===========================================================================
// PIMPL definition
// ===========================================================================

struct glyph_renderer::impl {
  // DWrite
  ComPtr<IDWriteFactory>  dwrite_factory;
  ComPtr<IDWriteFontFace1> font_face;   // v1 for GetDesignGlyphAdvances
  ComPtr<IDWriteTextFormat> text_format;

  // Font metrics
  uint32_t cell_width  = 0;
  uint32_t cell_height = 0;
  float    baseline_y  = 0.0f;  // distance from cell top to baseline (px)

  // Glyph atlas
  ComPtr<ID3D11Texture2D>          atlas_texture;
  ComPtr<ID3D11ShaderResourceView> atlas_srv;
  ComPtr<IDWriteFontFace1>         font_face_italic;  // italic variant for slots 128–255
  uint32_t atlas_width  = 0;
  uint32_t atlas_height = 0;
  uint32_t slot_width   = 0;
  uint32_t slot_height  = 0;
  std::array<glyph_slot, k_atlas_glyphs> glyph_slots{};

  // ── Dynamic glyph atlas for non-ASCII codepoints ─────────────────────
  static constexpr uint32_t k_dyn_atlas_cols = 32;
  static constexpr uint32_t k_dyn_atlas_rows = 32;
  static constexpr uint32_t k_dyn_max_glyphs = k_dyn_atlas_cols * k_dyn_atlas_rows;

  ComPtr<ID3D11Texture2D>          dyn_atlas_texture;
  ComPtr<ID3D11ShaderResourceView> dyn_atlas_srv;
  uint32_t dyn_atlas_width  = 0;
  uint32_t dyn_atlas_height = 0;
  // UV data per dynamic slot.
  std::array<glyph_slot, k_dyn_max_glyphs> dyn_glyph_slots{};

  // Hash map: codepoint → slot index.
  mutable std::unordered_map<char32_t, uint32_t> dyn_index_;
  // LRU: last-access generation counter per slot.
  mutable std::array<uint64_t, k_dyn_max_glyphs> dyn_access_{};
  mutable uint64_t dyn_clock_ = 0;
  // Next free slot (fills sequentially until full, then LRU eviction).
  mutable uint32_t dyn_next_ = 0;

  // D3D pipeline state
  ComPtr<ID3D11VertexShader>  vertex_shader;
  ComPtr<ID3D11PixelShader>   pixel_shader;
  ComPtr<ID3D11InputLayout>   input_layout;
  ComPtr<ID3D11Buffer>        constant_buffer;
  ComPtr<ID3D11Buffer>        vertex_buffer;
  ComPtr<ID3D11Buffer>        index_buffer;
  ComPtr<ID3D11SamplerState>  sampler_state;
  ComPtr<ID3D11BlendState>    blend_state;
  ComPtr<ID3D11DepthStencilState> depth_state;
  ComPtr<ID3D11RasterizerState>   rasterizer_state;

  // Window dimensions (for constant buffer)
  uint32_t window_width  = 0;
  uint32_t window_height = 0;
};

// ===========================================================================
// glyph_renderer — rule of five
// ===========================================================================

glyph_renderer::~glyph_renderer() = default;
glyph_renderer::glyph_renderer(glyph_renderer&&) noexcept = default;
glyph_renderer& glyph_renderer::operator=(glyph_renderer&&) noexcept = default;
glyph_renderer::glyph_renderer(empty_tag) noexcept {}

auto glyph_renderer::cell_width() const -> uint32_t {
  return impl_->cell_width;
}

auto glyph_renderer::cell_height() const -> uint32_t {
  return impl_->cell_height;
}

// ===========================================================================
// Helpers
// ===========================================================================

namespace {

// Compile an HLSL shader from a raw string.
auto compile_shader(const char* source, const char* entry_point, const char* target,
                    ID3DBlob** blob) -> HRESULT {
  ComPtr<ID3DBlob> error_blob;
  HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
                           entry_point, target, 0, 0,
                           blob, error_blob.GetAddressOf());
  if (FAILED(hr) && error_blob) {
    debug_println("{}", static_cast<const char*>(error_blob->GetBufferPointer()));
  }
  return hr;
}

// Get the font face (v1 interface) and metrics from the text format.
auto init_font_face(IDWriteFactory* factory, IDWriteTextFormat* format,
                    uint32_t& cell_width, uint32_t& cell_height,
                    ComPtr<IDWriteFontFace1>& font_face,
                    uint32_t& font_ascent, uint32_t& font_design_units_per_em) -> HRESULT {
  // 1. Get font face via the font collection.
  ComPtr<IDWriteFontCollection> font_collection;
  HRESULT hr = format->GetFontCollection(font_collection.GetAddressOf());
  if (FAILED(hr)) return hr;

  UINT32 family_index = 0;
  BOOL   exists = FALSE;
  hr = font_collection->FindFamilyName(L"Consolas", &family_index, &exists);
  if (FAILED(hr)) return hr;
  if (!exists) return E_FAIL;

  ComPtr<IDWriteFontFamily> font_family;
  hr = font_collection->GetFontFamily(family_index, font_family.GetAddressOf());
  if (FAILED(hr)) return hr;

  ComPtr<IDWriteFont> font;
  hr = font_family->GetFont(0, font.GetAddressOf());
  if (FAILED(hr)) return hr;

  // Get IDWriteFontFace, then query for IDWriteFontFace1.
  ComPtr<IDWriteFontFace> face;
  hr = font->CreateFontFace(face.GetAddressOf());
  if (FAILED(hr)) return hr;

  hr = face.As(&font_face);
  if (FAILED(hr)) return hr;

  // 2. Get font metrics (design units).
  DWRITE_FONT_METRICS font_metrics{};
  font_face->GetMetrics(&font_metrics);
  font_ascent                = font_metrics.ascent;
  font_design_units_per_em   = font_metrics.designUnitsPerEm;

  // 3. Get cell dimensions from a representative text layout.
  ComPtr<IDWriteTextLayout> layout;
  hr = factory->CreateTextLayout(L"W", 1, format,
                                   FLT_MAX, FLT_MAX,
                                   layout.GetAddressOf());
  if (FAILED(hr)) return hr;

  DWRITE_TEXT_METRICS metrics{};
  hr = layout->GetMetrics(&metrics);
  if (FAILED(hr)) return hr;

  cell_width  = static_cast<uint32_t>(std::ceil(metrics.width));
  cell_height = static_cast<uint32_t>(std::ceil(metrics.height));

  return S_OK;
}

// Rasterize a single glyph into the staging buffer.
// Returns true if rasterization produced any output.
auto rasterize_glyph(IDWriteFactory* factory, IDWriteFontFace1* font_face,
                     float font_size, uint32_t codepoint,
                     uint32_t slot_x, uint32_t slot_y,
                     uint32_t atlas_width,
                     uint32_t cell_width, uint32_t cell_height,
                     float baseline_y,
                     std::vector<uint8_t>& staging_buffer) -> bool {
  // 1. Get glyph index.
  UINT32 const cp32 = static_cast<uint32_t>(codepoint);
  UINT16 glyph_index = 0;
  HRESULT hr = font_face->GetGlyphIndices(&cp32, 1, &glyph_index);
  if (FAILED(hr)) return false;

  // 2. Get glyph advance (design units).
  INT32 advance_design = 0;
  hr = font_face->GetDesignGlyphAdvances(1, &glyph_index, &advance_design, FALSE);
  if (FAILED(hr)) return false;
  FLOAT advance = static_cast<FLOAT>(advance_design);

  DWRITE_GLYPH_OFFSET offset{};

  // 3. Build glyph run.
  // font_face (IDWriteFontFace1*) implicitly converts to IDWriteFontFace*.
  DWRITE_GLYPH_RUN glyph_run{};
  glyph_run.fontFace      = font_face;  // implicit upcast
  glyph_run.fontEmSize    = font_size;
  glyph_run.glyphCount    = 1;
  glyph_run.glyphIndices  = &glyph_index;
  glyph_run.glyphAdvances = &advance;
  glyph_run.glyphOffsets  = &offset;
  glyph_run.isSideways    = FALSE;
  glyph_run.bidiLevel     = 0;

  // 4. Create glyph run analysis.
  ComPtr<IDWriteGlyphRunAnalysis> analysis;
  hr = factory->CreateGlyphRunAnalysis(
    &glyph_run,
    1.0f,                                    // pixelsPerDip
    nullptr,                                 // transform (identity)
    DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL, // anti-aliased rendering
    DWRITE_MEASURING_MODE_NATURAL,
    0.0f, 0.0f,                              // originX, originY
    analysis.GetAddressOf()
  );
  if (FAILED(hr)) return false;

  // 5. Get alpha texture bounds.
  RECT bounds{};
  hr = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
  if (FAILED(hr)) return false;

  // Empty glyphs (e.g. space, control chars, .notdef) — nothing to draw.
  // RECT uses standard Windows convention: top < bottom for non-empty.
  if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) return false;

  UINT32 const bounds_width  = static_cast<UINT32>(bounds.right - bounds.left);
  UINT32 const bounds_height = static_cast<UINT32>(bounds.bottom - bounds.top);
  // ClearType texture is 3× wider (R, G, B subpixel channels per pixel).
  UINT32 const buffer_size   = bounds_width * bounds_height * 3;
  UINT32 const buffer_stride = bounds_width * 3;

  std::vector<BYTE> cleartype_buffer(buffer_size);

  // 6. Create alpha texture.
  hr = analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1,
                                      &bounds,
                                      cleartype_buffer.data(),
                                      buffer_size);
  if (FAILED(hr)) return false;

  // 7. Copy into staging buffer at the slot position.
  // The bounds from GetAlphaTextureBounds cover the full glyph extent
  // (ascender + descender).  Centering the bitmap within the cell gives
  // approximately correct baseline alignment for monospace rendering.

  int const cell_content_x = static_cast<int>(slot_x) + k_glyph_padding;
  int const cell_content_y = static_cast<int>(slot_y) + k_glyph_padding;

  // Center horizontally, but align vertically by the font baseline.
  // bounds.top is negative (above the baseline), so adding it shifts
  // the bitmap up so that the baseline lands at baseline_y.
  int const offset_x = (static_cast<int>(cell_width) - static_cast<int>(bounds_width)) / 2;
  int const origin_x = cell_content_x + offset_x;
  int const origin_y = cell_content_y + static_cast<int>(std::round(baseline_y)) + bounds.top;

  for (UINT32 by = 0; by < bounds_height; ++by) {
    int const dy = origin_y + static_cast<int>(by);
    int const slot_top = static_cast<int>(slot_y) + static_cast<int>(k_glyph_padding);
    int const slot_bottom = slot_top + static_cast<int>(cell_height);
    if (dy < slot_top)
      continue;
    if (dy >= slot_bottom)
      continue;

    for (UINT32 bx = 0; bx < bounds_width; ++bx) {
      int const dx = origin_x + static_cast<int>(bx);
      int const slot_left = static_cast<int>(slot_x) + static_cast<int>(k_glyph_padding);
      int const slot_right = slot_left + static_cast<int>(cell_width);
      if (dx < slot_left)
        continue;
      if (dx >= slot_right)
        continue;

      // ClearType buffer stores 3 bytes per pixel (R, G, B subpixel coverage).
      // Average the subpixel channels to get a single coverage value.
      size_t const ct_idx = static_cast<size_t>(by) * buffer_stride + static_cast<size_t>(bx) * 3;
      uint8_t const r = cleartype_buffer[ct_idx + 0];
      uint8_t const g = cleartype_buffer[ct_idx + 1];
      uint8_t const b = cleartype_buffer[ct_idx + 2];
      uint8_t const alpha = static_cast<uint8_t>((static_cast<uint32_t>(r) + g + b) / 3);
      if (alpha == 0) continue;

      // buf_idx: absolute pixel position within the full atlas texture.
      // dy and dx are already absolute (relative to slot origin + k_glyph_padding),
      // so no additional offset is needed.
      size_t const buf_idx = static_cast<size_t>(dy) * atlas_width + static_cast<size_t>(dx);
      size_t const pixel_offset = buf_idx * 4;
      // Alpha-only atlas: glyph shapes stored as white with alpha coverage.
      // Colour is applied per-vertex by the pixel shader.
      staging_buffer[pixel_offset + 0] = 255;
      staging_buffer[pixel_offset + 1] = 255;
      staging_buffer[pixel_offset + 2] = 255;
      staging_buffer[pixel_offset + 3] = alpha;
    }
  }

  return true;
}

} // anonymous namespace

// ===========================================================================
// make_glyph_renderer
// ===========================================================================

auto make_glyph_renderer(d3d_device const& device, window_dimensions const& window_size)
  -> std::expected<glyph_renderer, std::error_code> {

  auto* d3d_dev  = device.impl_->device.Get();
  auto* d3d_ctx  = device.impl_->context.Get();

  auto p = std::make_unique<glyph_renderer::impl>();

  constexpr float k_font_size = static_cast<float>(k_font_size_px);

  // --- 1. Create DWrite factory ---------------------------------------------
  HRESULT hr = DWriteCreateFactory(
    DWRITE_FACTORY_TYPE_SHARED,
    __uuidof(IDWriteFactory),
    reinterpret_cast<IUnknown**>(p->dwrite_factory.GetAddressOf())
  );
  if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));

  // --- 2. Create text format ------------------------------------------------
  hr = p->dwrite_factory->CreateTextFormat(
    L"Consolas", nullptr,
    DWRITE_FONT_WEIGHT_NORMAL,
    DWRITE_FONT_STYLE_NORMAL,
    DWRITE_FONT_STRETCH_NORMAL,
    k_font_size,
    L"en-US",
    p->text_format.GetAddressOf()
  );
  if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));

  // --- 3. Get font face & metrics -------------------------------------------
  uint32_t font_ascent = 0;
  uint32_t font_design_units_per_em = 0;
  hr = init_font_face(p->dwrite_factory.Get(), p->text_format.Get(),
                       p->cell_width, p->cell_height,
                       p->font_face,
                       font_ascent, font_design_units_per_em);
  if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));

  // Compute baseline position: ascent converted to pixels at raster size.
  p->baseline_y = static_cast<float>(font_ascent) * k_font_size
                / static_cast<float>(font_design_units_per_em);

  // --- 3b. Get italic font face ---------------------------------------------
  {
    ComPtr<IDWriteFontCollection> font_collection;
    hr = p->text_format->GetFontCollection(font_collection.GetAddressOf());
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));

    UINT32 family_index = 0;
    BOOL exists = FALSE;
    hr = font_collection->FindFamilyName(L"Consolas", &family_index, &exists);
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));

    if (exists) {
      ComPtr<IDWriteFontFamily> font_family;
      hr = font_collection->GetFontFamily(family_index, font_family.GetAddressOf());
      if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));

      UINT32 const count = font_family->GetFontCount();
      for (UINT32 i = 0; i < count; ++i) {
        ComPtr<IDWriteFont> font;
        hr = font_family->GetFont(i, font.GetAddressOf());
        if (FAILED(hr)) continue;

        if (font->GetStyle() == DWRITE_FONT_STYLE_ITALIC) {
          ComPtr<IDWriteFontFace> face;
          hr = font->CreateFontFace(face.GetAddressOf());
          if (FAILED(hr)) continue;

          hr = face.As(&p->font_face_italic);
          if (SUCCEEDED(hr)) break;
        }
      }
    }
    // If no italic face found, fall back to regular font face.
    if (!p->font_face_italic) {
      p->font_face_italic = p->font_face;
    }
  }

  // --- 4. Determine atlas layout --------------------------------------------
  p->slot_width  = p->cell_width  + k_glyph_padding * 2;
  p->slot_height = p->cell_height + k_glyph_padding * 2;
  p->atlas_width  = k_atlas_cols * p->slot_width;
  p->atlas_height = k_atlas_rows * p->slot_height;

  // --- 5. Create atlas texture ----------------------------------------------
  {
    D3D11_TEXTURE2D_DESC tex_desc{};
    tex_desc.Width            = p->atlas_width;
    tex_desc.Height           = p->atlas_height;
    tex_desc.MipLevels        = 1;
    tex_desc.ArraySize        = 1;
    tex_desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage            = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    hr = d3d_dev->CreateTexture2D(&tex_desc, nullptr,
                                   p->atlas_texture.GetAddressOf());
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));
  }

  // --- 6. Create SRV --------------------------------------------------------
  {
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels       = 1;
    srv_desc.Texture2D.MostDetailedMip = 0;

    hr = d3d_dev->CreateShaderResourceView(p->atlas_texture.Get(), &srv_desc,
                                            p->atlas_srv.GetAddressOf());
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));
  }

  // --- 7. Rasterize glyphs (0x00–0x7F regular + italic) --------------------
  {
    size_t const buf_pixels = static_cast<size_t>(p->atlas_width) * static_cast<size_t>(p->atlas_height);
    std::vector<uint8_t> staging_buffer(buf_pixels * 4, 0);

    // Helper: rasterize one 128-glyph block at a given starting row.
    auto rasterize_block = [&](IDWriteFontFace1* face, uint32_t base_row) {
      for (uint32_t cp = 0; cp < 128; ++cp) {
        uint32_t const slot_idx = cp + base_row * k_atlas_cols;
        uint32_t const col = cp % k_atlas_cols;
        uint32_t const row = base_row + cp / k_atlas_cols;
        uint32_t const slot_x = col * p->slot_width;
        uint32_t const slot_y = row * p->slot_height;

        rasterize_glyph(p->dwrite_factory.Get(), face,
                         k_font_size, cp,
                         slot_x, slot_y,
                         p->atlas_width,
                         p->cell_width, p->cell_height,
                         p->baseline_y,
                         staging_buffer);

        // Precompute UVs for this slot (content area, excluding padding).
        auto& slot = p->glyph_slots[slot_idx];
        slot.atlas_x = static_cast<uint16_t>(slot_x);
        slot.atlas_y = static_cast<uint16_t>(slot_y);
        slot.u0 = static_cast<float>(slot_x + k_glyph_padding)              / static_cast<float>(p->atlas_width);
        slot.v0 = static_cast<float>(slot_y + k_glyph_padding)              / static_cast<float>(p->atlas_height);
        slot.u1 = static_cast<float>(slot_x + k_glyph_padding + p->cell_width)  / static_cast<float>(p->atlas_width);
        slot.v1 = static_cast<float>(slot_y + k_glyph_padding + p->cell_height) / static_cast<float>(p->atlas_height);
      }
    };

    // Rows 0–7: regular font face.
    rasterize_block(p->font_face.Get(), 0);
    // Rows 8–15: italic font face.
    rasterize_block(p->font_face_italic.Get(), 8);

    // Upload staging buffer to atlas texture.
    D3D11_BOX box{};
    box.left   = 0;
    box.right  = p->atlas_width;
    box.top    = 0;
    box.bottom = p->atlas_height;
    box.front  = 0;
    box.back   = 1;

    d3d_ctx->UpdateSubresource(p->atlas_texture.Get(), 0, &box,
                                staging_buffer.data(),
                                p->atlas_width * 4, 0);
  }

  // --- 7b. Create dynamic atlas texture (non-ASCII glyphs) -------------------
  {
    p->dyn_atlas_width  = p->k_dyn_atlas_cols * p->slot_width;
    p->dyn_atlas_height = p->k_dyn_atlas_rows * p->slot_height;

    D3D11_TEXTURE2D_DESC dyn_tex_desc{};
    dyn_tex_desc.Width            = p->dyn_atlas_width;
    dyn_tex_desc.Height           = p->dyn_atlas_height;
    dyn_tex_desc.MipLevels        = 1;
    dyn_tex_desc.ArraySize        = 1;
    dyn_tex_desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    dyn_tex_desc.SampleDesc.Count = 1;
    dyn_tex_desc.Usage            = D3D11_USAGE_DEFAULT;
    dyn_tex_desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    hr = d3d_dev->CreateTexture2D(&dyn_tex_desc, nullptr,
                                   p->dyn_atlas_texture.GetAddressOf());
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));

    // Create SRV for dynamic atlas.
    D3D11_SHADER_RESOURCE_VIEW_DESC dyn_srv_desc{};
    dyn_srv_desc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    dyn_srv_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    dyn_srv_desc.Texture2D.MipLevels       = 1;
    dyn_srv_desc.Texture2D.MostDetailedMip = 0;

    hr = d3d_dev->CreateShaderResourceView(p->dyn_atlas_texture.Get(), &dyn_srv_desc,
                                            p->dyn_atlas_srv.GetAddressOf());
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));

    // Clear to transparent.
    {
      size_t const dyn_pixels = static_cast<size_t>(p->dyn_atlas_width) *
                                 static_cast<size_t>(p->dyn_atlas_height);
      std::vector<uint8_t> clear_buf(dyn_pixels * 4, 0);
      d3d_ctx->UpdateSubresource(p->dyn_atlas_texture.Get(), 0, nullptr,
                                  clear_buf.data(), p->dyn_atlas_width * 4, 0);
    }

    // Precompute UVs for each dynamic slot.
    for (uint32_t i = 0; i < p->k_dyn_max_glyphs; ++i) {
      uint32_t const col = i % p->k_dyn_atlas_cols;
      uint32_t const row = i / p->k_dyn_atlas_cols;
      uint32_t const slot_x = col * p->slot_width;
      uint32_t const slot_y = row * p->slot_height;

      auto& slot = p->dyn_glyph_slots[i];
      slot.atlas_x = static_cast<uint16_t>(slot_x);
      slot.atlas_y = static_cast<uint16_t>(slot_y);
      slot.u0 = static_cast<float>(slot_x + k_glyph_padding) / static_cast<float>(p->dyn_atlas_width);
      slot.v0 = static_cast<float>(slot_y + k_glyph_padding) / static_cast<float>(p->dyn_atlas_height);
      slot.u1 = static_cast<float>(slot_x + k_glyph_padding + p->cell_width) / static_cast<float>(p->dyn_atlas_width);
      slot.v1 = static_cast<float>(slot_y + k_glyph_padding + p->cell_height) / static_cast<float>(p->dyn_atlas_height);
    }
  }

  // --- 8. Compile shaders ---------------------------------------------------
  ComPtr<ID3DBlob> vs_blob;
  hr = compile_shader(k_vertex_shader_src, "main", "vs_5_0", vs_blob.GetAddressOf());
  if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));

  ComPtr<ID3DBlob> ps_blob;
  hr = compile_shader(k_pixel_shader_src, "main", "ps_5_0", ps_blob.GetAddressOf());
  if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));

  // --- 9. Create shader objects ---------------------------------------------
  hr = d3d_dev->CreateVertexShader(vs_blob->GetBufferPointer(),
                                    vs_blob->GetBufferSize(), nullptr,
                                    p->vertex_shader.GetAddressOf());
  if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));

  hr = d3d_dev->CreatePixelShader(ps_blob->GetBufferPointer(),
                                   ps_blob->GetBufferSize(), nullptr,
                                   p->pixel_shader.GetAddressOf());
  if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));

  // --- 10. Create input layout ----------------------------------------------
  {
    D3D11_INPUT_ELEMENT_DESC layout[] = {
      { "POSITION",  0, DXGI_FORMAT_R32G32_FLOAT,   0, 0,                             D3D11_INPUT_PER_VERTEX_DATA, 0 },
      { "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,   0, D3D11_APPEND_ALIGNED_ELEMENT,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
      { "COLOR",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
      { "TEXCOORD",  1, DXGI_FORMAT_R32_UINT,        0, D3D11_APPEND_ALIGNED_ELEMENT,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = d3d_dev->CreateInputLayout(layout, ARRAYSIZE(layout),
                                     vs_blob->GetBufferPointer(),
                                     vs_blob->GetBufferSize(),
                                     p->input_layout.GetAddressOf());
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));
  }

  // --- 11. Create constant buffer -------------------------------------------
  {
    glyph_constants constants{};
    constants.window_width  = static_cast<float>(window_size.width);
    constants.window_height = static_cast<float>(window_size.height);
    constants.cell_width    = static_cast<float>(p->cell_width);
    constants.cell_height   = static_cast<float>(p->cell_height);
    constants.inv_tex_width  = 1.0f / static_cast<float>(p->atlas_width);
    constants.inv_tex_height = 1.0f / static_cast<float>(p->atlas_height);
    constants._pad[0] = 0.0f;
    constants._pad[1] = 0.0f;

    D3D11_BUFFER_DESC buf_desc{};
    buf_desc.ByteWidth = sizeof(glyph_constants);
    buf_desc.Usage     = D3D11_USAGE_DEFAULT;
    buf_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    D3D11_SUBRESOURCE_DATA init_data{};
    init_data.pSysMem = &constants;

    hr = d3d_dev->CreateBuffer(&buf_desc, &init_data,
                                p->constant_buffer.GetAddressOf());
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));
  }

  // --- 12. Create dynamic vertex buffer -------------------------------------
  {
    D3D11_BUFFER_DESC buf_desc{};
    buf_desc.ByteWidth      = k_max_vertices * sizeof(glyph_vertex);
    buf_desc.Usage          = D3D11_USAGE_DYNAMIC;
    buf_desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    buf_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = d3d_dev->CreateBuffer(&buf_desc, nullptr,
                                p->vertex_buffer.GetAddressOf());
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));
  }

  // --- 13. Create static index buffer ---------------------------------------
  {
    std::array<uint16_t, k_max_indices> indices{};
    for (uint32_t q = 0; q < k_max_glyphs_per_frame; ++q) {
      uint16_t base = static_cast<uint16_t>(q * k_vertices_per_quad);
      indices[q * k_indices_per_quad + 0] = base + 0;
      indices[q * k_indices_per_quad + 1] = base + 1;
      indices[q * k_indices_per_quad + 2] = base + 2;
      indices[q * k_indices_per_quad + 3] = base + 0;
      indices[q * k_indices_per_quad + 4] = base + 2;
      indices[q * k_indices_per_quad + 5] = base + 3;
    }

    D3D11_BUFFER_DESC buf_desc{};
    buf_desc.ByteWidth = k_max_indices * sizeof(uint16_t);
    buf_desc.Usage     = D3D11_USAGE_DEFAULT;
    buf_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA init_data{};
    init_data.pSysMem = indices.data();

    hr = d3d_dev->CreateBuffer(&buf_desc, &init_data,
                                p->index_buffer.GetAddressOf());
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));
  }

  // --- 14. Create sampler state (bilinear, clamp) ---------------------------
  {
    D3D11_SAMPLER_DESC samp_desc{};
    samp_desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp_desc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    hr = d3d_dev->CreateSamplerState(&samp_desc,
                                      p->sampler_state.GetAddressOf());
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));
  }

  // --- 15. Create blend state (alpha blending) ------------------------------
  {
    D3D11_BLEND_DESC blend_desc{};
    blend_desc.RenderTarget[0].BlendEnable   = TRUE;
    blend_desc.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = d3d_dev->CreateBlendState(&blend_desc,
                                    p->blend_state.GetAddressOf());
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));
  }

  // --- 16. Create depth-stencil state (disabled) ----------------------------
  {
    D3D11_DEPTH_STENCIL_DESC ds_desc{};
    ds_desc.DepthEnable    = FALSE;
    ds_desc.StencilEnable  = FALSE;

    hr = d3d_dev->CreateDepthStencilState(&ds_desc,
                                           p->depth_state.GetAddressOf());
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));
  }

  // --- 17. Create rasterizer state (no culling) ----------------------------
  {
    D3D11_RASTERIZER_DESC rs_desc{};
    rs_desc.FillMode = D3D11_FILL_SOLID;
    rs_desc.CullMode = D3D11_CULL_NONE;
    rs_desc.ScissorEnable = FALSE;

    hr = d3d_dev->CreateRasterizerState(&rs_desc,
                                         p->rasterizer_state.GetAddressOf());
    if (FAILED(hr)) return std::unexpected(make_hresult_error(hr));
  }

  // --- 18. Store window dimensions ------------------------------------------
  p->window_width  = window_size.width;
  p->window_height = window_size.height;

  // Wrap and return.
  glyph_renderer result{ glyph_renderer::empty_tag{} };
  result.impl_ = std::move(p);
  return result;
}

// ===========================================================================
// Shared vertex-write helper
// ===========================================================================

namespace {

// Write four vertices for one textured glyph quad into the mapped buffer.
// `quad_idx` is zero-based; the helper computes vertex indices [4*q, 4*q+3].
inline void write_glyph_quad(glyph_vertex* vertices, uint32_t quad_idx,
                              float x0, float y0, float x1, float y1,
                              float u0, float v0, float u1, float v1,
                              float r, float g, float b, uint32_t tex_id) {
  uint32_t v = quad_idx * 4;
  vertices[v + 0] = { x0, y0, u0, v0, r, g, b, tex_id };
  vertices[v + 1] = { x1, y0, u1, v0, r, g, b, tex_id };
  vertices[v + 2] = { x1, y1, u1, v1, r, g, b, tex_id };
  vertices[v + 3] = { x0, y1, u0, v1, r, g, b, tex_id };
}

} // anonymous namespace

// ===========================================================================
// glyph_renderer::draw
// ===========================================================================

auto glyph_renderer::draw(d3d_device const& device,
                           d3d_render_target_view const& rtv,
                           std::span<const char> text) const
  -> std::expected<void, std::error_code> {

  auto* context = device.impl_->context.Get();

  // --- 1. Map vertex buffer ------------------------------------------------
  D3D11_MAPPED_SUBRESOURCE mapped{};
  HRESULT hr = context->Map(impl_->vertex_buffer.Get(), 0,
                              D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    return std::unexpected(make_hresult_error(hr));
  }

  auto* vertices = static_cast<glyph_vertex*>(mapped.pData);
  uint32_t col = 0;
  uint32_t quad_count = 0;

  for (size_t i = 0; i < text.size() && quad_count < k_max_glyphs_per_frame; ++i) {
    unsigned char cp = static_cast<unsigned char>(text[i]);
    if (cp > 127) continue;  // ASCII only — caller should ensure valid input

    auto& slot = impl_->glyph_slots[cp];
    float x0 = static_cast<float>(col * impl_->cell_width);
    float y0 = 0.0f;  // row 0 for now
    float x1 = x0 + static_cast<float>(impl_->cell_width);
    float y1 = y0 + static_cast<float>(impl_->cell_height);

    // Quad vertices (top-left, top-right, bottom-right, bottom-left).
    // Default white colour — no per-glyph colour without a grid.
    write_glyph_quad(vertices, quad_count,
                     x0, y0, x1, y1,
                     slot.u0, slot.v0, slot.u1, slot.v1,
                     1.0f, 1.0f, 1.0f,
                     static_cast<uint32_t>(atlas_kind::static_atlas));

    ++col;
    ++quad_count;
  }

  context->Unmap(impl_->vertex_buffer.Get(), 0);

  if (quad_count == 0) return {};

  // --- 2. Bind pipeline state ----------------------------------------------
  context->OMSetRenderTargets(1, rtv.impl_->rtv.GetAddressOf(), nullptr);
  context->OMSetBlendState(impl_->blend_state.Get(), nullptr, 0xFFFFFFFF);
  context->OMSetDepthStencilState(impl_->depth_state.Get(), 0);
  context->RSSetState(impl_->rasterizer_state.Get());

  // Set viewport to cover the full window (D3D11 does not auto-set this).
  D3D11_VIEWPORT vp{};
  vp.Width    = static_cast<FLOAT>(impl_->window_width);
  vp.Height   = static_cast<FLOAT>(impl_->window_height);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  context->RSSetViewports(1, &vp);

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
  context->PSSetShaderResources(1, 1, impl_->dyn_atlas_srv.GetAddressOf());
  context->PSSetSamplers(0, 1, impl_->sampler_state.GetAddressOf());

  // --- 3. Draw -------------------------------------------------------------
  context->DrawIndexed(quad_count * k_indices_per_quad, 0, 0);

  return {};
}


// ===========================================================================
// glyph_renderer::draw_text — multi-line text rendering
// ===========================================================================

auto glyph_renderer::draw_text(d3d_device const& device,
                                d3d_render_target_view const& rtv,
                                std::span<std::string_view const> lines,
                                uint32_t start_row) const
  -> std::expected<void, std::error_code> {

  auto* context = device.impl_->context.Get();

  // Compute max columns that fit in the window.
  uint32_t const max_cols = impl_->window_width / impl_->cell_width;
  // Compute max rows that fit in the window.
  uint32_t const max_rows = impl_->window_height / impl_->cell_height;

  // --- 1. Map vertex buffer ------------------------------------------------
  D3D11_MAPPED_SUBRESOURCE mapped{};
  HRESULT hr = context->Map(impl_->vertex_buffer.Get(), 0,
                              D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    return std::unexpected(make_hresult_error(hr));
  }

  auto* vertices = static_cast<glyph_vertex*>(mapped.pData);
  uint32_t quad_count = 0;

  for (uint32_t row_idx = 0; row_idx < lines.size(); ++row_idx) {
    uint32_t const screen_row = start_row + row_idx;
    if (screen_row >= max_rows) break;  // off-screen — skip remaining lines

    float const y0 = static_cast<float>(screen_row * impl_->cell_height);
    float const y1 = y0 + static_cast<float>(impl_->cell_height);

    auto const& line = lines[row_idx];
    uint32_t const line_len = std::min(static_cast<uint32_t>(line.size()), max_cols);

    for (uint32_t col = 0; col < line_len && quad_count < k_max_glyphs_per_frame; ++col) {
      unsigned char cp = static_cast<unsigned char>(line[col]);

      // Non-ASCII -> use '?' glyph (codepoint 63).
      if (cp > 127) cp = '?';

      auto& slot = impl_->glyph_slots[cp];
      float const x0 = static_cast<float>(col * impl_->cell_width);
      float const x1 = x0 + static_cast<float>(impl_->cell_width);

      // Quad vertices (top-left, top-right, bottom-right, bottom-left).
      write_glyph_quad(vertices, quad_count,
                       x0, y0, x1, y1,
                       slot.u0, slot.v0, slot.u1, slot.v1,
                       1.0f, 1.0f, 1.0f,
                       static_cast<uint32_t>(atlas_kind::static_atlas));

      ++quad_count;
    }
  }

  context->Unmap(impl_->vertex_buffer.Get(), 0);

  if (quad_count == 0) return {};

  // --- 2. Bind pipeline state ----------------------------------------------
  context->OMSetRenderTargets(1, rtv.impl_->rtv.GetAddressOf(), nullptr);
  context->OMSetBlendState(impl_->blend_state.Get(), nullptr, 0xFFFFFFFF);
  context->OMSetDepthStencilState(impl_->depth_state.Get(), 0);
  context->RSSetState(impl_->rasterizer_state.Get());

  D3D11_VIEWPORT vp{};
  vp.Width    = static_cast<FLOAT>(impl_->window_width);
  vp.Height   = static_cast<FLOAT>(impl_->window_height);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  context->RSSetViewports(1, &vp);

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
  context->PSSetShaderResources(1, 1, impl_->dyn_atlas_srv.GetAddressOf());
  context->PSSetSamplers(0, 1, impl_->sampler_state.GetAddressOf());

  // --- 3. Draw -------------------------------------------------------------
  context->DrawIndexed(quad_count * k_indices_per_quad, 0, 0);

  return {};
}

// ===========================================================================
// glyph_renderer::draw_grid — render a full terminal grid with per-cell colours
// ===========================================================================

auto glyph_renderer::draw_grid(d3d_device const& device,
                                d3d_render_target_view const& rtv,
                                std::span<const render_cell> cells,
                                size2d dims, point2d cursor) const
  -> std::expected<void, std::error_code> {

  auto* context = device.impl_->context.Get();

  // Clamp to the actual window dimensions.
  uint32_t const max_cols = impl_->window_width / impl_->cell_width;
  uint32_t const max_rows = impl_->window_height / impl_->cell_height;
  uint32_t const draw_cols = std::min(dims.width, max_cols);
  uint32_t const draw_rows = std::min(dims.height, max_rows);

  // --- 1. Map vertex buffer ------------------------------------------------
  D3D11_MAPPED_SUBRESOURCE mapped{};
  HRESULT hr = context->Map(impl_->vertex_buffer.Get(), 0,
                              D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr)) {
    return std::unexpected(make_hresult_error(hr));
  }

  auto* vertices = static_cast<glyph_vertex*>(mapped.pData);
  uint32_t quad_count = 0;

  // Helper: emit a background quad (solid colour, no texture).
  auto emit_bg_quad = [&](float x0, float y0, float x1, float y1,
                           float r, float g, float b) {
    if (quad_count >= k_max_glyphs_per_frame) return;
    write_glyph_quad(vertices, quad_count,
                     x0, y0, x1, y1,
                     0.0f, 0.0f, 0.0f, 0.0f,
                     r, g, b,
                     static_cast<uint32_t>(atlas_kind::none));
    ++quad_count;
  };

  // Helper: emit a textured glyph quad.
  auto emit_glyph_quad = [&](float x0, float y0, float x1, float y1,
                              float u0, float v0, float u1, float v1,
                              float r, float g, float b,
                              atlas_kind kind) {
    if (quad_count >= k_max_glyphs_per_frame) return;
    write_glyph_quad(vertices, quad_count,
                     x0, y0, x1, y1,
                     u0, v0, u1, v1,
                     r, g, b,
                     static_cast<uint32_t>(kind));
    ++quad_count;
  };

  // Attribute bit constants (canonical: terminal::cell_attr).
  using terminal::cell_attr;
  constexpr uint8_t k_attr_bold          = terminal::to_uint8(cell_attr::bold);
  constexpr uint8_t k_attr_italic        = terminal::to_uint8(cell_attr::italic);
  constexpr uint8_t k_attr_faint         = terminal::to_uint8(cell_attr::faint);
  constexpr uint8_t k_attr_underline     = terminal::to_uint8(cell_attr::underline);
  constexpr uint8_t k_attr_strikethrough = terminal::to_uint8(cell_attr::strikethrough);
  constexpr uint8_t k_attr_reverse       = terminal::to_uint8(cell_attr::reverse);
  constexpr uint8_t k_attr_wide          = terminal::to_uint8(cell_attr::wide);
  constexpr uint8_t k_attr_wide_tail     = terminal::to_uint8(cell_attr::wide_tail);

  // Only draw the cursor if it lies within the visible area.
  bool const cursor_visible =
      cursor.row < draw_rows && cursor.col < draw_cols;

  for (uint32_t row = 0; row < draw_rows; ++row) {
    float const y0 = static_cast<float>(row * impl_->cell_height);
    float const y1 = y0 + static_cast<float>(impl_->cell_height);

    for (uint32_t col = 0; col < draw_cols && quad_count < k_max_glyphs_per_frame; ) {
      size_t const idx = static_cast<size_t>(row) * dims.width + col;
      if (idx >= cells.size()) break;

      auto const& cell = cells[idx];

      // Skip wide_tail cells — they are rendered as part of the preceding wide cell.
      if (cell.attr & k_attr_wide_tail) {
        ++col;
        continue;
      }

      bool const is_wide = (cell.attr & k_attr_wide) != 0;
      float const glyph_cell_width = is_wide ? k_wide_cell_factor : k_normal_cell_factor;
      float const x0 = static_cast<float>(col * impl_->cell_width);
      float const x1 = x0 + glyph_cell_width * static_cast<float>(impl_->cell_width);

      bool const is_cursor =
        cursor_visible && row == cursor.row && col == cursor.col;

      // Resolve effective reverse: cell SGR reverse XOR cursor.
      // The cursor already swaps fg/bg, so if both are active they cancel.
      bool const cell_reverse = (cell.attr & k_attr_reverse) != 0;
      bool const effective_reverse = cell_reverse != is_cursor;

      // Determine fg/bg colours, swapping if reverse is effective.
      auto const& fg_src = effective_reverse ? cell.bg : cell.fg;
      auto const& bg_src = effective_reverse ? cell.fg : cell.bg;

      float const br = static_cast<float>(bg_src.r) / k_color_norm_div;
      float const bgg = static_cast<float>(bg_src.g) / k_color_norm_div;
      float const bb = static_cast<float>(bg_src.b) / k_color_norm_div;

      // Faint reduces glyph intensity. Background is unaffected.
      float const intensity = (cell.attr & k_attr_faint) ? k_faint_intensity : k_full_intensity;
      float const fr = static_cast<float>(fg_src.r) / k_color_norm_div * intensity;
      float const fg_f = static_cast<float>(fg_src.g) / k_color_norm_div * intensity;
      float const fb = static_cast<float>(fg_src.b) / k_color_norm_div * intensity;

      // Background quad — always emitted.
      emit_bg_quad(x0, y0, x1, y1, br, bgg, bb);

      // Foreground glyph — only if the cell is not a space.
      char32_t const cp = cell.codepoint;
      if (cp != U' ' && cp != 0) {
        if (cp <= k_ascii_max) {
          unsigned char const glyph = static_cast<unsigned char>(cp);

          // Italic uses upper half of atlas slots.
          uint32_t const slot_idx =
              glyph + ((cell.attr & k_attr_italic) ? k_italic_slot_offset : 0u);
          auto& slot = impl_->glyph_slots[slot_idx];

          emit_glyph_quad(x0, y0, x1, y1,
                         slot.u0, slot.v0, slot.u1, slot.v1,
                         fr, fg_f, fb, atlas_kind::static_atlas);

          // Bold: synthetic double-draw with 1px horizontal offset.
          if (cell.attr & k_attr_bold) {
            emit_glyph_quad(x0 + k_bold_offset_px, y0, x1 + k_bold_offset_px, y1,
                           slot.u0, slot.v0, slot.u1, slot.v1,
                           fr, fg_f, fb, atlas_kind::static_atlas);
          }
        } else {
          // Non-ASCII: look up in dynamic atlas.
          auto it = impl_->dyn_index_.find(cp);
          if (it != impl_->dyn_index_.end()) {
            auto& slot = impl_->dyn_glyph_slots[it->second];

            emit_glyph_quad(x0, y0, x1, y1,
                           slot.u0, slot.v0, slot.u1, slot.v1,
                           fr, fg_f, fb, atlas_kind::dyn_atlas);

            if (cell.attr & k_attr_bold) {
              emit_glyph_quad(x0 + k_bold_offset_px, y0, x1 + k_bold_offset_px, y1,
                             slot.u0, slot.v0, slot.u1, slot.v1,
                             fr, fg_f, fb, atlas_kind::dyn_atlas);
            }
          }
          // If glyph not in cache, silently skip (shouldn't happen after prepare).
        }
      }

      // Underline: thin solid quad at cell bottom.
      if (cell.attr & k_attr_underline) {
        float const uy0 = y1 - k_underline_thickness_px;
        emit_bg_quad(x0, uy0, x1, y1, fr, fg_f, fb);
      }

      // Strikethrough: thin solid quad.
      if (cell.attr & k_attr_strikethrough) {
        float const sy0 = y0 + static_cast<float>(impl_->cell_height) * k_strikethrough_position;
        float const sy1 = sy0 + k_strikethrough_thickness_px;
        emit_bg_quad(x0, sy0, x1, sy1, fr, fg_f, fb);
      }

      // Advance column.
      col += is_wide ? k_wide_col_advance : k_normal_col_advance;
    }
  }

  context->Unmap(impl_->vertex_buffer.Get(), 0);

  if (quad_count == 0) return {};

  // --- 2. Bind pipeline state ----------------------------------------------
  context->OMSetRenderTargets(1, rtv.impl_->rtv.GetAddressOf(), nullptr);
  context->OMSetBlendState(impl_->blend_state.Get(), nullptr, 0xFFFFFFFF);
  context->OMSetDepthStencilState(impl_->depth_state.Get(), 0);
  context->RSSetState(impl_->rasterizer_state.Get());

  D3D11_VIEWPORT vp{};
  vp.Width    = static_cast<FLOAT>(impl_->window_width);
  vp.Height   = static_cast<FLOAT>(impl_->window_height);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  context->RSSetViewports(1, &vp);

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
  context->PSSetShaderResources(1, 1, impl_->dyn_atlas_srv.GetAddressOf());
  context->PSSetSamplers(0, 1, impl_->sampler_state.GetAddressOf());

  // --- 3. Draw -------------------------------------------------------------
  context->DrawIndexed(quad_count * k_indices_per_quad, 0, 0);

  return {};
}

// ===========================================================================
// glyph_renderer::update_dimensions
// ===========================================================================

auto glyph_renderer::update_dimensions(d3d_device const& device,
                                        window_dimensions const& dims) const
    -> std::expected<void, std::error_code> {
  auto* context = device.impl_->context.Get();

  glyph_constants constants{};
  constants.window_width   = static_cast<float>(dims.width);
  constants.window_height  = static_cast<float>(dims.height);
  constants.cell_width     = static_cast<float>(impl_->cell_width);
  constants.cell_height    = static_cast<float>(impl_->cell_height);
  constants.inv_tex_width  = 1.0f / static_cast<float>(impl_->atlas_width);
  constants.inv_tex_height = 1.0f / static_cast<float>(impl_->atlas_height);
  constants._pad[0] = 0.0f;
  constants._pad[1] = 0.0f;

  context->UpdateSubresource(impl_->constant_buffer.Get(), 0, nullptr,
                              &constants, 0, 0);

  impl_->window_width  = dims.width;
  impl_->window_height = dims.height;

  return {};
}

// ===========================================================================
// glyph_renderer::ensure_glyph_cached
// ===========================================================================

auto glyph_renderer::ensure_glyph_cached(char32_t cp, d3d_device const& device) const
    -> uint32_t
{
  auto* ctx = device.impl_->context.Get();
  auto& p = *impl_;

  // Check if already cached.
  auto it = p.dyn_index_.find(cp);
  if (it != p.dyn_index_.end()) {
    uint32_t const slot = it->second;
    p.dyn_access_[slot] = ++p.dyn_clock_;
    return slot;
  }

  // Allocate a slot.
  uint32_t slot = 0;
  if (p.dyn_next_ < p.k_dyn_max_glyphs) {
    slot = p.dyn_next_++;
  } else {
    // LRU eviction: find the slot with the smallest access counter.
    uint64_t min_access = p.dyn_access_[0];
    uint32_t min_slot = 0;
    for (uint32_t i = 1; i < p.k_dyn_max_glyphs; ++i) {
      if (p.dyn_access_[i] < min_access) {
        min_access = p.dyn_access_[i];
        min_slot = i;
      }
    }
    slot = min_slot;
    // Remove the evicted codepoint from the index.
    for (auto const& kv : p.dyn_index_) {
      if (kv.second == slot) {
        p.dyn_index_.erase(kv.first);
        break;
      }
    }
  }

  p.dyn_access_[slot] = ++p.dyn_clock_;

  // Rasterize the glyph into a staging buffer (one slot).
  uint32_t const slot_x = (slot % p.k_dyn_atlas_cols) * p.slot_width;
  uint32_t const slot_y = (slot / p.k_dyn_atlas_cols) * p.slot_height;

  size_t const buf_pixels = static_cast<size_t>(p.slot_width) *
                             static_cast<size_t>(p.slot_height);
  std::vector<uint8_t> staging_buffer(buf_pixels * 4, 0);

  constexpr float k_font_size = static_cast<float>(k_font_size_px);

  // Pass slot origin (0, 0) — since the staging buffer is only one slot,
  // absolute positions would overflow.  origin_x/y computed inside
  // rasterize_glyph become relative to the slot, matching the buffer size.
  bool const rasterized = rasterize_glyph(
    p.dwrite_factory.Get(),
    p.font_face.Get(),
    k_font_size,
    static_cast<uint32_t>(cp),
    0, 0,
    p.slot_width,
    p.cell_width, p.cell_height,
    p.baseline_y,
    staging_buffer
  );

  // Upload the slot region to the dynamic atlas texture.
  if (rasterized) {
    D3D11_BOX box{};
    box.left   = slot_x;
    box.right  = slot_x + p.slot_width;
    box.top    = slot_y;
    box.bottom = slot_y + p.slot_height;
    box.front  = 0;
    box.back   = 1;

    ctx->UpdateSubresource(p.dyn_atlas_texture.Get(), 0, &box,
                            staging_buffer.data(),
                            p.slot_width * 4, 0);
  }

  // Store in index regardless of rasterization success (avoids retrying).
  p.dyn_index_[cp] = slot;
  return slot;
}

// ===========================================================================
// glyph_renderer::prepare_unicode_glyphs
// ===========================================================================

void glyph_renderer::prepare_unicode_glyphs(
    d3d_device const& device,
    std::span<const render_cell> cells) const
{
  constexpr uint8_t k_attr_wide_tail = terminal::to_uint8(terminal::cell_attr::wide_tail);

  for (auto const& cell : cells) {
    if (cell.codepoint <= 127) continue;
    if (cell.codepoint == U' ') continue;
    if (cell.attr & k_attr_wide_tail) continue;
    (void)ensure_glyph_cached(cell.codepoint, device);
  }
}

} // namespace betty::platform