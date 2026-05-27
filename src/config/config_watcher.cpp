#include "config_watcher.hpp"
#include <chrono>

namespace betty {

config_watcher::config_watcher(std::filesystem::path config_dir,
                               std::atomic<bool>* changed)
    : config_dir_(std::move(config_dir))
    , changed_(changed) {
  poll_thread_ = std::jthread(
      [config_dir = config_dir_, changed = changed_](std::stop_token stoken) {
    auto const config_path = config_dir / "config.toml";

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
        changed->store(true, std::memory_order_release);
        last_write = current_write;
      }

      // Detect: file was deleted.
      bool const existed_before = (last_write != std::filesystem::file_time_type{});
      if (!exists && existed_before) {
        changed->store(true, std::memory_order_release);
        last_write = std::filesystem::file_time_type{};
      }
      // File deletion alone does NOT clear the flag — the main thread
      // will handle that when it tries to re-parse and gets a missing
      // file (C10).  We update last_write so a subsequent re-creation
      // is detected as a change.
    }
  });
}

config_watcher::~config_watcher() {
  // jthread destructor requests stop and joins automatically.
}

} // namespace betty
