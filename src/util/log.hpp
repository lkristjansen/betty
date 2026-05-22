#pragma once
#include <string_view>
#include <system_error>

namespace betty::util {

void log_error(std::error_code ec, std::string_view context);

} // namespace betty::util
