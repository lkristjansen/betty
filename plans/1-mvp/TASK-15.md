# TASK 15 — Unicode + Wide Characters

**Goal:** Emoji, CJK characters, and other Unicode text render without crashing.
Wide characters (Chinese, Japanese, Korean) occupy two cells. Combining
characters (accents, diacritics) display correctly over their base character.

---

## Design Decisions *(confirmed)*

| Decision | Choice |
|---|---|
| Non-ASCII glyph rendering | Dynamic glyph atlas with LRU caching, rasterized via `IDWriteGlyphRunAnalysis` |
| Combining characters | NFC pre-composition using Windows `NormalizeString` API |
| Wide char at last column | Wrap to next line (standard xterm/Windows Terminal behavior) |

---

## Sequence Overview

```
Step 1 ──► Step 2 ──► Step 3 ──► Step 4 ──► Step 5 ──► Step 6
(vt_parser   (wcwidth)  (grid      (combining  (dynamic    (tests)
 UTF-8)                 changes)   chars)      atlas)
```

Steps 1–4 are strongly interdependent (all touch the grid write path), but
each is described separately for clarity.

---

## Step 1 — UTF-8 Decoder in VT Parser

**File:** `src/terminal/vt_parser.hpp`
**File:** `src/terminal/vt_parser.cpp`

### 1a. The problem

Currently, in the GROUND state, every byte `>= 0x20` is treated as a complete
`char32_t` codepoint:

```cpp
case state::ground:
    // ...
    default:
      if (byte >= 0x20) {
        return {action{
          .type = action_type::write_char,
          .codepoint = static_cast<char32_t>(byte)
        }};
      }
```

ConPTY output is UTF-8. Multi-byte sequences (e.g. `0xE4 0xB8 0xAD` for U+4E2D 中)
must be accumulated and decoded before emitting a `write_char` action.

### 1b. UTF-8 accumulation state

Add a private helper struct and member to `vt_parser`:

```cpp
// vt_parser.hpp — inside class vt_parser, private section:
struct {
  char32_t codepoint = 0;
  uint8_t  remaining = 0;  // how many continuation bytes still expected
} utf8_;
```

### 1c. Decoding logic

In the GROUND handler, a new branch for bytes `>= 0x80`:

| First byte range | Sequence length | Codepoint initial bits |
|---|---|---|
| `0xC0–0xDF` | 2 bytes | `byte & 0x1F` |
| `0xE0–0xEF` | 3 bytes | `byte & 0x0F` |
| `0xF0–0xF4` | 4 bytes | `byte & 0x07` |
| `0x80–0xBF` | continuation byte out of context → ignore |
| `0xF5–0xFF` | invalid (beyond Unicode range) → emit U+FFFD or ignore |

Continuation bytes (`0x80–0xBF`): shift codepoint left by 6, OR in `byte & 0x3F`.

On completion: emit `action{ .type = write_char, .codepoint = utf8_.codepoint }`.

Overlong sequences (e.g. `0xC0 0x80` for NUL) are rejected: after decoding,
if the resulting codepoint is less than the minimum value for that length,
treat as invalid.

Invalid sequences → emit U+FFFD (REPLACEMENT CHARACTER) as a `write_char`.

### 1d. Interaction with control codes

UTF-8 continuation bytes (0x80–0xBF) arriving outside a multi-byte sequence
should not be treated as printable characters. They are silently dropped.

If ESC (0x1B), CR (0x0D), LF (0x0A) arrive mid-sequence, the sequence is
aborted, the accumulated bytes are discarded, and the control code is processed
normally.

### 1e. Pseudocode for the GROUND state handler

```
case state::ground:
  if byte == '\r': return { carriage_return }
  if byte == '\n': return { newline }
  if byte == 0x1B: state_ = escape; return {}

  if byte < 0x80:
    if byte >= 0x20: return { write_char, codepoint = byte }
    else: /* C0 control — ignore */ return {}

  // byte >= 0x80 — UTF-8 multi-byte
  if byte >= 0xC0 && byte <= 0xF4:
    // Determine length
    if byte <= 0xDF:      utf8_.remaining = 1; utf8_.codepoint = byte & 0x1F
    else if byte <= 0xEF: utf8_.remaining = 2; utf8_.codepoint = byte & 0x0F
    else:                 utf8_.remaining = 3; utf8_.codepoint = byte & 0x07
    state_ = state::utf8_accum;  // new state
    return {}

  // 0x80–0xBF without a start byte — stray continuation, ignore
  return {}

case state::utf8_accum:
  if (byte & 0xC0) != 0x80: /* not a continuation byte — abort sequence */
    state_ = ground; return parse(byte);  // re-process this byte in ground
  utf8_.codepoint = (utf8_.codepoint << 6) | (byte & 0x3F)
  utf8_.remaining--
  if utf8_.remaining == 0:
    state_ = ground
    // Validate (reject surrogates U+D800–U+DFFF, overlong, > U+10FFFF)
    if invalid: return { write_char, codepoint = 0xFFFD }
    return { write_char, codepoint = utf8_.codepoint }
  return {}
```

**Add new state:** `utf8_accum` to the `enum class state` in `vt_parser.hpp`.

---

## Step 2 — `wcwidth()` Implementation

**File:** `src/terminal/wcwidth.hpp` *(new)*
**File:** `src/terminal/wcwidth.cpp` *(new)*

### 2a. Function signature

