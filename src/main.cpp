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
#include "terminal/text_buffer.hpp"
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
  terminal::text_buffer buffer(rows);
  terminal::input_handler input;
  std::string input_line;    // local echo of the current line being typed
  std::string pending_echo;  // echo chars to consume from shell output

  // 8. Create shell
  std::unique_ptr<platform::shell> shell;
  bool shell_failed = false;

  auto shell_result = platform::make_shell(platform::shell_settings{
    .cols = cols,
    .rows = rows
  });

  if (shell_result) {
    shell = std::move(*shell_result);
  } else {
    shell_failed = true;
    buffer.append_line("Failed to create shell process.");
  }



  // Helper: flush input_line to shell on Enter.
  auto send_line = [&]() {
    // Append to the last buffer line so it appears on the same line as the prompt.
    buffer.append_to_last(input_line);
    // Send command to the shell (with CRLF).
    std::string cmd = input_line + "\r\n";
    (void) platform::write_shell_input(*shell, cmd);
    // Remember visible characters so we can consume the shell's echo.
    pending_echo = input_line;
    input_line.clear();
  };

  // 9. Keyboard callback (WM_KEYDOWN) — non-printable keys only
  platform::set_key_callback(window,
    [&](platform::vk_code vk, bool ctrl, bool shift, bool alt) {
      if (!shell || !platform::is_shell_running(*shell)) return;

      std::string bytes = input.on_keydown(vk, ctrl, shift, alt);

      if (vk == platform::vk_code::enter) {
        send_line();
        return;
      }

      if (vk == platform::vk_code::backspace) {
        if (!input_line.empty()) input_line.pop_back();
        return;
      }

      // Forward non-printable keys (arrows, Ctrl combos, etc.) to shell.
      if (!bytes.empty()) {
        bool const is_printable =
          bytes.size() == 1 && bytes[0] >= 0x20 && bytes[0] < 0x7F;
        if (!is_printable) {
          (void) platform::write_shell_input(*shell, bytes);
        }
      }
    });

  // WM_CHAR — printable characters with local echo only (no per-char shell send)
  platform::set_char_callback(window,
    [&](uint32_t codepoint) {
      if (!shell || !platform::is_shell_running(*shell)) return;

      std::string bytes = input.on_char(codepoint);
      if (!bytes.empty()) {
        // Local echo: append to the current input line.
        for (unsigned char c : bytes) {
          if (c >= 0x20 && c < 0x7F && input_line.size() < cols) {
            input_line += static_cast<char>(c);
          }
        }
      }
    });

  // 10. Message loop
  while (platform::dispatch_pending_messages()) {
    device.clear(rtv, platform::mocha_base);

    // Read shell output
    if (shell && platform::is_shell_running(*shell)) {
      auto output = platform::read_shell_output(*shell);
      if (output && !output->empty()) {
        for (auto& line : *output) {
          // Consume pending echo: skip characters that match what we sent.
          while (!pending_echo.empty() && !line.empty()) {
            if (line[0] == pending_echo[0]) {
              line.erase(line.begin());
              pending_echo.erase(pending_echo.begin());
            } else {
              // Mismatch — the echo must have already been consumed.
              pending_echo.clear();
              break;
            }
          }
          if (line.empty()) continue;
          // Skip whitespace-only lines (e.g. stray \r from shell echo).
          bool const all_ws = line.find_first_not_of(" \t\r\n") == std::string::npos;
          if (all_ws) continue;
          buffer.append_line(std::move(line));
        }
      }
    } else if (shell && !platform::is_shell_running(*shell)) {
      static bool shown_exit = false;
      if (!shown_exit) {
        buffer.append_line("[shell exited]");
        shown_exit = true;
      }
      // Drain remaining output
      auto output = platform::read_shell_output(*shell);
      if (output && !output->empty()) {
        for (auto& line : *output) {
          // Consume pending echo
          while (!pending_echo.empty() && !line.empty()) {
            if (line[0] == pending_echo[0]) {
              line.erase(line.begin());
              pending_echo.erase(pending_echo.begin());
            } else {
              pending_echo.clear();
              break;
            }
          }
          if (line.empty()) continue;
          bool const all_ws = line.find_first_not_of(" \t\r\n") == std::string::npos;
          if (all_ws) continue;
          buffer.append_line(std::move(line));
        }
      }
    }

    // Build render lines: completed buffer lines
    auto const& completed = buffer.lines();
    std::vector<std::string> render_owned;   // owns any combined strings
    std::vector<std::string_view> render_lines;
    render_lines.reserve(completed.size() + 1);

    for (auto const& line : completed) {
      render_lines.emplace_back(line);
    }

    // Append input_line to the last buffer line for display
    // (local echo appears on the same line as the prompt)
    if (!input_line.empty()) {
      if (render_lines.empty()) {
        render_lines.emplace_back(input_line);
      } else {
        render_owned.emplace_back(render_lines.back());
        render_owned.back() += input_line;
        render_lines.back() = render_owned.back();
      }
    }

    if (!render_lines.empty()) {
      uint32_t const screen_rows = renderer.cell_height()
        ? platform::default_window_size.height / renderer.cell_height()
        : 1;
      // Scroll: if buffer has more lines than fit on screen, skip the oldest.
      uint32_t const start_row =
        render_lines.size() > screen_rows
          ? render_lines.size() - screen_rows
          : 0;
      if (auto draw_result = renderer.draw_text(device, rtv, render_lines, start_row); !draw_result) {
        log_error(draw_result.error(), "draw text");
        return 1;
      }
    }

    if (auto present_result = swap_chain.present(); !present_result) {
      log_error(present_result.error(), "present");
      return 1;
    }
  }

  // 11. Cleanup
  if (shell) {
    platform::destroy_shell(std::move(shell));
  }

  return 0;
}
