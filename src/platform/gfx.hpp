#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <system_error>
#include "types.hpp"

namespace betty::platform {

// Forward declarations
struct win32_window;
struct d3d_swap_chain;
struct d3d_render_target_view;
struct glyph_renderer;

// --- swap_chain_settings ---------------------------------------------------
// Derives dimensions directly from window_dimensions to avoid mismatches.

struct swap_chain_settings {
  window_dimensions size;
  // Buffer count, format, scaling, alpha mode, swap effect — all set
  // to sensible defaults inside make_swap_chain.
  // Note: no HWND — the window is passed directly to make_swap_chain.
};

// --- d3d_device ------------------------------------------------------------

struct d3d_device {
  // Clear the render target view to the given colour.
  // Internally calls OMSetRenderTargets + ClearRenderTargetView.
  auto clear(d3d_render_target_view const& rtv, rgba_color const& color) const -> void;

  // Defined as = default in gfx.cpp (where impl is complete).
  ~d3d_device();
  d3d_device(d3d_device&&) noexcept;
  d3d_device& operator=(d3d_device&&) noexcept;
  d3d_device(d3d_device const&) = delete;
  d3d_device& operator=(d3d_device const&) = delete;

private:
  struct empty_tag {};
  explicit d3d_device(empty_tag) noexcept;

  struct impl;
  std::unique_ptr<impl> impl_;

  friend auto make_device() -> std::expected<d3d_device, std::error_code>;
  friend auto make_swap_chain(d3d_device const&, win32_window const&, swap_chain_settings const&)
    -> std::expected<d3d_swap_chain, std::error_code>;
  friend auto make_render_target_view(d3d_device const&, d3d_swap_chain const&)
    -> std::expected<d3d_render_target_view, std::error_code>;
  friend struct glyph_renderer;  // needs impl_->context for drawing
  friend auto make_glyph_renderer(d3d_device const&,
                                    std::string_view, float, uint32_t,
                                    window_dimensions const&)
    -> std::expected<glyph_renderer, std::error_code>;  // needs impl_->device / context
  friend auto resize_swap_chain(d3d_device const&, d3d_swap_chain&,
                                d3d_render_target_view, window_dimensions)
    -> std::expected<d3d_render_target_view, std::error_code>;
};

[[nodiscard]] auto make_device() -> std::expected<d3d_device, std::error_code>;

// --- d3d_swap_chain --------------------------------------------------------

struct d3d_swap_chain {
  // Present the back buffer to the screen (vsynced).
  // Returns an error on device loss (DXGI_ERROR_DEVICE_REMOVED / _RESET).
  [[nodiscard]] auto present() const -> std::expected<void, std::error_code>;

  // Defined as = default in gfx.cpp (where impl is complete).
  ~d3d_swap_chain();
  d3d_swap_chain(d3d_swap_chain&&) noexcept;
  d3d_swap_chain& operator=(d3d_swap_chain&&) noexcept;
  d3d_swap_chain(d3d_swap_chain const&) = delete;
  d3d_swap_chain& operator=(d3d_swap_chain const&) = delete;

private:
  struct empty_tag {};
  explicit d3d_swap_chain(empty_tag) noexcept;

  struct impl;
  std::unique_ptr<impl> impl_;

  friend auto make_swap_chain(d3d_device const&, win32_window const&, swap_chain_settings const&)
    -> std::expected<d3d_swap_chain, std::error_code>;
  friend auto make_render_target_view(d3d_device const&, d3d_swap_chain const&)
    -> std::expected<d3d_render_target_view, std::error_code>;
  friend auto resize_swap_chain(d3d_device const&, d3d_swap_chain&,
                                d3d_render_target_view, window_dimensions)
    -> std::expected<d3d_render_target_view, std::error_code>;
};

[[nodiscard]] auto make_swap_chain(d3d_device const& device, win32_window const& window,
                                    swap_chain_settings const& settings)
  -> std::expected<d3d_swap_chain, std::error_code>;

// --- d3d_render_target_view ------------------------------------------------

struct d3d_render_target_view {
  // Defined as = default in gfx.cpp (where impl is complete).
  ~d3d_render_target_view();
  d3d_render_target_view(d3d_render_target_view&&) noexcept;
  d3d_render_target_view& operator=(d3d_render_target_view&&) noexcept;
  d3d_render_target_view(d3d_render_target_view const&) = delete;
  d3d_render_target_view& operator=(d3d_render_target_view const&) = delete;

  [[nodiscard]] explicit operator bool() const noexcept { return impl_ != nullptr; }

private:
  struct empty_tag {};
  explicit d3d_render_target_view(empty_tag) noexcept;

  struct impl;
  std::unique_ptr<impl> impl_;

  friend auto make_render_target_view(d3d_device const&, d3d_swap_chain const&)
    -> std::expected<d3d_render_target_view, std::error_code>;
  friend struct d3d_device;  // needs impl_->rtv for OMSetRenderTargets / ClearRenderTargetView
  friend struct glyph_renderer;  // needs impl_->rtv for OMSetRenderTargets
  friend auto resize_swap_chain(d3d_device const&, d3d_swap_chain&,
                                d3d_render_target_view, window_dimensions)
    -> std::expected<d3d_render_target_view, std::error_code>;
};

[[nodiscard]] auto make_render_target_view(d3d_device const& device, d3d_swap_chain const& swap_chain)
  -> std::expected<d3d_render_target_view, std::error_code>;

// Resize the swap chain buffers and create a new render target view.
// Takes ownership of the old RTV (passed by value) and releases it before
// calling IDXGISwapChain::ResizeBuffers.  Returns a new RTV for the resized
// back buffer on success.
[[nodiscard]] auto resize_swap_chain(
    d3d_device const& device,
    d3d_swap_chain& swap_chain,
    d3d_render_target_view old_rtv,
    window_dimensions new_size)
  -> std::expected<d3d_render_target_view, std::error_code>;

} // namespace betty::platform
