#pragma once
// Internal header: defines the PIMPL structs for d3d_device, d3d_swap_chain,
// and d3d_render_target_view. Only included from .cpp files that need
// concrete access to the D3D objects behind the opaque handles.
//
// Friends declared in gfx.hpp (glyph_renderer, make_* factories) need the
// complete type to access impl_->device / impl_->context / impl_->rtv.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

namespace betty::platform {

struct d3d_device::impl {
  Microsoft::WRL::ComPtr<ID3D11Device>        device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
};

struct d3d_swap_chain::impl {
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain;
};

struct d3d_render_target_view::impl {
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
};

} // namespace betty::platform
