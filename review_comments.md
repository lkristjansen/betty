# Code Review — `betty`

Scope: `src/` (platform, terminal, util, application). Tests, build files and
third-party code are out of scope unless directly referenced.

Legend:
- **Arch** = Design/Architecture
- **Read** = Readability/Clarity
- **Mod** = Modern C++

---

## Critical / High-impact

### [x] 1. `make_application` uses `show_fatal_error` for non-fatal shell failure (Arch)

`application.cpp` step 4:

```cpp
auto shell_result = platform::make_shell(...);
if (shell_result) {
  shell = std::move(*shell_result);
} else {
  util::show_fatal_error(shell_result.error(), "Failed to create shell process");
}
```

`show_fatal_error` pops a modal `MessageBoxW` and logs — yet the function then
continues and constructs a session with `std::nullopt`. The session also writes
*"Failed to create shell process."* into the grid. So the user sees **two**
diagnostics for the same failure (one blocking modal, one in the terminal
itself), and `show_fatal_error` is a misleading name for what is now a
recoverable condition.

Either:
- promote this to a real fatal (return `std::unexpected`), or
- call `log_error` and let the grid carry the user-facing message.

### [ ] 2. `application` exposes private constructor through a `friend make_application` (Arch)

```cpp
class application {
public:
  [[nodiscard]] int run();
private:
  explicit application(... );
  ...
  friend auto make_application() -> std::expected<application, std::error_code>;
};
```

The factory friendship and the `empty_tag` idiom used in `win32_window`,
`d3d_device`, `d3d_swap_chain`, `d3d_render_target_view`, `glyph_renderer`,
`shell` are all reinventing the same pattern. None of these types have any
genuine reason to hide their constructor — they're owned-by-value, move-only
RAII handles. A public constructor taking the already-constructed members is
just as safe, and the factory becomes a plain free function with no friendship.
The friend list on `win32_window` alone is 7 lines long; this is encapsulation
theater.

Recommended: drop the `empty_tag`/`friend` pattern and use a public
package-private constructor (or `std::expected<T,error>` factory returning by
value with a public constructor).

### [x] 3. Cursor "out-of-bounds sentinel" via `point2d` is a magic value (Arch)

`application::run`:

```cpp
platform::point2d const cursor{
  session_.is_following_output() ? session_.cursor_row() : session_.rows(),
  session_.is_following_output() ? session_.cursor_col() : session_.cols()
};
```

