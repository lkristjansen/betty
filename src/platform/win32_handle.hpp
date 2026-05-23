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

template <typename H, std::uintptr_t InvalidSentinelVal = 0, auto Deleter = ::CloseHandle>
class win32_handle {
private:
  static H sentinel() noexcept { return reinterpret_cast<H>(InvalidSentinelVal); }

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
    h_  = sentinel();
    return h;
  }

  void reset(H h = sentinel()) noexcept {
    if (h_ != sentinel()) Deleter(h_);
    h_ = h;
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return h_ != sentinel();
  }

private:
  H h_ = sentinel();
};

// ===========================================================================
// Convenience aliases for common handle types
// ===========================================================================

// Regular HANDLE — sentinel is nullptr (integer value 0).
using scoped_handle = win32_handle<HANDLE>;

// Pipe HANDLE — sentinel is INVALID_HANDLE_VALUE.
// CreatePipe returns this value on failure.  Its pointer representation
// is (HANDLE)-1, i.e. std::uintptr_t(-1).
using scoped_pipe = win32_handle<HANDLE, static_cast<std::uintptr_t>(-1)>;

// Pseudoconsole handle — closed via ClosePseudoConsole, not CloseHandle.
using scoped_conpty = win32_handle<HPCON, 0, ::ClosePseudoConsole>;

} // namespace betty::platform
