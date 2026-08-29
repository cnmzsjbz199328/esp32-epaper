# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An application project for the Waveshare ESP32-S3-Touch-ePaper-1.54 (200x200, 1bpp e-paper).
The app is "family video frames": full-refresh the first extracted frame on boot, then each
right-side screen tap advances to the next story frame using **partial refresh**.
Left-side tap goes back to the previous frame.

PlatformIO + Arduino framework. Single environment: `epaper_154`.

Board code was migrated from the sibling bring-up project at
`C:\Users\tj169\Flinders\work\Learning\esp32_test` (baseline `epaper_154 v0.7.10`,
commit `d5221ec6356fc4d0b419157cf4198ae5c1d5295b`, 16/16 selftests passing).
That project remains the hardware ground truth — when app behavior contradicts its
conclusions, re-test there before suspecting app logic.

## Commands

```powershell
pio run -e epaper_154                              # build
pio device list                                    # find the port — never assume COM9
pio run -e epaper_154 -t upload --upload-port COM9 # flash
pio device monitor -p COM9 -b 115200               # serial log
```

There are no unit tests. Verification is: build clean, flash, read the serial log, and
look at the screen. Serial log keywords that mean failure: `FAIL`, `WARN`, `panic`,
`abort`, `error`, `Guru Meditation`, `rst:`.

**Diagnostic mode**: hold BOOT during the first 800ms after reset to run the v0.7.10
interactive all-test app instead of the application. `BSP_EPAPER_INTERACTIVE_TEST_APP`
in `bsp_caps.h` is 0 here, but `selftest.cpp` and its entry point are deliberately kept.
Do not delete them to "clean up".

## Architecture

### Layering

- `lib/bsp_api/bsp.h` — the only public BSP surface. Board-agnostic declarations.
- `lib/bsp_core/` — board-agnostic implementations (selftest reporting, UI widgets).
- `boards/epaper_154/` — this board's implementation. `bsp_pins.h` is the single source
  of truth for every GPIO and hardware constant.
- `src/main.cpp` — the application. Milestone-gated (see below).
- `assets/generated/` — tool-generated C arrays and PNG previews compiled in through
  `build_src_filter`. Current firmware assets are `family_video_assets.c/.h`.

### Two EPD driver paths — this is the important part

`boards/epaper_154/` contains **two** independent SSD1681 drivers, and the split is
intentional:

- `ui.cpp` has an in-house driver (`epd_*` statics) used for the A/B LUT experiments.
- `epd_factory.cpp` is the official reference driver (`docs/refs/demo/epaper_driver_bsp.cpp`)
  transcribed byte-for-byte, deliberately sharing nothing with `ui.cpp` — not the SPI
  handle, not the LUT tables, not the GPIO init. Its 159-byte LUT tables are duplicated
  on purpose. The file header lists every intentional deviation from the original.

**The `epd_factory.cpp` path is the one that actually produces visible refreshes.**
All real display output (`bsp_ui_flush`, `bsp_ui_fb_flush_full`, partial refresh) routes
through it. When adding display features, extend `epd_factory.cpp` and wrap it in
`ui.cpp` — do not "equivalently rewrite" reference command sequences into the in-house
driver. Two rounds of visual code comparison already failed to find the bug that way;
the discipline is to copy the reference literally and let the hardware judge.

### Framebuffer bit semantics

`bsp_ui_fb()` returns 5000 bytes in **panel-native format: bit 1 = white, bit 0 = black
ink**, row-major, 25 bytes per row, MSB = leftmost pixel. It is fed to the panel with no
inversion anywhere — one fewer place for "the whole screen is inverted" to hide.

Video assets are packed directly in panel-native format by `tools/video2c.py`.

### Retired dual bit-planes and the mask

The first application used green-screen-keyed people over a separate background. E-paper
has no alpha channel, so every non-rectangular graphic needed **two** planes:

```c
const uint8_t* bits;   /* 1 = black ink */
const uint8_t* mask;   /* 1 = this pixel belongs to the figure */
/* fb = (fb & ~mask) | (ink & mask) */
```

Without `mask` there was no way to distinguish "white shirt on the person" from
"background outside the person's outline", and the whole bounding box got pasted onto
the screen. `bsp_ui_draw_bitmap()` still supports this because M0 diagnostics and future
non-rectangular assets may need it, but the current main app uses full-frame video assets
and does not compile the retired `family_assets.*` files.

### Partial refresh is session-based

Full refresh is one-shot: init → write → release. Partial refresh cannot be, because the
base image in RAM `0x26` must survive across frames:

