# betty — Configuration File Task Breakdown

Each task is a vertical slice: when completed, the user can launch `betty.exe` and see/experience the new behaviour. Tasks are ordered smallest-first within each phase.

---

## Phase 0: Preparatory Refactoring
*(No user-visible change — these tasks make the codebase accept runtime parameters instead of hardcoded constants.)*

---

### C1. Refactor: scrollback lines as runtime parameter

**User sees:** No change. betty still launches and scrolls as before.

- Change `terminal_grid` to accept `scrollback_max_lines` in its constructor instead of using the `k_scrollback_max` compile-time constant.
- Remove `k_scrollback_max` from `terminal_grid`.
- Update `terminal_grid::resize` to use the stored runtime value.
- Update `terminal_session` to accept and forward the scrollback line count to `terminal_grid`.
- Update `make_application` to pass the current hardcoded value (10000).
- **Tests:** Update existing grid and scrollback buffer tests to pass an explicit line count. Verify resizing preserves the configured capacity.

---

### C2. Refactor: glyph_renderer accepts font family and size

**User sees:** No change. Text still renders with Consolas at the current default size.

- Change `make_glyph_renderer` signature to accept `std::string_view font_family` and `float font_size_pt` instead of using the hardcoded `L"Consolas"` and `k_font_size_px`.
- Internally convert `font_size_pt` to pixels using the system DPI (the existing raster size logic stays intact, only the input changes).
- Update `make_renderer_context` to accept and forward these font parameters.
- Update `make_application` to pass the current hardcoded values (`"Consolas"`, `14.0`).
- **Tests:** No new unit tests (font rendering is visual). Ensure existing builds compile and launch correctly.

---

### C3. Refactor: shell command line as parameter

**User sees:** No change. betty still launches PowerShell with `-NoProfile -NoLogo`.

- Add `std::string command_line` field to `shell_settings` (default: `"powershell.exe -NoProfile -NoLogo"`).
- In `make_shell`, use `settings.command_line` to spawn the process instead of the hardcoded string.
- Update `make_application` to construct `shell_settings` with the default command line.
- **Tests:** Update any existing shell-related tests. Add a test verifying that a custom command line is passed through `shell_settings` correctly.

---

## Phase 1: Config Infrastructure

---

### C4. Parse config.toml into a betty_config struct

**User sees:** If a valid `config.toml` exists next to `betty.exe`, betty starts normally (values not yet wired — no visible change). If the file doesn't exist, betty starts normally with built-in defaults — no message, no file created. If the file has TOML syntax errors, a message box appears listing parse errors, then betty starts with defaults.

- CMake `FetchContent` for `tomlplusplus` (header-only, C++17).
- Define the `betty_config` struct with all fields from the PRD schema and their default values.
- Write a `parse_config()` free function that:
  - Constructs the path to `config.toml` next to the executable via `GetModuleFileNameW`.
  - If the file doesn't exist, returns the default `betty_config` silently.
  - If the file exists, parses it with `tomlplusplus`. On TOML syntax errors, displays a message box and returns the default `betty_config`.
- Call `parse_config()` from `main` before `make_application()` (parsed but not yet used).
- **Tests:** Unit tests for `parse_config()` — valid TOML, missing file, TOML syntax error. Test that all default values are correct.

---

### C5. Validate config values and show errors

**User sees:** If `config.toml` has invalid values (e.g., `cursor_style = "beam"`, `scrollback_lines = -50`), betty shows a **single message box** listing **all** validation errors, falls back to defaults for the bad keys, and continues launching. If `shell` is empty or invalid, betty shows an error message box and **terminates** (fatal).

- Add a `validate_config()` function that checks each field's constraints per the PRD:
  - `font_family`: non-empty string.
  - `font_size`: positive float; clamp to `[6.0, 36.0]`.
  - `cursor_style`: must be `"block"` or `"none"` (case-sensitive).
  - `scrollback_lines`: integer >= 1.
  - `columns`: integer >= 1.
  - `rows`: integer >= 1.
  - `shell`: non-empty string (fatal if invalid).
- Collect all errors into a single message box string. Show it with `MessageBoxW`.
- Unknown keys are silently ignored (forward compatibility).
- On shell validation failure, show the error and call `exit(1)`.
- Integrate `validate_config()` into `parse_config()` so the caller receives a validated config.
- **Tests:** Unit tests for each validation rule individually; tests for aggregated error reporting; test that unknown keys are tolerated; test that shell error is fatal.

---

## Phase 2: Wiring

---

### C6. Wire config values into the application

**User sees:** Editing `font_family`, `font_size`, `scrollback_lines`, `columns`, `rows`, or `shell` in `config.toml` changes the corresponding behaviour on the next launch. Custom fonts render. Custom scrollback depth works. Initial window geometry respects configured columns × rows. Custom shell launches.

