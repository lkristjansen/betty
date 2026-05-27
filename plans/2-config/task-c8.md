# C8 — File watcher: detect config.toml changes

## Goal

Spawn a background thread that polls for modifications to `config.toml`. When the file changes (or is newly created), set an atomic flag that the main loop checks each frame.

**User sees:** No visible change yet. The hot-reload handler (C9) will re-parse and apply changes.

---

## Design Decisions (from interview)

1. **Atomic flag, not PostMessage.** `std::atomic<bool> config_changed_` on `application`. The watcher sets it; the main loop polls it each frame. Simpler than window messages, no HWND exposure needed.

2. **Dedicated `config_watcher` class.** New files `src/config/config_watcher.hpp` + `src/config/config_watcher.cpp` in the config module. Clean ownership.

3. **Poll `last_write_time` every 500ms.** No `ReadDirectoryChangesW`, no overlapped I/O, no cancellation complexity. 500ms latency is imperceptible for a config file edited by hand.

4. **RAII with `std::jthread`.** Construction starts polling, destruction stops and joins. No explicit `start()`/`stop()` needed.

5. **`config_dir` as explicit parameter to `make_application`.** `make_application(betty_config const& cfg, std::filesystem::path const& config_dir)`. No path duplication, no config struct pollution.

---

## Files Changed

| File | Change |
|---|---|
| `src/config/config_watcher.hpp` | **New.** Declaration of `config_watcher` class. |
| `src/config/config_watcher.cpp` | **New.** Implementation: poll thread, last_write_time tracking. |
| `src/config/CMakeLists.txt` | Add `config_watcher.cpp` to the `config` library. |
| `src/application.hpp` | Add `config_watcher watcher_` and `std::atomic<bool> config_changed_` members. Add `config_dir` param to `make_application`. |
| `src/application.cpp` | Create watcher in constructor, pass config_dir. Check `config_changed_` in message loop (placeholder for C9). |
| `src/main.cpp` | Pass `exe_dir` to `make_application`. |

---

## Step-by-step Implementation

### Step 1: Create `config_watcher.hpp`

```cpp
#pragma once
#include <atomic>
#include <filesystem>

namespace betty {

// ===========================================================================
// config_watcher — background thread that detects config.toml changes
// ===========================================================================
// Polls config_dir/config.toml every 500ms for last_write_time changes.
// Sets an atomic flag when a modification is detected.  The main thread
// reads the flag each frame and resets it after handling the reload.
//
// Lifecycle: construction starts the poll thread.  Destruction requests
// stop and joins.  RAII — no explicit start/stop needed.

class config_watcher {
public:
  // Start watching `config_dir`.  The `changed` flag is set to `true`
  // whenever config.toml is created or modified.
  config_watcher(std::filesystem::path config_dir,
                 std::atomic<bool>& changed);

  // Requests the poll thread to stop and joins it.
  ~config_watcher();

  config_watcher(config_watcher const&) = delete;
  config_watcher& operator=(config_watcher const&) = delete;
  config_watcher(config_watcher&&) = default;
  config_watcher& operator=(config_watcher&&) = default;

private:
  std::filesystem::path config_dir_;
  std::atomic<bool>& changed_;
  std::jthread poll_thread_;
};

} // namespace betty
```

Note: `std::jthread` requires `<thread>`. We include it only in the .cpp via PIMPL or forward-declare. Since `std::jthread` is not forward-declarable, we include `<thread>` in the header (or use a unique_ptr to an impl). For simplicity, include `<thread>`.

### Step 2: Create `config_watcher.cpp`

