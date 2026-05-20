#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
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
