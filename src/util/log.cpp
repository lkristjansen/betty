#include "util/log.hpp"
#include <format>
#include <windows.h>
#include "platform/window.hpp"

namespace betty::util {

namespace {
  auto format_error(std::error_code ec, std::string_view context,
                    std::source_location const& loc) -> std::string {
    return std::format("{}:{}: {}: {} ({}:{})",
                       loc.file_name(), loc.line(),
                       context,
                       ec.message(),
                       ec.category().name(),
                       ec.value());
  }
} // anonymous namespace

void log_error(std::error_code ec, std::string_view context,
               std::source_location const loc) {
  auto const formatted = format_error(ec, context, loc);
  OutputDebugStringA(formatted.c_str());
  OutputDebugStringA("\n");
}

void show_fatal_error(std::error_code ec, std::string_view context,
                      std::source_location const loc) {
  auto const formatted = format_error(ec, context, loc);
  OutputDebugStringA(formatted.c_str());
  OutputDebugStringA("\n");
  platform::show_error_message("betty", formatted);
}

} // namespace betty::util
