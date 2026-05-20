#include <cstdio>
#include <string_view>
#include <system_error>

#include "platform/types.hpp"
#include "platform/window.hpp"
#include "platform/gfx.hpp"

namespace platform = betty::platform;

namespace {

void log_error(std::error_code ec, std::string_view context) {
    // Format: "betty: <context>: <message> (<category>:<code>)\n"
    auto msg = ec.message();
    std::fprintf(stderr, "betty: %.*s: %s (%s:%d)\n",
                 static_cast<int>(context.size()), context.data(),
                 msg.c_str(),
                 ec.category().name(),
                 ec.value());
}

} // anonymous namespace

int main() {
    // 1. Create window
    auto window_result = platform::make_window(
        platform::window_settings{
            .size = platform::default_window_size,
            .title = L"betty"
        }
    );
    if (!window_result) {
        log_error(window_result.error(), "create window");
        return 1;
    }
    auto& window = *window_result;

    // 2. Create D3D11 device
    auto device_result = platform::make_device();
    if (!device_result) {
        log_error(device_result.error(), "create device");
        return 1;
    }
    auto& device = *device_result;

    // 3. Create swap chain
    auto swap_chain_result = platform::make_swap_chain(
        device,
        window,
        platform::swap_chain_settings{
            .width = platform::default_window_size.width,
            .height = platform::default_window_size.height
        }
    );
    if (!swap_chain_result) {
        log_error(swap_chain_result.error(), "create swap chain");
        return 1;
    }
    auto& swap_chain = *swap_chain_result;

    // 4. Create render target view
    auto rtv_result = platform::make_render_target_view(device, swap_chain);
    if (!rtv_result) {
        log_error(rtv_result.error(), "create render target view");
        return 1;
    }
    auto& rtv = *rtv_result;

    // 5. Message loop (render on idle)
    while (platform::dispatch_pending_messages()) {
        device.clear(rtv, platform::mocha_base);
        swap_chain.present();
    }

    return 0;
}
