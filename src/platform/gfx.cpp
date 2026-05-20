#include "gfx.hpp"
#include "window.hpp"
#include "error.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace betty::platform {

// ===========================================================================
// PIMPL definitions (private to this translation unit)
// ===========================================================================

struct d3d_device::impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
};

struct d3d_swap_chain::impl {
    ComPtr<IDXGISwapChain1> swap_chain;
};

struct d3d_render_target_view::impl {
    ComPtr<ID3D11RenderTargetView> rtv;
};

// ===========================================================================
// d3d_device
// ===========================================================================

d3d_device::d3d_device() = default;
d3d_device::~d3d_device() = default;
d3d_device::d3d_device(d3d_device&&) noexcept = default;
d3d_device& d3d_device::operator=(d3d_device&&) noexcept = default;

void d3d_device::clear(d3d_render_target_view const& rtv, rgba_color const& color) const {
    impl_->context->OMSetRenderTargets(1, rtv.impl_->rtv.GetAddressOf(), nullptr);
    impl_->context->ClearRenderTargetView(rtv.impl_->rtv.Get(), &color.r);
}

auto make_device() -> std::expected<d3d_device, std::error_code> {
    UINT flags = 0;
#ifndef NDEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL selected_level;

    HRESULT hr = D3D11CreateDevice(
        nullptr,                            // pAdapter
        D3D_DRIVER_TYPE_HARDWARE,           // DriverType
        nullptr,                            // Software
        flags,                              // Flags
        feature_levels,                     // pFeatureLevels
        ARRAYSIZE(feature_levels),          // FeatureLevels
        D3D11_SDK_VERSION,                  // SDKVersion
        device.GetAddressOf(),              // ppDevice
        &selected_level,                    // pFeatureLevel
        context.GetAddressOf()              // ppImmediateContext
    );

    if (FAILED(hr)) {
        return std::unexpected(make_d3d_error(hr));
    }

    d3d_device result;
    result.impl_ = std::make_unique<d3d_device::impl>();
    result.impl_->device = std::move(device);
    result.impl_->context = std::move(context);
    return result;
}

// ===========================================================================
// d3d_swap_chain
// ===========================================================================

d3d_swap_chain::d3d_swap_chain() = default;
d3d_swap_chain::~d3d_swap_chain() = default;
d3d_swap_chain::d3d_swap_chain(d3d_swap_chain&&) noexcept = default;
d3d_swap_chain& d3d_swap_chain::operator=(d3d_swap_chain&&) noexcept = default;

void d3d_swap_chain::present() const {
    impl_->swap_chain->Present(1, 0);  // vsync on
}

auto make_swap_chain(d3d_device const& device, win32_window const& window, swap_chain_settings const& settings)
    -> std::expected<d3d_swap_chain, std::error_code> {

    // 1. Create DXGI factory
    UINT factory_flags = 0;
#ifndef NDEBUG
    factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    ComPtr<IDXGIFactory2> factory;
    HRESULT hr = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        return std::unexpected(make_d3d_error(hr));
    }

    // 2. Describe swap chain
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = settings.width;
    desc.Height = settings.height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = 0;

    // 3. Create swap chain for window
    ComPtr<IDXGISwapChain1> swap_chain;
    hr = factory->CreateSwapChainForHwnd(
        device.impl_->device.Get(),
        window.handle_,
        &desc,
        nullptr,
        nullptr,
        swap_chain.GetAddressOf()
    );

    if (FAILED(hr)) {
        return std::unexpected(make_d3d_error(hr));
    }

    d3d_swap_chain result;
    result.impl_ = std::make_unique<d3d_swap_chain::impl>();
    result.impl_->swap_chain = std::move(swap_chain);
    return result;
}

// ===========================================================================
// d3d_render_target_view
// ===========================================================================

d3d_render_target_view::d3d_render_target_view() = default;
d3d_render_target_view::~d3d_render_target_view() = default;
d3d_render_target_view::d3d_render_target_view(d3d_render_target_view&&) noexcept = default;
d3d_render_target_view& d3d_render_target_view::operator=(d3d_render_target_view&&) noexcept = default;

auto make_render_target_view(d3d_device const& device, d3d_swap_chain const& swap_chain)
    -> std::expected<d3d_render_target_view, std::error_code> {

    // 1. Get the back buffer from the swap chain
    ComPtr<ID3D11Texture2D> back_buffer;
    HRESULT hr = swap_chain.impl_->swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf()));
    if (FAILED(hr)) {
        return std::unexpected(make_d3d_error(hr));
    }

    // 2. Create render target view
    ComPtr<ID3D11RenderTargetView> rtv;
    hr = device.impl_->device->CreateRenderTargetView(back_buffer.Get(), nullptr, rtv.GetAddressOf());
    if (FAILED(hr)) {
        return std::unexpected(make_d3d_error(hr));
    }

    d3d_render_target_view result;
    result.impl_ = std::make_unique<d3d_render_target_view::impl>();
    result.impl_->rtv = std::move(rtv);
    return result;
}

} // namespace betty::platform
