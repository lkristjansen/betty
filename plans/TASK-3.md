# Task 3 — Live Shell I/O

> **User sees:** A PowerShell session runs in the window. Typing on the keyboard sends input to the shell; shell output appears as raw text in the window. Closing the window terminates the shell.

---

## Design Decisions

| Area | Decision |
|---|---|
| **Directory layout** | `src/platform/shell.{cpp,hpp}` for ConPTY plumbing; `src/terminal/` for higher-level logic |
| **No WinAPI leaks** | `src/terminal/` sees zero Windows types. Platform layer exposes opaque, abstract interfaces |
| **Threading** | `std::deque<std::string>` + `std::mutex` + `std::condition_variable` for read→main communication |
| **Render trigger** | Idle-only — current `dispatch_pending_messages()` loop polls the shared queue |
| **Text buffer** | `std::vector<std::string>`, fixed-size = screen rows, oldest line drops on overflow |
| **Input routing** | `WndProc` forwards to a callback registered by the terminal module |
| **Key handling** | `WM_KEYDOWN` primary, `WM_CHAR` fallback. Enter → `\r`, Backspace → `\x7F`, Arrows/Tab → ANSI escape sequences |
| **Shell close** | Send `exit\r`, then `TerminateProcess` after timeout |
| **Shell crash/exit** | Window stays open, last output remains visible |
| **Exit detection** | Pipe closure (`ReadFile` returns 0) |
| **ConPTY failure** | Skip shell spawn, render error text in the window |
| **Wide lines** | Truncate at column boundary |
| **Non-ASCII chars** | Render `?` replacement character |
| **Rendering API** | New `draw_text(device, rtv, lines, start_row)` method on `glyph_renderer` |
| **Code style** | Consistent with existing codebase: `snake_case`, `PascalCase` types, `k_` constants, PIMPL, section comments |

---

## Step-by-Step Plan

### Step 1: Add `src/terminal/` module to CMake

1. Create `src/terminal/CMakeLists.txt`
2. Define a `terminal` static library (empty for now, files added later)
3. Link `terminal` to `betty` target in `src/CMakeLists.txt`
4. Verify build still passes

### Step 2: Create `terminal::text_buffer`

**File:** `src/terminal/text_buffer.hpp` + `src/terminal/text_buffer.cpp`

- A `text_buffer` class with:
  - Constructor taking `uint32_t max_rows` (screen height)
  - `void append_line(std::string line)` — pushes a line; if `size() == max_rows`, drops the first line
  - `std::span<const std::string> lines() const` — returns a view into the buffer for rendering
  - `void clear()` — resets the buffer
  - `uint32_t max_rows() const`
  - No dynamic allocation beyond the internal vector
- No platform dependencies — pure C++23
- PIMPL not needed (value type, simple ownership)
- Write unit-test-friendly API (can be tested without Windows)

### Step 3: Create `terminal::input_handler`

**File:** `src/terminal/input_handler.hpp` + `src/terminal/input_handler.cpp`

- An `input_handler` class responsible for translating keyboard events into byte sequences for ConPTY:
  - `void on_keydown(VK_CODE vk, bool control, bool shift, bool alt)` — primary path
  - `void on_char(uint32_t codepoint)` — fallback for printable chars not caught by `WM_KEYDOWN`
  - Returns `std::string` (the bytes to send to the input pipe)
- Maps virtual-key codes to:
  - Printable ASCII → raw byte
  - Enter → `\r`
  - Backspace → `\x7F` (DEL)
  - Tab → `\t`
  - Escape → `\x1B`
  - Arrow Up → `\x1B[A`, Down → `\x1B[B`, Right → `\x1B[C`, Left → `\x1B[D`
  - Home → `\x1B[H`, End → `\x1B[F`, Page Up → `\x1B[5~`, Page Down → `\x1B[6~`
  - Delete → `\x1B[3~`
  - Ctrl+modifiers: Ctrl+C → `\x03`, Ctrl+Break → handled later (Task 16)
