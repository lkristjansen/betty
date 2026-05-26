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

// Move-only shell handle.  On destruction, waits briefly for the child
// process to exit (force-terminating if needed), then releases all resources
// (ConPTY, pipes, read thread).  The caller should send any shutdown command
// before destroying the shell.
struct shell {
  ~shell();
  shell(shell&&) noexcept;
  shell& operator=(shell&&) noexcept;
  shell(shell const&) = delete;
  shell& operator=(shell const&) = delete;

  [[nodiscard]] auto native_handle() const noexcept -> shell_handle;

  // --- Queries ------------------------------------------------------------

  [[nodiscard]] auto is_running() const -> bool;
  [[nodiscard]] auto read_output() -> std::string;

  // --- I/O & resize -------------------------------------------------------

  auto write_input(std::string_view data) -> std::expected<void, std::error_code>;
  auto resize(uint32_t cols, uint32_t rows) -> std::expected<void, std::error_code>;

private:
  struct empty_tag {};
  explicit shell(empty_tag) noexcept;

  std::unique_ptr<shell_impl> impl_;

  friend auto make_shell(shell_settings const& settings)
    -> std::expected<shell, std::error_code>;
};

// Create a ConPTY shell (PowerShell).
// Returns the shell handle, or an error_code on failure.
[[nodiscard]] auto make_shell(shell_settings const& settings)
  -> std::expected<shell, std::error_code>;



} // namespace betty::platform
