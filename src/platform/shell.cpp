#include "shell.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <atomic>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string_view>
#include <chrono>

#include "error.hpp"

namespace betty::platform {

// ===========================================================================
// shell_impl — PIMPL with all Windows types hidden here
// ===========================================================================

struct shell_impl {
  HANDLE  process{ nullptr };
  HANDLE  input_write{ nullptr };
  HANDLE  output_read{ nullptr };

  std::thread   read_thread;
  std::mutex    output_mutex;
  std::deque<std::string> output_queue;
  std::condition_variable output_cv;

  std::atomic<bool> running{ true };
};

// --- shell — rule of five --------------------------------------------------

shell::~shell() = default;
shell::shell(shell&&) noexcept = default;
shell& shell::operator=(shell&&) noexcept = default;

namespace {

auto safe_close(HANDLE h) -> void {
  if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

void read_thread_fn(shell_impl* p) {
  std::string buffer;
  char read_buf[4096];
  auto last_flush = std::chrono::steady_clock::now();
  constexpr auto flush_interval = std::chrono::milliseconds(100);

  while (p->running.load()) {
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(p->output_read, read_buf, sizeof(read_buf), &bytes_read, nullptr);

    if (!ok || bytes_read == 0) {
      p->running.store(false);
      p->output_cv.notify_one();
      break;
    }

    buffer.append(read_buf, bytes_read);

    while (true) {
      size_t crlf_pos = buffer.find("\r\n");
      size_t lf_pos   = buffer.find('\n');

      size_t split_pos = std::string::npos;
      if (crlf_pos != std::string::npos && (lf_pos == std::string::npos || crlf_pos <= lf_pos)) {
        split_pos = crlf_pos;
      } else if (lf_pos != std::string::npos) {
        split_pos = lf_pos;
      }

      if (split_pos == std::string::npos) break;

      std::string line = buffer.substr(0, split_pos);

      size_t erase_len = (split_pos + 1 < buffer.size() && buffer[split_pos] == '\r' &&
                          buffer[split_pos + 1] == '\n') ? 2 : 1;
      buffer.erase(0, split_pos + erase_len);

      {
        std::lock_guard<std::mutex> lock(p->output_mutex);
        p->output_queue.push_back(std::move(line));
      }
      p->output_cv.notify_one();
    }

    // Flush any partial line that's been sitting in the buffer longer than
    // flush_interval. This captures unterminated prompts like "PS C:\\> ".
    auto now = std::chrono::steady_clock::now();
    if (!buffer.empty() && (now - last_flush) >= flush_interval) {
      std::string partial;
      {
        std::lock_guard<std::mutex> lock(p->output_mutex);
        partial.swap(buffer);
      }
      {
        std::lock_guard<std::mutex> lock(p->output_mutex);
        p->output_queue.push_back(std::move(partial));
      }
      p->output_cv.notify_one();
      last_flush = now;
    }
  }

  // Flush remaining data on exit.
  if (!buffer.empty()) {
    std::lock_guard<std::mutex> lock(p->output_mutex);
    p->output_queue.push_back(std::move(buffer));
    p->output_cv.notify_one();
  }
}

} // anonymous namespace

// ===========================================================================
// make_shell
// ===========================================================================
// Uses plain pipe redirection (stdin/stdout/stderr) for the child process.
// ConPTY (pseudoconsole) is not used because it fails with 0xc0000142
// when the parent is a GUI-subsystem process on some Windows builds.

auto make_shell(shell_settings const& settings)
  -> std::expected<std::unique_ptr<shell>, std::error_code> {
  (void)settings;  // cols/rows ignored without ConPTY

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  HANDLE hInputRead  = nullptr;
  HANDLE hInputWrite = nullptr;
  HANDLE hOutputRead = nullptr;
  HANDLE hOutputWrite = nullptr;

  if (!CreatePipe(&hInputRead, &hInputWrite, &sa, 0))
    return std::unexpected(make_win32_error());
  if (!CreatePipe(&hOutputRead, &hOutputWrite, &sa, 0)) {
    safe_close(hInputRead); safe_close(hInputWrite);
    return std::unexpected(make_win32_error());
  }

  // Mark parent-side handles as non-inheritable.
  if (!SetHandleInformation(hInputWrite, HANDLE_FLAG_INHERIT, 0)) {
    safe_close(hInputRead); safe_close(hInputWrite);
    safe_close(hOutputRead); safe_close(hOutputWrite);
    return std::unexpected(make_win32_error());
  }
  if (!SetHandleInformation(hOutputRead, HANDLE_FLAG_INHERIT, 0)) {
    safe_close(hInputRead); safe_close(hInputWrite);
    safe_close(hOutputRead); safe_close(hOutputWrite);
    return std::unexpected(make_win32_error());
  }

  STARTUPINFOW si{};
  si.cb = sizeof(STARTUPINFOW);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.hStdInput  = hInputRead;
  si.hStdOutput = hOutputWrite;
  si.hStdError  = hOutputWrite;
  si.wShowWindow = SW_HIDE;

  std::wstring cmd_line = L"powershell.exe -NoProfile -NoLogo";
  PROCESS_INFORMATION pi{};
  BOOL created = CreateProcessW(
    nullptr, cmd_line.data(), nullptr, nullptr,
    TRUE, CREATE_NO_WINDOW,
    nullptr, nullptr, &si, &pi);

  DWORD create_err = created ? 0 : GetLastError();

  safe_close(hInputRead);
  safe_close(hOutputWrite);

  if (!created) {
    safe_close(hInputWrite); safe_close(hOutputRead);
    return std::unexpected(make_win32_error(create_err));
  }

  safe_close(pi.hThread);

  auto impl_ptr = std::make_unique<shell_impl>();
  impl_ptr->process     = pi.hProcess;
  impl_ptr->input_write = hInputWrite;
  impl_ptr->output_read = hOutputRead;
  impl_ptr->read_thread = std::thread(read_thread_fn, impl_ptr.get());

  auto sh = std::make_unique<shell>();
  sh->impl_ = std::move(impl_ptr);
  return sh;
}

// ===========================================================================
// destroy_shell
// ===========================================================================

auto destroy_shell(std::unique_ptr<shell> sh) -> void {
  if (!sh || !sh->impl_) return;
  auto* p = sh->impl_.get();

  if (p->input_write && p->input_write != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    const char exit_cmd[] = "exit\r\n";
    WriteFile(p->input_write, exit_cmd, static_cast<DWORD>(sizeof(exit_cmd) - 1), &written, nullptr);
  }

  if (p->process && p->process != INVALID_HANDLE_VALUE) {
    if (WaitForSingleObject(p->process, 2000) != WAIT_OBJECT_0)
      TerminateProcess(p->process, 1);
  }

  p->running.store(false);
  if (p->read_thread.joinable()) p->read_thread.join();

  safe_close(p->input_write);
  safe_close(p->output_read);
  safe_close(p->process);
}

// ===========================================================================
// is_shell_running
// ===========================================================================

auto is_shell_running(shell const& sh) -> bool {
  if (!sh.impl_) return false;
  return sh.impl_->running.load();
}

// ===========================================================================
// read_shell_output
// ===========================================================================

auto read_shell_output(shell& sh)
  -> std::expected<std::vector<std::string>, std::error_code> {

  if (!sh.impl_)
    return std::unexpected(make_win32_error(ERROR_INVALID_HANDLE));

  std::vector<std::string> result;
  std::lock_guard<std::mutex> lock(sh.impl_->output_mutex);
  while (!sh.impl_->output_queue.empty()) {
    result.push_back(std::move(sh.impl_->output_queue.front()));
    sh.impl_->output_queue.pop_front();
  }
  return result;
}

// ===========================================================================
// write_shell_input
// ===========================================================================

auto write_shell_input(shell& sh, std::string_view data)
  -> std::expected<void, std::error_code> {

  if (!sh.impl_ || !sh.impl_->input_write || sh.impl_->input_write == INVALID_HANDLE_VALUE)
    return std::unexpected(make_win32_error(ERROR_INVALID_HANDLE));

  DWORD written = 0;
  if (!WriteFile(sh.impl_->input_write, data.data(),
                  static_cast<DWORD>(data.size()), &written, nullptr))
    return std::unexpected(make_win32_error());

  return {};
}

} // namespace betty::platform