```cpp
// Returns the display width of a Unicode codepoint in a monospace terminal:
//   2 — wide (East Asian Wide or Fullwidth)
//   1 — normal
//   0 — zero-width (combining marks, control chars)
//  -1 — non-printable control character (U+0000–U+001F, U+007F–U+009F)
[[nodiscard]] int wcwidth(char32_t cp) noexcept;
```

### 2b. Implementation strategy

A simple range-based lookup using a static table of `{ start, end }` ranges
for wide characters. This avoids pulling in large Unicode data tables.

**Control characters (return –1):**
- U+0000–U+001F, U+007F–U+009F

**Zero-width characters (return 0):**
- Combining Diacritical Marks: U+0300–U+036F
- Combining Diacritical Marks Extended: U+1AB0–U+1AFF
- Combining Diacritical Marks Supplement: U+1DC0–U+1DFF
- Combining Diacritical Marks for Symbols: U+20D0–U+20FF
- Combining Half Marks: U+FE20–U+FE2F
- Variation Selectors: U+FE00–U+FE0F
- Zero Width Space/Joiner/Non-Joiner: U+200B–U+200F, U+2028–U+2029, U+202A–U+202E, U+2060–U+2064, U+2066–U+206F
- Hangul Jungseong/Jongseong (medial/final): U+1160–U+11FF
- Hangul Compatibility Jamo (zero-width): U+3131–U+318E → actually these are width 1 in most terminals. Let me reconsider...

Actually, the safest approach for zero-width:
- General Category Mn, Mc, Me (nonspacing marks, spacing combining marks, enclosing marks)

But we don't have ICU. A simpler proxy:
- Any codepoint in the combining diacritical marks ranges listed above
- U+200B (zero width space)
- U+FEFF (BOM / zero width no-break space)

For the MVP, a reasonably complete list of combining ranges covers 99% of real-world use.

**Wide characters (return 2):** Major blocks:

| Start | End | Block |
|---|---|---|
| 0x1100 | 0x115F | Hangul Jamo |
| 0x2329 | 0x232A | Misc Technical (wide angle brackets) |
| 0x2E80 | 0x303E | CJK Radicals, Kangxi, Ideographic Description, CJK Symbols |
| 0x3040 | 0x33BF | Hiragana, Katakana, Bopomofo, Hangul Compat Jamo, Kanbun, CJK Strokes, Katakana Extensions, Enclosed CJK |
| 0x3400 | 0x4DBF | CJK Unified Ideographs Extension A |
| 0x4E00 | 0xA4CF | CJK Unified Ideographs, Yi Syllables, Yi Radicals |
| 0xA960 | 0xA97C | Hangul Jamo Extended-A |
| 0xAC00 | 0xD7A3 | Hangul Syllables |
| 0xF900 | 0xFAFF | CJK Compatibility Ideographs |
| 0xFE10 | 0xFE19 | Vertical forms |
| 0xFE30 | 0xFE6F | CJK Compatibility Forms, Small Form Variants |
| 0xFF01 | 0xFF60 | Fullwidth Forms |
| 0xFFE0 | 0xFFE6 | Fullwidth Signs |
| 0x1F004 | 0x1F004 | Mahjong tile (single char) |
| 0x1F0CF | 0x1F0CF | Playing card (single char) |
| 0x1F18E | 0x1F18E | Negative squared AB (single char) |
| 0x1F191 | 0x1F19A | Squared symbols |
| 0x1F200 | 0x1F251 | Enclosed Ideographic Supplement |
| 0x1F300 | 0x1F64F | Misc Symbols & Pictographs, Emoticons |
| 0x1F680 | 0x1F6FF | Transport & Map Symbols |
| 0x1F900 | 0x1F9FF | Supplemental Symbols & Pictographs |
| 0x1FA00 | 0x1FA6F | Chess Symbols |
| 0x1FA70 | 0x1FAFF | Symbols & Pictographs Extended-A |
| 0x20000 | 0x2FFFD | CJK Unified Ideographs Extension B (and C–F) |
| 0x30000 | 0x3FFFD | CJK Unified Ideographs Extension G & H |

Default: return 1.

### 2c. Range lookup helper

Use a linear scan over a `constexpr std::array` of `{char32_t lo, char32_t hi}`
pairs. There are ~30 ranges — linear scan is fast enough (cache-friendly,
branch-predictable for common ASCII paths).

The function does:
1. Check control chars → return –1 (for C0, DEL, C1)
2. Check zero-width ranges → return 0
3. Check wide ranges → return 2
4. Default → return 1

---

## Step 3 — Grid Changes for Wide Characters

**File:** `src/terminal/vt_parser.hpp` — add `cell_attr` flags
**File:** `src/terminal/grid.hpp`
**File:** `src/terminal/grid.cpp`

### 3a. New cell_attr flags

In `enum class cell_attr` (vt_parser.hpp), add:

```cpp
wide      = 1 << 6,  // first cell of a 2-cell-wide character
wide_tail = 1 << 7,  // continuation cell (empty, occupied by right half of wide char)
```

These flags use the remaining bits in the `uint8_t` attribute mask (bits 6–7
were previously unused).

The `operator|`, `operator&`, `operator~` overloads already support all 8 bits.

### 3b. Modified `write_char(char32_t cp)`

Replace the current implementation with:

