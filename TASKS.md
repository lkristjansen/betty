# betty — Task Breakdown

Each task is a vertical slice: when completed, the user can launch `betty.exe` and see/experience the new behaviour. Tasks are ordered smallest-first where practical.

---

### 1. ✅ Project scaffold + bare window
**User sees:** Launching `betty.exe` opens a window filled with the Catppuccin Mocha background colour (`#1e1e2e`). The window has a standard title bar and can be closed.
- CMake project structure
- Win32 window class, message loop
- DirectX swap chain + clear to background colour

---

### 2. ✅ Static text rendering
**User sees:** Hardcoded text ("betty") appears in the window, rendered with Consolas at 14 pt via DirectWrite. Text is positioned at the top-left of the window.
- DirectWrite factory, text format, text layout
- Render text to a DirectWrite bitmap/atlas or draw directly each frame
- Verify font metrics (cell width, cell height) are calculated

---

### 3. ✅ Live shell I/O
**User sees:** A PowerShell session runs in the window. Typing on the keyboard sends input to the shell; shell output appears as raw text in the window. Closing the window terminates the shell.
- Spawn PowerShell via ConPTY (`CreatePseudoConsole`)
- Read thread for ConPTY output pipe
- Forward keystrokes (WM_KEYDOWN) to ConPTY input pipe
- Display raw output text line-by-line in the window

---

### 4. ✅ Terminal grid
**User sees:** Output fills the window top-to-bottom and scrolls when it reaches the bottom, instead of just placing each line at a fixed position. Characters sit in a proper grid.
- Cell grid data structure (rows × columns of character cells)
- Character placement at cursor position, cursor advances
- Newline (`\n`) moves to next row
- Auto-scroll when output hits the bottom row

---

### 5. ✅ Cursor movement sequences
**User sees:** The shell prompt appears at the correct position. Cursor-aware terminal apps position text correctly. Typing `clear` moves the cursor but doesn't yet erase anything.
- Parse CUP, CUU, CUD, CUF, CUB, HVP
- Save/restore cursor (DECSC/DECRC)
- Render text at the correct cursor position on the grid

---

### 6. ✅ SGR colours
**User sees:** Colourful command output — `ls` listings, git diffs, syntax-highlighted prompts all render with correct Catppuccin Mocha colours. True-colour apps (e.g. `neofetch`) display correctly.
- Parse SGR sequences for standard 16 colours, bright variants, true colour (`38;2;R;G;B`, `48;2;R;G;B`)
- Apply Catppuccin Mocha palette for the 16 ANSI colour slots
- Render foreground and background colours per cell

---

### 7. ✅ Block cursor
**User sees:** A solid block cursor is visible at the current input position, rendered in reverse video (foreground/background swapped). It follows text as the user types.
- Track cursor position on the grid
- Render the cell at cursor position with colours inverted
- Cursor does not blink (static)

---

### 8. ✅ Erase operations
**User sees:** `clear` actually clears the screen. Ctrl+L works. Line editing in the shell (backspace, rewrites) erases characters properly.
- Parse ED (Erase in Display) — clear from cursor to end, clear from beginning to cursor, clear entire screen (and optionally scrollback)
- Parse EL (Erase in Line) — same variants for the current line
- Grid cells are erased to default background/foreground

---

### 9. Window title
**User sees:** The window title bar updates to reflect the shell's current working directory (e.g. `~`, `C:\Users\...`). Apps that set the title via escape sequences work.
- Parse OSC 0 and OSC 2 (set window title)
- Call `SetWindowText` to update the title bar
- Restore default title on shell exit

---

### 10. Window resizing
**User sees:** Dragging the window edge reflows the terminal content. Text wraps to the new column count. Shell commands like `ls` re-layout to fill the new width.
- Handle `WM_SIZE` to detect client area changes
- Recompute rows and columns from new client size and font metrics
- Resize the terminal grid buffer (preserving existing content where possible)
- Notify ConPTY of new dimensions via `ResizePseudoConsole`