- No platform dependencies — takes abstract `VK_CODE` (own enum, not `winuser.h`)
- Define `enum class vk_code : uint32_t` in the header

### Step 4: Create `platform::shell` (PIMPL-based)

**File:** `src/platform/shell.hpp` + `src/platform/shell.cpp`

- **Public interface** (`shell.hpp` — no Windows includes visible):
  ```cpp
  namespace betty::platform {

  struct shell; // opaque

  // Settings for creating a shell
  struct shell_settings {
      uint32_t cols;  // initial column count
      uint32_t rows;  // initial row count
  };

  // Create a shell. Returns the shell handle, or an error_code on failure.
  auto make_shell(shell_settings const& settings)
      -> std::expected<shell, std::error_code>;

  // Destroy the shell — sends "exit\r", waits briefly, then terminates.
  void destroy_shell(shell& sh);

  // Read available output from the shell. Returns a vector of lines.
  // Returns empty vector if no data available.
  // Returns nullopt (or empty with a flag) if the shell has exited.
  auto read_shell_output(shell& sh)
      -> std::expected<std::vector<std::string>, std::error_code>;

  // Send input bytes to the shell.
  auto write_shell_input(shell& sh, std::string_view data)
      -> std::expected<void, std::error_code>;

  }
  ```

- **Implementation** (`shell.cpp` — full Windows API access):
  - `shell::impl` struct (PIMPL) holds:
    - `HPCON` pseudoconsole handle
    - `HANDLE` process handle, input pipe write handle, output pipe read handle
    - `std::thread` read thread (or synchronous reads)
    - `std::mutex` + `std::deque<std::string>` + `std::condition_variable` for buffered output
    - `std::atomic<bool>` running flag
  - `make_shell`:
    1. Create input/output pipes (`CreatePipe`)
    2. `CreatePseudoConsole(cols, rows, hInput, hOutput, 0, &hPC)`
    3. `CreateProcessW` launching `pwsh.exe` (or `powershell.exe` as fallback) with `STARTUPINFOEXW` + `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE`
    4. Close pipe handles that were inherited by the child
    5. Spawn the read thread
  - **Read thread**:
    1. Loop: `ReadFile` from output pipe
    2. Accumulate bytes into a `std::string` buffer
    3. Split on `\r\n` or `\n` — for each complete line, push to the shared deque
    4. On `ReadFile` returning 0 (pipe closed / process exited), set running=false, notify
  - `read_shell_output`:
    1. Lock mutex, drain the deque, return as vector of strings
    2. Caller can check if shell is still running via a separate `is_running(shell&)` call
  - `destroy_shell`:
    1. Write `exit\r` to input pipe
    2. Wait for process handle (e.g. `WaitForSingleObject` with 2s timeout)
    3. If still alive, `TerminateProcess`
    4. Set running flag to false, join read thread
    5. Close all handles
  - `write_shell_input`:
    1. `WriteFile` to the input pipe write handle

### Step 5: Wire up the callback system in `window.hpp` / `window.cpp`

**File:** `src/platform/window.hpp`

- Add a callback type and setter to the window:
  ```cpp
  using key_event_callback = std::function<void(vk_code vk, bool ctrl, bool shift, bool alt)>;
  void set_key_callback(win32_window& window, key_event_callback callback);
  ```
  Alternatively, store the callback inside the window struct via `std::shared_ptr` or by using `SetWindowLongPtr` / `GWLP_USERDATA` to attach a context struct to the HWND.

**File:** `src/platform/window.cpp`

- Extend `WndProc`:
  - On `WM_KEYDOWN`: extract VK code, modifier keys, invoke the callback
  - On `WM_CHAR`: invoke the callback (or a separate char callback)
  - Use `GetWindowLongPtr(hwnd, GWLP_USERDATA)` to retrieve the callback pointer
