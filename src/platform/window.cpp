#include "window.hpp"
#include "error.hpp"
#include "types.hpp"
#include "unicode.hpp"
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
  // All OEM (punctuation) keys — fall through to default.
  // Printable characters for any keyboard layout are handled by the
  // WM_CHAR path; WM_KEYDOWN only needs to map non-printable keys
  // and A–Z for Ctrl+letter control-code generation.
  default:
    // A–Z only — needed for Ctrl+letter control-code generation.
    if (wParam >= 'A' && wParam <= 'Z') {
      return static_cast<vk_code>(wParam - 'A' + 'a');
    }
    return vk_code::unknown;
  }
}

// Retrieve the callbacks pointer from an HWND.
auto get_state(HWND hwnd) -> detail::window_state* {
  auto ptr = reinterpret_cast<detail::window_state*>(
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
    if (auto* state = get_state(hwnd)) {
      if (state->callbacks.on_key) {
        auto vk = map_vk(wParam);
        bool const ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool const shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        bool const alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;
        state->callbacks.on_key(vk, ctrl, shift, alt);
      }
    }
    return 0;
  }

  case WM_CHAR: {
    if (auto* state = get_state(hwnd)) {
      if (state->callbacks.on_char) {
        // WM_CHAR gives us the translated Unicode codepoint.
        // Skip control characters (they're handled via WM_KEYDOWN).
        if (wParam >= 0x20) {
          state->callbacks.on_char(static_cast<uint32_t>(wParam));
        }
      }
    }
    return 0;
  }

  case WM_SIZE: {
    if (auto* state = get_state(hwnd)) {
      if (state->callbacks.on_resize) {
        uint32_t const width  = static_cast<uint32_t>(LOWORD(lParam));
        uint32_t const height = static_cast<uint32_t>(HIWORD(lParam));
        // WM_EXITSIZEMOVE is only sent for drag-resize.  For maximize/restore
        // we treat WM_SIZE itself as the completed resize.
        bool const completed =
            (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED);
        state->callbacks.on_resize(width, height, completed);
      }
    }
    return 0;
  }

  case WM_EXITSIZEMOVE: {
    if (auto* state = get_state(hwnd)) {
      if (state->callbacks.on_resize) {
        RECT rect{};
        if (GetClientRect(hwnd, &rect)) {
          uint32_t const width  = static_cast<uint32_t>(rect.right - rect.left);
          uint32_t const height = static_cast<uint32_t>(rect.bottom - rect.top);
          state->callbacks.on_resize(width, height, true);
        }
      }
    }
    return 0;
  }

  case WM_GETMINMAXINFO: {
    if (auto* state = get_state(hwnd)) {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
      if (state->min_client_width > 0 && state->min_client_height > 0) {
        RECT rect{0, 0,
                  static_cast<LONG>(state->min_client_width),
                  static_cast<LONG>(state->min_client_height)};
        DWORD const style =
            static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        if (AdjustWindowRectEx(&rect, style, FALSE, 0)) {
          mmi->ptMinTrackSize.x = rect.right - rect.left;
          mmi->ptMinTrackSize.y = rect.bottom - rect.top;
        }
      }
    }
    return 0;
  }

  default:
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
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
  , state_(std::move(other.state_)) {
  other.handle_ = nullptr;
}

win32_window& win32_window::operator=(win32_window&& other) noexcept {
  if (this != &other) {
    if (handle_ != nullptr && IsWindow(static_cast<HWND>(handle_))) {
      DestroyWindow(static_cast<HWND>(handle_));
    }
    handle_ = other.handle_;
    other.handle_ = nullptr;
    state_ = std::move(other.state_);
  }
  return *this;
}

// --- make_window -----------------------------------------------------------

auto make_window(window_settings const& settings)
  -> std::expected<win32_window, std::error_code> {

  HINSTANCE hInstance = GetModuleHandleW(nullptr);

  // Widen UTF-8 strings to UTF-16 — must outlive the WNDCLASSEXW and CreateWindowExW calls.
  auto wide_class_name_result = widen(settings.class_name);
  if (!wide_class_name_result) return std::unexpected(wide_class_name_result.error());
  auto const& wide_class_name = *wide_class_name_result;

  auto wide_title_result = widen(settings.title);
  if (!wide_title_result) return std::unexpected(wide_title_result.error());
  auto const& wide_title = *wide_title_result;

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
  DWORD dwStyle = WS_OVERLAPPEDWINDOW;
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
  result.state_ = std::make_unique<detail::window_state>();

  // 5. Store the callbacks pointer via GWLP_USERDATA so WndProc can reach it.
  SetWindowLongPtrW(hwnd, GWLP_USERDATA,
    reinterpret_cast<LONG_PTR>(result.state_.get()));

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
  auto wide_title_result = widen(title);
  auto const& wide_title = wide_title_result ? *wide_title_result : std::wstring{};
  auto wide_message_result = widen(message);
  auto const& wide_message = wide_message_result ? *wide_message_result : std::wstring{};
  MessageBoxW(nullptr, wide_message.c_str(), wide_title.c_str(),
              MB_OK | MB_ICONERROR);
}

// --- set_window_title ------------------------------------------------------

auto win32_window::set_window_title(std::string_view title) -> void {
  auto wide_title_result = widen(title);
  if (!wide_title_result) return;  // silently ignore malformed UTF-8
  SetWindowTextW(static_cast<HWND>(handle_), wide_title_result->c_str());
}

// --- set_key_callback / set_char_callback ----------------------------------

auto win32_window::set_key_callback(on_key_callback cb) -> void {
  if (state_) {
    state_->callbacks.on_key = std::move(cb);
  }
}

auto win32_window::set_char_callback(on_char_callback cb) -> void {
  if (state_) {
    state_->callbacks.on_char = std::move(cb);
  }
}

auto win32_window::set_resize_callback(on_resize_callback cb) -> void {
  if (state_) {
    state_->callbacks.on_resize = std::move(cb);
  }
}

auto win32_window::set_min_window_size(uint32_t client_width, uint32_t client_height) -> void {
  if (state_) {
    state_->min_client_width  = client_width;
    state_->min_client_height = client_height;
  }
}

// --- get_client_size -------------------------------------------------------

auto win32_window::get_client_size() const -> window_dimensions {
  RECT rect{};
  if (GetClientRect(static_cast<HWND>(as_hwnd()), &rect)) {
    return {
      static_cast<uint32_t>(rect.right - rect.left),
      static_cast<uint32_t>(rect.bottom - rect.top)
    };
  }
  return default_window_size;
}

} // namespace betty::platform