```cpp
#include "config_watcher.hpp"
#include <chrono>
#include <thread>

namespace betty {

config_watcher::config_watcher(std::filesystem::path config_dir,
                               std::atomic<bool>& changed)
    : config_dir_(std::move(config_dir))
    , changed_(changed) {
  poll_thread_ = std::jthread([this](std::stop_token stoken) {
    auto const config_path = config_dir_ / "config.toml";

    // Snapshot the initial write time (or lack thereof).
    auto last_write = std::filesystem::exists(config_path)
                          ? std::filesystem::last_write_time(config_path)
                          : std::filesystem::file_time_type{};

    while (!stoken.stop_requested()) {
      // Poll every 500ms.
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      bool const exists = std::filesystem::exists(config_path);
      auto const current_write = exists
          ? std::filesystem::last_write_time(config_path)
          : std::filesystem::file_time_type{};

      // Detect: file was created, deleted and recreated, or modified.
      if (exists && current_write != last_write) {
        // File exists and its write time changed (includes creation).
        changed_.store(true, std::memory_order_release);
        last_write = current_write;
      }
      // Note: file deletion alone is NOT a "change" here — the main
      // thread will handle that when it tries to re-parse and gets a
      // missing file (C10).  We keep tracking the last known write time
      // so that if the file reappears, we detect it.
    }
  });
}

config_watcher::~config_watcher() {
  // jthread destructor requests stop and joins automatically.
}

} // namespace betty
```

### Step 3: Update `src/config/CMakeLists.txt`

Add `config_watcher.cpp`:
```cmake
add_library(config STATIC
    config.cpp
    config_watcher.cpp
)
```

### Step 4: Update `application.hpp`

```cpp
#include "config/config_watcher.hpp"  // NEW
#include <atomic>                     // NEW

class application {
  // ...
  platform::win32_window window_;
  platform::renderer_context renderer_ctx_;
  terminal::terminal_session session_;
  betty_config config_;
  config_watcher watcher_;                     // NEW: watches config.toml
  std::atomic<bool> config_changed_{false};     // NEW: set by watcher, polled by loop
  std::optional<std::error_code> fatal_error_;

  friend auto make_application(betty_config const&, std::filesystem::path const&)
      -> std::expected<application, std::error_code>;
};

[[nodiscard]] auto make_application(betty_config const& cfg,
                                     std::filesystem::path const& config_dir)
    -> std::expected<application, std::error_code>;
```

### Step 5: Update `application.cpp`

**Constructor:** accept config_dir, construct watcher.

```cpp
application::application(platform::win32_window window,
                         platform::renderer_context renderer_ctx,
                         terminal::terminal_session session,
                         betty_config config,
                         std::filesystem::path config_dir)     // NEW
    : window_(std::move(window))
    , renderer_ctx_(std::move(renderer_ctx))
    , session_(std::move(session))
    , config_(std::move(config))
    , watcher_(std::move(config_dir), config_changed_) {}      // NEW
```

**make_application:** take config_dir, pass to constructor:
```cpp
auto make_application(betty_config const& cfg,
                      std::filesystem::path const& config_dir)
    -> std::expected<application, std::error_code> {
  // ... (unchanged) ...

  return application{std::move(window), std::move(renderer_ctx),
                     std::move(session), cfg, config_dir};
}
```

**Message loop:** check the flag each frame (placeholder for C9):

Add inside the `while` loop, after processing shell output:
```cpp
    // Check for config file changes (hot-reload handler in C9).
    if (config_changed_.exchange(false, std::memory_order_acquire)) {
      // C9 will re-parse config.toml and apply changes here.
    }
```

### Step 6: Update `main.cpp`

Pass `exe_dir`:
```cpp
  auto app = betty::make_application(cfg, exe_dir);
```

---

## Build and Test

```bash
cmake --build build/debug && ctest --test-dir build/debug --output-on-failure
```

No automated tests — filesystem watchers are impractical to unit test. Manual verification: launch betty, edit `config.toml` and save, verify via debug output or breakpoint that `config_changed_` is set.

---

## Acceptance

- `config_watcher` class exists in the config module.
- When `config.toml` is modified/created, `config_changed_` is set to `true` within 500ms.
- The main loop checks and resets `config_changed_` each frame.
- The poll thread exits cleanly when `application` is destroyed.
- No crashes, no resource leaks.
- betty launches and runs normally (flag is checked but no action taken yet).
