#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace betty::platform {

// ===========================================================================
// shell — opaque handle to a ConPTY-backed shell process
// ===========================================================================
// Public interface contains zero Windows types.  The implementation lives
// entirely in shell.cpp behind a PIMPL.

struct shell_settings {
  uint32_t cols;  // initial column count
  uint32_t rows;  // initial row count
};

// Forward-declared implementation struct (defined in shell.cpp).
struct shell_impl;

struct shell {
  shell() noexcept = default;
  ~shell();
  shell(shell&&) noexcept;
  shell& operator=(shell&&) noexcept;
  shell(shell const&) = delete;
  shell& operator=(shell const&) = delete;

  std::unique_ptr<shell_impl> impl_;
};

// Create a ConPTY shell (PowerShell).
// Returns the shell handle, or an error_code on failure.
[[nodiscard]] auto make_shell(shell_settings const& settings)
  -> std::expected<std::unique_ptr<shell>, std::error_code>;

// Destroy the shell: sends "exit\r", waits briefly, then terminates.
auto destroy_shell(std::unique_ptr<shell> sh) -> void;

// Check whether the shell process is still running.
[[nodiscard]] auto is_shell_running(shell const& sh) -> bool;

// Read available output from the shell.
// Returns a vector of completed lines.  Empty vector means no data ready.
// The caller checks is_shell_running separately to detect exit.
[[nodiscard]] auto read_shell_output(shell& sh)
  -> std::expected<std::vector<std::string>, std::error_code>;

// Send input bytes to the shell's input pipe.
auto write_shell_input(shell& sh, std::string_view data)
  -> std::expected<void, std::error_code>;

} // namespace betty::platform
