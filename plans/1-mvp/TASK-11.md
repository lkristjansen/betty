# TASK-11: Scrollback — Implementation Plan

## Summary

Add a 10,000-line scrollback buffer to the terminal. Pressing **Ctrl+Shift+Up** scrolls back through previous output; **Ctrl+Shift+Down** scrolls forward. When scrolled away from the bottom, new output does not steal the view. Returning to the bottom resumes following output.

## Design Decisions

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | **Oversized circular buffer** inside `terminal_grid` | A single contiguous logical space (scrollback + visible grid) with a viewport offset. Wraps around physically to avoid memmove on every scroll. |
| 2 | **Reflow scrollback on column-width resize** | When `cols_` changes, every stored row (scrollback + visible) is re-wrapped to the new width. Preserves all history. |
| 3 | **Scrollback keys handled in `app.cpp` key callback** | `input_handler` stays pure "keys → shell bytes." The lambda in `Application::run()` already has access to both the handler and the grid. |
| 4 | **Row-by-row reflow** | Each stored row is rewrapped independently using simple character-count boundaries. No soft-wrap tracking between rows. |

---

## Step 1: Extend `terminal_grid` with scrollback data members

**File:** `src/terminal/grid.hpp`

Add the scrollback circular buffer, capacity constant, and viewport tracking.

```cpp
// New constants
static constexpr uint32_t k_scrollback_max = 10000;

// New members (add to private section):
// Circular buffer capacity = rows_ + k_scrollback_max (physical rows allocated).
// Logical layout: [oldest scrollback] ... [newest scrollback] [visible row 0] ... [visible row rows_-1]
//
// Physical layout wraps at total_capacity. We track:
//   scrollback_head_  — physical index of oldest scrollback row (or where next would go if empty)
//   scrollback_count_ — how many scrollback rows exist (0 .. k_scrollback_max)
//   viewport_scroll_  — how far the user scrolled back (0 = follow output)
//
// When scrollback_count_ == 0, scrollback_head_ points to the row just before
// the visible grid (even though there's nothing there yet).

uint32_t total_capacity_rows_ = 0;   // rows_ + k_scrollback_max (set in ctor & resize)
uint32_t scrollback_head_ = 0;       // physical index of oldest scrollback row
uint32_t scrollback_count_ = 0;      // rows currently in scrollback (0 .. k_scrollback_max)
uint32_t viewport_scroll_ = 0;       // how many rows above bottom of history we are viewing

// Helper to convert a logical row index (0 = oldest scrollback, scrollback_count_+rows_-1 = newest visible) 
// into a physical index into cells_.
[[nodiscard]] auto physical_index(uint32_t logical_row) const -> uint32_t;
```

Also add public methods:

```cpp
// Scroll the viewport up/down by `delta` rows.
// Positive delta = scroll back (up), negative = scroll forward (down).
// Returns the new viewport_scroll_ value.
auto scroll_viewport(int32_t delta) -> uint32_t;

// Whether the viewport is following output (at the bottom).
[[nodiscard]] auto is_following_output() const noexcept -> bool { return viewport_scroll_ == 0; }
```

---

## Step 2: Modify constructor and `cells_` allocation

**File:** `src/terminal/grid.cpp`

Change the constructor to allocate the circular buffer:

```cpp
terminal_grid::terminal_grid(uint32_t cols, uint32_t rows)
  : cols_(cols)
  , rows_(rows)
  , total_capacity_rows_(rows + k_scrollback_max)
  , cells_(static_cast<size_t>(cols) * total_capacity_rows_, grid_cell{}) {}
```

The `cells_` vector now holds `cols_ * (rows_ + k_scrollback_max)` elements instead of `cols_ * rows_`.

---

## Step 3: Implement `physical_index` helper

**File:** `src/terminal/grid.cpp`

```cpp
auto terminal_grid::physical_index(uint32_t logical_row) const -> uint32_t {
  // logical_row is an offset from the oldest scrollback row (row 0) to
  // the newest visible row (row = scrollback_count_ + rows_ - 1).
  //
  // Physical row = (scrollback_head_ + logical_row) % total_capacity_rows_
  //
  // But scrollback_head_ itself points to the oldest scrollback row.
  // So row 0 physically is at scrollback_head_.
  // Row 1 physically is at (scrollback_head_ + 1) % total_capacity_rows_, etc.
  
  assert(logical_row < scrollback_count_ + rows_);
  return (scrollback_head_ + logical_row) % total_capacity_rows_;
}
```

---

## Step 4: Rewrite `scroll_up()` to preserve lines in scrollback

**File:** `src/terminal/grid.cpp`

The current `scroll_up()` discards the top visible row via `memmove`. The new version:

