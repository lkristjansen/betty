#include "shell.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// Try to include the ConPTY header; if the SDK has it, declarations are
// provided.  On older SDKs we supply manual forward-declarations.
#if __has_include(<consoleapi2.h>)
#  include <consoleapi2.h>
#else
using HPCON = void*;
extern "C" HRESULT WINAPI CreatePseudoConsole(
    COORD size, HANDLE hInput, HANDLE hOutput,
    DWORD dwFlags, HPCON* phPC);
extern "C" HRESULT WINAPI ResizePseudoConsole(
    HPCON hPC, COORD size);
extern "C" void WINAPI ClosePseudoConsole(HPCON hPC);
#endif

#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <stop_token>
#include <string_view>

#include "error.hpp"

namespace betty::platform {

// ===========================================================================
// Forward-declared helpers
// ===========================================================================

namespace {
  auto safe_close(HANDLE h) -> void {
    if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
  }
}

// ===========================================================================
// shell_impl — PIMPL with all Windows types hidden here
// ===========================================================================

struct shell_impl {
  HANDLE  process{ nullptr };
  HPCON   hpc{ nullptr };          // Pseudoconsole handle
  HANDLE  input_pipe{ nullptr };   // Parent write end of input pipe
  HANDLE  output_pipe{ nullptr };  // Parent read end of output pipe
  HANDLE  conpty_input{ nullptr }; // ConPTY read end of input pipe
  HANDLE  conpty_output{ nullptr };// ConPTY write end of output pipe

  std::jthread  read_thread;
  std::mutex    output_mutex;
  std::string   raw_buffer;         // Raw ConPTY output (unfiltered)
  std::condition_variable output_cv;

  // Destructor handles the hard resource cleanup.
  // destroy_shell() should be called first to send "exit" and wait for
  // the child process to terminate gracefully.
  ~shell_impl() {
    // 1. Cancel any blocking synchronous I/O the read thread might be
    //    stuck on (ReadFile on the output pipe).  This must happen
    //    BEFORE the jthread destructor runs, otherwise join() blocks
    //    indefinitely on a still-pending ReadFile.
    if (read_thread.joinable()) {
      CancelSynchronousIo(read_thread.native_handle());
    }

    // 2. Close the ConPTY.  This also closes the write-end of the
    //    output pipe (conpty_output), causing ReadFile to return EOF
    //    as a secondary unblocking mechanism.
    if (hpc) {
      ClosePseudoConsole(hpc);
      hpc = nullptr;
    }

    // 3. jthread's destructor automatically calls request_stop() then
    //    join().  The thread will have already exited due to step 1
    //    (ReadFile was cancelled) so join() returns promptly.

    // 4. Close all remaining handles.
    safe_close(input_pipe);
    safe_close(output_pipe);
    safe_close(conpty_input);
    safe_close(conpty_output);
    safe_close(process);
  }

