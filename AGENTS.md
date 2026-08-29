# Repository Guidelines

## Project Structure & Module Organization

This is a PlatformIO Arduino project for the Waveshare ESP32-S3 Touch ePaper 1.54 board. The application entry point is `src/main.cpp`. Public BSP declarations live in `lib/bsp_api/`, shared board-agnostic support lives in `lib/bsp_core/`, and board-specific implementation lives in `boards/epaper_154/`. Treat `boards/epaper_154/bsp_pins.h` as the hardware source of truth. Project documentation is in `docs/`; board reference notes and copied vendor examples are under `boards/epaper_154/docs/`. Generated bitmap assets are expected in `assets/generated/` when the asset pipeline is added.

## Build, Test, and Development Commands

Use the `epaper_154` PlatformIO environment.

```powershell
pio run -e epaper_154
pio device list
pio run -e epaper_154 -t upload --upload-port COM9
pio device monitor -p COM9 -b 115200
```

`pio run` builds the firmware. `pio device list` finds the active serial port; do not assume a fixed COM port. The upload command flashes the board, and the monitor command reads runtime logs at 115200 baud.

## Coding Style & Naming Conventions

Follow the existing C/C++ style: 4-space indentation, braces on their own line for functions, compact braces for short control blocks where already used, and `snake_case` for functions and local helpers. BSP symbols use the existing `bsp_*` and `BSP_*` prefixes. Keep board-specific constants in `bsp_pins.h`; do not hard-code GPIO values elsewhere. Comments should explain hardware rationale, provenance, or non-obvious sequencing rather than restating code.

UI screens under `src/shell/` and `src/apps/` follow the shared layout spec in `CLAUDE.md` -> "App shell UI design system" (font metrics, margins, title/footer positions, launcher grid, icon size, progress-bar geometry). Keep new screens on those conventions.

## Testing Guidelines

There are no unit tests in this repository. Verification is hardware-oriented: build cleanly, flash the board, inspect the e-paper output, and check serial logs. Failure keywords include `FAIL`, `WARN`, `panic`, `abort`, `error`, `Guru Meditation`, and `rst:`. Holding BOOT during the first 800 ms after reset enters the retained diagnostic path; do not remove diagnostic files as cleanup.

## Commit & Pull Request Guidelines

Current history uses concise conventional-style subjects, for example `docs: scope the family-photo partial-refresh application`. Prefer `type: imperative summary` such as `fix: correct partial refresh busy handling` or `docs: update asset pipeline notes`. Pull requests should describe the behavior change, list build and hardware validation performed, note the board/port used, and include serial log excerpts or photos when display behavior changes.

## Hardware & Configuration Tips

Keep `platformio.ini` memory and PSRAM settings aligned with the N8R8 board baseline. When display behavior conflicts with assumptions, compare against the sibling bring-up project documented in `README.md` and `CLAUDE.md` before changing driver sequences.