1. **Copy the top visible row into the scrollback buffer** at position `scrollback_count_` (just after the last scrollback row).
2. **Increment `scrollback_count_`**. If it exceeds `k_scrollback_max`, advance `scrollback_head_` by 1 (modulo `total_capacity_rows_`) to drop the oldest scrollback row.
3. **Shift the visible portion** (rows 1..rows_-1) up to rows 0..rows_-2 — but now using the circular buffer's physical indexing, not a flat `memmove`.
4. **Clear the (new) bottom visible row.**

The logical viewport (which rows are "visible") is always the last `rows_` rows of the logical buffer. After scroll_up:
- The old visible row 0 → scrollback row `scrollback_count_ - 1` (newest scrollback)
- The rest of visible shifts up
- A new blank row appears at the bottom

If `viewport_scroll_ > 0` (user is scrolled back), the viewport offset does not change — the user stays where they are. New output appends "below" their view.

Implementation sketch:

```cpp
void terminal_grid::scroll_up() {
  if (rows_ <= 1) {
    // Single-row grid: just push that row into scrollback and clear it.
    push_visible_row_to_scrollback(0);
    clear_visible_row(0);
    return;
  }

  // 1. Push the top visible row (logical row scrollback_count_) into scrollback.
  push_visible_row_to_scrollback(0);

  // 2. Shift remaining visible rows up by one within the logical buffer.
  //    We do this by adjusting: the visible portion now starts one logical
  //    row later (scrollback_count_ is now +1, so the visible portion's
  //    logical start has advanced by 1 — effectively the shift is "free"
  //    because we use logical indexing). We just need to clear the new
  //    bottom visible row.
  
  // 3. Clear the new bottom visible row.
  uint32_t const last_logical = scrollback_count_ + rows_ - 1;
  uint32_t const phys = physical_index(last_logical);
  size_t const row_start = static_cast<size_t>(phys) * cols_;
  std::fill_n(cells_.data() + row_start, cols_, grid_cell{});
}
```

Wait — this is a key insight: with the circular buffer and logical indexing, `scroll_up()` doesn't need to move any data in the visible portion at all! The visible grid is defined as logical rows `[scrollback_count_, scrollback_count_ + rows_)`. When `scrollback_count_` increments (after pushing a row to scrollback), the visible grid naturally shifts: what was visible row 1 becomes the new visible row 0.

So:

```cpp
void terminal_grid::scroll_up() {
  if (rows_ == 0) return;

  // 1. Copy the current top visible row into scrollback.
  //    Visible row 0 = logical row scrollback_count_.
  uint32_t const src_phys = physical_index(scrollback_count_);
  uint32_t const dst_phys = physical_index(scrollback_count_); // same spot! It's becoming scrollback.
  
  // The row at logical position scrollback_count_ is ALREADY at the correct physical
  // location to become scrollback. We just need to:
  // a) If scrollback is full, advance scrollback_head_ to discard the oldest.
  // b) Increment scrollback_count_.
  
  if (scrollback_count_ < k_scrollback_max) {
    scrollback_count_++;
  } else {
    // Buffer full: advance head to drop the oldest row, then overwrite it later.
    scrollback_head_ = (scrollback_head_ + 1) % total_capacity_rows_;
    // scrollback_count_ stays at k_scrollback_max.
    // The old visible row 0's physical location may collide with the new scrollback write
    // position. Need to think about this more carefully...
  }

  // 2. Clear the new bottom visible row.
  //    New bottom = logical row scrollback_count_ + rows_ - 1.
  if (rows_ > 1) {
    uint32_t const bottom_phys = physical_index(scrollback_count_ + rows_ - 1);
    size_t const offset = static_cast<size_t>(bottom_phys) * cols_;
    std::fill_n(cells_.data() + offset, cols_, grid_cell{});
  } else {
    // Single-row grid: visible row 0 IS the bottom row. Need to clear it.
    uint32_t const phys = physical_index(scrollback_count_);
    size_t const offset = static_cast<size_t>(phys) * cols_;
    std::fill_n(cells_.data() + offset, cols_, grid_cell{});
  }
}
```

Hmm, this needs more careful thought. The key constraint is: `scrollback_head_` points to physical row of oldest scrollback. The visible grid occupies logical rows `[scrollback_count_, scrollback_count_ + rows_)`.

When `scroll_up()` is called:
- The current visible row 0 at logical position `scrollback_count_` should become scrollback.
- Since we're incrementing `scrollback_count_`, the row that was at logical position `scrollback_count_` is now at logical position `scrollback_count_ - 1` (after increment... no wait).

Let me think again:

Before scroll_up:
- scrollback_count_ = N
- Visible rows are at logical indices [N, N+1, ..., N+rows_-1]
- The oldest scrollback is at physical index scrollback_head_

When we push visible row 0 into scrollback:
- scrollback_count_ becomes N+1 (or stays at MAX if full)
- The visible row 0 was at logical index N. After increment, that row now sits at logical index N which is... it hasn't moved. The VIEWPORT definition changed.
  
