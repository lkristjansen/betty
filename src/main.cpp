#include "application.hpp"
#include "config/config.hpp"
#include "platform/window.hpp"
#include <windows.h>

int main() {
  // Resolve the executable's directory so we can find config.toml.
  std::filesystem::path exe_dir;
  {
    wchar_t path_buf[MAX_PATH];
    DWORD const len = GetModuleFileNameW(nullptr, path_buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
      exe_dir = std::filesystem::path(std::wstring_view(path_buf, len)).parent_path();
    }
  }

  // Parse and validate configuration.
  auto [cfg, errors] = betty::parse_config(exe_dir);

  // Check for fatal errors (shell) — terminate after showing the message.
  bool has_fatal = false;
  for (auto const& e : errors) {
    if (e.fatal) has_fatal = true;
  }

  if (!errors.empty()) {
    auto msg = betty::format_validation_errors(errors);
    betty::platform::show_error_message("betty - Configuration Error", msg);
    if (has_fatal) return 1;
  }

  // (cfg is not yet wired — C6 will pass it into make_application.)

  auto app = betty::make_application();
  if (!app) return 1;
  return app->run();
}
