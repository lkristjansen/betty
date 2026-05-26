#include "renderer_context.hpp"
#include "window.hpp"
#include "util/log.hpp"

namespace betty::platform {

// ===========================================================================
// renderer_context — move-only semantics
// ===========================================================================

renderer_context::~renderer_context() = default;
renderer_context::renderer_context(renderer_context&&) noexcept = default;
renderer_context& renderer_context::operator=(renderer_context&&) noexcept = default;

renderer_context::renderer_context(d3d_device device, d3d_swap_chain swap_chain,
                                   d3d_render_target_view rtv, glyph_renderer renderer)
    : device_(std::move(device))
    , swap_chain_(std::move(swap_chain))
    , rtv_(std::move(rtv))
    , renderer_(std::move(renderer)) {}

// ===========================================================================
// Frame-level API
// ===========================================================================

void renderer_context::begin_frame(rgba_color const& clear_color) const {
  device_.clear(rtv_, clear_color);
}

auto renderer_context::draw_grid(std::span<render_cell const> cells,
                                  size2d dims, std::optional<point2d> cursor, uint32_t padding) const
    -> std::expected<void, std::error_code> {
  if (cells.empty()) return {};

  renderer_.prepare_unicode_glyphs(device_, cells);
  return renderer_.draw_grid(device_, rtv_, cells, dims, cursor, padding);
}

auto renderer_context::end_frame() const -> std::expected<void, std::error_code> {
  return swap_chain_.present();
}

// ===========================================================================
// Resize
// ===========================================================================

auto renderer_context::handle_resize(uint32_t width, uint32_t height)
    -> std::expected<void, std::error_code> {
  if (width == 0 || height == 0) return {};

  window_dimensions const new_dims{width, height};

  // Resize swap chain → new RTV.
  auto new_rtv = resize_swap_chain(device_, swap_chain_, std::move(rtv_), new_dims);
  if (!new_rtv) {
    return std::unexpected(new_rtv.error());
  }
  rtv_ = std::move(*new_rtv);

  // Update renderer constant buffer.
  auto dims_result = renderer_.update_dimensions(device_, new_dims);
  if (!dims_result) {
    return std::unexpected(dims_result.error());
  }
  return {};
}

// ===========================================================================
// Queries
// ===========================================================================

auto renderer_context::cell_width() const -> uint32_t {
  return renderer_.cell_width();
}

auto renderer_context::cell_height() const -> uint32_t {
  return renderer_.cell_height();
}



// ===========================================================================
// Factory
// ===========================================================================

auto make_renderer_context(win32_window const& window)
    -> std::expected<renderer_context, std::error_code> {

  // 1. Create D3D11 device.
  auto device_result = make_device();
  if (!device_result) {
    return std::unexpected(device_result.error());
  }
  auto device = std::move(*device_result);

  // 2. Query client area dimensions.
  auto const size = get_client_size(window);

  // 3. Create swap chain.
  auto swap_chain_result = make_swap_chain(
      device, window,
      swap_chain_settings{.size = size});
  if (!swap_chain_result) {
    return std::unexpected(swap_chain_result.error());
  }
  auto swap_chain = std::move(*swap_chain_result);

  // 4. Create render target view.
  auto rtv_result = make_render_target_view(device, swap_chain);
  if (!rtv_result) {
    return std::unexpected(rtv_result.error());
  }
  auto rtv = std::move(*rtv_result);

  // 5. Create glyph renderer.
  auto renderer_result = make_glyph_renderer(device, size);
  if (!renderer_result) {
    return std::unexpected(renderer_result.error());
  }
  auto renderer = std::move(*renderer_result);

  return renderer_context{std::move(device), std::move(swap_chain),
                           std::move(rtv), std::move(renderer)};
}

} // namespace betty::platform