```c
bsp_ui_fb_flush_full();   /* screen now equals fb */
bsp_ui_partial_begin();   /* takes current fb as base image, switches to partial LUT */
/* ...edit fb...  */ bsp_ui_flush_partial();   /* repeat per tap */
bsp_ui_partial_end();
```

Invariants:

- Before `partial_begin()`, the screen must **already equal** the framebuffer. A base
  image that disagrees with what's displayed produces ghosting on every subsequent frame.
- Inserting a full refresh mid-sequence requires `partial_end()` → `fb_flush_full()` →
  `partial_begin()`. Full refresh swaps the LUT and invalidates `0x26`.
- Refresh is whole-frame (5000 bytes), not windowed. Windowed addressing buys nothing at
  this size and costs 8-pixel alignment, coordinate math, and base-image sync.
- Ignore touch input while BUSY, or a tap re-enters mid-refresh.

### Milestone diagnostics

`src/main.cpp` keeps `APP_MODE=1` and `APP_MODE=2` as diagnostics. **M1
(partial-refresh path verification with a moving square) is still the fastest way to
check the partial-refresh path on real hardware.** The acceptance criterion is the
measured partial-refresh BUSY duration: full refresh is 1755ms; partial should be
~300–500ms. If partial is still ~1755ms the partial LUT never took effect, and a picture
that "looks right" does not count as passing.

`FAC_PARTIAL_REAPPLY_WINDOW` in `epd_factory.cpp` is the single-variable switch to try if
partial refresh comes out flipped or row-shifted — the reference `EPD_Init_Partial()`
hard-resets without re-applying entry mode `0x11` / window registers.

### App shell UI design system

Every screen under `src/shell/` and `src/apps/` is drawn with the same primitive,
`bsp_ui_fb_draw_text(x, y, text, scale)` — a 5×7 bitmap font, always black ink, no
background fill. Keep new screens visually consistent with the existing launcher /
settings / usage pages by following these measured conventions. They are not arbitrary;
they come from the font metrics and the 200×200 panel, and drifting from them makes
partial-refresh regions and tap hit-tests disagree with what's on screen.

