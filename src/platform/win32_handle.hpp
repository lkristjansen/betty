#pragma once
#include <windows.h>

#include <utility>

namespace betty::platform {

// ===========================================================================
// win32_handle — move-only RAII wrapper for Windows handle types
// ===========================================================================
// H: handle type (HANDLE, HPCON, etc.)
// InvalidSentinel: value representing an invalid/empty handle
// Deleter: function called when the handle is released
// ===========================================================================

template <typename H, H InvalidSentinel = nullptr, auto Deleter = ::CloseHandle>
class win32_handle {
public:
  win32_handle() noexcept = default;

  explicit win32_handle(H h) noexcept : h_(h) {}

  ~win32_handle() { reset(); }

  win32_handle(win32_handle&& o) noexcept : h_(o.release()) {}

  win32_handle& operator=(win32_handle&& o) noexcept {
    if (this != &o) reset(o.release());
    return *this;
  }

  win32_handle(win32_handle const&)            = delete;
  win32_handle& operator=(win32_handle const&) = delete;

  // --- Access ---------------------------------------------------------------

  [[nodiscard]] H get() const noexcept { return h_; }

  [[nodiscard]] H release() noexcept {
    H h = h_;
    h_  = InvalidSentinel;
    return h;
  }

  void reset(H h = InvalidSentinel) noexcept {
    if (h_ != InvalidSentinel) Deleter(h_);
    h_ = h;
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return h_ != InvalidSentinel;
  }

private:
  H h_ = InvalidSentinel;
};

// ===========================================================================
// Convenience aliases for common handle types
// ===========================================================================

// Regular HANDLE — sentinel is nullptr.
using scoped_handle = win32_handle<HANDLE>;

// Pipe HANDLE — sentinel is INVALID_HANDLE_VALUE.
// CreatePipe returns this value on failure.
using scoped_pipe = win32_handle<HANDLE, INVALID_HANDLE_VALUE>;

// Pseudoconsole handle — closed via ClosePseudoConsole, not CloseHandle.
using scoped_conpty = win32_handle<HPCON, nullptr, ::ClosePseudoConsole>;

} // namespace betty::platform
