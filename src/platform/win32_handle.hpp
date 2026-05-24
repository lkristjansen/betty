#pragma once
#include <windows.h>

#include <bit>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace betty::platform {

// ===========================================================================
// win32_handle — move-only RAII wrapper for Windows handle types
// ===========================================================================
//
// Parameterized by a Traits type that supplies:
//   typename Traits::handle_type      — the raw handle type (HANDLE, HPCON, …)
//   static constexpr uintptr_t invalid — sentinel for "no handle"
//   static void close(handle_type)    — deleter
//
// Compile-time guards:
//   - handle_type must be pointer-sized (sizeof == sizeof(uintptr_t))
//   - handle_type must be trivially copyable (required by bit_cast)
// ===========================================================================

template <typename Traits>
class win32_handle {
public:
  using handle_type = typename Traits::handle_type;

private:
  static_assert(sizeof(handle_type) == sizeof(std::uintptr_t),
                "handle_type must be a pointer-sized type");
  static_assert(std::is_trivially_copyable_v<handle_type>,
                "handle_type must be trivially copyable");

  static handle_type sentinel() noexcept {
    return std::bit_cast<handle_type>(Traits::invalid);
  }

public:
  win32_handle() noexcept = default;

  explicit win32_handle(handle_type h) noexcept : h_(h) {}

  ~win32_handle() { reset(); }

  win32_handle(win32_handle&& o) noexcept : h_(o.release()) {}

  win32_handle& operator=(win32_handle&& o) noexcept {
    if (this != &o) reset(o.release());
    return *this;
  }

  win32_handle(win32_handle const&)            = delete;
  win32_handle& operator=(win32_handle const&) = delete;

  // --- Access ---------------------------------------------------------------

  [[nodiscard]] handle_type get() const noexcept { return h_; }

  [[nodiscard]] handle_type release() noexcept {
    handle_type h = h_;
    h_ = sentinel();
    return h;
  }

  void reset(handle_type h = sentinel()) noexcept {
    if (std::bit_cast<std::uintptr_t>(h_) != Traits::invalid) {
      Traits::close(h_);
    }
    h_ = h;
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return std::bit_cast<std::uintptr_t>(h_) != Traits::invalid;
  }

private:
  handle_type h_ = sentinel();
};

// ===========================================================================
// Built-in traits for common handle types
// ===========================================================================

struct handle_traits {
  using handle_type = HANDLE;
  static constexpr std::uintptr_t invalid = 0;
  static void close(handle_type h) noexcept { ::CloseHandle(h); }
};

struct pipe_handle_traits {
  using handle_type = HANDLE;
  // CreatePipe returns INVALID_HANDLE_VALUE on failure.
  static constexpr std::uintptr_t invalid = static_cast<std::uintptr_t>(-1);
  static void close(handle_type h) noexcept { ::CloseHandle(h); }
};

struct conpty_traits {
  using handle_type = HPCON;
  static constexpr std::uintptr_t invalid = 0;
  static void close(handle_type h) noexcept { ::ClosePseudoConsole(h); }
};

// ===========================================================================
// Convenience aliases
// ===========================================================================

using scoped_handle = win32_handle<handle_traits>;
using scoped_pipe   = win32_handle<pipe_handle_traits>;
using scoped_conpty = win32_handle<conpty_traits>;

} // namespace betty::platform