- Register the callback during window creation or after creation (e.g. `SetWindowLongPtr`)

### Step 6: Create the main integration — `src/main.cpp` updates

- After creating window/device/swapchain/RTV/renderer:
  1. Compute initial terminal dimensions:
     - `cols = window_width / renderer.cell_width()`
     - `rows = window_height / renderer.cell_height()`
  2. Create `text_buffer(rows)` and `input_handler`
  3. Call `platform::make_shell({cols, rows})`:
     - On success: proceed
     - On failure: set an error string, skip shell creation
  4. Register the keyboard callback on the window:
     - Callback calls `input_handler.on_keydown(...)` or `on_char(...)`
     - Resulting byte string is sent via `platform::write_shell_input`
  5. Enter message loop:
     - `dispatch_pending_messages()`
     - If shell exists: call `platform::read_shell_output`, append each line to `text_buffer`
     - Clear to `mocha_base`
     - Call `renderer.draw_text(device, rtv, text_buffer.lines(), 0)`
     - `swap_chain.present()`

### Step 7: Extend `glyph_renderer` for multi-line drawing

**File:** `src/platform/text.hpp` + `src/platform/text.cpp`

- Add a new method:
  ```cpp
  auto draw_text(d3d_device const& device,
                 d3d_render_target_view const& rtv,
                 std::span<std::string_view const> lines,
                 uint32_t start_row) const
      -> std::expected<void, std::error_code>;
  ```
- Implementation:
  - Map the vertex buffer
  - For each line at row `start_row + i`:
    - For each character in the line (up to `cols`):
      - If ASCII (0–127): look up in atlas
      - If non-ASCII: use a pre-defined `?` glyph (or map to codepoint 63)
      - Compute `x = col * cell_width`, `y = (start_row + i) * cell_height`
      - Emit quad vertex
  - Draw indexed as before
- Truncate lines longer than `cols` characters

### Step 8: Shell exit handling in the render loop

- After `read_shell_output` returns, check if the shell has exited:
  - Add `auto is_shell_running(shell const&) -> bool` to the platform API
  - When `is_shell_running` returns false:
    - Don't call `destroy_shell` yet (keep last output on screen)
    - Stop trying to read input/output
    - Optionally show "[shell exited]" as the last line
  - The window closes only on `WM_CLOSE` / `WM_DESTROY`

### Step 9: Error rendering for ConPTY failure

- If `make_shell` fails:
  - Render a static error message into the `text_buffer` instead of spawning the shell
  - Example: `"Failed to create pseudoconsole — ConPTY not available on this system."`
  - Display it using the same `draw_text` path

### Step 10: Build, test, verify

1. Compile and launch `betty.exe`
2. Verify:
   - PowerShell prompt appears
   - Typing produces visible output (characters echo back)
   - `echo hello` prints `hello`
   - `exit` closes the shell, window stays open
   - Closing window terminates shell cleanly
   - Wide lines truncate at window edge
   - Non-ASCII characters render as `?`
3. Check for memory leaks, handle leaks, thread safety

---

## File Summary

| File | Action |
|---|---|
| `src/terminal/CMakeLists.txt` | Create |
| `src/terminal/text_buffer.hpp` | Create |
| `src/terminal/text_buffer.cpp` | Create |
| `src/terminal/input_handler.hpp` | Create |
| `src/terminal/input_handler.cpp` | Create |
| `src/platform/shell.hpp` | Create |
| `src/platform/shell.cpp` | Create |
| `src/platform/window.hpp` | Modify (add callback types) |
| `src/platform/window.cpp` | Modify (extend WndProc) |
| `src/platform/text.hpp` | Modify (add `draw_text`) |
| `src/platform/text.cpp` | Modify (implement `draw_text`) |
| `src/CMakeLists.txt` | Modify (add `terminal` subdir, link) |
| `src/main.cpp` | Modify (integrate shell + text buffer + rendering) |
