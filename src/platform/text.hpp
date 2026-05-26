#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include "types.hpp"

namespace betty::terminal { struct grid_cell; }

namespace betty::platform {

// Opaque forward declarations from gfx.hpp
struct d3d_device;
struct d3d_render_target_view;

// --- glyph_renderer ---------------------------------------------------------

// Renders monospace glyphs from a pre-baked texture atlas.
// Supports per-cell foreground/background colours via vertex colour data.
struct glyph_renderer {
  // Accessors for computed font metrics (available after successful creation).
  auto cell_width() const -> uint32_t;
  auto cell_height() const -> uint32_t;

  // Update the constant buffer with new window dimensions.
  // Must be called after a window resize so the vertex shader's NDC
  // transform uses the correct pixel dimensions.
  [[nodiscard]] auto update_dimensions(d3d_device const& device,
                                        window_dimensions const& dims)
      -> std::expected<void, std::error_code>;

  // Pre-rasterize any non-ASCII glyphs in `cells` that aren't already cached.
  // Must be called once per frame before draw_grid().
  void prepare_unicode_glyphs(d3d_device const& device,
                              std::span<const render_cell> cells);

  // Draw a terminal grid. `cells` is a row-major flat array of `dims.height`
  // rows × `dims.width` cols. Each render_cell carries a resolved codepoint
  // and fg/bg colours. Background quads are always emitted.
  // Non-ASCII codepoints are rendered from the dynamic glyph cache
  // (populated by prepare_unicode_glyphs).  Wide characters span 2 cells.
  //
  // `cursor`, if set, specifies which cell to render with reverse video
  // (foreground/background swapped).  Pass std::nullopt to suppress the cursor.
  // `padding` offsets the grid origin from the top-left corner in pixels.
  [[nodiscard]] auto draw_grid(d3d_device const& device, d3d_render_target_view const& rtv,
                                std::span<const render_cell> cells,
                                size2d dims, std::optional<point2d> cursor, uint32_t padding)
      -> std::expected<void, std::error_code>;

  ~glyph_renderer();
  glyph_renderer(glyph_renderer&&) noexcept;
  glyph_renderer& operator=(glyph_renderer&&) noexcept;
  glyph_renderer(glyph_renderer const&) = delete;
  glyph_renderer& operator=(glyph_renderer const&) = delete;

private:
  struct empty_tag {};
  explicit glyph_renderer(empty_tag) noexcept;

  struct impl;
  std::unique_ptr<impl> impl_;

  // Bind the full D3D pipeline state and issue a DrawIndexed call for
  // `quad_count` quads.  The vertex buffer must already be filled.
  void bind_pipeline_and_draw(d3d_device const& device,
                               d3d_render_target_view const& rtv,
                               uint32_t quad_count) const;

  // Ensure a non-ASCII glyph is in the dynamic atlas cache, rasterizing if needed.
  // Returns the slot index or SIZE_MAX on failure.
  [[nodiscard]] auto ensure_glyph_cached(char32_t cp, struct d3d_device const& device)
      -> uint32_t;

  friend auto make_glyph_renderer(d3d_device const&, window_dimensions const&)
    -> std::expected<glyph_renderer, std::error_code>;
};

// --- factory ----------------------------------------------------------------

// Creates the glyph renderer. The device is needed for texture/buffer creation.
// The window dimensions are used for the constant buffer's orthographic transform.
[[nodiscard]] auto make_glyph_renderer(d3d_device const& device, window_dimensions const& window_size)
  -> std::expected<glyph_renderer, std::error_code>;

} // namespace betty::platform
