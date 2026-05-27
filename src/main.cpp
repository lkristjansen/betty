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

  // Parse configuration.  On TOML syntax errors, show a message box and
  // continue with built-in defaults (cfg falls back to the default struct).
  auto config_result = betty::parse_config(exe_dir);
  if (!config_result) {
    betty::platform::show_error_message("betty - Configuration Error", config_result.error());
  }

  auto app = betty::make_application();
  if (!app) return 1;
  return app->run();
}
