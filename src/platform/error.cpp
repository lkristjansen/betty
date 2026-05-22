#include "error.hpp"
#include <windows.h>
#include <d3d11.h>
#include <string>

namespace betty::platform {

namespace {

// Convert a wchar_t buffer (UTF-16) to a UTF-8 std::string and trim trailing
// whitespace / newlines.  `wlen` is the character count from FormatMessageW
// (excluding the null terminator), or -1 for a null-terminated string.
auto wide_to_utf8_trimmed(wchar_t const* wstr, int wlen) -> std::string {
  if (wstr == nullptr || wlen == 0) return {};
  int needed = WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, nullptr, 0, nullptr, nullptr);
  std::string msg;
  if (needed > 1) {
    // needed includes the null terminator; drop it.
    msg.resize(static_cast<size_t>(needed) - 1);
    WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, msg.data(), needed, nullptr, nullptr);
  }
  while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' ')) {
    msg.pop_back();
  }
  return msg;
}

// -----------------------------------------------------------------------

class win32_error_category_impl : public std::error_category {
public:
  auto name() const noexcept -> char const* override {
    return "win32";
  }

  auto message(int ev) const -> std::string override {
    wchar_t* buffer = nullptr;
    DWORD result = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      static_cast<DWORD>(ev),
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPWSTR>(&buffer),
      0,
      nullptr
    );

    if (result > 0 && buffer != nullptr) {
      auto msg = wide_to_utf8_trimmed(buffer, static_cast<int>(result));
      LocalFree(buffer);
      return msg;
    }
    return "Unknown error " + std::to_string(ev);
  }
};

// -----------------------------------------------------------------------

class d3d_error_category_impl : public std::error_category {
public:
  auto name() const noexcept -> char const* override {
    return "d3d";
  }

  auto message(int ev) const -> std::string override {
    // Many HRESULT values (especially FACILITY_WIN32 subset, and some
    // DXGI/D3D codes registered in the system message table) decode
    // correctly via FormatMessageW.  For those that don't, format the
    // raw HRESULT in hex.
    wchar_t* buffer = nullptr;
    DWORD result = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      static_cast<DWORD>(ev),
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPWSTR>(&buffer),
      0,
      nullptr
    );

    if (result > 0 && buffer != nullptr) {
      auto msg = wide_to_utf8_trimmed(buffer, static_cast<int>(result));
      LocalFree(buffer);
      if (!msg.empty()) {
        return msg;
      }
    }

    // Fallback: hex HRESULT with severity / facility.
    char hexbuf[64];
    auto hr = static_cast<HRESULT>(ev);
    std::snprintf(hexbuf, sizeof(hexbuf),
                  "HRESULT 0x%08lX (severity %lu, facility %lu, code %u)",
                  static_cast<unsigned long>(hr),
                  static_cast<unsigned long>(HRESULT_SEVERITY(hr)),
                  static_cast<unsigned long>(HRESULT_FACILITY(hr)),
                  static_cast<unsigned>(HRESULT_CODE(hr)));
    return hexbuf;
  }
};

} // anonymous namespace

auto win32_category() -> std::error_category const& {
  static win32_error_category_impl instance;
  return instance;
}

auto d3d_category() -> std::error_category const& {
  static d3d_error_category_impl instance;
  return instance;
}

auto make_win32_error() -> std::error_code {
  return make_win32_error(GetLastError());
}

auto make_win32_error(unsigned long code) -> std::error_code {
  return std::error_code(static_cast<int>(code), win32_category());
}

auto make_d3d_error(long hr) -> std::error_code {
  return std::error_code(static_cast<int>(hr), d3d_category());
}

} // namespace betty::platform
