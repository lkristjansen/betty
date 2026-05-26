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
#include "util/log.hpp"

namespace {

constexpr uint32_t k_read_buffer_size        = 4096;
constexpr DWORD    k_process_exit_timeout_ms = 2000;

} // anonymous namespace

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

  // Initiate thread shutdown before the jthread destructor runs.
  // Must be called BEFORE shell_impl is destroyed so that the read thread
  // is unblocked and joined cleanly.
  void shutdown() noexcept {
    // 1. Signal the read thread to stop at its next loop iteration.
    //    This alone won't unblock a thread stuck in ReadFile, but it
    //    ensures the stop token is set by the time step 2 unblocks it.
    read_thread.request_stop();

    // 2. Close the ConPTY.  This closes the write-end of the output
    //    pipe (conpty_output), causing ReadFile on the parent's read-end
    //    to return EOF — the deterministic unblock mechanism.  By this
    //    point request_stop() has already been called, so the read thread
    //    will see the stop token and break silently.
    hpc.reset();

    // 3. Wake any consumers blocked on output_cv so they don't hang.
    output_cv.notify_all();
  }

  // All member destructors (jthread, handles, pipes) run automatically
  // in reverse declaration order.  shutdown() must have been called first.
  ~shell_impl() = default;

  shell_impl(shell_impl const&) = delete;
  shell_impl& operator=(shell_impl const&) = delete;
  shell_impl() = default;
};

// --- shell — rule of five --------------------------------------------------
shell::~shell() {
  if (!impl_) return;
  auto* p = impl_.get();

  // 1. Wait for the process to exit (2 second timeout).
  //    The caller is expected to have sent an exit command before
  //    destroying the shell; this is a safety net in case it didn't.
  if (p->process) {
    if (WaitForSingleObject(p->process.get(), k_process_exit_timeout_ms) != WAIT_OBJECT_0)
      TerminateProcess(p->process.get(), 1);
  }

  // 2. Shut down the read thread gracefully before destroying shell_impl.
  p->shutdown();

  // 3. impl_ is destroyed here.  shell_impl's =default destructor joins
  //    the jthread and closes all remaining handles.
}
shell::shell(shell&&) noexcept = default;
shell& shell::operator=(shell&&) noexcept = default;
shell::shell(empty_tag) noexcept {}

auto shell::native_handle() const noexcept -> shell_handle {
  return shell_handle{ impl_ ? impl_->process.get() : nullptr };
}

namespace {

void read_thread_fn(shell_impl* p, std::stop_token stoken) {
  char read_buf[k_read_buffer_size];

  while (!stoken.stop_requested()) {
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(p->output_pipe.get(), read_buf, sizeof(read_buf), &bytes_read, nullptr);

    if (!ok || bytes_read == 0) {
      if (!stoken.stop_requested()) {
        util::log_error(make_win32_error(), "shell read thread I/O error");
      }
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
    return std::unexpected(make_hresult_error(hr));
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
// is_running
// ===========================================================================

auto shell::is_running() const -> bool {
  if (!impl_ || !impl_->process) {
    return false;
  }
  // Poll the process handle directly — more accurate than a flag that
  // the read thread can set to false for unrelated I/O errors.
  return WaitForSingleObject(impl_->process.get(), 0) != WAIT_OBJECT_0;
}

// ===========================================================================
// read_output
// ===========================================================================

auto shell::read_output() -> std::string {
  if (!impl_) return {};

  std::string result;
  {
    std::lock_guard<std::mutex> lock(impl_->output_mutex);
    result = std::move(impl_->raw_buffer);
    impl_->raw_buffer.clear();
  }
  return result;
}

// ===========================================================================
// write_input
// ===========================================================================

auto shell::write_input(std::string_view data)
  -> std::expected<void, std::error_code> {

  if (!impl_ || !impl_->input_pipe)
    return std::unexpected(make_win32_error(ERROR_INVALID_HANDLE));

  DWORD written = 0;
  if (!WriteFile(impl_->input_pipe.get(), data.data(),
                  static_cast<DWORD>(data.size()), &written, nullptr))
    return std::unexpected(make_win32_error());

  return {};
}

// ===========================================================================
// resize
// ===========================================================================

auto shell::resize(uint32_t cols, uint32_t rows)
  -> std::expected<void, std::error_code> {
  if (!impl_ || !impl_->hpc)
    return std::unexpected(make_win32_error(ERROR_INVALID_HANDLE));

  COORD size{ static_cast<SHORT>(cols), static_cast<SHORT>(rows) };
  HRESULT hr = ResizePseudoConsole(impl_->hpc.get(), size);
  if (FAILED(hr))
    return std::unexpected(make_hresult_error(hr));
  return {};
}

} // namespace betty::platform
