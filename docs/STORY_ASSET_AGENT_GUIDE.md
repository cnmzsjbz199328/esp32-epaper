# Story asset agent guide

This repository targets a 200x200, 1-bit Waveshare e-paper display. Every
story is an independent package under `assets/stories/<story_id>/` and must
keep the same scene number in `source/scenes/`, `story.json`, `audio/`, and the
generated FVID container.

## Image prompt baseline

Use this text in every image-generation prompt. The image is not expected to
carry the story by atmosphere alone: the scene-specific prompt must name one
visible action, one main subject, and one or two unmistakable props.

```text
200x200 monochrome e-paper illustration, clear black ink line art on a mostly white paper background, one unmistakable narrative action, one dominant subject, no more than three secondary figures, two or three large identifying props, strong readable outer silhouettes, very thick clean contours, sparse deliberate hatching, large simple shapes, no text, no border, no watermark, no gradients, no photorealism, no solid black background, no full-bleed dark sky, no black vignette, no dense dark fog, no tiny crowd, no fine texture, leave generous white negative space around the subject, designed to survive 1-bit thresholding on a small 200x200 e-ink display.
```

After generation, inspect both the source image and the final 200x200 1-bit
FVID result. The scene must still be identifiable at thumbnail size before it
is accepted. Target black pixel ratio is 8%–32%; below 5% or above 38% needs
review, and above 45% is a hard failure. Night, forest and storm scenes use
white negative space, sparse branches, arcs, rain strokes and silhouettes
rather than a dark background. If the subject boundary disappears after
thresholding, regenerate the image; do not rely on a higher threshold to
rescue it.

For the Odyssey rewrite, use this scene prompt shape:

```text
Scene meaning: <one sentence copied from the matching narration>
Visible action: <what the viewer can see happening now>
Main subject: <one dominant person, creature, ship, or object>
Identifying props: <one or two props that make this scene unique>
Composition: <large separated silhouettes with white space; no montage>
```

The generated image must be checked against the matching narration before it
is numbered. A beautiful image that only matches the general mood is rejected
if its action cannot identify the scene.

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