Passing `rows()/cols()` as "out of bounds" so the renderer suppresses the
cursor is brittle (any future change to the renderer's bounds check breaks it)
and is reproduced in `glyph_renderer::draw_grid` with `cursor.row < draw_rows`.
The intent is *"render cursor at this position, or not at all"*.

Recommended:

```cpp
auto cursor = session_.is_following_output()
              ? std::optional<point2d>{{session_.cursor_row(), session_.cursor_col()}}
              : std::nullopt;
renderer_ctx_.draw_grid(cells, dims, cursor, ...);
```

### [ ] 4. Resize failures silently invalidate the renderer, main loop exits with a generic error (Arch)

`renderer_context::handle_resize` logs and swallows errors:

```cpp
auto new_rtv = resize_swap_chain(...);
if (new_rtv) { rtv_ = std::move(*new_rtv); }
else {
  util::log_error(new_rtv.error(), "resize swap chain");
  return; // rtv_ is now empty; is_valid() returns false.
}
```

…then `application::run` later detects `!renderer_ctx_.is_valid()` and
synthesizes a fake `std::errc::io_error`:

```cpp
util::log_error(std::make_error_code(std::errc::io_error),
    "RTV was invalidated by a failed resize, exiting");
```

This is a fabricated error code — the real `std::error_code` was already lost
by the time the loop notices. Surface the original error through
`handle_resize` (return `std::expected<void, std::error_code>`) instead of the
"two-phase" log-then-detect dance.

### [ ] 5. `cell_attr::wide` / `wide_tail` conflated with stylistic attributes (Arch)

`cell_attr` is documented as *"text attribute bitmask (bold, italic, etc.)"*
but bits 6 and 7 are structural metadata (this cell is the leading/trailing
half of a wide character). They are checked in renderer and grid code with the
same bitwise ops as `bold`/`italic`, but they:
- can never coexist meaningfully with each other,
- are never set by SGR,
- propagate through `sgr_state` if a user does `sgr_.attr | cell_attr::wide`
  during `write_char`. (And in fact the code does exactly this in `grid.cpp`,
  which means the *next* normal cell could spuriously carry a stale `wide` bit
  if the chain of operations is wrong.)

```cpp
row[cursor_.col()] = grid_cell{cp, sgr_.fg, sgr_.bg,
                                sgr_.attr | cell_attr::wide};
```

Recommended: separate `cell_kind { normal, wide_lead, wide_tail }` from
`cell_attr`. Renderer dispatches on `kind` first, then on stylistic attrs.

### [ ] 6. `action` always carries a `std::string title` payload (Arch / Mod)

`vt_parser.hpp`:

```cpp
struct action {
  action_type type = action_type::write_char;
  char32_t codepoint = 0;
  uint32_t count     = 1;
  uint32_t row       = 0;
  uint32_t col       = 0;
  terminal_color color{};
  std::string title{};       // <-- always allocated/zeroed
};
```

`sizeof(action)` is ~64 bytes on x64, and every `write_char` (the by-far most
common action) pays an empty-string constructor/destructor on every push into
the per-byte `output_` buffer. With `process_output` running tens of thousands
of bytes per frame this is meaningful, and the design is muddied (`action` is
an unnamed sum type implemented by hand).

Recommended:

```cpp
struct action {
  action_type type;
  std::variant<std::monostate, char32_t, motion, sgr_payload,
               std::string> payload;
};
```

Or keep the existing struct but move the title into a side-channel (`std::vector<std::string> titles_;`) referenced by `count`. Either eliminates
the per-action heap-tracked string member.

### [ ] 7. `glyph_renderer::draw` and `draw_text` are dead code (Arch)

Neither is referenced anywhere outside `text.cpp`/`text.hpp`. They duplicate
~50 lines of pipeline state setup with `draw_grid`. The dead paths also lack
the bounds clamping (`draw` happily writes to `col >= max_cols`).

Recommended: delete both, or extract `bind_pipeline_state(...)` and have a
single `draw_quads(span<glyph_vertex>, quad_count)` core that all three (one,
after deletion) variants share.

### [ ] 8. PIMPL types are punctured by `friend` declarations everywhere (Arch)

```cpp
// gfx.hpp
struct d3d_device {
  ...
private:
  struct impl;
  std::unique_ptr<impl> impl_;
  friend auto make_device() -> ...;
  friend auto make_swap_chain(d3d_device const&, ...);
  friend auto make_render_target_view(d3d_device const&, ...);
  friend struct glyph_renderer;          // needs impl_->context
  friend auto make_glyph_renderer(...);  // needs impl_->device / context
  friend auto resize_swap_chain(...);
};
```

The whole point of the PIMPL is to hide the D3D types from headers, but every
non-trivial consumer (`glyph_renderer`, all factories, `resize_swap_chain`)
needs the concrete impl and is granted friend access. The end result: the
implementation is "private" but available to half the platform layer through
friendship. The forwarding `gfx_impl.hpp` then has to be included by all those
TUs anyway.

Recommended: either
- expose `ID3D11Device*` etc. via narrow `protected`/internal accessors
  (`device.native_device()`), keeping the public API clean; or
- collapse the PIMPL: hide it behind an internal `gfx_internal.hpp` and let
  consumers in the platform library access it as a normal struct. The PIMPL
  is buying nothing because nothing outside the platform library uses these
  types.

### [ ] 9. `glyph_renderer::draw_grid` is `const` but mutates the glyph cache (Mod)

`text.cpp`:

```cpp
mutable std::unordered_map<char32_t, uint32_t> dyn_index_;
mutable std::array<uint64_t, k_dyn_max_glyphs> dyn_access_{};
mutable uint64_t dyn_clock_ = 0;
mutable uint32_t dyn_next_ = 0;
```

Every `mutable` here is a lie about const-correctness so `prepare_unicode_glyphs`
and `draw_grid` can keep `const` in the signature. The pipeline is *clearly*
mutating per frame (it updates a constant buffer, maps a dynamic vertex
buffer, calls `UpdateSubresource`).

Recommended: drop `const` from `draw_grid`, `draw`, `draw_text`, and
`prepare_unicode_glyphs`. Drop the `mutable` qualifiers. Make
`renderer_context::draw_grid` non-const accordingly.

### [ ] 10. Read thread shutdown relies on `CancelSynchronousIo` (Arch)

`shell.cpp::shell_impl::shutdown`:

```cpp
if (!CancelSynchronousIo(read_thread.native_handle())) {
  util::log_error(make_win32_error(), ...);
}
hpc.reset();
```

`CancelSynchronousIo` is fragile (it races the read thread's actual entry into
`ReadFile`) and the "secondary unblock" via `ClosePseudoConsole` is the actual
guarantee. Cleaner shutdown: use overlapped (async) `ReadFile` with an
`OVERLAPPED.hEvent`, or `WaitForSingleObject` on the process handle and a
manual-reset cancel event with `WaitForMultipleObjects`. The current code is
documented well, but it depends on Windows internals to be reliable.

---

## Important

### [ ] 11. `terminal_session` redundantly tracks shell-exit state, `application` repeats the pattern (Arch)

`terminal_session::exit_notified_` records "we've put `[shell exited]` in the
grid", and `application::session_dead_` records "we've reset the title".
`process_output` already returns `session_status::dead` exactly once at the
transition. The duplicate flag in `application` is unnecessary if
`process_output` just returns a richer transition signal, e.g.:

```cpp
enum class session_status { ok, dead, dead_first_notification };
```

…or, cleaner: have the session call a `on_exited` observer once, the same way
it does for `set_observer` on the title.

### [ ] 12. `make_shell` mutates global process state (Arch)

```cpp
if (!AllocConsole()) return std::unexpected(make_win32_error());
HWND const hwndConsole = GetConsoleWindow();
if (hwndConsole) ShowWindow(hwndConsole, SW_HIDE);
...
FreeConsole();
```

The function (a) attaches/detaches a console, (b) hides the console window, all
as a workaround for `CreatePseudoConsole` in a GUI subsystem process. The
side-effect is global to the process: any concurrent code that observes
`GetConsoleWindow()` etc. during shell construction will be confused. If
`make_shell` is ever called twice concurrently the workaround races.

Recommended: document this loudly in `shell.hpp` (this is a process-wide
side-effect, not a clean "make this object"), or move the AllocConsole/
FreeConsole dance to a single one-shot init in `make_application`.

### [ ] 13. `shell` destructor encodes shell-specific protocol (Arch)

```cpp
shell::~shell() {
  ...
  const char exit_cmd[] = "exit\r\n";
  WriteFile(p->input_pipe.get(), exit_cmd, sizeof(exit_cmd)-1, ...);
  ...
}
```

`shell` is documented as a generic ConPTY handle, but the destructor knows
that the child is a command interpreter that understands `exit\r\n`. Anyone
who later spawns, say, `vim` via this class will get garbage typed into their
buffer on shutdown. The exit policy should live in the caller (or in a
configuration parameter), not in the RAII handle.

### [ ] 14. `read_shell_output_raw` empties the buffer via `swap` (Mod)

```cpp
std::string result;
{
  std::lock_guard<std::mutex> lock(sh.impl_->output_mutex);
  result.swap(sh.impl_->raw_buffer);
}
return result;
```

After this swap, `raw_buffer` has `result`'s previous capacity (zero) — so the
read thread immediately reallocates on its next append. Use
`std::exchange(sh.impl_->raw_buffer, {})` (semantically identical) or, better,
`auto result = std::move(sh.impl_->raw_buffer); sh.impl_->raw_buffer.clear();`
which preserves capacity. For a busy shell this avoids per-frame reallocation.

### [ ] 15. `window_callbacks` mixes callbacks and configuration (Read)

`window.hpp`:

```cpp
struct window_callbacks {
  std::function<void(vk_code, bool, bool, bool)> on_key;
  std::function<void(uint32_t)> on_char;
  std::function<void(uint32_t, uint32_t, bool)> on_resize;
  uint32_t min_client_width  = 0;
  uint32_t min_client_height = 0;
};
```

`min_client_width/height` are not callbacks; they're configuration. Either
rename the struct (`window_state`) or split into `callbacks` + `constraints`.
Today the name lies, and `set_min_window_size` accessing this struct reads
oddly:

```cpp
window.callbacks_->min_client_width = client_width;
```

### [ ] 16. Free-function setters on opaque types are inconsistent with the rest of the API (Arch)

```cpp
auto set_key_callback(win32_window& w, ...) -> void;
auto set_char_callback(win32_window& w, ...) -> void;
auto set_window_title(win32_window& w, ...) -> void;
auto set_resize_callback(win32_window& w, ...) -> void;
auto set_min_window_size(win32_window& w, ...) -> void;
auto get_client_size(win32_window const& w) -> window_dimensions;
```

But `win32_window` is a class with methods (`native_handle`), and other types
in the codebase use methods exclusively (`d3d_swap_chain::present`,
`renderer_context::draw_grid`). The free functions exist because they need
private access — which is solved by making them members. Today, all six are
listed as `friend` declarations on the class anyway, so the privacy argument
is moot.

### [ ] 17. `widen()` returns the literal `L"<invalid UTF-8>"` (Arch / Read)

`window.cpp`:

```cpp
auto widen(std::string_view sv) -> std::wstring {
  ...
  if (needed <= 0) return L"<invalid UTF-8>";
  ...
  if (converted <= 0) return L"<conversion error>";
}
```

If a malformed OSC title is sent by the shell, `set_window_title` will set
the window title to `<invalid UTF-8>`. This is silent failure surfacing in
the UI. At minimum, `widen` should return `std::expected<std::wstring,
std::error_code>` so callers can choose what to do.

### [ ] 18. `vt_parser` is owned by `terminal_grid` (Arch)

```cpp
class terminal_grid {
  ...
  vt_parser parser_;
};
```

The grid is a passive cell-store; the parser is a stream-stateful tokenizer.
Keeping them in the same class means:
- `grid_test.cpp` can't exercise the grid without dragging in the parser,
- mocking shell output in tests means going through the byte interface, which
  re-tests the parser at the same time,
- the grid's `write_bytes` advertises a `string_view` API that is actually a
  byte stream — `data().write_bytes("foo\n")` invokes parsing too.

Recommended: move `vt_parser` to `terminal_session` (where it logically
belongs alongside shell I/O), and let `terminal_grid::apply(action const&)`
remain its narrowest input.

### [ ] 19. `dispatch_il` etc. check `params.size() > 0` but `split_params` guarantees ≥1 (Read)

`vt_parser::split_params` ends with:

```cpp
if (param_values_.empty()) param_values_.push_back(0);
return param_values_;
```

So later checks like:

```cpp
uint32_t const count = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
```

are over-defensive — the `params.size() > 0` is always true. Same pattern in
`dispatch_il`, `dispatch_dl`, `dispatch_su`, `dispatch_sd`, `dispatch_ich`,
`dispatch_dch`, `dispatch_ech`, `dispatch_cursor`. Drop the redundant size
check, or change `split_params` to return raw (and assert ≥1 at top of each
dispatch_).

### [ ] 20. `dispatch_sgr` is a 90-line if/else-if chain (Read)

The structure begs for a table-driven dispatch, especially since most cases
are "set/clear one attribute bit":

```cpp
struct sgr_entry {
  uint32_t code;
  cell_attr on;    // bit to set in pending_on
  cell_attr off;   // bit to set in pending_off
};
constexpr std::array sgr_attr_table = {
  sgr_entry{1,  cell_attr::bold,   cell_attr::faint},  // bold on
  sgr_entry{2,  cell_attr::faint,  cell_attr::bold},   // faint on
  ...
};
```

Then the SGR loop becomes a single `find_if` per param.

### [ ] 21. `default_fg()` and `default_bg()` return identical sentinel values (Read)

```cpp
constexpr auto default_fg() -> terminal_color { return {0, 0, 0, k_default_flag}; }
constexpr auto default_bg() -> terminal_color { return {0, 0, 0, k_default_flag}; }
```

The two helpers exist purely for self-documentation but mislead readers into
thinking they differ. Either keep one (`default_color()`) or store the
resolved RGB inside each, even if `is_default()` short-circuits later.

### [ ] 22. `terminal_color::flags` exposes a single-bit field as public data (Mod)

```cpp
struct terminal_color {
  uint8_t r = 0, g = 0, b = 0, flags = 0;
  static constexpr uint8_t k_default_flag = 1;
  [[nodiscard]] constexpr bool is_default() const noexcept {...}
};
```

The whole flags byte is reserved for one bit that means "default colour". This
is a textbook `std::optional<rgb_color>`. Today, anyone can write
`color.flags = 0xFF` and the type silently misbehaves.

Recommended:

```cpp
using terminal_color = std::optional<rgb_color>;  // nullopt == default
```

### [ ] 23. `xterm_256_color(uint8_t)` performs no validation of its input mapping (Read)

```cpp
constexpr auto xterm_256_color(uint8_t index) -> terminal_color {
  if (index < 16) return catppuccin_palette[index];
  if (index < 232) {
    index -= 16;
    ...
  }
  uint8_t const gray = (index - 232) * 10 + 8;  // wraps if index < 232 reached here
  return {gray, gray, gray, 0};
}
```

The fall-through to the gray ramp is correct only because `index >= 232` is
guaranteed by the earlier branches, but written this way the function looks
like it can underflow. A `switch`/early-return style or explicit
`if (index >= 232)` would be clearer.

### [ ] 24. Free-function API on `shell` is inconsistent (Read)

```cpp
[[nodiscard]] auto is_shell_running(shell const& sh) -> bool;
[[nodiscard]] auto read_shell_output_raw(shell& sh) -> std::string;
auto write_shell_input(shell& sh, std::string_view data) -> ...;
auto resize_shell(shell& sh, uint32_t cols, uint32_t rows) -> ...;
```

`sh.is_running()`, `sh.read_output_raw()`, `sh.write_input(...)`,
`sh.resize(cols, rows)` would be shorter, more discoverable, and avoid the
`shell` token prefix being repeated in every name. Same pattern issue as
`win32_window` (item 16).

### [ ] 25. `win32_handle` traits design conflates two sentinels (Read)

`win32_handle.hpp`:

```cpp
struct handle_traits      { static constexpr uintptr_t invalid = 0; ... };
struct pipe_handle_traits { static constexpr uintptr_t invalid = -1; ... };
```

`HANDLE` is a `void*`; both `nullptr` (returned by many APIs on failure) and
`INVALID_HANDLE_VALUE` (returned by `CreateFile`, `CreatePipe`) are "no
handle". The traits force callers to pick one — if a `scoped_handle` is later
assigned an `INVALID_HANDLE_VALUE` (e.g. from a future `CreateFile` use), its
`operator bool` returns `true` and the destructor will call `CloseHandle(-1)`
which is technically harmless but error-prone.

Recommended: store the handle and treat both `nullptr` and
`INVALID_HANDLE_VALUE` as "no handle" centrally:

```cpp
[[nodiscard]] static bool is_valid(HANDLE h) noexcept {
  return h != nullptr && h != INVALID_HANDLE_VALUE;
}
```

### [ ] 26. `application::on_resize` underflows when `width < 2*pad` (Arch)

```cpp
uint32_t const new_cols = std::max(k_min_columns, (width - 2 * pad) / cell_w);
```

`width` is `uint32_t`; `width - 2*pad` underflows to a huge value if `width <
16`. The minimum-size enforcement via `WM_GETMINMAXINFO` prevents this in
practice, but if min-size enforcement ever races or the user drags into a
multi-monitor edge case, we get wraparound. Make the subtraction defensive:

```cpp
uint32_t const usable_w = (width > 2*pad) ? width - 2*pad : 0;
```

### [ ] 27. `point2d` uses `row/col`, `size2d` uses `width/height` (Read)

```cpp
struct size2d  { uint32_t width, height; };
struct point2d { uint32_t row,  col;    };
```

When both appear in `draw_grid(span, size2d dims, point2d cursor, ...)`, the
reader has to mentally translate between two coordinate conventions. Pick one
— either both `(x, y)` or both `(col, row)` — and stick to it. Right now
`dims.height` is the number of rows and `dims.width` is the number of cols;
that's row-major in one struct and Cartesian in the other.

### [ ] 28. `vk_code::end_`, `delete_`, `insert_` — keyword collisions exposed in the public API (Read)

```cpp
enum class vk_code : uint32_t {
  ...
  end_           = 0x15,
  delete_        = 0x18,
  insert_        = 0x19,
};
```

The trailing underscore is the standard escape, but every call site reads
`vk_code::end_` etc. — three reserved-word collisions in a row suggest the
naming scheme is too close to keywords. Prefix consistently:
`key_end`, `key_delete`, `key_insert` (or rename the enum to `key`).

### [ ] 29. `point2d cursor{rows, cols}` cursor sentinel duplicated in two places (Arch)

Related to item 3. `glyph_renderer::draw_grid` then computes:

```cpp
bool const cursor_visible = cursor.row < draw_rows && cursor.col < draw_cols;
```

This is a second check for the same "out of bounds means no cursor"
convention; if `draw_rows < session.rows()` due to a small window, the cursor
suppression breaks (a real in-bounds cursor at the bottom row gets hidden).
Use `std::optional<point2d>` end-to-end.

---

## Minor / Style

### [ ] 30. Inconsistent factory styles within the same layer (Read)

- `make_window`, `make_swap_chain`, `make_device`, `make_render_target_view`,
  `make_glyph_renderer`, `make_shell`, `make_renderer_context` return
  `std::expected<T, std::error_code>` — good.
- But `make_application` is the only one whose result type can't be re-created
  by the caller because its constructor is private. The rest could be public
  constructors with no loss of safety.

### [ ] 31. `glyph_constants::_pad[2]` (Read)

`text.cpp`:

```cpp
struct glyph_constants {
  float ...
  float _pad[2];
};
static_assert(sizeof(glyph_constants) == 32, ...);
```

`_pad` is a reserved identifier in some contexts (leading underscore + lowercase
is fine at namespace scope per the standard, but the convention is ugly).
Prefer `padding` or use `alignas(16)` on the struct.

### [ ] 32. C-style array `wchar_t input[4] = {}` (Mod)

`unicode.cpp::nfc_compose`:

```cpp
wchar_t input[4] = {};
int input_len = 0;
auto encode = [](char32_t cp, wchar_t* out) -> int { ... };
input_len = encode(base, input);
input_len += encode(combining, input + input_len);
wchar_t composed[4] = {};
```

Replace with `std::array<wchar_t, 4>` and pass `.data()` / `.size()` to the
Win32 API. The encode lambda can return `std::span<wchar_t>` of the written
slice.

### [ ] 33. `make_unique<BYTE[]>(attr_list_size)` (Mod)

`shell.cpp`:

```cpp
auto attr_list = std::make_unique<BYTE[]>(attr_list_size);
siex.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_list.get());
```

Use `std::vector<std::byte>` for the attribute-list buffer. Eliminates the
`reinterpret_cast` (`std::byte*` is implicitly castable to `void*`-shaped
APIs with no UB worry) and gives `.size()`.

### [ ] 34. `log_error` issues two `OutputDebugStringA` calls (Mod)

```cpp
void log_error(...) {
  auto const formatted = format_error(...);
  OutputDebugStringA(formatted.c_str());
  OutputDebugStringA("\n");
}
```

Append the newline once in `format_error` (cheaper, atomic from the debugger's
point of view). `debug_print.hpp::debug_println` already does this in two
calls too; consolidate via `std::format` returning the trailing `\n`.

### [ ] 35. `std::function` callbacks — consider `std::move_only_function` (Mod, C++23)

`window_callbacks`:

```cpp
std::function<void(vk_code, bool, bool, bool)> on_key;
std::function<void(uint32_t)> on_char;
std::function<void(uint32_t, uint32_t, bool)> on_resize;
```

These are stored once, called many times, and never copied. `std::move_only_function`
(C++23, already enabled via `cxx_std_23`) is the precise type. Avoids the
copyability constraint and the small-buffer-optimization quirks of `std::function`.

### [ ] 36. `vt_parser::parse` returns a span invalidated on next call (Mod)

```cpp
[[nodiscard]] auto parse(unsigned char byte) -> std::span<const action>;
```

The contract — *"returned span refers to internal storage that is invalidated
on the next call"* — is easy to forget. With C++23 already in use, a
`std::generator<action>` coroutine would be both safer (the caller can't
accidentally outlive the span) and remove the `output_` member buffer
altogether.

### [ ] 37. `terminal_color` `flags` field could be `bool is_default` (Mod)

See item 22; standalone bool field is even simpler than `std::optional` if
the RGB storage matters.

### [ ] 38. `cell_attr` operator boilerplate (Mod)

```cpp
inline constexpr auto operator|(cell_attr, cell_attr) -> cell_attr { ... }
inline constexpr auto operator|=(cell_attr&, cell_attr) -> cell_attr& { ... }
inline constexpr auto operator&(cell_attr, cell_attr) -> cell_attr { ... }
inline constexpr auto operator~(cell_attr) -> cell_attr { ... }
```

Then half the call sites still drop back to `to_uint8(cell_attr::X)` and use
raw bit ops on `uint8_t`:

```cpp
constexpr uint8_t k_attr_bold = terminal::to_uint8(cell_attr::bold);
... if (cell.attr & k_attr_bold) ...
```

Either commit to `cell_attr` as the type used throughout (which means
`cell_attr & cell_attr` everywhere and `operator==(cell_attr, cell_attr)` for
zero-test), or drop the operators and use a tagged `std::bitset<8>` /
`enum_set` helper. Today there are two parallel ways to test/set bits in
different files.

### [ ] 39. Re-export `using platform::vk_code` in terminal layer (Read)

`input_handler.hpp`:

```cpp
namespace betty::terminal {
  using platform::vk_code;
  ...
}
```

Cross-layer `using` declarations make the dependency from terminal→platform
implicit and breaks symmetry. Either:
- accept that `vk_code` is part of the terminal-facing API and move it to
  `terminal::vk_code` (the platform layer would translate Win32 → terminal::vk_code
  in `window.cpp`); or
- fully qualify `platform::vk_code` at call sites.

### [ ] 40. `pending_wrap_` reset duplicated across many `action_type` cases (Read)

`grid.cpp::apply`:

```cpp
case action_type::move_cursor:         pending_wrap_ = false; cursor_.move_to(...); break;
case action_type::move_cursor_up:      pending_wrap_ = false; cursor_.move_up(...); break;
case action_type::move_cursor_down:    pending_wrap_ = false; cursor_.move_down(...); break;
case action_type::move_cursor_forward: pending_wrap_ = false; ...
case action_type::move_cursor_back:    pending_wrap_ = false; ...
case action_type::restore_cursor:      pending_wrap_ = false; ...
```

Group these into a single `if (is_explicit_cursor_action(a.type)) pending_wrap_ = false;`
guard, or push `pending_wrap_` into `cursor_state` itself (it conceptually
*is* cursor state).

### [ ] 41. `format_error` formats `loc.file_name()` directly (Read)

`log.cpp`:

```cpp
return std::format("{}:{}: {}: {} ({}:{})",
                   loc.file_name(), loc.line(), ...);
```

`source_location::file_name()` returns the full path embedded by the compiler
(e.g. `C:\Users\lkr\Code\personal\betty\src\application.cpp`). Strip to the
basename, otherwise debug logs are very wide and leak the developer's machine
layout.

### [ ] 42. `mocha_base` colour in `platform/types.hpp` (Read)

`platform::types.hpp` declares structural types (`rgba_color`, `point2d`,
`size2d`, `window_dimensions`) and policy constants (`mocha_base`,
`k_default_fg_color`, `k_default_bg_color`, `k_padding_px`,
`default_window_size`, `default_show_command`) in one file. The colour theme
and padding don't belong in a "types" header — they're configuration. Split
into `platform/types.hpp` + `platform/theme.hpp` (or just `config.hpp`).

### [ ] 43. `unsigned char b` byte stream — could be `std::byte` (Mod)

Used throughout `vt_parser` and `grid::write_bytes`. `std::byte` makes intent
explicit (this is opaque data, not a "small integer") and prevents accidental
arithmetic. Casts to `char32_t` become `std::to_integer<unsigned>` at the
boundary.

### [ ] 44. `inline constexpr` is redundant for in-class `static constexpr` (Mod)

Some files use:

```cpp
inline constexpr uint32_t k_font_size_px = 18u;
```

at namespace scope (fine, but `inline` only matters for non-`constexpr` data;
all `constexpr` variables at namespace scope are already implicitly `inline`
since C++17). Pure style nit.

### [ ] 45. `class normalized_float` is a 4-byte clamped-float wrapper used only once (Read)

`platform/types.hpp` defines `normalized_float` to clamp values to [0, 1],
but the only construction sites are inside `rgba_from_rgb` (where the input
math already guarantees [0, 1]) and the `rgba_color` ctor (where every caller
goes through `rgba_from_rgb`). The runtime clamp is redundant and the type
adds friction (no implicit conversion from float). Consider inlining at the
two call sites and dropping the class — or keep it as a *concept*-driven
`strong_float<bound{0,1}>` if more constrained floats appear.

### [ ] 46. `format_error`'s context placement (Read)

```cpp
return std::format("{}:{}: {}: {} ({}:{})",
                   loc.file_name(), loc.line(),
                   context,
                   ec.message(),
                   ec.category().name(),
                   ec.value());
```

Compiler-style output is normally `file:line: error: <message>`, which makes
the `context: ec.message()` pattern read as "error: <message>". That's the
intent — but in the example output, the user-supplied `context` ("create
window") sits where the compiler usually prints `error`. Consider:

```
{file}:{line}: {context}: {message} [{category}:{value}]
```

…which keeps the same info but reads less like a compiler error.

### [ ] 47. `terminal_grid::render_cells` is not `const` despite docs implying it is (Read)

```cpp
[[nodiscard]] auto render_cells() -> std::span<const platform::render_cell>;
```

It mutates `render_cache_` (`mutable std::vector<platform::render_cell>`).
That's the same `mutable` lie as item 9 but here the function is already
non-const, so just drop `mutable` on `render_cache_`.

### [ ] 48. `glyph_renderer::draw_grid` clamps to `max_cols/max_rows` derived from window size (Arch)

```cpp
uint32_t const max_cols = impl_->window_width / impl_->cell_width;
uint32_t const max_rows = impl_->window_height / impl_->cell_height;
uint32_t const draw_cols = std::min(dims.width, max_cols);
uint32_t const draw_rows = std::min(dims.height, max_rows);
```

This silently truncates the terminal when the window is smaller than the grid
suggests. But the application already computes `dims = (cols, rows)` from the
same window size and padding, so `dims` is always ≤ `(max_cols, max_rows)` —
*except* that this calculation here ignores `padding`. If `padding > 0` you
end up drawing fewer cells than the session believes are visible, and the
cursor-suppression check (item 3) gets the wrong `draw_cols`. The two
calculations should share a helper.

### [ ] 49. `dispatch_pending_messages` returns bool but discards `WM_QUIT.wParam` (Read)

```cpp
[[nodiscard]] auto dispatch_pending_messages() -> bool;
```

`WM_QUIT.wParam` is the process exit code — typically what
`PostQuitMessage(int)` was called with. Throwing it away means
`application::run` always reports its own `exit_code` (which is just 0/1).
Consider returning `std::optional<int>` (nullopt = continue; value = exit
code).

### [ ] 50. `static_cast<int>(wParam)` in `map_vk` (Read)

```cpp
auto map_vk(WPARAM wParam) -> vk_code {
  switch (static_cast<int>(wParam)) {
  ...
  default:
    if (wParam >= 'A' && wParam <= 'Z') { ... }
  }
}
```

`WPARAM` is `UINT_PTR`. Casting to `int` then comparing with VK constants
works for the standard VK range but is logically off. Use `static_cast<UINT>`
or compare the `WPARAM` directly (the VK constants are `int` but they're
non-negative). Minor, but a `/W4` build with stricter sign-conversion would
catch it.

