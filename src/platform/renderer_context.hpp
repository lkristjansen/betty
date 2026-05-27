#pragma once
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include "gfx.hpp"
#include "text.hpp"

namespace betty::platform {

// ===========================================================================
// renderer_context — owns the D3D device, swap chain, RTV, and glyph renderer
// ===========================================================================

struct win32_window;

class renderer_context {
public:
  // --- Frame-level API ------------------------------------------------------

  void begin_frame(rgba_color const& clear_color) const;

  [[nodiscard]] auto draw_grid(std::span<render_cell const> cells,
                               size2d dims, std::optional<point2d> cursor, uint32_t padding)
      -> std::expected<void, std::error_code>;

  [[nodiscard]] auto end_frame() const -> std::expected<void, std::error_code>;

  // --- Font ----------------------------------------------------------------

  // Recreate the glyph renderer with new font settings while keeping the
  // D3D device and swap chain intact.  Returns an error if font creation
  // fails (current renderer is left unchanged in that case).
  [[nodiscard]] auto recreate_font(std::string_view font_family,
                                    float font_size_pt)
      -> std::expected<void, std::error_code>;

  // --- Resize ---------------------------------------------------------------

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
                   d3d_render_target_view rtv, glyph_renderer renderer,
                   uint32_t dpi, std::string font_family, float font_size_pt);

  d3d_device device_;
  d3d_swap_chain swap_chain_;
  d3d_render_target_view rtv_;
  glyph_renderer renderer_;

  // Cached font identity for hot-reload.
  uint32_t dpi_ = 96;
  std::string font_family_;
  float font_size_pt_ = 14.0f;

  // Current window dimensions, updated on resize.
  uint32_t window_width_ = 0;
  uint32_t window_height_ = 0;

  friend auto make_renderer_context(win32_window const&, std::string_view, float)
      -> std::expected<renderer_context, std::error_code>;
};

[[nodiscard]] auto make_renderer_context(win32_window const& window,
                                         std::string_view font_family,
                                         float font_size_pt)
    -> std::expected<renderer_context, std::error_code>;

} // namespace betty::platform