- Pass `betty_config` (or its relevant fields) through `make_application()` into:
  - `make_renderer_context` → `make_glyph_renderer` (font family, font size).
  - `terminal_session` → `terminal_grid` (scrollback lines, columns, rows).
  - `make_shell` (command line, columns, rows for initial ConPTY size).
  - Window creation (size derived from columns × rows × cell metrics).
- The config is stored on `application` for later hot-reload access.
- **Tests:** Integration test: write a temp `config.toml`, launch betty programmatically (or test the config→application wiring path), verify values reach each subsystem.
- **Tests:** Test that when `columns` and `rows` are set, the initial window size is computed correctly.

---

### C7. Support cursor_style = "none"

**User sees:** Setting `cursor_style = "none"` in `config.toml` makes the cursor invisible. Setting it to `"block"` (or omitting it) shows the block cursor as before.

- When `cursor_style` is `"none"`, pass `std::nullopt` to `draw_grid`'s cursor parameter instead of the cursor position.
- The config's `cursor_style` field is stored on `application` so it can be checked each frame.
- **Tests:** Integration test verifying that with `cursor_style = "none"`, no cursor cell is highlighted in the render output. Test that `"block"` restores the cursor.

---

## Phase 3: Hot-Reload

---

### C8. File watcher: detect config.toml changes

**User sees:** No visible change yet. betty internally detects when `config.toml` is modified, deleted, or created.

- Spawn a dedicated background thread that calls `ReadDirectoryChangesW` on the directory containing `config.toml`, filtering for `FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME`.
- On a change event, post a custom window message (`WM_APP + N`) to the main window to signal "config changed."
- The watcher thread cleanly shuts down on application exit.
- **Tests:** Manual verification (filesystem watchers are impractical to unit test on Windows). Confirm via debug output that file changes are detected.

---

### C9. Hot-reload: apply config changes at runtime

**User sees:** Editing `font_family`, `font_size`, `cursor_style`, `scrollback_lines`, `columns`, or `rows` in `config.toml` while betty is running takes effect immediately — no restart needed. Editing `shell` has no effect until restart (it is silently ignored on reload).

- In the main message loop, handle the watcher's "config changed" message by calling `parse_config()` + `validate_config()`.
- Apply changes per-setting:
  - **Font family / size:** Recreate the `glyph_renderer` via a new `renderer_context` factory method (or by replacing the internal renderer). The next frame uses the new font.
  - **Cursor style:** Update the stored value checked in `run()`.
  - **Scrollback lines:** Call `scrollback_buffer::resize()` with the new capacity, preserving existing scrollback content where possible.
  - **Columns / rows:** Resize the window and trigger the existing resize flow (which reflows the grid and notifies the shell).
  - **Shell:** Log that shell changes require restart; take no action.
- Thread safety: config is read by the main thread only; the watcher thread only posts messages.
- **Tests:** Manual verification — launch betty, edit each setting in `config.toml`, save, confirm the change appears in the running terminal.

---

### C10. Hot-reload: error recovery

**User sees:** If `config.toml` is corrupted or deleted while betty is running:
- **Corrupted (parse/validation error):** A message box appears listing the errors. All settings revert to built-in defaults. betty continues running.
- **Deleted:** Settings silently revert to built-in defaults. No message box.
- **Restored to valid:** When the file becomes valid again (e.g., user fixes the typo and saves), settings are re-applied automatically without restart.

- In the hot-reload handler (from C9), catch parse and validation errors.
- On error: revert `application`'s config to the hardcoded defaults, apply them (recreate glyph_renderer with Consolas/14pt, reset scrollback to 10000, restore block cursor), and show a message box.
- On file deletion: detect a `FILE_ACTION_REMOVED` event and revert silently (no message box — the user intentionally deleted it).
- On file restoration: detect a `FILE_ACTION_ADDED` or `FILE_ACTION_MODIFIED` event, re-parse, and re-apply if valid.
- **Tests:** Manual verification — corrupt the file, confirm message box + defaults. Delete the file, confirm silent revert. Restore a valid file, confirm settings re-apply.

---

## Summary of Dependencies

```
C1 ──┐
C2 ──┤
C3 ──┘
      │
      ▼
C4 ──► C5 ──► C6 ──► C7
                    │
                    ▼
              C8 ──► C9 ──► C10
```

- C1, C2, C3 are independent of each other and come first (no user-visible change).
- C4 depends on all three preparatory tasks being done (the config struct references types/values those tasks define).
- C5 depends on C4 (validates the struct C4 produces).
- C6 depends on C5 (wires validated config into the refactored factories).
- C7 depends on C6 (needs the wired-up application to check cursor_style).
- C8 is independent and can be done anytime after C4.
- C9 depends on C6 + C8.
- C10 depends on C9.