```cpp
void terminal_grid::write_char(char32_t cp) {
    int const w = wcwidth(cp);

    if (w == 0) {
        // Zero-width combining character — try NFC pre-composition
        // (handled in Step 4; for now, write as width-1 fallback)
        write_combining_char(cp);
        return;
    }

    if (w < 0) {
        // Control character — silently ignore (shouldn't reach here from parser)
        return;
    }

    if (w == 2) {
        // Wide character — need 2 cells.
        // If at last column, wrap to next line first.
        if (cursor_col_ >= cols_ - 1) {
            newline();  // wrap (respects scroll region)
        }

        // Write the glyph to current cell.
        if (cursor_col_ < cols_ && rows_ > 0) {
            write_cell(cursor_col_, cp, current_fg_, current_bg_,
                       current_attr_ | cell_attr::wide);
        }

        cursor_col_++;

        // Mark the continuation cell.
        if (cursor_col_ < cols_ && rows_ > 0) {
            write_cell(cursor_col_, U' ', current_fg_, current_bg_,
                       current_attr_ | cell_attr::wide_tail);
        }

        cursor_col_++;
    } else {
        // Normal width-1 character (existing behavior).
        if (cursor_col_ < cols_ && rows_ > 0) {
            write_cell(cursor_col_, cp, current_fg_, current_bg_, current_attr_);
        }
        cursor_col_++;
    }

    // Auto-wrap: if cursor is past the last column, move to next row.
    if (cursor_col_ >= cols_) {
        cursor_col_ = 0;
        if (cursor_row_ >= scroll_bottom_) {
            scroll_up();
        } else {
            cursor_row_++;
        }
    }
}
```

### 3c. Helper: `write_cell`

Extract the cell-writing logic into a private helper:

```cpp
void terminal_grid::write_cell(uint32_t col, char32_t cp,
                                rgb_color fg, rgb_color bg, cell_attr attr) {
    uint32_t const logical = scrollback_count_ + cursor_row_;
    uint32_t const phys = physical_index(logical);
    auto& cell = cells_[static_cast<size_t>(phys) * cols_ + col];
    cell.codepoint = cp;
    cell.fg = fg;
    cell.bg = bg;
    cell.attr = attr;
}
```

### 3d. Wide char at edge of row (wrap behavior)

If the cursor is at column `N-1` (last column) and a wide character arrives:
1. `wcwidth() == 2`
2. `cursor_col_ >= cols_ - 1` → call `newline()` which sets `cursor_col_ = 0`
   and increments `cursor_row_` (or scrolls)
3. Now write the wide char at column 0–1 on the new row

If the cursor is at column `N-2` (second to last), the wide char fits:
1. First cell at N-2
2. Continuation cell at N-1
3. After writing both, `cursor_col_` becomes N → auto-wrap to next row

### 3e. Interaction with character operations (ICH, DCH, ECH)

For MVP (Task 15), character and line operations treat wide continuation cells
as independent cells. Inserting/deleting characters may break wide characters
in half — this is acceptable for now and can be hardened in Task 18.

**Minimal protection:** When `insert_chars` or `delete_chars` shifts cells,
the shifted cells retain their `wide`/`wide_tail` flags. If a `wide_tail` cell
ends up at column 0 (no preceding cell to pair with), it will render as a blank
space (the renderer skips `wide_tail` cells without a preceding `wide` cell).

### 3f. Modified `render_cells()`

In `render_cells()`, copy `src.attr` directly to `dst.attr`. The renderer
(`draw_grid`) will use the `k_attr_wide` and `k_attr_wide_tail` bits to adjust
rendering. No changes needed to `render_cells()` itself — the attribute byte
already flows through.

### 3g. Modified `erase_display` / `erase_line`

No changes needed — erased cells become `grid_cell{}` (space, default fg/bg,
no attributes), which automatically clears `wide`/`wide_tail` flags.

### 3h. Modified `scroll_up` / `scroll_page_up` / `scroll_page_down`

No changes needed — these move entire rows of cells including their attributes.

### 3i. Grid resize

No changes needed — the existing `resize()` reflows cells row-by-row.
Wide continuation cells that end up at column 0 after reflow will render as
blank spaces (acceptable for MVP).

---

## Step 4 — Combining Character NFC Pre-Composition

**File:** `src/terminal/grid.hpp`
**File:** `src/terminal/grid.cpp`

### 4a. New private method

```cpp
// Write a zero-width combining character (wcwidth == 0).
// If the previous cell has a valid base character, NFC-compose them.
// Otherwise, write the combining char as width 1 at the current cursor.
void write_combining_char(char32_t cp);
```

### 4b. Implementation

