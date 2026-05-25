#pragma once
#include <format>
#include <utility>

// OutputDebugStringA requires the Windows SDK. The platform library already
// compiles with WIN32_LEAN_AND_MEAN / NOMINMAX so this is lightweight.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace betty::platform {

// ===========================================================================
// debug_println — std::println-like debug output via OutputDebugStringA
// ===========================================================================
// Mimics the C++23 std::println API: variadic format string + args, always
// appends a trailing newline. Output is sent to the Windows debugger stream
// (OutputDebugStringA), which is the correct target for a WIN32 GUI app
// that has no console.
//
// Usage:
//   debug_println("{}:{}: {} ({})", file, line, msg, errno);
//   debug_println("{}", raw_blob);   // double-newline if blob ends with \n

template <typename... Args>
void debug_println(std::format_string<Args...> fmt, Args&&... args) {
  auto const msg = std::format(fmt, std::forward<Args>(args)...);
  OutputDebugStringA(msg.c_str());
  OutputDebugStringA("\n");
}

} // namespace betty::platform
