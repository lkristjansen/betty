#include "util/log.hpp"
#include <format>
#include "platform/window.hpp"

namespace betty::util {

void log_error(std::error_code ec, std::string_view context) {
  auto formatted = std::format("{}: {} ({}:{})",
                               context,
                               ec.message(),
                               ec.category().name(),
                               ec.value());
  platform::show_error_message("betty", formatted);
}

} // namespace betty::util
