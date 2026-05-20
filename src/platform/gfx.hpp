#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <system_error>
#include "types.hpp"

namespace betty::platform {

// Forward declarations (definitions in window.hpp / this header)
struct win32_window;
struct d3d_swap_chain;
struct d3d_render_target_view;

// --- swap_chain_settings ---------------------------------------------------

struct swap_chain_settings {
  uint32_t width;
  uint32_t height;
  // Buffer count, format, scaling, alpha mode, swap effect — all set
  // to sensible defaults inside make_swap_chain.
  // Note: no HWND — the window is passed directly to make_swap_chain.
};

// --- d3d_device ------------------------------------------------------------

struct d3d_device {
  // Clear the render target view to the given colour.
  // Internally calls OMSetRenderTargets + ClearRenderTargetView.
  auto clear(d3d_render_target_view const& rtv, rgba_color const& color) const -> void;

  d3d_device() = default;
  // Defined as = default in gfx.cpp (where impl is complete).
  ~d3d_device();
  d3d_device(d3d_device&&) noexcept;
  d3d_device& operator=(d3d_device&&) noexcept;
  d3d_device(d3d_device const&) = delete;
  d3d_device& operator=(d3d_device const&) = delete;

private:
  struct impl;
  std::unique_ptr<impl> impl_;

  friend auto make_device() -> std::expected<d3d_device, std::error_code>;
  friend auto make_swap_chain(d3d_device const&, win32_window const&, swap_chain_settings const&)
    -> std::expected<d3d_swap_chain, std::error_code>;
  friend auto make_render_target_view(d3d_device const&, d3d_swap_chain const&)
    -> std::expected<d3d_render_target_view, std::error_code>;
};

auto make_device() -> std::expected<d3d_device, std::error_code>;

// --- d3d_swap_chain --------------------------------------------------------

struct d3d_swap_chain {
  // Present the back buffer to the screen (vsynced).
  // Returns an error on device loss (DXGI_ERROR_DEVICE_REMOVED / _RESET).
  auto present() const -> std::expected<void, std::error_code>;

  d3d_swap_chain() = default;
  // Defined as = default in gfx.cpp (where impl is complete).
  ~d3d_swap_chain();
  d3d_swap_chain(d3d_swap_chain&&) noexcept;
  d3d_swap_chain& operator=(d3d_swap_chain&&) noexcept;
  d3d_swap_chain(d3d_swap_chain const&) = delete;
  d3d_swap_chain& operator=(d3d_swap_chain const&) = delete;

private:
  struct impl;
  std::unique_ptr<impl> impl_;

  friend auto make_swap_chain(d3d_device const&, win32_window const&, swap_chain_settings const&)
    -> std::expected<d3d_swap_chain, std::error_code>;
  friend auto make_render_target_view(d3d_device const&, d3d_swap_chain const&)
    -> std::expected<d3d_render_target_view, std::error_code>;
};

auto make_swap_chain(d3d_device const& device, win32_window const& window, swap_chain_settings const& settings)
  -> std::expected<d3d_swap_chain, std::error_code>;

// --- d3d_render_target_view ------------------------------------------------

struct d3d_render_target_view {
  // Opaque handle. No public methods needed for task 1;
  // d3d_device::clear() accesses it via friendship.

  d3d_render_target_view() = default;
  // Defined as = default in gfx.cpp (where impl is complete).
  ~d3d_render_target_view();
  d3d_render_target_view(d3d_render_target_view&&) noexcept;
  d3d_render_target_view& operator=(d3d_render_target_view&&) noexcept;
  d3d_render_target_view(d3d_render_target_view const&) = delete;
  d3d_render_target_view& operator=(d3d_render_target_view const&) = delete;

private:
  struct impl;
  std::unique_ptr<impl> impl_;

  friend auto make_render_target_view(d3d_device const&, d3d_swap_chain const&)
    -> std::expected<d3d_render_target_view, std::error_code>;
  friend struct d3d_device;  // needs impl_->rtv for OMSetRenderTargets / ClearRenderTargetView
};

auto make_render_target_view(d3d_device const& device, d3d_swap_chain const& swap_chain)
  -> std::expected<d3d_render_target_view, std::error_code>;

} // namespace betty::platform
