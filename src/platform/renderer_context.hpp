#pragma once
#include <cstdint>
#include <expected>
#include <span>
#include <system_error>
#include "gfx.hpp"
#include "text.hpp"

namespace betty::platform {

// ===========================================================================
// renderer_context — owns the D3D device, swap chain, RTV, and glyph renderer
// ===========================================================================
// Provides a frame-level API: begin_frame() / draw_grid() / end_frame().
// Handles resize internally (swap chain recreation + constant buffer update).

struct win32_window;

class renderer_context {
public:
  // --- Frame-level API ------------------------------------------------------

  // Clear the render target to `clear_color`.
  void begin_frame(rgba_color const& clear_color) const;

  // Draw a terminal grid.  Internally calls prepare_unicode_glyphs() before
  // drawing.  `cells` is a row-major flat array of dims.height × dims.width.
  // `cursor`, if set, specifies which cell to render with reverse video.
  // Pass std::nullopt to suppress the cursor.
  [[nodiscard]] auto draw_grid(std::span<render_cell const> cells,
                               size2d dims, std::optional<point2d> cursor, uint32_t padding)
      -> std::expected<void, std::error_code>;

  // Present the back buffer.  Returns an error on device loss.
  [[nodiscard]] auto end_frame() const -> std::expected<void, std::error_code>;

  // --- Resize ---------------------------------------------------------------

  // Handle a window resize: recreate swap chain buffers and update the
  // renderer's constant buffer.  Returns an error if either operation fails.
  [[nodiscard]] auto handle_resize(uint32_t width, uint32_t height)
      -> std::expected<void, std::error_code>;

  // --- Queries --------------------------------------------------------------

  [[nodiscard]] auto cell_width() const -> uint32_t;
  [[nodiscard]] auto cell_height() const -> uint32_t;

  // --- Move-only ------------------------------------------------------------

  ~renderer_context();
  renderer_context(renderer_context&&) noexcept;
  renderer_context& operator=(renderer_context&&) noexcept;
  renderer_context(renderer_context const&) = delete;
  renderer_context& operator=(renderer_context const&) = delete;

private:
  renderer_context(d3d_device device, d3d_swap_chain swap_chain,
                   d3d_render_target_view rtv, glyph_renderer renderer);

  d3d_device device_;
  d3d_swap_chain swap_chain_;
  d3d_render_target_view rtv_;
  glyph_renderer renderer_;

  friend auto make_renderer_context(win32_window const&)
      -> std::expected<renderer_context, std::error_code>;
};

// Create a renderer_context for the given window.
// Internally creates the device, swap chain, RTV, and glyph renderer.
// Returns std::unexpected on any fatal failure.
[[nodiscard]] auto make_renderer_context(win32_window const& window)
    -> std::expected<renderer_context, std::error_code>;

} // namespace betty::platform
