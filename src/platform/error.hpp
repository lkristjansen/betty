#pragma once
#include <system_error>

namespace betty::platform {

// Error category for Win32 (GetLastError) errors.
auto win32_category() -> std::error_category const&;

// Error category for COM/HRESULT errors (D3D, ConPTY, etc.).
auto hresult_category() -> std::error_category const&;

// Create an error_code from GetLastError().
auto make_win32_error() -> std::error_code;

// Create an error_code from a specific Win32 error code.
auto make_win32_error(unsigned long code) -> std::error_code;

// Create an error_code from an HRESULT.
auto make_hresult_error(long hr) -> std::error_code;

} // namespace betty::platform
