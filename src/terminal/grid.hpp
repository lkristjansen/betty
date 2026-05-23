#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include "platform/types.hpp"
#include "vt_parser.hpp"

namespace betty::terminal {

// ===========================================================================
// grid_cell — a single character cell in the terminal grid
// ===========================================================================

struct grid_cell {
  char32_t codepoint = U' ';         // default: space
  rgb_color fg = default_fg();       // foreground colour
  rgb_color bg = default_bg();       // background colour
  cell_attr attr = cell_attr::none;  // text attributes (bold, italic, etc.)
};

// ===========================================================================
// terminal_grid — 2D cell grid with cursor tracking and auto-scroll
// ===========================================================================

class terminal_grid {
public:
  // Create a grid of `cols` × `rows` cells, all initialised to space.
  // Cursor starts at (0, 0).
  terminal_grid(uint32_t cols, uint32_t rows);

  // --- Dimensions -----------------------------------------------------------

  [[nodiscard]] auto cols() const noexcept -> uint32_t { return cols_; }
  [[nodiscard]] auto rows() const noexcept -> uint32_t { return rows_; }

  // --- Cursor ---------------------------------------------------------------

  [[nodiscard]] auto cursor_col() const noexcept -> uint32_t { return cursor_col_; }
  [[nodiscard]] auto cursor_row() const noexcept -> uint32_t { return cursor_row_; }

  // --- Write operations -----------------------------------------------------

  // Write a single printable codepoint at the cursor, then advance.
  // Handles wide characters (2 cells), combining characters (NFC pre-composition),
  // auto-wrap at column boundary, and scroll at bottom row.
  void write_char(char32_t cp);

  // Process a sequence of raw bytes (VT-stripped shell output).
  // Internally feeds bytes through vt_parser and applies resulting actions.
  void write_bytes(std::string_view data);

  // Apply a single parsed action to the grid.
  void apply(action const& a);

  // --- Explicit cursor control ----------------------------------------------

  // Move to start of next line; scroll if already on last row.
  void newline();

  // Move cursor to column 0 (same row).
  void carriage_return();

  // Shift all rows up by one, clearing the bottom row.
  // Cursor row is unchanged (caller adjusts if needed).
  // Respects scroll region: only rows [scroll_top_, scroll_bottom_] shift.
  void scroll_up();

  // --- Scroll region (DECSTBM) ---------------------------------------------

  // Set the scrolling region (top and bottom margins, 1-based).
  // CSI Ps ; Ps r.  If top >= bottom the call is ignored.
  // A bottom of 0 resets to full screen.
  void set_scroll_region(uint32_t top, uint32_t bottom);

  // --- Line operations ------------------------------------------------------

  // IL — insert n blank lines at cursor within the scroll region.
  // Ignored if cursor is outside the scroll region.
  // Cursor column is reset to 0.
  void insert_lines(uint32_t n);

  // DL — delete n lines at cursor within the scroll region.
  // Ignored if cursor is outside the scroll region.
  // Cursor column is reset to 0.
  void delete_lines(uint32_t n);

  // --- Character operations (Task 14) --------------------------------------

  // ICH — insert n blank cells at cursor, shifting the row right.
  // Cells shifted past the right edge are lost. Cursor is unchanged.
  void insert_chars(uint32_t n);

  // DCH — delete n cells at cursor, shifting the row left.
  // Blank cells fill the vacated positions on the right. Cursor is unchanged.
  void delete_chars(uint32_t n);

  // ECH — overwrite n cells at cursor with blank cells (no shifting).
  // Cursor is unchanged.
  void erase_chars(uint32_t n);

  // SU — scroll the scroll region up by n lines.
  // If scroll_top_ == 0 (full screen), scrolled-off rows go into scrollback.
  void scroll_page_up(uint32_t n);

  // SD — scroll the scroll region down by n lines.
  // Always inserts blank rows at top; no scrollback interaction.
  void scroll_page_down(uint32_t n);

  // --- Scrollback -----------------------------------------------------------

  // Maximum number of off-screen lines retained.
  static constexpr uint32_t k_scrollback_max = 10000;