  shell_impl(shell_impl const&) = delete;
  shell_impl& operator=(shell_impl const&) = delete;
  shell_impl() = default;
};

// --- shell — rule of five --------------------------------------------------
shell::~shell() {
  if (!impl_) return;
  auto* p = impl_.get();

  // 1. Send exit command to give the shell a chance to terminate gracefully.
  if (p->input_pipe && p->input_pipe != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    const char exit_cmd[] = "exit\r\n";
    WriteFile(p->input_pipe, exit_cmd, static_cast<DWORD>(sizeof(exit_cmd) - 1), &written, nullptr);
  }

  // 2. Wait for process to exit (2 second timeout),
  //    then forcefully terminate if it didn't.
  if (p->process && p->process != INVALID_HANDLE_VALUE) {
    if (WaitForSingleObject(p->process, 2000) != WAIT_OBJECT_0)
      TerminateProcess(p->process, 1);
  }

  // 3. impl_ is destroyed here, which invokes shell_impl's destructor.
  //    The destructor closes the ConPTY (unblocking the read thread),
  //    joins the jthread, and closes all remaining handles.
}
shell::shell(shell&&) noexcept = default;
shell& shell::operator=(shell&&) noexcept = default;
shell::shell(empty_tag) noexcept {}

auto shell::native_handle() const noexcept -> shell_handle {
  return shell_handle{ impl_ ? impl_->process : nullptr };
}

namespace {

void read_thread_fn(shell_impl* p, std::stop_token stoken) {
  char read_buf[4096];

  while (!stoken.stop_requested()) {
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(p->output_pipe, read_buf, sizeof(read_buf), &bytes_read, nullptr);

    if (!ok || bytes_read == 0) {
      p->output_cv.notify_one();
      break;
    }

    {
      std::lock_guard<std::mutex> lock(p->output_mutex);
      p->raw_buffer.append(read_buf, bytes_read);
    }
    p->output_cv.notify_one();
  }
}

} // anonymous namespace

// ===========================================================================
// make_shell
// ===========================================================================
// Uses CreatePseudoConsole (ConPTY) so that PowerShell behaves as a real
// terminal: ANSI colours, cursor state, screen dimensions, etc.

auto make_shell(shell_settings const& settings)
  -> std::expected<shell, std::error_code> {
  // Validate dimensions — default to 120x40 if zero.
  uint32_t const cols = settings.cols ? settings.cols : 120;
  uint32_t const rows = settings.rows ? settings.rows : 40;

  // 1. Temporarily allocate a console so CreatePseudoConsole can succeed
  //    in a GUI-subsystem process (workaround for 0xc0000142).
  if (!AllocConsole())
    return std::unexpected(make_win32_error());

  // Hide the console window so it doesn't flash on screen.
  HWND const hwndConsole = GetConsoleWindow();
  if (hwndConsole) ShowWindow(hwndConsole, SW_HIDE);

  // 2. Create anonymous pipes for ConPTY I/O.
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  // Input pipe: parent writes (hInWrite) → ConPTY reads (hInRead) → child stdin
  HANDLE hInRead  = nullptr;
  HANDLE hInWrite = nullptr;
  if (!CreatePipe(&hInRead, &hInWrite, &sa, 0)) {
    FreeConsole();
    return std::unexpected(make_win32_error());
  }

  // Output pipe: child stdout → ConPTY writes (hOutWrite) → parent reads (hOutRead)
  HANDLE hOutRead  = nullptr;
  HANDLE hOutWrite = nullptr;
  if (!CreatePipe(&hOutRead, &hOutWrite, &sa, 0)) {
    safe_close(hInRead); safe_close(hInWrite);
    FreeConsole();
    return std::unexpected(make_win32_error());
  }

  // 3. Create the pseudoconsole, passing the child-facing pipe ends.
  COORD size{ static_cast<SHORT>(cols), static_cast<SHORT>(rows) };
  HPCON hpc = nullptr;

  HRESULT hr = CreatePseudoConsole(size, hInRead, hOutWrite, 0, &hpc);
  if (FAILED(hr)) {
    safe_close(hInRead); safe_close(hInWrite);
    safe_close(hOutRead); safe_close(hOutWrite);
    FreeConsole();
    return std::unexpected(make_win32_error(static_cast<unsigned long>(hr)));
  }

  // Note: do NOT close hInRead/hOutWrite here. Keep them alive so the
  // ConPTY can read/write through them. Close in destroy_shell.

  // 4. Prepare EXTENDED STARTUPINFO for ConPTY attachment.
  //    When using PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hStdInput/Output/Error
  //    should be NULL — the ConPTY attribute replaces standard handle redirection.
  STARTUPINFOEXW siex{};
  siex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
  siex.StartupInfo.dwFlags = 0;

  // Attach the ConPTY pseudoconsole to the child process.
  SIZE_T attr_list_size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_list_size);
  auto attr_list = std::make_unique<BYTE[]>(attr_list_size);
  siex.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_list.get());
  if (!InitializeProcThreadAttributeList(siex.lpAttributeList, 1, 0, &attr_list_size)) {
    ClosePseudoConsole(hpc);
    safe_close(hInRead); safe_close(hInWrite);
    safe_close(hOutRead); safe_close(hOutWrite);
    FreeConsole();
    return std::unexpected(make_win32_error());
  }
  if (!UpdateProcThreadAttribute(
        siex.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
        hpc, sizeof(HPCON), nullptr, nullptr)) {
    DeleteProcThreadAttributeList(siex.lpAttributeList);
    ClosePseudoConsole(hpc);
    safe_close(hInRead); safe_close(hInWrite);
    safe_close(hOutRead); safe_close(hOutWrite);
    FreeConsole();
    return std::unexpected(make_win32_error());
  }

  // 6. Spawn PowerShell.
  // cmd_line is a non-const std::wstring, so .data() returns wchar_t* (not
  // const wchar_t*).  CreateProcessW may modify the command-line buffer in
  // place, so we need a mutable string — this is correct and standards-conformant.
  std::wstring cmd_line = L"powershell.exe -NoProfile -NoLogo";
  PROCESS_INFORMATION pi{};
  BOOL created = CreateProcessW(
    nullptr, cmd_line.data(), nullptr, nullptr,
    FALSE,  // ConPTY handles are passed via attribute list, not inheritance
    EXTENDED_STARTUPINFO_PRESENT,
    nullptr, nullptr, &siex.StartupInfo, &pi);

  DeleteProcThreadAttributeList(siex.lpAttributeList);

  if (!created) {
    ClosePseudoConsole(hpc);
    safe_close(hInRead); safe_close(hInWrite);
    safe_close(hOutRead); safe_close(hOutWrite);
    FreeConsole();
    return std::unexpected(make_win32_error());
  }

  // Now the child is connected to the ConPTY — we can release our console.
  FreeConsole();

  CloseHandle(pi.hThread);  // Don't need the thread handle.

  auto impl_ptr = std::make_unique<shell_impl>();
  impl_ptr->process      = pi.hProcess;
  impl_ptr->hpc          = hpc;
  impl_ptr->input_pipe   = hInWrite;   // parent write end
  impl_ptr->output_pipe  = hOutRead;   // parent read end
  impl_ptr->conpty_input = hInRead;    // ConPTY read end
  impl_ptr->conpty_output= hOutWrite; // ConPTY write end
  impl_ptr->read_thread  = std::jthread(
    [p = impl_ptr.get()](std::stop_token stoken) { read_thread_fn(p, stoken); }
  );

  shell result{ shell::empty_tag{} };
  result.impl_ = std::move(impl_ptr);
  return result;
}

