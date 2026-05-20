#include "error.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>  // transitively includes <dxgi.h> for DXGetErrorString
#include <string>

namespace betty::platform {

namespace {

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
            int needed = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
            std::string msg;
            if (needed > 1) {
                msg.resize(static_cast<size_t>(needed) - 1);
                WideCharToMultiByte(CP_UTF8, 0, buffer, -1, msg.data(), needed, nullptr, nullptr);
            }
            LocalFree(buffer);
            // Trim trailing whitespace and newlines
            while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' ')) {
                msg.pop_back();
            }
            return msg;
        }
        return "Unknown error " + std::to_string(ev);
    }
};

class d3d_error_category_impl : public std::error_category {
public:
    auto name() const noexcept -> char const* override {
        return "d3d";
    }

    auto message(int ev) const -> std::string override {
        // HRESULT values can be decoded by FormatMessageW with
        // FORMAT_MESSAGE_FROM_SYSTEM just like Win32 error codes.
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
            int needed = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
            std::string msg;
            if (needed > 1) {
                msg.resize(static_cast<size_t>(needed) - 1);
                WideCharToMultiByte(CP_UTF8, 0, buffer, -1, msg.data(), needed, nullptr, nullptr);
            }
            LocalFree(buffer);
            while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' ')) {
                msg.pop_back();
            }
            return msg;
        }
        return "Unknown D3D error " + std::to_string(ev);
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