Actually: the visible portion is ALWAYS the last `rows_` logical rows.
Before: visible = logical [scrollback_count_, scrollback_count_ + rows_)
After incrementing scrollback_count_: visible = logical [scrollback_count_ + 1, scrollback_count_ + 1 + rows_)

So the old visible row 0 (logical index N) is now at logical index N, but since scrollback_count_ is now N+1, the visible range starts at N+1. So logical index N is now "below" the visible range — it's now the newest scrollback row.

But wait — logical index N was the top VISIBLE row. After scroll_up, it SHOULD be the newest scrollback row. And it's already at the correct physical location! We don't need to copy anything.

The only things we need to do:
1. If scrollback is not full: `scrollback_count_++`. The row at old logical index N automatically becomes scrollback.
2. If scrollback is full: `scrollback_head_ = (scrollback_head_ + 1) % capacity` (drop oldest), `scrollback_count_` stays at MAX. But now we need to be careful — the old visible row 0's physical location might be the same as the new scrollback_head_ position if the buffer wrapped. Actually, the visible portion and the scrollback portion are non-overlapping by definition (they share the circular buffer, and total capacity >= rows_ + k_scrollback_max). So advancing scrollback_head_ by 1 just means the oldest scrollback is discarded and its physical slot becomes available for a future row. The visible row 0 stays where it is physically — it just changes LOGICAL status from "visible row 0" to "scrollback row N+1".
3. Clear the new bottom visible row (logical index `scrollback_count_ + rows_ - 1`).

This is much simpler than I initially thought. The circular buffer's power is that we never move data — we just change which logical indices map to which parts of the buffer.

Let me formalize:

```
BEFORE scroll_up (example: 5 visible rows, 2 scrollback rows):
  Logical:  [S0   S1   V0   V1   V2   V3   V4]
  scrollback_count_ = 2
  
AFTER scroll_up:
  Logical:  [S0   S1   S2   V0'  V1'  V2'  V3'  V4']
  Where S2 = old V0 (same physical location, different logical status)
  V0' = old V1, V1' = old V2, ... , V3' = old V4
  V4' = new blank row at a newly claimed physical slot
  scrollback_count_ = 3
```

The "shift" of visible rows is purely logical — V1→V0 means V1's physical row is now at a different logical index (which changed from scrollback_count_+1 to scrollback_count_+0). Since we compute physical from logical via `(scrollback_head_ + logical_index) % capacity`, the physical index naturally changes when logical index changes. No data movement needed.

This is elegant. Let me refine the implementation:

```cpp
void terminal_grid::scroll_up() {
  if (rows_ == 0) return;
  
  // 1. Absorb current top visible row into scrollback.
  if (scrollback_count_ < k_scrollback_max) {
    scrollback_count_++;
  } else {
    // Drop the oldest scrollback row.
    scrollback_head_ = (scrollback_head_ + 1) % total_capacity_rows_;
    // scrollback_count_ stays at k_scrollback_max.
  }
  
  // 2. Clear the new bottom visible row.
  uint32_t const bottom_logical = scrollback_count_ + rows_ - 1;
  uint32_t const bottom_phys = physical_index(bottom_logical);
  size_t const offset = static_cast<size_t>(bottom_phys) * cols_;
  std::fill_n(cells_.data() + offset, cols_, grid_cell{});
  
  // 3. If viewport is following output, no change needed (it tracks the bottom).
  //    If viewport is scrolled back and the new scrollback row pushed the total
  //    content beyond the viewport range, adjust viewport_scroll_.
  //    Actually: if the user is scrolled back, inserting a new scrollback row
  //    increases total_logical_rows by 1. The viewport_scroll_ is measured from
  //    the bottom. If we add a row "below" the viewport, we need to increment
  //    viewport_scroll_ to keep the same rows visible.
  if (viewport_scroll_ > 0) {
    viewport_scroll_ = std::min(viewport_scroll_ + 1, scrollback_count_);
  }
}
```

Wait, point 3 needs more thought. When scrolled back:
- `viewport_scroll_ = N` means we're viewing logical rows [scrollback_count_ - N, scrollback_count_ - N + rows_)
- When scrollback_count_ increases by 1, total increases by 1. To view the SAME rows, we'd need to increment viewport_scroll_ by 1 too.
- But if viewport_scroll_ was already at max (scrollback_count_), incrementing it would go beyond the oldest content. In that case, clamp.

Actually, the behavior should be: when the user is scrolled back and new output arrives, they should NOT be moved. The viewport_scroll_ should auto-increment to compensate for the new row being added "below" them.

Let me think more carefully...

The viewport shows rows [viewport_start, viewport_start + rows_) from logical space.
`viewport_start = total_logical_rows - rows_ - viewport_scroll_`

Before scroll_up: total_logical_rows = scrollback_count_ + rows_
After scroll_up: total_logical_rows = scrollback_count_' + rows_ = (scrollback_count_ + 1) + rows_

