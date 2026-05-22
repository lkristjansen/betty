#include "window.hpp"
#include "error.hpp"
#include "types.hpp"
#include <windows.h>
#include <string>

namespace betty::platform {

namespace {

// Map a Win32 virtual-key code to our abstract vk_code.
auto map_vk(WPARAM wParam) -> vk_code {
  switch (static_cast<int>(wParam)) {
  case VK_RETURN:  return vk_code::enter;
  case VK_BACK:    return vk_code::backspace;
  case VK_TAB:     return vk_code::tab;
  case VK_ESCAPE:  return vk_code::escape;
  case VK_SPACE:   return vk_code::space;
  case VK_UP:      return vk_code::arrow_up;
  case VK_DOWN:    return vk_code::arrow_down;
  case VK_LEFT:    return vk_code::arrow_left;
  case VK_RIGHT:   return vk_code::arrow_right;
  case VK_HOME:    return vk_code::home;
  case VK_END:     return vk_code::end_;
  case VK_PRIOR:   return vk_code::page_up;
  case VK_NEXT:    return vk_code::page_down;
  case VK_DELETE:  return vk_code::delete_;
  case VK_INSERT:  return vk_code::insert_;
  case VK_F1:      return vk_code::f1;
  case VK_F2:      return vk_code::f2;
  case VK_F3:      return vk_code::f3;
  case VK_F4:      return vk_code::f4;
  case VK_F5:      return vk_code::f5;
  case VK_F6:      return vk_code::f6;
  case VK_F7:      return vk_code::f7;
  case VK_F8:      return vk_code::f8;
  case VK_F9:      return vk_code::f9;
  case VK_F10:     return vk_code::f10;
  case VK_F11:     return vk_code::f11;
  case VK_F12:     return vk_code::f12;
  default:
    // A–Z, 0–9, and other printable ASCII.
    if ((wParam >= 'A' && wParam <= 'Z') ||
        (wParam >= '0' && wParam <= '9')) {
      // Convert A–Z to lowercase to match our vk_code enum.
      if (wParam >= 'A' && wParam <= 'Z') {
        return static_cast<vk_code>(wParam - 'A' + 'a');
      }
      return static_cast<vk_code>(wParam);
    }
    return vk_code::unknown;
  }
}

// Retrieve the callbacks pointer from an HWND.
auto get_callbacks(HWND hwnd) -> detail::window_callbacks* {
  auto ptr = reinterpret_cast<detail::window_callbacks*>(
    GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  return ptr;
}

// File-static window procedure — not exposed outside this translation unit.
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;

  case WM_DESTROY:
    // Clear the userdata pointer so we don't access freed memory.
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    PostQuitMessage(0);
    return 0;

  case WM_KEYDOWN: {
    if (auto* cbs = get_callbacks(hwnd)) {
      if (cbs->on_key) {
        auto vk = map_vk(wParam);
        bool const ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool const shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        bool const alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;
        cbs->on_key(vk, ctrl, shift, alt);
      }
    }
    return 0;
  }

  case WM_CHAR: {
    if (auto* cbs = get_callbacks(hwnd)) {
      if (cbs->on_char) {
        // WM_CHAR gives us the translated Unicode codepoint.
        // Skip control characters (they're handled via WM_KEYDOWN).
        if (wParam >= 0x20) {
          cbs->on_char(static_cast<uint32_t>(wParam));
        }
      }
    }
    return 0;
  }

  default:
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

// UTF-8 → UTF-16 converter with error reporting.
auto widen(std::string_view sv) -> std::wstring {
  if (sv.empty()) return {};
  int needed = MultiByteToWideChar(CP_UTF8, 0, sv.data(),
                                    static_cast<int>(sv.size()), nullptr, 0);
  if (needed <= 0) {
    // Invalid UTF-8 input — return a hardcoded fallback.
    return L"<invalid UTF-8>";
  }
  std::wstring result(needed, L'\0');
  int converted = MultiByteToWideChar(CP_UTF8, 0, sv.data(),
                                       static_cast<int>(sv.size()), result.data(), needed);
  if (converted <= 0) {
    return L"<conversion error>";
  }
  // MultiByteToWideChar includes the null terminator in `needed`.
  result.resize(static_cast<size_t>(converted));
  return result;
}

} // anonymous namespace

// --- win32_window ----------------------------------------------------------

auto win32_window::native_handle() const noexcept -> window_handle {
  return window_handle{ handle_ };
}

win32_window::~win32_window() {
  if (handle_ != nullptr && IsWindow(static_cast<HWND>(handle_))) {
    DestroyWindow(static_cast<HWND>(handle_));
  }
}

win32_window::win32_window(win32_window&& other) noexcept
  : handle_(other.handle_)
  , callbacks_(std::move(other.callbacks_)) {
  other.handle_ = nullptr;
}

win32_window& win32_window::operator=(win32_window&& other) noexcept {
  if (this != &other) {
    if (handle_ != nullptr && IsWindow(static_cast<HWND>(handle_))) {
      DestroyWindow(static_cast<HWND>(handle_));
    }
    handle_ = other.handle_;
    other.handle_ = nullptr;
    callbacks_ = std::move(other.callbacks_);
  }
  return *this;
}

// --- make_window -----------------------------------------------------------

auto make_window(window_settings const& settings)
  -> std::expected<win32_window, std::error_code> {

  HINSTANCE hInstance = GetModuleHandleW(nullptr);

  // Widen UTF-8 strings to UTF-16 — must outlive the WNDCLASSEXW and CreateWindowExW calls.
  auto const wide_class_name = widen(settings.class_name);
  auto const wide_title = widen(settings.title);

  // 1. Register window class
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.hIcon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  wc.hbrBackground = nullptr;  // D3D clears the colour
  wc.lpszClassName = wide_class_name.c_str();
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
    wide_class_name.c_str(),        // lpClassName
    wide_title.c_str(),             // lpWindowName
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
  ShowWindow(hwnd, static_cast<int>(settings.show_command));

  win32_window result{ win32_window::empty_tag{} };
  result.handle_ = hwnd;
  result.callbacks_ = std::make_unique<detail::window_callbacks>();

  // 5. Store the callbacks pointer via GWLP_USERDATA so WndProc can reach it.
  SetWindowLongPtrW(hwnd, GWLP_USERDATA,
    reinterpret_cast<LONG_PTR>(result.callbacks_.get()));

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
  auto const wide_title = widen(title);
  auto const wide_message = widen(message);
  MessageBoxW(nullptr, wide_message.c_str(), wide_title.c_str(),
              MB_OK | MB_ICONERROR);
}

// --- set_window_title ------------------------------------------------------

auto set_window_title(win32_window& window, std::string_view title) -> void {
  auto const wide_title = widen(title);
  SetWindowTextW(static_cast<HWND>(window.handle_), wide_title.c_str());
}

// --- set_key_callback / set_char_callback ----------------------------------

auto set_key_callback(win32_window& window, std::function<void(vk_code, bool ctrl, bool shift, bool alt)> cb) -> void {
  if (window.callbacks_) {
    window.callbacks_->on_key = std::move(cb);
  }
}

auto set_char_callback(win32_window& window, std::function<void(uint32_t codepoint)> cb) -> void {
  if (window.callbacks_) {
    window.callbacks_->on_char = std::move(cb);
  }
}

} // namespace betty::platform
