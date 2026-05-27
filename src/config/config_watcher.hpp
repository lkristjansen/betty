#pragma once
#include <atomic>
#include <filesystem>
#include <thread>

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
                 std::atomic<bool>* changed);

  // Requests the poll thread to stop and joins it.
  ~config_watcher();

  config_watcher(config_watcher const&) = delete;
  config_watcher& operator=(config_watcher const&) = delete;
  config_watcher(config_watcher&&) = default;
  config_watcher& operator=(config_watcher&&) = delete;

private:
  std::filesystem::path config_dir_;
  std::atomic<bool>* changed_;
  std::jthread poll_thread_;
};

} // namespace betty