If we keep viewport_scroll_ the same, then:
Before: viewport_start = (scrollback_count_ + rows_) - rows_ - viewport_scroll_ = scrollback_count_ - viewport_scroll_
After: viewport_start = (scrollback_count_+1 + rows_) - rows_ - viewport_scroll_ = scrollback_count_+1 - viewport_scroll_

So the viewport advances by 1 row (it now starts one row later in logical space, meaning it shifted down/"newer"). The user effectively sees content shift up by one row, following the new output. That's NOT what we want when scrolled back.

To keep the viewport on the SAME rows, we need to increase viewport_scroll_ by 1:
After: viewport_start = (scrollback_count_+1 + rows_) - rows_ - (viewport_scroll_+1) = scrollback_count_ - viewport_scroll_

Same viewport_start. Good. So:

```cpp
if (viewport_scroll_ > 0) {
  viewport_scroll_ = std::min(viewport_scroll_ + 1, scrollback_count_);
}
```

This is correct. When following output (viewport_scroll_ == 0), we stay at 0 (following).

---

## Step 5: Add `scroll_viewport()` method

**File:** `src/terminal/grid.hpp` and `src/terminal/grid.cpp`

```cpp
auto terminal_grid::scroll_viewport(int32_t delta) -> uint32_t {
  if (delta > 0) {
    // Scroll back (up): increase viewport_scroll_, clamped to scrollback_count_.
    uint32_t const increment = static_cast<uint32_t>(delta);
    viewport_scroll_ = std::min(viewport_scroll_ + increment, scrollback_count_);
  } else if (delta < 0) {
    // Scroll forward (down): decrease viewport_scroll_, clamped to 0.
    uint32_t const decrement = static_cast<uint32_t>(-delta);
    viewport_scroll_ = (viewport_scroll_ > decrement) ? viewport_scroll_ - decrement : 0;
  }
  return viewport_scroll_;
}
```

---

## Step 6: Rewrite `render_cells()` to render only the viewport

**File:** `src/terminal/grid.cpp`

Currently `render_cells()` copies all visible cells. With scrollback, it must copy only the rows that are currently in the viewport.

```cpp
auto terminal_grid::render_cells() -> std::span<const platform::render_cell> {
  size_t const n = static_cast<size_t>(cols_) * rows_;
  render_cache_.resize(n);

  // Determine which logical rows to render.
  // total_logical_rows = scrollback_count_ + rows_
  // viewport_start = total_logical_rows - rows_ - viewport_scroll_
  //                = scrollback_count_ + rows_ - rows_ - viewport_scroll_
  //                = scrollback_count_ - viewport_scroll_
  uint32_t const viewport_start = scrollback_count_ - viewport_scroll_;

  for (uint32_t r = 0; r < rows_; ++r) {
    uint32_t const logical_row = viewport_start + r;
    uint32_t const phys = physical_index(logical_row);
    size_t const src_offset = static_cast<size_t>(phys) * cols_;
    size_t const dst_offset = static_cast<size_t>(r) * cols_;

    for (uint32_t c = 0; c < cols_; ++c) {
      auto const& src = cells_[src_offset + c];
      auto& dst = render_cache_[dst_offset + c];
      dst.codepoint = src.codepoint;

      if (src.fg.flags & 1) {
        dst.fg = {k_default_fg_color.r, k_default_fg_color.g, k_default_fg_color.b};
      } else {
        dst.fg = {src.fg.r, src.fg.g, src.fg.b};
      }

      if (src.bg.flags & 1) {
        dst.bg = {k_default_bg_color.r, k_default_bg_color.g, k_default_bg_color.b};
      } else {
        dst.bg = {src.bg.r, src.bg.g, src.bg.b};
      }
    }
  }

  return render_cache_;
}
```

---

## Step 7: Suppress cursor rendering when scrolled back

**File:** `src/app.cpp`

