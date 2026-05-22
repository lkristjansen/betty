#include "shell.hpp"
#include "win32_handle.hpp"
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
// shell_impl — PIMPL with all Windows types hidden here
// ===========================================================================

struct shell_impl {
  scoped_handle  process;
  scoped_conpty  hpc;                 // Pseudoconsole handle
  scoped_pipe    input_pipe;          // Parent write end of input pipe
  scoped_pipe    output_pipe;         // Parent read end of output pipe
  scoped_pipe    conpty_input;        // ConPTY read end of input pipe
  scoped_pipe    conpty_output;       // ConPTY write end of output pipe

  std::jthread  read_thread;
  std::mutex    output_mutex;
  std::string   raw_buffer;         // Raw ConPTY output (unfiltered)
  std::condition_variable output_cv;

  // Destructor handles the hard resource cleanup.
  // shell::~shell() sends "exit" and waits for the child process before
  // this destructor runs, so the handles are ready to be released.
  ~shell_impl() {
    // 1. Cancel any blocking synchronous I/O the read thread might be
    //    stuck on (ReadFile on the output pipe).  This must happen
    //    BEFORE the jthread destructor runs, otherwise join() blocks
    //    indefinitely on a still-pending ReadFile.
    if (read_thread.joinable()) {
      CancelSynchronousIo(read_thread.native_handle());
    }

    // 2. Close the ConPTY early.  This also closes the write-end of the
    //    output pipe (conpty_output), causing ReadFile to return EOF
    //    as a secondary unblocking mechanism.
    hpc.reset();

    // 3. jthread's destructor automatically calls request_stop() then
    //    join().  The thread will have already exited due to step 1
    //    (ReadFile was cancelled) so join() returns promptly.

    // 4. All remaining handles (process, pipes) are closed automatically
    //    by their scoped_* destructors when shell_impl is destroyed.
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
  if (p->input_pipe) {
    DWORD written = 0;
    const char exit_cmd[] = "exit\r\n";
    WriteFile(p->input_pipe.get(), exit_cmd, static_cast<DWORD>(sizeof(exit_cmd) - 1), &written, nullptr);
  }

  // 2. Wait for process to exit (2 second timeout),
  //    then forcefully terminate if it didn't.
  if (p->process) {
    if (WaitForSingleObject(p->process.get(), 2000) != WAIT_OBJECT_0)
      TerminateProcess(p->process.get(), 1);
  }

  // 3. impl_ is destroyed here, which invokes shell_impl's destructor.
  //    The destructor closes the ConPTY (unblocking the read thread),
  //    joins the jthread, and closes all remaining handles.
}
shell::shell(shell&&) noexcept = default;
shell& shell::operator=(shell&&) noexcept = default;
shell::shell(empty_tag) noexcept {}

auto shell::native_handle() const noexcept -> shell_handle {
  return shell_handle{ impl_ ? impl_->process.get() : nullptr };
}

namespace {

void read_thread_fn(shell_impl* p, std::stop_token stoken) {
  char read_buf[4096];

  while (!stoken.stop_requested()) {
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(p->output_pipe.get(), read_buf, sizeof(read_buf), &bytes_read, nullptr);

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
  HANDLE hInRead_raw  = nullptr;
  HANDLE hInWrite_raw = nullptr;
  if (!CreatePipe(&hInRead_raw, &hInWrite_raw, &sa, 0)) {
    FreeConsole();
    return std::unexpected(make_win32_error());
  }
  scoped_pipe hInRead(hInRead_raw);
  scoped_pipe hInWrite(hInWrite_raw);

  // Output pipe: child stdout → ConPTY writes (hOutWrite) → parent reads (hOutRead)
  HANDLE hOutRead_raw  = nullptr;
  HANDLE hOutWrite_raw = nullptr;
  if (!CreatePipe(&hOutRead_raw, &hOutWrite_raw, &sa, 0)) {
    FreeConsole();
    return std::unexpected(make_win32_error());
  }
  scoped_pipe hOutRead(hOutRead_raw);
  scoped_pipe hOutWrite(hOutWrite_raw);

  // 3. Create the pseudoconsole, passing the child-facing pipe ends.
  //    scoped_conpty is declared LAST so it is destroyed FIRST on error —
  //    ClosePseudoConsole must run before the pipe handles are closed.
  COORD size{ static_cast<SHORT>(cols), static_cast<SHORT>(rows) };
  HPCON hpc_raw = nullptr;

  HRESULT hr = CreatePseudoConsole(size, hInRead.get(), hOutWrite.get(), 0, &hpc_raw);
  if (FAILED(hr)) {
    FreeConsole();
    return std::unexpected(make_win32_error(static_cast<unsigned long>(hr)));
  }
  scoped_conpty hpc(hpc_raw);

  // Note: do NOT close hInRead/hOutWrite here. Keep them alive so the
  // ConPTY can read/write through them. Their ownership is transferred to
  // shell_impl at the end of this function via std::move.

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
    FreeConsole();
    return std::unexpected(make_win32_error());
  }
  if (!UpdateProcThreadAttribute(
        siex.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
        hpc.get(), sizeof(HPCON), nullptr, nullptr)) {
    DeleteProcThreadAttributeList(siex.lpAttributeList);
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
    FreeConsole();
    return std::unexpected(make_win32_error());
  }

  // Now the child is connected to the ConPTY — we can release our console.
  FreeConsole();

  scoped_handle thread_handle(pi.hThread);  // auto-closed on scope exit.

  auto impl_ptr = std::make_unique<shell_impl>();
  impl_ptr->process      = scoped_handle(pi.hProcess);
  impl_ptr->hpc          = std::move(hpc);
  impl_ptr->input_pipe   = std::move(hInWrite);
  impl_ptr->output_pipe  = std::move(hOutRead);
  impl_ptr->conpty_input = std::move(hInRead);
  impl_ptr->conpty_output = std::move(hOutWrite);
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
  if (!sh.impl_ || !sh.impl_->process) {
    return false;
  }
  // Poll the process handle directly — more accurate than a flag that
  // the read thread can set to false for unrelated I/O errors.
  return WaitForSingleObject(sh.impl_->process.get(), 0) != WAIT_OBJECT_0;
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

  if (!sh.impl_ || !sh.impl_->input_pipe)
    return std::unexpected(make_win32_error(ERROR_INVALID_HANDLE));

  DWORD written = 0;
  if (!WriteFile(sh.impl_->input_pipe.get(), data.data(),
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
  HRESULT hr = ResizePseudoConsole(sh.impl_->hpc.get(), size);
  if (FAILED(hr))
    return std::unexpected(make_win32_error(static_cast<unsigned long>(hr)));
  return {};
}

} // namespace betty::platform
