# betty Configuration File — Product Requirements Document

## 1. Overview

This document defines the configuration file system for **betty**. The config allows users to customize a small set of terminal settings via a single TOML file placed next to the executable. All other settings remain hardcoded as defined in the main PRD.

---

## 2. File Specification

### 2.1 Format

**TOML** — chosen for human readability and clean syntax with minimal ceremony.

### 2.2 Location & Discovery

- betty looks for `config.toml` in the **same directory as `betty.exe`** (portable layout).
- No other locations are searched.
- No CLI flag to override the path (no CLI flags, period).

### 2.3 Missing File Behavior

If `config.toml` does not exist:
- betty starts normally with **built-in defaults**.
- betty **never** creates, writes, or modifies the config file.
- No warning or message is shown.

### 2.4 File Watcher (Hot-Reload)

After startup, betty registers a filesystem watcher on the directory containing `config.toml` via `ReadDirectoryChangesW`. If the config file is modified:

1. betty re-parses `config.toml`.
2. Valid settings are applied immediately where possible.
3. Settings that **cannot** be hot-applied (currently: `shell`) are ignored on reload — they only take effect on restart. If only shell changed, betty shows no message.
4. If the file is deleted or becomes unparseable, betty reverts to built-in defaults for all settings and shows an error message box.
5. If the file is restored to a valid state, settings are re-applied without restart.

---

## 3. Configurable Settings

### 3.1 Schema

```toml
# Font family name. Must be a monospace font installed on the system.
# Default: "Consolas"
font_family = "Consolas"

# Font size in points (logical, resolution-independent).
# Default: 14.0
font_size = 14.0

# Cursor rendering style.
# Valid values: "block", "none"
# - "block": solid block cursor at the current position (reverse video).
# - "none":  no visible cursor.
# Default: "block"
cursor_style = "block"

# Scrollback buffer size in lines.
# Must be a positive integer.
# Default: 10000
scrollback_lines = 10000

# Initial terminal grid dimensions in columns (width) and rows (height).
# The window is sized accordingly on first launch. Must each be >= 1.
# These are overridden on subsequent launches if the window was resized
# and the OS restores the previous window size.
# Default: columns = 120, rows = 40
columns = 120
rows = 40

# Shell command line to execute. A single string containing the full
# executable path and arguments.
# If the executable cannot be found or fails validation, betty shows an
# error message box and terminates (this is the only fatal config error).
# Default: "powershell.exe -NoProfile -NoLogo"
shell = "powershell.exe -NoProfile -NoLogo"
```

### 3.2 All Keys

| Key | Type | Default | Required | Hot-Reload |
|---|---|---|---|---|
| `font_family` | string | `"Consolas"` | No | Yes |
| `font_size` | float | `14.0` | No | Yes |
| `cursor_style` | string | `"block"` | No | Yes |
| `scrollback_lines` | integer | `10000` | No | Yes |
| `columns` | integer | `120` | No | Yes (next render) |
| `rows` | integer | `40` | No | Yes (next render) |
| `shell` | string | `"powershell.exe -NoProfile -NoLogo"` | No | **No — restart required** |

All keys are optional. Omitted keys fall back to their default value.

Unknown keys are silently ignored (forward compatibility).

### 3.3 Type & Value Validation

| Key | Constraints |
|---|---|
| `font_family` | Non-empty string. Validation that the font exists on the system is **not** performed at config time — if the font is missing, DirectWrite will fall back to a system default font at render time. |
| `font_size` | Positive float (e.g., `8.0` to `72.0`). Values outside a reasonable range are clamped, not rejected. |
| `cursor_style` | Must be exactly `"block"` or `"none"` (case-sensitive). |
| `scrollback_lines` | Positive integer. Zero is rejected (scrollback must be at least 1). |
| `columns` | Integer >= 1. |
| `rows` | Integer >= 1. |
| `shell` | Non-empty string. If the executable part fails basic validation (empty, or not a valid path format), betty shows an error and **terminates**. |

### 3.4 Complete Example

```toml
font_family = "Cascadia Code"
font_size = 13.0
cursor_style = "block"
scrollback_lines = 20000
columns = 140
rows = 50
shell = "pwsh.exe -NoLogo"
```

---

## 4. Error Handling

### 4.1 TOML Parse Errors

If `config.toml` exists but contains invalid TOML syntax (the `tomlplusplus` parser throws):