When the viewport is not following output (`grid_.is_following_output() == false`), we should not render the cursor (it's off-screen). Pass an out-of-bounds cursor position to suppress:

```cpp
// Render the grid
auto const cells = grid_.render_cells();
if (!cells.empty()) {
  platform::size2d const dims{grid_.cols(), grid_.rows()};
  platform::point2d cursor{
    grid_.is_following_output() ? grid_.cursor_row() : grid_.rows(),  // out of bounds = no cursor
    grid_.is_following_output() ? grid_.cursor_col() : grid_.cols()
  };
  if (auto draw_result = renderer_.draw_grid(device_, rtv_, cells, dims, cursor);
      !draw_result) {
    util::log_error(draw_result.error(), "draw grid");
    exit_code = 1;
    break;
  }
}
```

---

## Step 8: Handle Ctrl+Shift+Up / Ctrl+Shift+Down in the key callback

**File:** `src/app.cpp`

In `Application::run()`, intercept Ctrl+Shift+Up / Ctrl+Shift+Down in the `set_key_callback` lambda before passing to the shell:

```cpp
platform::set_key_callback(window_,
  [this](platform::vk_code vk, bool ctrl, bool shift, bool alt) {
    // ── Scrollback navigation ──────────────────────────────────────
    if (ctrl && shift && !alt) {
      if (vk == platform::vk_code::arrow_up) {
        grid_.scroll_viewport(1);   // scroll back one row
        return;
      }
      if (vk == platform::vk_code::arrow_down) {
        grid_.scroll_viewport(-1);  // scroll forward one row
        return;
      }
    }

    // ── Normal shell input ─────────────────────────────────────────
    if (!shell_ || !platform::is_shell_running(*shell_)) return;
    if (shell_input_failed_) return;

    std::string bytes = input_.on_keydown(vk, ctrl, shift, alt);
    if (!bytes.empty()) {
      if (auto res = platform::write_shell_input(*shell_, bytes); !res) {
        util::log_error(res.error(), "write shell input");
        shell_input_failed_ = true;
      }
    }
  });
```

Note: We also need to add `platform::vk_code::page_up` / `page_down` for page-at-a-time scrolling. Let's add Ctrl+Shift+PageUp / Ctrl+Shift+PageDown to scroll by `rows_ - 1` (one full page):

```cpp
if (ctrl && shift && !alt) {
  if (vk == platform::vk_code::arrow_up) {
    grid_.scroll_viewport(1);
    return;
  }
  if (vk == platform::vk_code::arrow_down) {
    grid_.scroll_viewport(-1);
    return;
  }
  if (vk == platform::vk_code::page_up) {
    grid_.scroll_viewport(static_cast<int32_t>(grid_.rows() - 1));
    return;
  }
  if (vk == platform::vk_code::page_down) {
    grid_.scroll_viewport(-static_cast<int32_t>(grid_.rows() - 1));
    return;
  }
}
```

---

## Step 9: Update `erase_display()` for mode 3 (clear scrollback)

**File:** `src/terminal/grid.cpp`

Mode 3 of ED (CSI 3 J) means "erase entire display and clear scrollback." Currently it's treated identically to mode 2. Update `erase_display`:

```cpp
void terminal_grid::erase_display(uint32_t mode) {
  // ...
  case 2: // Erase entire display.
    erase_cell_range(0, total - 1);  // Note: this erases visible grid only
    break;
  case 3: // Erase entire display + scrollback.
    // Clear all cells (both scrollback and visible grid).
    cells_.assign(cells_.size(), grid_cell{});
    scrollback_count_ = 0;
    scrollback_head_ = 0;
    viewport_scroll_ = 0;
    break;
  // ...
}
```

Wait — `erase_cell_range` currently uses `cells_` size which includes the full circular buffer. For mode 2 (erase entire display), we only want to erase the visible portion. Let me reconsider.

Currently `erase_cell_range(total - 1, ...)` uses `cells_.size()` which is `cols_ * rows_`. After our change, `cells_.size()` is `cols_ * (rows_ + k_scrollback_max)`. So we need separate logic:

- **Mode 0**: Erase from cursor to end of visible display.
- **Mode 1**: Erase from beginning of visible display to cursor.
- **Mode 2**: Erase entire visible display.
- **Mode 3**: Erase entire visible display AND clear scrollback.

For modes 0, 1, 2, we need to compute indices relative to the visible portion only (logical rows `[scrollback_count_, scrollback_count_+rows_)`).

Let me rewrite `erase_display` more carefully:

```cpp
void terminal_grid::erase_display(uint32_t mode) {
  if (cols_ == 0 || rows_ == 0) return;

  // Helper: erase a single cell in the visible grid.
  auto erase_visible_cell = [this](uint32_t vis_row, uint32_t col) {
    uint32_t const logical = scrollback_count_ + vis_row;
    uint32_t const phys = physical_index(logical);
    cells_[static_cast<size_t>(phys) * cols_ + col] = grid_cell{};
  };

  // Helper: erase a range of cells in the visible grid.
  // vis_start_row, vis_end_row are 0-based visible row indices.
  auto erase_visible_range = [this](uint32_t vis_start_row, uint32_t vis_start_col,
                                      uint32_t vis_end_row, uint32_t vis_end_col) {
    for (uint32_t r = vis_start_row; r <= vis_end_row; ++r) {
      uint32_t const logical = scrollback_count_ + r;
      uint32_t const phys = physical_index(logical);
      size_t const base = static_cast<size_t>(phys) * cols_;
      uint32_t const start_c = (r == vis_start_row) ? vis_start_col : 0;
      uint32_t const end_c = (r == vis_end_row) ? vis_end_col : cols_ - 1;
      for (uint32_t c = start_c; c <= end_c; ++c) {
        cells_[base + c] = grid_cell{};
      }
    }
  };

  uint32_t const last_vis_row = rows_ - 1;
  uint32_t const last_vis_col = cols_ - 1;

  switch (mode) {
  case 0: // Erase from cursor to end of display.
    erase_visible_range(cursor_row_, cursor_col_, last_vis_row, last_vis_col);
    break;
  case 1: // Erase from beginning of display to cursor.
    erase_visible_range(0, 0, cursor_row_, cursor_col_);
    break;
  case 2: // Erase entire visible display.
    erase_visible_range(0, 0, last_vis_row, last_vis_col);
    break;
  case 3: // Erase entire visible display + scrollback.
    cells_.assign(cells_.size(), grid_cell{});
    scrollback_count_ = 0;
    scrollback_head_ = 0;
    viewport_scroll_ = 0;
    break;
  default:
    erase_visible_range(cursor_row_, cursor_col_, last_vis_row, last_vis_col);
    break;
  }
}
```

This is cleaner and handles the circular buffer correctly.

Similarly, `erase_line` needs updating since line indices are now relative to the visible grid:

```cpp
void terminal_grid::erase_line(uint32_t mode) {
  if (cols_ == 0 || rows_ == 0) return;

  uint32_t const logical = scrollback_count_ + cursor_row_;  // cursor_row_ is visible-relative
  uint32_t const phys = physical_index(logical);
  size_t const base = static_cast<size_t>(phys) * cols_;

  switch (mode) {
  case 0: // Erase from cursor to end of line.
    for (uint32_t c = cursor_col_; c < cols_; ++c) {
      cells_[base + c] = grid_cell{};
    }
    break;
  case 1: // Erase from beginning of line to cursor.
    for (uint32_t c = 0; c <= cursor_col_ && c < cols_; ++c) {
      cells_[base + c] = grid_cell{};
    }
    break;
  case 2: // Erase entire line.
    for (uint32_t c = 0; c < cols_; ++c) {
      cells_[base + c] = grid_cell{};
    }
    break;
  default:
    for (uint32_t c = cursor_col_; c < cols_; ++c) {
      cells_[base + c] = grid_cell{};
    }
    break;
  }
}
```

---

## Step 10: Update all grid access paths to use logical indexing

**File:** `src/terminal/grid.cpp`

Several methods access `cells_` directly with flat indexing assuming visible-only storage. They must all be updated for the circular buffer:

### `write_char`:
```cpp
void terminal_grid::write_char(char32_t cp) {
  if (cursor_col_ < cols_) {
    uint32_t const logical = scrollback_count_ + cursor_row_;
    uint32_t const phys = physical_index(logical);
    auto& cell = cells_[static_cast<size_t>(phys) * cols_ + cursor_col_];
    cell.codepoint = cp;
    cell.fg = current_fg_;
    cell.bg = current_bg_;
  }
  // ... cursor advance + wrap + scroll logic unchanged
}
```

### `cell()` accessor:
```cpp
auto terminal_grid::cell(uint32_t row, uint32_t col) const -> grid_cell const& {
  assert(row < rows_ && col < cols_);
  uint32_t const logical = scrollback_count_ + row;
  uint32_t const phys = physical_index(logical);
  return cells_[static_cast<size_t>(phys) * cols_ + col];
}
```

### `cells()` accessor:
This returns the raw `cells_` span. Since `cells_` now contains the circular buffer (scrollback + visible), this can't be a simple `return cells_` anymore. It's only used in tests. We'll need to update the tests or deprecate this method.

For now, leave it but update `grid_test.cpp` later.

---

## Step 11: Update `resize()` to reflow scrollback

**File:** `src/terminal/grid.cpp`

When `resize(new_cols, new_rows)` is called and `new_cols != cols_`, we must:

1. Extract all existing logical rows (scrollback + visible) from the old circular buffer.
2. For each extracted row, reflow it to `new_cols` (row-by-row wrapping based on character count).
3. Build a new circular buffer with capacity `new_rows + k_scrollback_max`.
4. Place the reflowed rows into it.
5. The visible portion is the last `new_rows` rows of the reflowed content (or fewer if there isn't enough content).

When `new_cols == cols_` (only row count changed), we just need to adjust the capacity and possibly re-visible the rows. Since the visible portion is always the last `rows_` rows of the logical buffer, changing `rows_` alone is straightforward — we just change `rows_` and `total_capacity_rows_` and reallocate if needed.

```cpp
void terminal_grid::resize(uint32_t new_cols, uint32_t new_rows) {
  if (new_cols == cols_ && new_rows == rows_) return;

  uint32_t const new_capacity = new_rows + k_scrollback_max;

  if (new_cols == cols_) {
    // Only row count changed — just reallocate.
    std::vector<grid_cell> new_cells(static_cast<size_t>(new_cols) * new_capacity, grid_cell{});
    
    // Copy existing logical rows (scrollback + min(rows_, new_rows) visible rows).
    uint32_t const keep_logical = scrollback_count_ + std::min(rows_, new_rows);
    for (uint32_t log = 0; log < keep_logical; ++log) {
      uint32_t const old_phys = physical_index(log);
      // Physical index in new buffer = log % new_capacity (straight linear since new buffer is empty)
      uint32_t const new_phys = log % new_capacity;
      size_t const old_offset = static_cast<size_t>(old_phys) * cols_;
      size_t const new_offset = static_cast<size_t>(new_phys) * new_cols;
      std::copy_n(cells_.data() + old_offset, cols_, new_cells.data() + new_offset);
    }

    // If new_rows > rows_, extra blank rows are already blank in new_cells.
    // If new_rows < rows_, we lose the bottom visible rows (they're not in keep_logical).
    
    cells_ = std::move(new_cells);
    scrollback_head_ = 0;
    
    // Adjust scrollback_count_ if we lost visible rows:
    if (new_rows < rows_) {
      // We lost rows_ - new_rows visible rows. They were the bottommost.
      // They simply don't exist anymore.
      // scrollback_count_ is unchanged (those rows were visible, not scrollback).
    }
    
    rows_ = new_rows;
    total_capacity_rows_ = new_capacity;
    
    // Clamp cursor.
    if (cursor_row_ >= rows_) cursor_row_ = rows_ > 0 ? rows_ - 1 : 0;
  } else {
    // Column width changed — full reflow.
    std::vector<grid_cell> new_cells(static_cast<size_t>(new_cols) * new_capacity, grid_cell{});
    
    // Extract all logical rows from old buffer.
    uint32_t const total_logical = scrollback_count_ + rows_;
    uint32_t new_logical_idx = 0;  // next write position in new buffer
    
    for (uint32_t log_old = 0; log_old < total_logical; ++log_old) {
      uint32_t const old_phys = physical_index(log_old);
      size_t const old_offset = static_cast<size_t>(old_phys) * cols_;
      
      // Reflow this old row into new_cols-width chunks.
      // Each old row is cols_ cells. After reflow, it may produce
      // 1 or more new rows (if cols_ > new_cols, it wraps; if cols_ < new_cols, it pads).
      for (uint32_t c = 0; c < cols_; c += new_cols) {
        uint32_t const copy_count = std::min(new_cols, cols_ - c);
        uint32_t const new_phys = new_logical_idx % new_capacity;
        size_t const new_offset = static_cast<size_t>(new_phys) * new_cols;
        std::copy_n(cells_.data() + old_offset + c, copy_count, new_cells.data() + new_offset);
        // Remaining cells in the new row are already blank (default-constructed).
        new_logical_idx++;
      }
    }
    
    // new_logical_idx is the total number of rows after reflow.
    // The visible portion is the last new_rows rows.
    uint32_t new_scrollback_count = 0;
    uint32_t new_scrollback_head = 0;
    
    if (new_logical_idx <= new_rows) {
      // All content fits in visible area. No scrollback.
      new_scrollback_count = 0;
    } else {
      new_scrollback_count = new_logical_idx - new_rows;
      if (new_scrollback_count > k_scrollback_max) {
        // Trim oldest scrollback.
        uint32_t const trim = new_scrollback_count - k_scrollback_max;
        new_scrollback_head = trim % new_capacity;
        new_scrollback_count = k_scrollback_max;
      }
    }
    
    cells_ = std::move(new_cells);
    scrollback_head_ = new_scrollback_head;
    scrollback_count_ = new_scrollback_count;
    cols_ = new_cols;
    rows_ = new_rows;
    total_capacity_rows_ = new_capacity;
    
    // Clamp cursor and viewport.
    if (cursor_col_ >= cols_) cursor_col_ = cols_ > 0 ? cols_ - 1 : 0;
    if (cursor_row_ >= rows_) cursor_row_ = rows_ > 0 ? rows_ - 1 : 0;
    if (viewport_scroll_ > scrollback_count_) viewport_scroll_ = scrollback_count_;
    
    saved_cursor_row_ = 0;
    saved_cursor_col_ = 0;
  }
}
```

---

## Step 12: Update `apply()` to handle cursor_row_ as visible-relative

**File:** `src/terminal/grid.cpp`

When `move_cursor` sets `cursor_row_`, it must stay within `rows_` (visible rows), not the total capacity. The current code already does this with `std::min(a.row, rows_ > 0 ? rows_ - 1 : 0)`. No change needed — `cursor_row_` is already visible-relative.

---

## Step 13: Update `save_cursor` / `restore_cursor`

**File:** `src/terminal/grid.cpp`

The saved cursor is relative to the visible grid. No changes needed — `saved_cursor_row_` and `saved_cursor_col_` remain as visible coordinates.

However, after a resize, we currently reset saved cursor to (0,0). That's fine — we do the same.

---

## Step 14: Update `cell()` and `cells()` for tests  

**File:** `src/terminal/grid.cpp`

`cell(row, col)` is used in tests (`grid_test.cpp`). It should continue returning visible-grid-relative cells. The implementation already accesses via `scrollback_count_ + row` — updated in Step 10.

`cells()` returns `std::span<const grid_cell>` of the internal buffer. Tests use it to check the raw buffer. Since the buffer now includes scrollback, tests may break. We should:
- Keep `cells()` returning the full `cells_` span (scrollback + visible) for backward compatibility  
- OR deprecate it and update tests to use `cell(row, col)` or a new `visible_cells()` method.

The tests only check visible content. Let's add a `visible_cells()` method that returns only the visible portion (copied into a temporary, or return a view). Actually, for simplicity, keep `cells()` returning the full buffer and update tests. But many tests do `REQUIRE(grid.cells().size() == cols * rows)`, which will now fail since `cells_.size()` is `cols * (rows + 10000)`.

Best approach: change `cells()` to return only the visible portion's span directly (not a copy). Since the visible portion is at logical indices `[scrollback_count_, scrollback_count_+rows_)`, it's NOT contiguous in memory due to the circular buffer. So we can't return a direct span.

Options:
1. Remove `cells()` and update all tests to use `cell(row, col)`.
2. Have `cells()` return a newly constructed contiguous vector (expensive, test-only).

Going with option 1 is cleanest. Update `grid_test.cpp` to use `cell()` and `cols()`/`rows()`.

Let me check what tests exist.

We'll handle test updates in the plan.

---

## Step 15: Test updates

**File:** `tests/grid_test.cpp`

Tests that currently use `cells()` must be updated. For example:
```cpp
REQUIRE(grid.cells().size() == 80 * 25);
```
becomes:
```cpp
REQUIRE(grid.rows() == 25);
REQUIRE(grid.cols() == 80);
```

And any direct cell access via `cells()[row * 80 + col]` becomes `grid.cell(row, col)`.

---

## Step 16: Observations about newline() and cursor_column

**File:** `src/terminal/grid.cpp`

The `newline()` method calls `scroll_up()` which now preserves the top row into scrollback. This is correct. No other changes needed.

`carriage_return()` just sets `cursor_col_ = 0` — no changes.

---

## Step 17: Edge Cases

| Edge case | Behavior |
|-----------|----------|
| `scroll_up()` called while `viewport_scroll_ > 0` | Viewport auto-adjusts (see Step 4, point 3) so same rows remain visible. |
| User scrolls to oldest scrollback, then new output arrives | Scrollback count increases, `viewport_scroll_` is incremented to compensate, oldest row remains visible in viewport. |
| User is scrolled back, then types | Input goes to shell normally. New output appears "below" their view without moving it. |
| `clear` (CSI 2 J) while scrolled back | Only erases visible display (mode 2). Scrollback is preserved. The viewport shows blank rows. |
| `clear` with CSI 3 J | Erases both visible display and scrollback. `viewport_scroll_` resets to 0. |
| Window resize while scrolled back | Reflow recalculates everything. `viewport_scroll_` is clamped to new `scrollback_count_`. User may lose their scroll position since reflow changes row counts. |
| Ctrl+Shift+Up at the very top (viewport_scroll_ == scrollback_count_) | No-op. Already at the oldest content. |
| Ctrl+Shift+Down at the very bottom (viewport_scroll_ == 0) | No-op. Already following output. |
| Starting with empty scrollback | `scrollback_count_` = 0. Ctrl+Shift+Up does nothing. Viewport shows only visible grid. |

---

## Files Changed

| File | Changes |
|------|---------|
| `src/terminal/grid.hpp` | Add scrollback members, `physical_index()`, `scroll_viewport()`, `is_following_output()`. Remove `cells()` return type change. |
| `src/terminal/grid.cpp` | Rewrite constructor, `scroll_up()`, `render_cells()`, `erase_display()`, `erase_line()`, `write_char()`, `cell()`, `resize()`. Add `physical_index()`, `scroll_viewport()`. |
| `src/app.cpp` | Intercept Ctrl+Shift+Up/Down/PgUp/PgDown in key callback. Suppress cursor when scrolled back. |
| `tests/grid_test.cpp` | Update tests that use `cells()` to use `cell()` or row/col access methods instead. |

---

## Build & Verification

1. **Build**: `cmake --build build/debug`
2. **Run tests**: `build/debug/betty_tests.exe`
3. **Manual test**:
   - Launch `betty.exe`
   - Run `ls -la` several times or `Get-Content somefile.txt` to fill the screen past the bottom
   - Press **Ctrl+Shift+Up** — should scroll back through history
   - Press **Ctrl+Shift+Down** — should scroll forward, returning to the prompt
   - While scrolled back, run a command. Output should appear without moving the viewport.
   - Press **Ctrl+Shift+Down** to return to bottom — should see the new output
   - Run `clear` — visible display clears, scrollback preserved (verify by scrolling up)
   - Resize window — scrollback should reflow, still accessible
4. **Performance**: With 10,000 lines of scrollback, `render_cells()` should remain fast (it only processes `rows_` rows, not the entire scrollback).
