#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "platform/types.hpp"
#include "platform/window.hpp"
#include "platform/gfx.hpp"
#include "platform/text.hpp"
#include "platform/shell.hpp"
#include "terminal/grid.hpp"
#include "terminal/input_handler.hpp"

namespace platform = betty::platform;
namespace terminal = betty::terminal;

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

  // 3. Create swap chain
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

  // 6. Compute terminal dimensions
  uint32_t const cell_w = renderer.cell_width();
  uint32_t const cell_h = renderer.cell_height();
  uint32_t const cols = platform::default_window_size.width / cell_w;
  uint32_t const rows = platform::default_window_size.height / cell_h;

  // 7. State
  terminal::terminal_grid grid(cols, rows);
  terminal::input_handler input;

  // 8. Create shell
  std::unique_ptr<platform::shell> shell;

  auto shell_result = platform::make_shell(platform::shell_settings{
    .cols = cols,
    .rows = rows
  });

  if (shell_result) {
    shell = std::move(*shell_result);
  } else {
    log_error(shell_result.error(), "Failed to create shell process");
    grid.write_bytes("Failed to create shell process.\r\n");
  }



  // 9. Keyboard callback — forward all input to the ConPTY shell.
  //    WM_KEYDOWN exclusively: on_keydown handles everything (printable,
  //    control keys, arrows, Ctrl combos).  No WM_CHAR needed.
  platform::set_key_callback(window,
    [&](platform::vk_code vk, bool ctrl, bool shift, bool alt) {
      if (!shell || !platform::is_shell_running(*shell)) return;

      std::string bytes = input.on_keydown(vk, ctrl, shift, alt);
      if (!bytes.empty()) {
        (void) platform::write_shell_input(*shell, bytes);
      }
    });

  // 10. Message loop
  int exit_code = 0;
  while (platform::dispatch_pending_messages()) {
    device.clear(rtv, platform::mocha_base);

    // Read shell output
    if (shell && platform::is_shell_running(*shell)) {
      std::string raw = platform::read_shell_output_raw(*shell);
      if (!raw.empty()) {
        grid.write_bytes(raw);
      }
    } else if (shell && !platform::is_shell_running(*shell)) {
      static bool shown_exit = false;
      if (!shown_exit) {
        grid.write_bytes("[shell exited]\r\n");
        shown_exit = true;
      }
      // Drain remaining output
      std::string raw = platform::read_shell_output_raw(*shell);
      if (!raw.empty()) {
        grid.write_bytes(raw);
      }
    }

    // Render the grid
    auto const& cells = grid.cells();
    if (cells.size() > 0) {
      std::vector<char32_t> codepoints;
      codepoints.reserve(cells.size());
      for (auto const& cell : cells) {
        codepoints.push_back(cell.codepoint);
      }
      if (auto draw_result = renderer.draw_grid(device, rtv, codepoints, grid.cols(), grid.rows());
          !draw_result) {
        log_error(draw_result.error(), "draw grid");
        exit_code = 1;
        break;
      }
    }

    if (auto present_result = swap_chain.present(); !present_result) {
      log_error(present_result.error(), "present");
      exit_code = 1;
      break;
    }
  }

  // 11. Cleanup — destroy_shell gives the child process a chance to exit
  //     gracefully, then shell_impl's destructor handles hard resource cleanup.
  if (shell) {
    platform::destroy_shell(std::move(shell));
  }

  return exit_code;
}