// ===========================================================================
// is_shell_running
// ===========================================================================

auto is_shell_running(shell const& sh) -> bool {
  if (!sh.impl_ || !sh.impl_->process || sh.impl_->process == INVALID_HANDLE_VALUE) {
    return false;
  }
  // Poll the process handle directly — more accurate than a flag that
  // the read thread can set to false for unrelated I/O errors.
  return WaitForSingleObject(sh.impl_->process, 0) != WAIT_OBJECT_0;
}

// ===========================================================================
// read_shell_output_raw
// ===========================================================================

auto read_shell_output_raw(shell& sh) -> std::string {
  if (!sh.impl_) return {};

  std::string result;
  {
    std::lock_guard<std::mutex> lock(sh.impl_->output_mutex);
    result.swap(sh.impl_->raw_buffer);
  }
  return result;
}

// ===========================================================================
// write_shell_input
// ===========================================================================

auto write_shell_input(shell& sh, std::string_view data)
  -> std::expected<void, std::error_code> {

  if (!sh.impl_ || !sh.impl_->input_pipe || sh.impl_->input_pipe == INVALID_HANDLE_VALUE)
    return std::unexpected(make_win32_error(ERROR_INVALID_HANDLE));

  DWORD written = 0;
  if (!WriteFile(sh.impl_->input_pipe, data.data(),
                  static_cast<DWORD>(data.size()), &written, nullptr))
    return std::unexpected(make_win32_error());

  return {};
}

// ===========================================================================
// resize_shell
// ===========================================================================

auto resize_shell(shell& sh, uint32_t cols, uint32_t rows)
  -> std::expected<void, std::error_code> {
  if (!sh.impl_ || !sh.impl_->hpc)
    return std::unexpected(make_win32_error(ERROR_INVALID_HANDLE));

  COORD size{ static_cast<SHORT>(cols), static_cast<SHORT>(rows) };
  HRESULT hr = ResizePseudoConsole(sh.impl_->hpc, size);
  if (FAILED(hr))
    return std::unexpected(make_win32_error(static_cast<unsigned long>(hr)));
  return {};
}

} // namespace betty::platform
