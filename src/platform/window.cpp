#include "window.hpp"
#include "error.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace betty::platform {

namespace {

// File-static window procedure — not exposed outside this translation unit.
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

} // anonymous namespace

// --- win32_window ----------------------------------------------------------

win32_window::win32_window() = default;

win32_window::~win32_window() {
  if (handle_ != nullptr && IsWindow(handle_)) {
    DestroyWindow(handle_);
  }
}

win32_window::win32_window(win32_window&& other) noexcept
  : handle_(other.handle_) {
  other.handle_ = nullptr;
}

win32_window& win32_window::operator=(win32_window&& other) noexcept {
  if (this != &other) {
    if (handle_ != nullptr && IsWindow(handle_)) {
      DestroyWindow(handle_);
    }
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

// --- make_window -----------------------------------------------------------

auto make_window(window_settings const& settings)
  -> std::expected<win32_window, std::error_code> {

  HINSTANCE hInstance = GetModuleHandleW(nullptr);

  // 1. Register window class
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.hIcon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  wc.hbrBackground = nullptr;  // D3D clears the colour
  wc.lpszClassName = settings.class_name.data();
  wc.hIconSm = nullptr;

  // If registration fails for a reason other than "class already exists", error out.
  if (!RegisterClassExW(&wc)) {
    DWORD err = GetLastError();
    if (err != ERROR_CLASS_ALREADY_EXISTS) {
      return std::unexpected(make_win32_error(err));
    }
  }

  // 2. Adjust window rect to get the desired client area size
  DWORD dwStyle = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
  RECT rect{ 0, 0, static_cast<LONG>(settings.size.width), static_cast<LONG>(settings.size.height) };
  if (!AdjustWindowRectEx(&rect, dwStyle, FALSE, 0)) {
    return std::unexpected(make_win32_error());
  }

  int windowWidth = rect.right - rect.left;
  int windowHeight = rect.bottom - rect.top;

  // 3. Create window
  HWND hwnd = CreateWindowExW(
    0,                              // dwExStyle
    settings.class_name.data(),     // lpClassName
    settings.title.data(),          // lpWindowName
    dwStyle,                        // dwStyle
    CW_USEDEFAULT, CW_USEDEFAULT,   // x, y
    windowWidth,                    // nWidth
    windowHeight,                   // nHeight
    nullptr,                        // hWndParent
    nullptr,                        // hMenu
    hInstance,                      // hInstance
    nullptr                         // lpParam
  );

  if (hwnd == nullptr) {
    return std::unexpected(make_win32_error());
  }

  // 4. Show window
  ShowWindow(hwnd, settings.show_command);

  win32_window result;
  result.handle_ = hwnd;
  return result;
}

// --- dispatch_pending_messages ---------------------------------------------

auto dispatch_pending_messages() -> bool {
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      return false;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return true;
}

// --- show_error_message ----------------------------------------------------

auto show_error_message(std::string_view title, std::string_view message) -> void {
  auto widen = [](std::string_view sv) -> std::wstring {
    if (sv.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, sv.data(), static_cast<int>(sv.size()), nullptr, 0);
    std::wstring result(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, sv.data(), static_cast<int>(sv.size()), result.data(), needed);
    return result;
  };
  MessageBoxW(nullptr, widen(message).c_str(), widen(title).c_str(), MB_OK | MB_ICONERROR);
}

} // namespace betty::platform