1. betty **does not terminate**.
2. A **Windows message box** is displayed listing the parse error(s).
3. All settings fall back to built-in defaults.
4. betty continues running with defaults.

### 4.2 Validation Errors

If the TOML parses successfully but individual values fail validation:

1. betty **does not terminate**.
2. A **Windows message box** is displayed listing **all** validation errors found (not just the first).
3. Invalid keys fall back to their built-in defaults.
4. Valid keys are applied normally.
5. betty continues running.

### 4.3 Shell Validation (Special Case)

If `shell` is set to an empty string or otherwise fails validation:

1. betty shows an error message box.
2. betty **terminates** — it cannot launch without a valid shell.

This is the **only fatal config error**.

### 4.4 Error Message Format

The message box should identify each error with the key name and a brief explanation. Example:

```
betty — Configuration Error

The following settings in config.toml are invalid:

  • cursor_style: "beam" is not a valid value. Expected "block" or "none".
  • scrollback_lines: must be a positive integer, got -50.
  • columns: must be >= 1, got 0.

These settings have been reset to their defaults. betty will continue.
```

---

## 5. Hardcoded (Non-Configurable) Settings

The following remain hardcoded in the source and are **not** exposed in the config file:

| Setting | Hardcoded Value |
|---|---|
| ANSI color palette (16 colors) | Catppuccin Mocha |
| Default foreground | `#cdd6f4` |
| Default background | `#1e1e2e` |
| Cursor color | `#f5e0dc` |
| Cursor blink | Off (static) |
| Keybindings | Fixed — `Ctrl+Shift+Up/Down` for scrollback |
| Window opacity / transparency | Solid background only |
| Shell startup directory | Inherited from betty process |
| Window title bar | Standard Windows 11 (non-custom) |

---

## 6. Implementation Notes

### 6.1 Parser Dependency

- **`tomlplusplus`** (header-only, C++17) via CMake `FetchContent`.
- Repo: `https://github.com/marzer/tomlplusplus`
- No other third-party dependencies introduced.

### 6.2 Architecture

- The parsed config lives as a plain struct (`betty_config`) populated once at startup and updated on hot-reload.
- Modules that consume settings receive the relevant values through their existing interfaces (e.g., `make_glyph_renderer` receives font family and size; the grid constructor receives columns, rows, and scrollback).
- The file watcher runs on a dedicated thread using `ReadDirectoryChangesW` with `FILE_NOTIFY_CHANGE_LAST_WRITE`. On change, it re-parses and pushes the new config to the main thread via a thread-safe mechanism (e.g., `PostMessage` to the main window).

### 6.3 Hot-Reload Thread Safety

- The config struct is protected by a read-write lock or replaced atomically (`std::atomic<std::shared_ptr<betty_config>>`).
- The watcher thread writes a new config and posts a message to the main window.
- The main thread applies the new config on the next frame/render cycle.

---

## 7. Acceptance Criteria

1. If `config.toml` does not exist next to `betty.exe`, betty launches with defaults and no message is shown.
2. A valid `config.toml` with a custom font family, font size, cursor style, scrollback, geometry, and shell is applied correctly on launch.
3. A `config.toml` with TOML syntax errors triggers a message box and betty starts with defaults.
4. A `config.toml` with valid TOML but invalid values (e.g., `cursor_style = "beam"`) triggers a single message box listing **all** errors and betty starts with defaults for the bad keys.
5. Setting `shell = ""` causes betty to show an error message box and **terminate**.
6. Editing `config.toml` while betty is running hot-reloads font, cursor, scrollback, and geometry settings without restart.
7. Editing `shell` while betty is running has no effect until restart.
8. Deleting or corrupting `config.toml` while betty is running reverts to defaults and shows an error message box.
9. Unknown keys in `config.toml` are silently ignored.
10. The `tomlplusplus` dependency is fetched by CMake at build time and does not require pre-installation.

---

## 8. Explicitly Out of Scope

- Multiple config files or includes.
- Environment variable interpolation in config values (e.g., `shell = "${HOME}/..."`).
- Config file generation or `--init` flag.
- `--config <path>` CLI override.
- Keybinding configuration.
- Color scheme / palette configuration.
- Profiles, tabs, panes, or multi-session settings.
- Per-shell profiles (only one shell configured).
- Startup working directory.
- Conditional sections or platform-specific blocks.
- Comments/documentation in the default config (since betty never writes a file).
