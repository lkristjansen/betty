#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

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

// Opaque handle to the underlying shell process.
// Provides no operations — callers can only check validity or store it.
struct shell_handle {
  [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
private:
  explicit shell_handle(void* h) noexcept : handle_(h) {}
  void* handle_;
  friend struct shell;
  friend auto make_shell(shell_settings const& settings)
    -> std::expected<shell, std::error_code>;
};

// Move-only shell handle.  On destruction, sends "exit\r\n" to the child
// process, waits briefly for graceful shutdown, then releases all resources
// (ConPTY, pipes, read thread).
struct shell {
  ~shell();
  shell(shell&&) noexcept;
  shell& operator=(shell&&) noexcept;
  shell(shell const&) = delete;
  shell& operator=(shell const&) = delete;

  [[nodiscard]] auto native_handle() const noexcept -> shell_handle;

private:
  struct empty_tag {};
  explicit shell(empty_tag) noexcept;

  std::unique_ptr<shell_impl> impl_;

  friend auto make_shell(shell_settings const& settings)
    -> std::expected<shell, std::error_code>;
  friend auto is_shell_running(shell const& sh) -> bool;
  friend auto read_shell_output_raw(shell& sh) -> std::string;
  friend auto write_shell_input(shell& sh, std::string_view data)
    -> std::expected<void, std::error_code>;
  friend auto resize_shell(shell& sh, uint32_t cols, uint32_t rows)
    -> std::expected<void, std::error_code>;
};

// Create a ConPTY shell (PowerShell).
// Returns the shell handle, or an error_code on failure.
[[nodiscard]] auto make_shell(shell_settings const& settings)
  -> std::expected<shell, std::error_code>;

// Check whether the shell process is still running.
[[nodiscard]] auto is_shell_running(shell const& sh) -> bool;

// Read raw output from the shell.
// Returns a string of bytes as produced by ConPTY (including VT/ANSI escape
// sequences).  Empty string means no data ready.
[[nodiscard]] auto read_shell_output_raw(shell& sh) -> std::string;

// Send input bytes to the shell's input pipe.
auto write_shell_input(shell& sh, std::string_view data)
  -> std::expected<void, std::error_code>;

// Resize the ConPTY terminal to the given dimensions.
auto resize_shell(shell& sh, uint32_t cols, uint32_t rows)
  -> std::expected<void, std::error_code>;

} // namespace betty::platform