```cpp
#include <winnls.h>  // NormalizeString

void terminal_grid::write_combining_char(char32_t cp) {
    // If at column 0 or previous cell is empty/space, can't compose.
    if (cursor_col_ == 0) {
        // Fallback: write as normal width-1 character.
        if (rows_ > 0) {
            write_cell(cursor_col_, cp, current_fg_, current_bg_, current_attr_);
        }
        cursor_col_++;
        return;
    }

    // Get the previous cell.
    uint32_t const logical = scrollback_count_ + cursor_row_;
    uint32_t const phys = physical_index(logical);
    auto& prev_cell = cells_[static_cast<size_t>(phys) * cols_ + cursor_col_ - 1];

    // Don't compose onto wide_tail cells or empty cells.
    if (has_attr(prev_cell.attr, cell_attr::wide_tail)) {
        // Previous cell is a wide continuation — compose onto the cell before that.
        // For simplicity in MVP: treat as width 1 at current cursor.
        write_cell(cursor_col_, cp, current_fg_, current_bg_, current_attr_);
        cursor_col_++;
        return;
    }

    char32_t const base = prev_cell.codepoint;
    if (base == U' ' || base == 0) {
        // Empty cell — write combining char as width 1.
        write_cell(cursor_col_, cp, current_fg_, current_bg_, current_attr_);
        cursor_col_++;
        return;
    }

    // NFC-compose base + combining char.
    // Build UTF-16 input string (handle supplementary planes via surrogate pairs).
    wchar_t input[4] = {};
    int input_len = 0;

    auto encode_utf16 = [](char32_t cp, wchar_t* out) -> int {
        if (cp <= 0xFFFF) {
            out[0] = static_cast<wchar_t>(cp);
            return 1;
        }
        // Surrogate pair for U+10000–U+10FFFF.
        cp -= 0x10000;
        out[0] = static_cast<wchar_t>(0xD800 | (cp >> 10));
        out[1] = static_cast<wchar_t>(0xDC00 | (cp & 0x3FF));
        return 2;
    };

    input_len = encode_utf16(base, input);
    input_len += encode_utf16(cp, input + input_len);

    wchar_t composed[4] = {};
    int const composed_len = NormalizeString(
        NormalizationC,   // NFC
        input, input_len,
        composed, 4
    );

    if (composed_len > 0 && composed_len <= 2) {
        // Successfully composed. Decode back to char32_t.
        char32_t result = 0;
        if (composed_len == 1) {
            result = composed[0];
        } else {
            // Surrogate pair back to char32_t.
            result = 0x10000
                + ((static_cast<char32_t>(composed[0]) - 0xD800) << 10)
                + (static_cast<char32_t>(composed[1]) - 0xDC00);
        }

        // Update previous cell with the composed codepoint.
        prev_cell.codepoint = result;
        // Cursor does not advance (combining char is zero-width).
    } else {
        // NormalizeString failed or result is multi-codepoint (uncomposable).
        // Fallback: write combining char as width 1 at current cursor.
        write_cell(cursor_col_, cp, current_fg_, current_bg_, current_attr_);
        cursor_col_++;
    }
}
```

**Note:** `NormalizeString` requires `<winnls.h>` and `Normaliz.lib`. The CMake
build already links against system libraries; add `Normaliz` to the linker.

### 4c. Edge cases