---

### 11. Scrollback
**User sees:** Pressing Ctrl+Shift+Up scrolls back through previous output. Ctrl+Shift+Down scrolls forward. New output does not steal the view when scrolled away from the bottom. Returning to the bottom resumes following output.
- Fixed scrollback buffer (10,000 lines)
- Move lines from the main grid into scrollback when they scroll off-screen
- Keyboard handling for Ctrl+Shift+Up / Ctrl+Shift+Down
- Render a viewport into the combined scrollback + visible grid
- When scrolled back, new output appends without moving the viewport

---

### 12. Text attributes
**User sees:** `git diff` shows bold commit hashes. Italic comments appear in code. Underlined links are visible. Strikethrough text renders in applicable apps.
- Parse SGR bold, italic, faint, underline, strikethrough, reverse video
- Render each attribute correctly via DirectWrite (font weight, style, underline, strikethrough)
- Reverse video swaps foreground/background colours for the cell

---

### 13. Line operations
**User sees:** Terminal apps that insert or delete lines (e.g. `top`, `htop`, progress bars) work correctly. Scrolling regions behave as expected.
- Parse IL (Insert Lines), DL (Delete Lines)
- Parse SU (Scroll Up), SD (Scroll Down)
- Shift grid rows accordingly

---

### 14. Character operations
**User sees:** Inline editing in the shell works correctly — inserting text shifts characters right, deleting characters pulls them left. Cursor stays in the correct position.
- Parse ICH (Insert Characters), DCH (Delete Characters)
- Shift characters within a row

---

### 15. Unicode + wide characters
**User sees:** Emoji, CJK characters, and other Unicode text render without crashing. Wide characters (Chinese, Japanese, Korean) occupy two cells. Combining characters (accents, diacritics) display correctly over their base character.
- Handle UTF-8 decoding of ConPTY output
- Detect wide characters (East Asian Width = Wide/Fullwidth); allocate two grid cells
- Render wide and combining characters via DirectWrite text layout
- Handle wide characters at the edge of a row (wrap to next line)

---

### 16. Signal handling
**User sees:** Ctrl+C interrupts a running command (e.g. `ping -t localhost`). Ctrl+Break behaves similarly. The shell becomes responsive again.
- Handle Ctrl+C (0x03) and Ctrl+Break via ConPTY
- Send the appropriate control character to the ConPTY input pipe

---

### 17. Shell exit cleanup
**User sees:** Typing `exit` or closing the shell process closes the betty window cleanly. No lingering processes or zombie windows.
- Detect shell process exit (ConPTY pipe closure or process handle signal)
- Close the window gracefully (`PostQuitMessage` or `DestroyWindow`)
- Clean up ConPTY handles and the DirectX device

---

### 18. Unknown sequence resilience
**User sees:** Malformed or unrecognised escape sequences do not crash betty. Garbled text or unsupported sequences are silently ignored (or rendered as plain text where safe).
- ANSI parser handles unrecognised sequences gracefully
- Invalid UTF-8 sequences do not crash or corrupt the grid
- Edge cases in cursor positioning (out-of-bounds) are clamped, not asserted

---

## Summary of Dependencies

```
 1 ──► 2 ──► 3 ──► 4 ──► 5 ──► 6
                              │
                              ├──► 7
                              │
                              ├──► 8
                              │
                              ├──► 9
                              │
                              ├──► 10
                              │
                              ├──► 11
                              │
                              ├──► 12
                              │
                              ├──► 13 ──► 14
                              │
                              ├──► 15
                              │
                              ├──► 16
                              │
                              └──► 17

 18 can be done at any point after 5
```

Tasks 1→2→3→4→5 are strictly sequential. Task 6 (colours) depends on 5. After 6, tasks 7–17 can largely be done in any order (with the noted exception of 13→14). Task 18 (resilience) can be tackled any time after ANSI parsing exists (task 5+).
