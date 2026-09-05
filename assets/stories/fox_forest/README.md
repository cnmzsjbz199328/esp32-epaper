# 小狐狸的月光森林

This is the first end-to-end audio story pack. The source illustrations are
kept under `source/scenes/`; the playable pack is:

```text
fox_forest.fvid
fox_forest/story.json
fox_forest/audio/000.wav ... 004.wav
```

Copy `fox_forest.fvid` and the `fox_forest/` directory into the SD card's
`/video/` directory. The firmware then resolves scene audio as:

```text
/sdcard/video/fox_forest.fvid
/sdcard/video/fox_forest/audio/000.wav
```

Regenerate the visual pack with:

```powershell
python tools/build_stories.py --manifest assets/stories/fox_forest/build.json --out-dir assets/stories/fox_forest
```

The playable WAV files are generated with Google AI Studio's Gemini 3.1 Flash
TTS Preview using the Achernar voice, then normalized to mono 16 kHz, 16-bit
PCM WAV for the board audio path. The original 24 kHz downloads are kept under
`source/audio_ai_studio/`. The ROM demo embeds all scene audio in compressed
form so every scene remains complete and audible even without an SD card.