  // Scroll the viewport up/down by `delta` rows.
  // Positive delta = scroll back (up), negative = scroll forward (down).
  // Returns the new viewport scroll offset.
  auto scroll_viewport(int32_t delta) -> uint32_t;

  // Whether the viewport is following output (at the bottom).
  [[nodiscard]] auto is_following_output() const noexcept -> bool { return viewport_scroll_ == 0; }

  // --- Access (for rendering) -----------------------------------------------

  [[nodiscard]] auto cell(uint32_t row, uint32_t col) const -> grid_cell const&;
  [[nodiscard]] auto cells() const noexcept -> std::span<const grid_cell>;

  // Produce a flat row-major buffer of resolved render_cell structs.
  // Default fg/bg colours are resolved to actual RGB values internally.
  // The returned span references a stable internal cache that is rebuilt on
  // each call; capacity is reused across calls to avoid repeated allocations.
  [[nodiscard]] auto render_cells() -> std::span<const platform::render_cell>;

  // --- Resize (placeholder for Task 10) -------------------------------------

  void resize(uint32_t new_cols, uint32_t new_rows);

  // --- Observer for out-of-band terminal events (e.g. OSC window title) -----

  void set_observer(std::function<void(std::string_view)> on_title);

private:
  uint32_t cols_;
  uint32_t rows_;
  uint32_t cursor_col_ = 0;
  uint32_t cursor_row_ = 0;
  uint32_t saved_cursor_col_ = 0;
  uint32_t saved_cursor_row_ = 0;

  // ── Circular buffer ────────────────────────────────────────────────────
  //
  // The grid stores scrollback + visible rows in a single circular buffer.
  // Physical capacity: total_capacity_rows_ = rows_ + k_scrollback_max.
  // cells_ has size = cols_ * total_capacity_rows_.
  //
  // Logical layout (conceptual):
  //   [oldest scrollback] ... [newest scrollback] [visible row 0] ... [visible row rows_-1]
  //
  // scrollback_head_  — physical index of the oldest scrollback row.
  // scrollback_count_ — how many scrollback rows exist (0..k_scrollback_max).
  // visible_idx(logical) = (scrollback_head_ + logical) % total_capacity_rows_.
  //
  // The visible portion is always the last rows_ logical rows
  // (i.e. logical indices [scrollback_count_, scrollback_count_+rows_)).

  uint32_t total_capacity_rows_ = 0;
  uint32_t scrollback_head_ = 0;
  uint32_t scrollback_count_ = 0;
  uint32_t viewport_scroll_ = 0;  // 0 = following output; >0 = scrolled back
  uint32_t scroll_top_ = 0;       // 0-based, inclusive
  uint32_t scroll_bottom_ = 0;    // 0-based, inclusive
  std::vector<grid_cell> cells_;
  vt_parser parser_;

  // Convert a logical row index (0 = oldest scrollback) to a physical
  // index into cells_.  Assumes logical < scrollback_count_ + rows_.
  [[nodiscard]] auto physical_index(uint32_t logical_row) const -> uint32_t;

  // Cache of resolved render_cell values for the platform renderer.
  mutable std::vector<platform::render_cell> render_cache_;

  // Current SGR state — applied to each cell on write_char.
  rgb_color current_fg_ = default_fg();
  rgb_color current_bg_ = default_bg();
  cell_attr current_attr_ = cell_attr::none;

  // --- Erase helpers (Task 8) ----------------------------------------------

  // ED — Erase in Display (CSI Ps J).
  void erase_display(uint32_t mode);

  // EL — Erase in Line (CSI Ps K).
  void erase_line(uint32_t mode);

  // Observer for out-of-band terminal events (OSC window title, etc.).
  std::function<void(std::string_view)> on_window_title_;

  // --- Private Unicode helpers --------------------------------------------

  // Write a single cell at (cursor_row_, col) with the given properties.
  void write_cell(uint32_t col, char32_t cp, rgb_color fg, rgb_color bg, cell_attr attr);

  // Handle a zero-width combining character (wcwidth == 0).
  // Attempts NFC pre-composition with the previous cell; falls back to width 1.
  void write_combining_char(char32_t cp);
};

} // namespace betty::terminal