| Situation | Behavior |
|---|---|
| Combining char at column 0 | Write as width-1 at current cursor |
| Previous cell is empty (space) | Write as width-1 |
| Previous cell is `wide_tail` | Write as width-1 (don't reach back 2 cells) |
| `NormalizeString` fails (multi-codepoint result) | Write as width-1 |
| Base + combining = single pre-composed char | Replace previous cell; cursor unchanged |
| Iterative composition (e.g., base + 2 combining marks) | Multiple calls each compose with the progressively updated previous cell |

---

## Step 5 — Dynamic Glyph Atlas

**File:** `src/platform/text.hpp`
**File:** `src/platform/text.cpp`

### 5a. Overview

Keep the existing pre-baked 256-glyph atlas for ASCII (fast path). Add a
second dynamic atlas texture for non-ASCII glyphs, populated lazily at render
time using the same `IDWriteGlyphRunAnalysis` rasterization path.

### 5b. Dynamic atlas configuration

```
Slot size:     cell_width + 2×k_glyph_padding  ×  cell_height + 2×k_glyph_padding
Atlas layout:  32 columns × 32 rows = 1024 slots
Texture size:  slot_width×32  ×  slot_height×32 (typically 608×704 with 18px font)
```

At initialization, create the dynamic atlas texture and SRV. Initialize all
slots to transparent.

### 5c. Slot cache data structures

In `glyph_renderer::impl`:

```cpp
// Dynamic glyph slot — same structure as glyph_slot
// (reuse the existing glyph_slot struct).

// Cache structures
mutable std::unordered_map<char32_t, uint32_t> dynamic_index_;  // cp → slot index (0..1023)
mutable std::vector<char32_t>                 dynamic_lru_;     // slot_index → codepoint (for eviction)
mutable uint32_t                              dynamic_next_ = 0; // next free slot
mutable std::vector<bool>                     dynamic_used_;    // 1024 flags

// Dynamic atlas
ComPtr<ID3D11Texture2D>          dynamic_atlas_;
ComPtr<ID3D11ShaderResourceView> dynamic_srv_;
std::array<glyph_slot, 1024>     dynamic_slots_;  // UV data per slot
```

### 5d. Cache population: `ensure_glyph_cached`

```cpp
// Ensure a glyph is present in the dynamic atlas, rasterizing if needed.
// Returns the slot index (0..1023) or SIZE_MAX on failure.
auto ensure_glyph_cached(char32_t cp, ID3D11DeviceContext* ctx) -> uint32_t;
```

Algorithm:
1. Check `dynamic_index_`: if found, return index.
2. Allocate a slot:
   - If `dynamic_next_ < 1024`: use `dynamic_next_`, increment.
   - Else: evict LRU (the slot at the back of `dynamic_lru_`, which is the least recently used). Remove from `dynamic_index_`.
3. Rasterize the glyph into a temporary buffer (one slot large) using the existing `rasterize_glyph()` function.
4. Upload to the dynamic atlas texture using `ctx->UpdateSubresource()` with a `D3D11_BOX` covering just the allocated slot.
5. Compute UV coordinates for the slot (same math as the ASCII atlas).
6. Store in `dynamic_slots_[slot]`, `dynamic_index_[cp] = slot`, `dynamic_lru_[slot] = cp`.
7. Return slot index.

### 5e. LRU management

On each cache hit: move the slot to the front of the LRU list. This can be
done efficiently by storing the LRU as a doubly-linked list or by using a
generation counter.

Simplified approach (sufficient for 1024 slots):
- Instead of a true LRU list, use a `std::array<uint32_t, 1024>` last-access timestamp.
- On eviction, scan for the minimum timestamp (O(N), but N=1024 and eviction is rare).
- Update timestamp on each access.

### 5f. `prepare_unicode_glyphs` — pre-render pass

Add a new public method to `glyph_renderer` (called before `draw_grid`):

```cpp
// Scan cells for non-ASCII codepoints not yet in the cache and rasterize them.
// Must be called once per frame before draw_grid().
void prepare_unicode_glyphs(d3d_device const& device,
                            std::span<const render_cell> cells) const;
```

This method iterates all cells, identifies codepoints > 127 (and not
`wide_tail`), and calls `ensure_glyph_cached` for each. The device context
is available via `device.impl_->context`.

### 5g. Modified `draw_grid` — use dynamic atlas

In the vertex-building loop, when `cp > 127`:

```cpp
if (cp <= 127) {
    // ASCII fast path (unchanged).
    unsigned char glyph = static_cast<unsigned char>(cp);
    uint32_t const slot_idx = glyph + ((cell.attr & k_attr_italic) ? 128u : 0u);
    auto& slot = impl_->glyph_slots[slot_idx];
    // ... emit quad using slot.u0, slot.v0, etc.
} else {
    // Non-ASCII: look up in dynamic atlas.
    auto it = impl_->dynamic_index_.find(cp);
    if (it == impl_->dynamic_index_.end()) {
        // Should not happen — prepare_unicode_glyphs is called first.
        // Fallback: render '?'.
        auto& slot = impl_->glyph_slots['?'];
        // ... emit quad
    } else {
        auto& slot = impl_->dynamic_slots_[it->second];
        // ... emit quad using slot.u0, slot.v0, etc.
        // Update LRU timestamp.
        impl_->dynamic_lru_[it->second] = impl_->dynamic_clock_++;
    }
}
```

### 5h. Wide character rendering

When a cell has `k_attr_wide` (first cell of a wide char pair):

- Render the glyph quad with **2× the normal width**:
  ```cpp
  float x1_wide = x0 + 2.0f * static_cast<float>(impl_->cell_width);
  ```
- After emitting the quad, skip the next column (add `++col` in the loop).
- The `wide_tail` cell (next column) should be skipped entirely (no quads emitted).

Modified loop structure:

```cpp
for (uint32_t col = 0; col < draw_cols; ) {
    // ... get cell ...
    if (cell.attr & k_attr_wide_tail) {
        ++col;
        continue;  // skip — rendered as part of previous wide char
    }

    bool const is_wide = (cell.attr & k_attr_wide) != 0;
    float const glyph_width = is_wide ? 2.0f : 1.0f;
    float const x1 = x0 + glyph_width * static_cast<float>(impl_->cell_width);

    // ... emit background quad (same width) ...
    // ... emit foreground quad (same width) ...

    if (is_wide) {
        col += 2;
    } else {
        ++col;
    }
}
```

### 5i. Wide character + bold/italic/underline

- **Bold:** Apply the 1px horizontal offset to each of the two cells' worth of width.
- **Italic:** Use the italic atlas (slot + 128 offset) for ASCII. For non-ASCII
  wide chars, rasterize using the italic font face (`font_face_italic`) and
  store as a separate cache entry. The italic font face was already loaded
  during initialization (Task 12).
- **Underline/Strikethrough:** Extend the underline quad to span the full 2-cell width.

### 5j. Dynamic atlas texture and SRV creation

In `make_glyph_renderer`, after the ASCII atlas creation, add:

```cpp
// Create dynamic atlas texture.
D3D11_TEXTURE2D_DESC dyn_tex_desc{};
dyn_tex_desc.Width  = k_dynamic_atlas_cols * p->slot_width;
dyn_tex_desc.Height = k_dynamic_atlas_rows * p->slot_height;
// ... same as ASCII atlas texture ...

hr = d3d_dev->CreateTexture2D(&dyn_tex_desc, nullptr,
                               p->dynamic_atlas_.GetAddressOf());
// ... SRV creation ...

// Initialize to transparent.
std::vector<uint8_t> clear_buf(dyn_tex_desc.Width * dyn_tex_desc.Height * 4, 0);
d3d_ctx->UpdateSubresource(p->dynamic_atlas_.Get(), 0, nullptr,
                            clear_buf.data(), dyn_tex_desc.Width * 4, 0);
```

### 5k. Shader changes

The pixel shader currently uses a single texture `glyph_atlas : register(t0)`.
For the dynamic atlas, we have two options:

**Option 1:** Use a second texture register `register(t1)` and switch based on
UV coordinates (e.g., use a flag in the vertex data to select texture).

**Option 2:** Pack the dynamic atlas into the same texture register. Use a
single large texture that contains both the ASCII atlas and the dynamic atlas
side by side (or stacked). This avoids shader changes.

**Recommendation: Option 2** — simpler, no shader changes. Layout the combined
atlas as:
```
[ASCII regular 8 rows] [ASCII italic 8 rows]
[dynamic slots 0–511]  [dynamic slots 512–1023]
```
Or simply place the dynamic atlas at offset `(0, atlas_height)` — just below
the ASCII atlas in the same texture. But the current atlas is a fixed 16×16
grid; the dynamic one is 32×32. Combining them into one texture requires
allocating the texture with enough space.

Actually, the simplest approach for the shader: bind both textures as an
array or use a second texture slot. The pixel shader already has `register(t0)`.
We can add a second texture at `register(t1)` and select based on UV sign:
the background quads have `uv.x < 0` to signal "no texture lookup". We could
use `uv.y < 0` as a flag to sample from the dynamic atlas instead.

But the current convention is already:
- Background quad: `uv.x == -1.0f` → pixel shader detects this and renders solid color
- Foreground quad: normal UV → samples from atlas

We could extend this:
- `uv.x == -2.0f` → sample from dynamic atlas

Or simpler: just use a single combined atlas texture. In `make_glyph_renderer`,
create one texture large enough for both:

```
Combined atlas layout:
  Row 0–7:   ASCII regular  (128 glyphs, 16 cols × 8 rows)
  Row 8–15:  ASCII italic   (128 glyphs)
  Row 16–47: Dynamic slots  (1024 glyphs, 32 cols × 32 rows)

Total rows: 48
Total height: 48 × slot_height
Total width: max(16, 32) × slot_width = 32 × slot_width
```

The ASCII atlas currently uses 16 cols. We adjust to use 32 cols for
compatibility with the dynamic atlas layout. Actually no — that would change
the ASCII slot positions and require re-baking the whole atlas. Better to keep
16-cols for ASCII and place the dynamic atlas below with 32 cols.

The combined texture would be:
```
Width:  max(16 * slot_width, 32 * slot_width) = 32 * slot_width
Height: 16 * slot_height + 32 * slot_height = 48 * slot_height
```

Each slot stores UV coordinates relative to the full texture, so no shader
changes needed — the pixel shader still samples from `register(t0)`.

This is the cleanest approach. Let me finalize the plan with this.

---

## Step 6 — Tests

**File:** `tests/vt_parser_test.cpp`
**File:** `tests/grid_test.cpp`

### 6a. Parser tests — UTF-8 decoding

Use the existing `parse_sequence()` helper. Tests use explicit byte arrays
because source files are ASCII. Raw byte initializers encode UTF-8.

```cpp
// UTF-8: 2-byte sequence for U+00E9 (é)
// 0xC3 0xA9 → é
{
  std::vector<uint8_t> const bytes = { 0xC3, 0xA9 };
  auto actions = run_sequence(bytes);
  REQUIRE(actions.size() == 1);
  CHECK(actions[0].type == action_type::write_char);
  CHECK(actions[0].codepoint == 0x00E9);
}

// UTF-8: 3-byte sequence for U+4E2D (中)
// 0xE4 0xB8 0xAD → 中
{
  std::vector<uint8_t> const bytes = { 0xE4, 0xB8, 0xAD };
  auto actions = run_sequence(bytes);
  REQUIRE(actions.size() == 1);
  CHECK(actions[0].type == action_type::write_char);
  CHECK(actions[0].codepoint == 0x4E2D);
}

// UTF-8: 4-byte sequence for U+1F600 (😀)
// 0xF0 0x9F 0x98 0x80 → 😀
{
  std::vector<uint8_t> const bytes = { 0xF0, 0x9F, 0x98, 0x80 };
  auto actions = run_sequence(bytes);
  REQUIRE(actions.size() == 1);
  CHECK(actions[0].type == action_type::write_char);
  CHECK(actions[0].codepoint == 0x1F600);
}

// UTF-8: mixed ASCII + multi-byte
// "A" + 中 + "B" → three write_char actions
{
  std::vector<uint8_t> const bytes = { 'A', 0xE4, 0xB8, 0xAD, 'B' };
  auto actions = run_sequence(bytes);
  REQUIRE(actions.size() == 3);
  CHECK(actions[0].codepoint == U'A');
  CHECK(actions[1].codepoint == 0x4E2D);
  CHECK(actions[2].codepoint == U'B');
}

// UTF-8: invalid sequence (0xFF) → U+FFFD
{
  std::vector<uint8_t> const bytes = { 0xFF };
  auto actions = run_sequence(bytes);
  REQUIRE(actions.size() == 1);
  CHECK(actions[0].codepoint == 0xFFFD);
}

// UTF-8: overlong encoding → U+FFFD
// 0xC0 0x80 (overlong for NUL)
{
  std::vector<uint8_t> const bytes = { 0xC0, 0x80 };
  auto actions = run_sequence(bytes);
  REQUIRE(actions.size() == 1);
  CHECK(actions[0].codepoint == 0xFFFD);
}

// UTF-8: stray continuation byte → ignored
{
  std::vector<uint8_t> const bytes = { 'A', 0x80, 'B' };
  auto actions = run_sequence(bytes);
  REQUIRE(actions.size() == 2);
  CHECK(actions[0].codepoint == U'A');
  CHECK(actions[1].codepoint == U'B');
}

// UTF-8: truncated sequence (start byte without continuations)
// ESC interrupts → control code processed, UTF-8 aborted
{
  std::vector<uint8_t> const bytes = { 0xE4, 0x1B };
  auto actions = run_sequence(bytes);
  // The 0xE4 starts UTF-8 sequence; 0x1B (ESC) aborts it → goes to escape state
  // ESC alone becomes save_cursor? No, ESC + nothing just resets to ground.
  // Actually ESC followed by end-of-input → parser stays in escape state,
  // no action emitted. Or parse_sequence might return nothing.
  // We verify no write_char for the broken sequence.
  for (auto const& a : actions) {
    CHECK(a.type != action_type::write_char);
  }
}
```

**Note:** The `run_sequence` helper or `parse_sequence` helper may need
adjustment to accept `std::vector<uint8_t>` or raw bytes. The existing helper
takes `std::string_view`. We can create a helper that takes `std::span<const uint8_t>`.

### 6b. Grid tests — wide characters

```cpp
// Wide char occupies two cells.
TEST_CASE("Grid — wide char occupies two cells") {
  terminal_grid g(10, 5);
  g.write_char(0x4E2D);  // 中 (CJK, width 2)
  // Cell (0,0) = 中 with wide attr
  CHECK(g.cell(0, 0).codepoint == 0x4E2D);
  CHECK(has_attr(g.cell(0, 0).attr, cell_attr::wide));
  // Cell (0,1) = continuation
  CHECK(has_attr(g.cell(0, 1).attr, cell_attr::wide_tail));
  // Cursor at column 2
  CHECK(g.cursor_col() == 2);
}

// Wide char at last column wraps to next line.
TEST_CASE("Grid — wide char at last column wraps") {
  terminal_grid g(5, 5);
  g.write_char(U'A');  // col 0
  g.write_char(U'A');  // col 1
  g.write_char(U'A');  // col 2
  g.write_char(0x4E2D);  // wide char at col 3 → fits at cols 3–4
  CHECK(g.cursor_row() == 0);
  CHECK(g.cursor_col() == 0);  // wrapped (then auto-wrap moved to next line)

  // Need a test that specifically verifies wide char wraps from last column.
  // Create a new grid and test:
  terminal_grid g2(5, 5);
  g2.write_char(U'A'); g2.write_char(U'A'); g2.write_char(U'A'); g2.write_char(U'A'); // cols 0-3
  // Cursor at col 4 (last column)
  CHECK(g2.cursor_col() == 4);
  g2.write_char(0x4E2D);  // wide char at last column → wrap to next row, cols 0-1
  CHECK(g2.cursor_row() == 1);
  CHECK(g2.cell(1, 0).codepoint == 0x4E2D);
  CHECK(has_attr(g2.cell(1, 0).attr, cell_attr::wide));
  CHECK(has_attr(g2.cell(1, 1).attr, cell_attr::wide_tail));
}

// Normal char after wide char placed at correct column.
TEST_CASE("Grid — normal char after wide char") {
  terminal_grid g(10, 5);
  g.write_char(0x4E2D);  // wide, cols 0–1
  g.write_char(U'A');    // col 2
  CHECK(g.cell(0, 2).codepoint == U'A');
  CHECK(g.cursor_col() == 3);
}

// Wide continuation skipped by render_cells.
TEST_CASE("Grid — render_cells marks wide continuation") {
  terminal_grid g(10, 5);
  g.write_char(0x4E2D);
  auto cells = g.render_cells();
  // Cell 0: codepoint = 0x4E2D, attr has k_attr_wide
  CHECK(cells[0].codepoint == 0x4E2D);
  CHECK((cells[0].attr & (1 << 6)) != 0);  // k_attr_wide
  // Cell 1: continuation, attr has k_attr_wide_tail
  CHECK((cells[1].attr & (1 << 7)) != 0);  // k_attr_wide_tail
}
```

### 6c. Grid tests — combining characters

These tests verify NFC pre-composition. Because they depend on Windows
`NormalizeString`, they run only on Windows (same as all other tests).

```cpp
// Combining acute accent after 'e' → composed é.
TEST_CASE("Grid — combining char composes with base") {
  terminal_grid g(10, 5);
  g.write_char(U'e');      // col 0
  g.write_char(0x0301);    // combining acute accent (zero-width)
  // Cursor should still be at col 1 (zero-width didn't advance).
  CHECK(g.cursor_col() == 1);
  // Cell (0,0) should now contain é (U+00E9).
  CHECK(g.cell(0, 0).codepoint == 0x00E9);
}

// Combining char at column 0 → written as width 1.
TEST_CASE("Grid — combining char at column 0 is width 1") {
  terminal_grid g(10, 5);
  g.write_char(0x0301);    // combining acute at col 0
  CHECK(g.cursor_col() == 1);  // treated as width 1
  CHECK(g.cell(0, 0).codepoint == 0x0301);
}

// Combining char after space → written as width 1.
TEST_CASE("Grid — combining char after space is width 1") {
  terminal_grid g(10, 5);
  g.write_char(U' ');      // space
  g.write_char(0x0301);    // combining acute
  CHECK(g.cursor_col() == 2);  // treated as width 1
  CHECK(g.cell(0, 1).codepoint == 0x0301);
}
```

### 6d. `wcwidth` unit tests

```cpp
TEST_CASE("wcwidth — ASCII returns 1") {
  CHECK(wcwidth(U'A') == 1);
  CHECK(wcwidth(U' ') == 1);
}

TEST_CASE("wcwidth — CJK returns 2") {
  CHECK(wcwidth(0x4E2D) == 2);  // 中
  CHECK(wcwidth(0x6587) == 2);  // 文
  CHECK(wcwidth(0x3042) == 2);  // あ (Hiragana)
  CHECK(wcwidth(0xAC00) == 2);  // 가 (Hangul)
}

TEST_CASE("wcwidth — fullwidth returns 2") {
  CHECK(wcwidth(0xFF01) == 2);  // ！ fullwidth exclamation
  CHECK(wcwidth(0xFF21) == 2);  // Ａ fullwidth A
}

TEST_CASE("wcwidth — combining returns 0") {
  CHECK(wcwidth(0x0301) == 0);  // combining acute
  CHECK(wcwidth(0x0300) == 0);  // combining grave
}

TEST_CASE("wcwidth — control returns -1") {
  CHECK(wcwidth(0x0000) == -1);
  CHECK(wcwidth(0x000A) == -1);
  CHECK(wcwidth(0x007F) == -1);
}

TEST_CASE("wcwidth — emoji returns 2") {
  CHECK(wcwidth(0x1F600) == 2);  // 😀
  CHECK(wcwidth(0x2764) == 1);   // ❤ (narrow emoji)
}
```

---

## Files Changed

| File | Change |
|---|---|
| `src/terminal/vt_parser.hpp` | Add `utf8_` accumulation state to parser; add `utf8_accum` state enum; add `cell_attr::wide` and `cell_attr::wide_tail` |
| `src/terminal/vt_parser.cpp` | Implement UTF-8 decoding in GROUND and new `utf8_accum` state |
| `src/terminal/wcwidth.hpp` *(new)* | Declare `wcwidth(char32_t)` |
| `src/terminal/wcwidth.cpp` *(new)* | Implement `wcwidth()` with range tables |
| `src/terminal/grid.hpp` | Add `write_cell` and `write_combining_char` private helpers; add `#include <winnls.h>` |
| `src/terminal/grid.cpp` | Rewrite `write_char` for width awareness; implement `write_combining_char` with NFC; implement `write_cell` helper |
| `src/platform/text.hpp` | Add `prepare_unicode_glyphs()` method declaration |
| `src/platform/text.cpp` | Add dynamic atlas creation (combined texture); implement `prepare_unicode_glyphs()` and `ensure_glyph_cached()`; modify `draw_grid` for dynamic atlas lookup and wide char rendering |
| `src/platform/gfx.hpp` or `text.hpp` | Expose `ID3D11DeviceContext*` from device (may already be accessible via `device.impl_->context`) |
| `src/app.cpp` | Call `renderer_.prepare_unicode_glyphs(device_, cells)` before `draw_grid` |
| `tests/vt_parser_test.cpp` | Add UTF-8 decoding tests |
| `tests/grid_test.cpp` | Add wide char tests and combining char tests; add `wcwidth` tests |
| `CMakeLists.txt` | Add `Normaliz` library link; add `wcwidth.cpp` to build |

---

## Edge Cases Covered

| Edge case | Behavior |
|---|---|
| Wide char at last column (N-1) | Wrap to next row, place at col 0–1 |
| Wide char at N-2 | Fits: cols N-2 and N-1. Auto-wrap to next line after (cursor at N → wrap) |
| Combining char at column 0 | Write as width-1 glyph (no base to compose with) |
| Combining char after space/empty | Write as width-1 |
| Combining char after wide continuation | Write as width-1 (don't compose with wide tail) |
| Multiple combining chars on same base | Each call to `write_combining_char` NFC-composes with the progressively updated cell |
| Overlong UTF-8 sequences | Rejected → emits U+FFFD |
| Invalid UTF-8 (stray continuation bytes) | Silently dropped |
| Truncated UTF-8 (control code interrupts) | Sequence aborted; control code processed normally |
| Surrogates in UTF-8 (U+D800–U+DFFF) | Emit U+FFFD (CESU-8/overlong encoding is invalid) |
| Codepoint > U+10FFFF | Emit U+FFFD |
| Dynamic atlas full (1024 glyphs) | LRU eviction: least recently used glyph slot is reused |
| Glyph rasterization fails (e.g., missing font) | Fallback: render '?' from ASCII atlas |
| Wide char + bold | Bold double-draw applied across 2× cell width |
| Wide char + underline | Underline quad spans 2× cell width |
| Grid resize with wide chars | Existing reflow treats cells individually; wide pairs may break (acceptable for MVP) |
| ICH/DCH through a wide character | Wide pair may be broken; cells retain their flags (acceptable for MVP; hardened in Task 18) |

---

## What's NOT in scope (Task 15)

| Item | Reason |
|---|---|
| Ligature support | Explicitly out of scope per PRD §6 |
| Unicode normalization of shell input | Input path is ASCII-focused; non-ASCII keyboard input deferred |
| Perfect wide-character handling in ICH/DCH/IL/DL | Cells are shifted individually; wide pairs may split. Hardened in Task 18 |
| Right-to-left (RTL) text or bidi | Out of scope for MVP |
| VS16 (emoji variation selector) making glyphs wide | Emoji width is based on Unicode block only, not variation selectors |
| Dynamic atlas for italic non-ASCII | Non-ASCII + italic rasterizes with the regular face for now; can be extended later |
| Anti-aliasing for dynamic glyphs | Cleartype rendering mode (same as ASCII atlas) |
