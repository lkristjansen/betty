# betty (be-tty) — Product Requirements Document

## 1. Overview

**betty** is a minimal, GPU-accelerated terminal emulator for Windows 11. It is a personal project built for the joy of building. The MVP is deliberately scoped to be a single-window, single-session terminal with no tabs, no panes, and no configuration system — just a fast, beautiful terminal that works out of the box.

---

## 2. Goals & Non-Goals

### Goals
- Provide a fast, GPU-accelerated terminal emulation experience on Windows 11.
- Render text beautifully with Unicode support, true color, and a curated default appearance.
- Keep the codebase modern and approachable (C++23, CMake).
- Ship a working MVP as a build-from-source project.

### Non-Goals (for MVP)
- Tabs, panes, or multi-session support.
- Configuration files, settings UI, or command-line flags.
- Copy/paste (text selection & clipboard).
- Transparency, acrylic, or Mica effects.
- Plugin/extension system.
- Support for shells other than PowerShell.
- Support for Windows versions older than Windows 11.
- Installer or packaged distribution.

---

## 3. Technology Stack

| Concern              | Choice                |
|----------------------|-----------------------|
| Language             | C++23                 |
| Build system         | CMake                 |
| Rendering            | GPU-accelerated via DirectX 11/12 |
| Shell integration    | Windows ConPTY API    |
| Font rendering       | DirectWrite           |
| Target platform      | Windows 11 (x86-64)   |

---

## 4. Architecture Overview

```
┌─────────────┐    ConPTY     ┌──────────────────┐
│  PowerShell  │◄────────────►│     betty        │
│  (conhost    │  stdin/stdout │  ┌────────────┐  │
│   headless)  │               │  │  Terminal   │  │
└─────────────┘               │  │  Emulator   │  │
                               │  │  (state)    │  │
                               │  └─────┬──────┘  │
                               │        │         │
                               │  ┌─────▼──────┐  │
                               │  │  Renderer   │  │
                               │  │  (DirectX)  │  │
                               │  └────────────┘  │
                               └──────────────────┘
```

Three core modules:

1. **PTY Manager** — Spawns PowerShell via ConPTY, manages I/O pipes, handles shell lifecycle.
2. **Terminal Emulator** — Parses ANSI/VT sequences, maintains grid state (cells, attributes, scrollback).
3. **Renderer** — GPU-accelerated rendering via DirectX. Consumes terminal state and draws to the window each frame.

---

## 5. Feature Requirements

### 5.1 Shell Integration
- **FR1:** betty shall spawn a single PowerShell session using the Windows ConPTY API.
- **FR2:** betty shall forward all keystrokes (including control characters) to the shell via the ConPTY input pipe.
- **FR3:** betty shall read output from the ConPTY pipe and feed it to the terminal emulator for parsing and display.
- **FR4:** betty shall handle Ctrl+C (SIGINT) and Ctrl+Break correctly via ConPTY.
- **FR5:** When the shell process exits, betty shall close the window.

### 5.2 ANSI / VT Sequence Support
- **FR6:** betty shall parse and correctly render the following ANSI escape code categories:
  - **Cursor movement:** CUP, CUU, CUD, CUF, CUB, HVP, save/restore cursor (DECSC/DECRC).
  - **Erase:** ED (clear screen/scrollback), EL (clear line).
  - **SGR (Select Graphic Rendition):** Bold, italic, faint, underline, strikethrough, reverse video, and the standard 16 foreground/background color sequences.
  - **True color:** SGR `38;2;R;G;B` (foreground) and `48;2;R;G;B` (background).
  - **Insert/delete lines and characters:** IL, DL, ICH, DCH.
  - **Scrolling:** SU, SD (scroll up/down).
  - **Window title:** OSC 0/2 (set window title).
- **FR7:** The terminal shall ignore unknown/unrecognized escape sequences gracefully (no crash, no corruption).

### 5.3 Display & Rendering
- **FR8:** betty shall render using GPU-accelerated DirectX (11 or 12).
- **FR9:** betty shall use DirectWrite for text layout and glyph rendering.
- **FR10:** The terminal grid shall be rendered with the Consolas font at a fixed cell size.
- **FR11:** betty shall apply the **Catppuccin Mocha** dark color palette as the default (and only) theme.
- **FR12:** betty shall support a scrollback buffer, allowing the user to scroll back through output history. Scrollback size shall be fixed (e.g., 10,000 lines).
- **FR13:** The window shall have a standard Windows 11 title bar (non-custom).
- **FR14:** The window background shall be a solid color — no transparency or blur effects.

