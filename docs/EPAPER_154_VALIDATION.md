# EPAPER 1.54 Validation Log

This file records hardware observations that firmware constants depend on.
Do not tune product behavior from guesses; run the relevant mode and write the
result here before changing limits.

## Maturity Statement, 2026-08-09

This tree is a **release candidate**, not a finished product firmware. Everything
below is what still separates the two. Development is paused here on purpose; the
gaps are recorded rather than closed so that whoever picks this up next does not
mistake "serial log is clean" for "the product is validated".

What is closed:

- Hardware baseline: full refresh, partial refresh, touch, BOOT diagnostics all present and exercised.
- Partial refresh path is trustworthy: 20/20 consecutive partial frames at 575 ms, clearly under the 1755 ms full refresh.
- Application and diagnostics are separated (`app_family_video.*` / `app_diagnostics.*`); `main.cpp` is thin.
- Build modes are declared in `platformio.ini` (`epaper_154_app` / `_m0` / `_m1`), no macro hand-editing.
- Refresh policy is product-shaped: first frame full, backward navigation full + rebase, final frame full, per-frame diff decides partial vs full, failure falls back to full.
- Asset pipeline accepts a config with crop / threshold / manifest / contact sheet.
- Touch requires a stable press and a stable release before re-arming.

What is open (all four block "finished product"):

1. **Partial-refresh ghosting limit is not measured by eye.** The M1 table below is
   `visual pending` on every row. `APP_PARTIAL_MAX_STREAK=4` is therefore a
   conservative guess, not a measured limit. Close it by running `epaper_154_m1`,
   finding the first visually objectionable step N, and pinning the macro to it.
2. **Shipped assets are a legacy import.** `assets/generated/family_video_manifest.json`
   reports `tool: legacy-current-assets-import`, `input: unknown`, and null
   timecode / threshold / crop. The frames compiled into the firmware cannot be
   reproduced from the source video. Close it by regenerating once through
   `tools/video2c.py --config`.
3. **PWR long-press power-off is unverified on battery only.** Never tested with USB
   disconnected.
4. **BOOT diagnostics entry was not re-tested after this refactor.** The code is kept
   deliberately, but the entry path has not been exercised since `main.cpp` was split.

Only after all four are closed, plus a full acceptance pass and a tagged commit that
locks firmware + assets + validation log together, should this be called a mature
product firmware.

## Current Firmware Baseline

- Board: Waveshare ESP32-S3 Touch ePaper 1.54, 200x200, N8R8
- PlatformIO app env: `epaper_154_app`
- Diagnostic envs: `epaper_154_m0`, `epaper_154_m1`
- Product partial streak limit: `APP_PARTIAL_MAX_STREAK=4`
- Status: provisional until the M1 table below is filled from the physical board

## M0 Full Refresh And Mask Check

Run:

```powershell
pio run -e epaper_154_m0 -t upload --upload-port <PORT>
pio device monitor -p <PORT> -b 115200
```

Expected panel image:

- Diagonal background across the panel
- Heavy border
- Masked oval near center
- White band through the oval

Record:

| Date | Firmware commit | Result | Full refresh busy_ms | Notes |
|---|---|---|---:|---|
| 2026-08-09 | working tree | PASS serial-side | 1755 | Mask visual check still needs human confirmation |

## M1 Partial Refresh Streak Check

Run:

```powershell
pio run -e epaper_154_m1 -t upload --upload-port <PORT>
pio device monitor -p <PORT> -b 115200
```

Record every partial step. The decision point is the first step where ghosting
is visible enough to hurt the product experience.

| Step | busy_ms | Flashing? | Ghosting visible? | Notes |
|---:|---:|---|---|---|
| 1 | 575 | visual pending | visual pending | Serial OK |
| 2 | 575 | visual pending | visual pending | Serial OK |
| 3 | 575 | visual pending | visual pending | Serial OK |
| 4 | 575 | visual pending | visual pending | Serial OK |
| 5 | 575 | visual pending | visual pending | Serial OK |
| 6 | 575 | visual pending | visual pending | Serial OK |
| 7 | 575 | visual pending | visual pending | Serial OK |
| 8 | 575 | visual pending | visual pending | Serial OK |
| 9 | 575 | visual pending | visual pending | Serial OK |
| 10 | 575 | visual pending | visual pending | Serial OK |
| 11 | 575 | visual pending | visual pending | Serial OK |
| 12 | 575 | visual pending | visual pending | Serial OK |
| 13 | 575 | visual pending | visual pending | Serial OK |
| 14 | 575 | visual pending | visual pending | Serial OK |
| 15 | 575 | visual pending | visual pending | Serial OK |
| 16 | 575 | visual pending | visual pending | Serial OK |
| 17 | 575 | visual pending | visual pending | Serial OK |
| 18 | 575 | visual pending | visual pending | Serial OK |
| 19 | 575 | visual pending | visual pending | Serial OK |
| 20 | 575 | visual pending | visual pending | Serial OK |

Serial-side conclusion, 2026-08-09:

```text
Full refresh busy_ms: 1755
Partial begin base busy_ms: 574
Partial frame busy_ms: 575 for all 20/20 steps
No serial FAIL, panic, abort, Guru Meditation, busy-timeout, or abnormal reset loop observed.
Visual ghosting threshold is not yet recorded.
Keep APP_PARTIAL_MAX_STREAK=4 until the visual result is confirmed.
```

Conclusion template:

```text
This board is acceptable for N consecutive partial refreshes.
Ghosting becomes visible at step N+1.
Set APP_PARTIAL_MAX_STREAK=N.
```

## Product App Acceptance

Before a showcase flash, run:

```powershell
pio run -e epaper_154_app
pio device list
pio run -e epaper_154_app -t upload --upload-port <PORT>
pio device monitor -p <PORT> -b 115200
```

Checklist:

- First frame appears after a full refresh
- Repeated right-half touches reach the final frame
- Repeated left-half touches return to the first frame
- Backward navigation uses full refresh and rebuilds the partial baseline
- Final frame uses full refresh
- Long right-half touch jumps to final frame
- Long left-half touch jumps to first frame
- Holding PWR for 3 seconds clears the screen and powers off on battery
- Holding BOOT during the first 800 ms enters diagnostics
- Serial log has no `FAIL`, `panic`, `abort`, `Guru Meditation`, `busy-timeout`,
  or repeated abnormal `rst:` loop

## Product App Run, 2026-08-09

Serial-side result:

```text
FT6336 ready: n0 int1
First frame full refresh busy_ms: 1755
Partial begin base busy_ms: 574
Idle verification: no frame changes for 30 seconds after stable-touch rearm logic
Single right tap: action=next x=150 y=119 hold=621ms, frame 1 -> 2, partial busy_ms=575
Same-session right then left:
  action=next x=179 y=125 hold=621ms, frame 1 -> 2, partial busy_ms=575
  action=prev x=27 y=110 hold=661ms, frame 2 -> 1, full refresh + partial rebase
```

Earlier in the same session, the app advanced frames while no intentional touch
was expected. A first fix that required the FT6336 interrupt line blocked real
touches on this board. The accepted app behavior now uses stable coordinates,
requires a stable release before re-arming, and does not depend on the INT line.
As of the latest tuning, release re-arm is 150 ms and long-press navigation is
1500 ms to reduce accidental jumps to the final/first frame.

## Version Control Rule

When hardware results change firmware constants, commit the code change and this
validation log together. The commit message should name the observed limit, for
example:

```text
fix: cap partial refresh streak at measured limit
```
