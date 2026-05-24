#pragma once
#include <source_location>
#include <string_view>
#include <system_error>

namespace betty::util {

// Log a non-fatal error to the debugger output (OutputDebugString).
// Suitable for runtime/render-loop errors where a blocking dialog would
// freeze the application.
void log_error(std::error_code ec, std::string_view context,
               std::source_location loc = std::source_location::current());

// Show a modal error dialog AND log to the debugger output.
// Use only for fatal startup errors where the application cannot proceed.
void show_fatal_error(std::error_code ec, std::string_view context,
                      std::source_location loc = std::source_location::current());

} // namespace betty::util
