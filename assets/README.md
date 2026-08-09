# Asset Pipeline

Current firmware uses a staged story asset pack:

1. Six single-character frames generated from green-screen source images in
   `assets/src/`.
2. The existing staged group reveal frames from `assets/generated/video_frames/`.

The firmware still consumes the same generated interface:

```text
assets/generated/family_video_assets.c
assets/generated/family_video_assets.h
```

Generate the current pack with:

```powershell
python tools/solo_story2c.py
```

Generated inspection files:

```text
assets/generated/video_preview.png
assets/generated/video_preview_final.png
assets/generated/story_frames/
```

`story_frames/` and `video_frames/` are intermediate PNGs. The C arrays are the
source used by PlatformIO.

The older pure video workflow is still available:

```powershell
python tools/video2c.py path\to\video.mp4 00:00:00 00:01:08 00:02:14 00:04:02 00:04:17 00:05:12
```

That command overwrites `family_video_assets.*` with video-only frames, so rerun
`tools/solo_story2c.py` before building the single-character prelude version.
