# Story asset agent guide

This repository targets a 200x200, 1-bit Waveshare e-paper display. Every
story is an independent package under `assets/stories/<story_id>/` and must
keep the same scene number in `source/scenes/`, `story.json`, `audio/`, and the
generated FVID container.

## Image prompt baseline

Use this text in every image-generation prompt:

```text
200x200 monochrome e-paper illustration, clear black ink line art on a mostly white paper background, strong readable silhouettes, sparse hatching, large simple shapes, one main subject, no text, no border, no watermark, no gradients, no photorealism, no solid black background, no full-bleed dark sky, no black vignette, no dense dark fog, leave generous white negative space around the subject, designed to survive 1-bit thresholding on a small 200x200 e-ink display.
```

After generation, inspect a 1:1 crop and run the asset checker. Target black
pixel ratio is 8%–32%; below 5% or above 38% needs review, and above 45% is a
hard failure. Night, forest and storm scenes use white negative space, sparse
branches, arcs, rain strokes and silhouettes rather than a dark background.

## Audio handoff

Freeze `story.json` narration before generating audio. Generate one scene at a
time and name the final files `000.wav`, `001.wav`, and so on. The board accepts
only mono, 16 kHz, 16-bit PCM WAV. Keep one final audio set under the story's
`audio/` directory; do not add a local SAPI backup set.

Choose and record a distinct voice per story; do not assume the voice used by
another story is the default. For MiniMax or another approved TTS provider,
record the provider/model/voice in `story.json` and the README. Keep provider
source files only when they are needed for provenance or later conversion; the
firmware-facing files remain the single normalized `audio/` set.

## Build and check

```powershell
python tools/build_stories.py --manifest assets/stories/<story_id>/build.json --out-dir assets/stories/<story_id>
python tools/check_story_assets.py assets/stories/<story_id>
python tools/check_story_library.py assets/stories
```

Copy the resulting `.fvid` and same-named story directory to `/video/` on the
SD card. For a checked, formal package, use the staging helper; it refuses
stories whose audio is still pending:

```powershell
powershell -ExecutionPolicy Bypass -File tools/stage_story_sd.ps1 `
  -StoryDir assets/stories/<story_id> `
  -OutputDir .tmp/sd-stage/<story_id>
```

Hardware acceptance still requires `pio run -e epaper_154_app`, a
flash, and serial checks for list count, selected ID, source, scene number,
refresh result, and audio completion.