**Font metrics (do the math with these, never eyeball).** At `scale` s: glyph is 5×7 px,
horizontal advance is `6*s` px per character (5 glyph + 1 gap), newline advance is `8*s`
px. `y` passed to `bsp_ui_fb_draw_text` is the **top** of the glyph, not a baseline. So a
string is `strlen*6*s` px wide and 7*s px tall. Standard body text is `scale = 1`; no
screen currently uses a larger scale for prose. Centre a label with
`x = box_x + (box_w - strlen*6) / 2`; right-align with
`x = BSP_EPD_W - strlen*6 - 2` (the status bar's right margin).

**Status bar** (`status_bar.cpp`, drawn by the shell for every `fullscreen == false`
app): height `STATUS_H = 26`, full 200 px wide. Text sits at `y = 7` (≈7 px top padding,
7 px glyph, ~4 px to the rule). Left string at `x = 2`, right string right-aligned with a
2 px margin. A 1 px black rule closes it at `y = 25`. It self-refreshes every 30 s.
Do not draw app content above `y = 26`.

**Content region.** Non-fullscreen apps own `y = 26 .. 199` and clear it with
`bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false)` on entry. Fullscreen apps
(`app_family_video`) skip the status bar and own all 200×200.

**Page / section title:** `scale 1`, ALL CAPS, drawn at `x = 5, y = 29` (settings pages
use 29–30; usage sections use `y = top`). Left margin for all body text is `x = 5`
(the status bar and bar-chart labels are the sole `x = 2` exceptions).

**Footer / key-hint line:** `scale 1` at `x = 5, y = 184`, e.g. `"ENTER run  ESC home"`,
`"ESC back"`, `"LEFT/RIGHT focus  ESC back"`. Always the last line on the screen.

**Menu lists** (`app_settings.cpp`): first row at `y = 42`, row pitch **21 px**. The
selection stripe is `bsp_ui_fb_fill_rect(2, y - 2, BSP_EPD_W - 4, 13, true)`. The tap
hit-test must match the layout: `index = (tap_y - 37) / 21`, valid while
`37 <= tap_y < 37 + COUNT*21`. Dense read-only rows (results, usage detail) use a
tighter 15–20 px pitch starting at `y = 45`–`52`.

**Launcher grid** (`launcher.cpp`): 2×2, `GRID_TOP = 34`, `CELL_W = 88`, `CELL_H = 82`
(34 + 2×82 = 198, fits). Per cell: icon at the top, centred horizontally
(`icon_x = cell_x + (CELL_W - SHELL_ICON_W) / 2`), then the title `scale 1` centred at
`y = cell_y + 75` (the last ~5 px overlap the 80 px icon on purpose — the cell is 2 px
shorter than icon+title). Selected item gets a 1 px underline the width of its title at
`y = GRID_TOP + row*CELL_H + 83`. Empty slots draw a faint `+` from two rects at the cell
centre.

**Icons:** `SHELL_ICON_W = SHELL_ICON_H = 80`, 1bpp **panel-native** (1 = black ink),
`SHELL_ICON_LEN = 800` bytes, one `extern const uint8_t SHELL_ICON_*[SHELL_ICON_LEN]` per
app in `assets/generated/icons.{h,c}`. Blitted with `bsp_ui_draw_bitmap(..., nullptr,
false)`. Icons are original glyph art (bars / gauge / grid) — no third-party mascots or
proprietary fonts (see `docs/USAGE_APP_PLAN.md` §1).

**Horizontal progress bars** (`app_usage.cpp` `draw_bar`): 2-char label at `x = 2`; bar
box at `x = 22, w = 104, h = 12` with a 1 px border; fill inset 2 px on every side
(`fill_rect(x+2, y+2, (w-4)*pct/100, h-4)`, nothing drawn at `pct == 0`); numeric `%`
column at `x = 130` (`"%3u%%"`); a secondary/countdown column at `x = 174`. Stacked bars
use a **16 px** vertical pitch. "Due / expired" state replaces the fill with a diagonal
hatch and a `~` marker rather than a number.

**Divider rules:** 1 px black line for a minor separator, 2 px for a major one
(`app_usage.cpp` uses `y = 72` minor between providers, `y = 120` major above the
banner). An emphasis box around a warning row is a 1 px rectangle, ~16 px tall, full
width — never inverse video or blink (ugly and refresh-expensive on e-paper).

**Vertical rhythm within an app section:** provider sections in the usage overview are
48 px tall (`top = 28 + slot*48`); info lines below a heading step 14–18 px. When adding
a screen, pick one pitch from {14, 16, 18, 21} and stay on it.

### Retired attempt lesson

The retired first app was useful but should not be revived as the default: it looked
blurred on the 200x200 1bpp panel and did not communicate the family's spatial
relationships well. Keep that lesson in version history and in
`docs/FAMILY_PHOTO_APP.md` when touching the asset pipeline.

## Hardware conventions

Take every GPIO and hardware constant from `boards/epaper_154/bsp_pins.h`. Do not
re-derive pins from web resources or memory — v0.1.0 had to retract six pin macros for
exactly that.

Values in `bsp_pins.h` carry provenance markers:

- `[OK]` — verified by flashing real hardware
- `[REF]` — from the official schematic or a chip datasheet
- `[DOC]` — wiki text or silkscreen only, not cross-checked

`boards/epaper_154/docs/SOURCES.md` §3 mirrors these; the source text in a `[REF]`
comment must match SOURCES.md verbatim (the sibling project's
`tools/check_status_sync.py` cross-checks this). If a value cannot be sourced, it is not
defined — inventing one by copying from another board manufactures a false truth
(e.g. `BSP_BAT_ADC_RATIO` is deliberately absent).

Board-specific gotchas that have already cost time:

- `BSP_PIN_EPD_PWR_EN` is **active-low**: `LOW` = ON, `HIGH` = OFF.
- BUSY is **active-high**: HIGH = busy.
- SD card is SD_MMC 1-bit, not SPI. There is no `SD_CS`.
- `bsp_mic_init()` must come after `bsp_audio_init()` — the mic depends on the I2S the
  playback side installs.
- Board identity is fingerprinted (8MB flash + 8MB PSRAM, N8R8) before driving EPD pins,
  because on a different board the BUSY pin can read low and make a dead command chain
  look like a passing one.

## Documentation

- `docs/EPAPER_154_APP_GUIDE.md` — hardware facts, verified peripherals, partial-refresh
  reference material, BSP gaps. Hardware truth lives here.
- `docs/FAMILY_PHOTO_APP.md` — current video-frame application design, asset pipeline,
  retired first-app lessons, diagnostics, risks.
  Application design lives here; it does not restate hardware facts.
- `boards/epaper_154/CHANGELOG.md` — versioned board history with a Validation section
  per entry. Keep the format when changing board code.
- `boards/epaper_154/docs/refs/demo/` — the official Waveshare examples, kept as
  executable ground truth rather than as reading material.

Comments in this codebase explain *why* a line exists, often citing the bug it prevents
or the experiment it belongs to. Several comments explicitly say "do not remove this
redundancy". Match that density and keep the reasoning when editing nearby code.