### 5.4 Cursor
- **FR15:** betty shall display a **block cursor**.
- **FR16:** The cursor shall **not blink**.
- **FR17:** The cursor color shall use the terminal's foreground color with reverse video.

### 5.5 Window Resizing
- **FR18:** The window shall be freely resizable.
- **FR19:** Upon resize, betty shall recompute the terminal grid dimensions (rows × columns) based on the new client area and the fixed font metrics.
- **FR20:** betty shall notify the shell of the new dimensions via the ConPTY resize API.

### 5.6 Scrollback
- **FR21:** betty shall maintain a scrollback buffer of at least 10,000 lines.
- **FR22:** The user shall be able to scroll back through history using **Ctrl+Shift+Up / Ctrl+Shift+Down**.
- **FR23:** Scrolling shall stop when reaching the top or bottom of the buffer.
- **FR24:** When the user is scrolled back and new output arrives, betty shall remain at the scrolled position (do not auto-scroll to bottom).

### 5.7 Input Handling
- **FR25:** betty shall capture and forward all standard keyboard input to the shell.
- **FR26:** betty shall handle keyboard input via the Windows message loop (WM_CHAR, WM_KEYDOWN).
- **FR27:** betty shall support modifier keys (Ctrl, Alt, Shift) and pass them through to the shell where appropriate.

### 5.8 Unicode & Text
- **FR28:** betty shall support UTF-8 encoded input and output via ConPTY.
- **FR29:** betty shall correctly render Unicode characters including wide characters (CJK) and combining characters.

---

## 6. Explicitly Out of Scope (MVP)

| Feature                    | Notes                                    |
|----------------------------|------------------------------------------|
| Tabs                       | Single session only.                     |
| Split panes                | Not in v1.                               |
| Copy/paste                 | No clipboard interaction.                |
| Configuration file/UI      | Everything hardcoded.                    |
| Font selection             | Consolas only.                           |
| Transparency / Mica        | Solid background only.                   |
| Cursor blink               | Block cursor only, static.               |
| Multi-shell support        | PowerShell only.                         |
| Ligatures                  | Not supported.                           |
| Sixel / Kitty image protocol | Not supported.                         |
| Hyperlinks (OSC 8)         | Not supported.                           |
| Bracketed paste mode       | Not applicable (no paste).               |
| Microsoft Store / Winget   | Build from source only.                  |
| Logging / telemetry        | Not included.                            |

---

## 7. Color Palette: Catppuccin Mocha

| Element         | Hex       |
|-----------------|-----------|
| Background      | `#1e1e2e` |
| Foreground      | `#cdd6f4` |
| Black           | `#45475a` |
| Red             | `#f38ba8` |
| Green           | `#a6e3a1` |
| Yellow          | `#f9e2af` |
| Blue            | `#89b4fa` |
| Magenta         | `#f5c2e7` |
| Cyan            | `#94e2d5` |
| White           | `#bac2de` |
| Bright Black    | `#585b70` |
| Bright Red      | `#f38ba8` |
| Bright Green    | `#a6e3a1` |
| Bright Yellow   | `#f9e2af` |
| Bright Blue     | `#89b4fa` |
| Bright Magenta  | `#f5c2e7` |
| Bright Cyan     | `#94e2d5` |
| Bright White    | `#a6adc8` |
| Cursor          | `#f5e0dc` |
| Selection (N/A) | (future)  |

---

## 8. Default Dimensions

| Parameter       | Value   |
|-----------------|---------|
| Font            | Consolas |
| Font size       | 14 pt   |
| Columns         | 120     |
| Rows            | 40      |
| Scrollback      | 10,000 lines |

---

## 9. Acceptance Criteria (MVP Done)

1. Launching `betty.exe` opens a resizable window with a PowerShell session.
2. Text typed into the window appears and commands execute correctly.
3. Command output renders with correct ANSI colors (16-color + true color).
4. Window title updates to reflect the shell's working directory or set title.
5. The Catppuccin Mocha palette is visibly applied (dark background, correct accent colors).
6. The block cursor appears at the correct position and follows input.
7. Resizing the window reflows text and re-layouts correctly.
8. Ctrl+Shift+Up/Down scrolls through output history.
9. Ctrl+C interrupts the running command.
10. Closing the window terminates the shell process cleanly.
11. Unicode characters (e.g., emoji, CJK) render without crashing.
