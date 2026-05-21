#include <format>
#include <string_view>
#include <system_error>

#include "platform/types.hpp"
#include "platform/window.hpp"
#include "platform/gfx.hpp"
#include "platform/text.hpp"

namespace platform = betty::platform;

namespace {

void log_error(std::error_code ec, std::string_view context) {
  auto formatted = std::format("{}: {} ({}:{})",
                               context,
                               ec.message(),
                               ec.category().name(),
                               ec.value());

  platform::show_error_message("betty", formatted);
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

  // 3. Create swap chain (dimensions come from window_dimensions directly)
  auto swap_chain_result = platform::make_swap_chain(
    device,
    window,
    platform::swap_chain_settings{
      .size = platform::default_window_size
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

  // 5. Create glyph renderer
  auto renderer_result = platform::make_glyph_renderer(
    device,
    platform::default_window_size
  );
  
  if (!renderer_result) {
    log_error(renderer_result.error(), "create glyph renderer");
    return 1;
  }

  auto& renderer = *renderer_result;

  // 6. Message loop (render on idle)
  while (platform::dispatch_pending_messages()) {
    device.clear(rtv, platform::mocha_base);
    if (auto draw_result = renderer.draw(device, rtv, "betty"); !draw_result) {
      log_error(draw_result.error(), "draw glyphs");
      return 1;
    }
    const auto present_result = swap_chain.present();
    if (!present_result) {
      log_error(present_result.error(), "present");
      return 1;
    }
  }

  return 0;
}
