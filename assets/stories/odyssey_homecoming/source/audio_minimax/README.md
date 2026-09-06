# MiniMax source audio

Generated in MiniMax Audio using model `speech-2.8-hd`, voice `Tender CEO`,
Chinese (Mandarin). The downloaded 32 kHz mono 16-bit files are retained here;
the previous six-scene board-ready copies are retained under `legacy_six_scene/` and were
converted with:

```powershell
ffmpeg -i source.wav -ar 16000 -ac 1 -sample_fmt s16 final.wav
```

No background music or sound tags were used in this pass. Natural pauses came
from Chinese punctuation. The old six-scene source files remain at the top
level for provenance, while the expanded 14-scene source files are in
`expanded_14_scene/`, ordered by download time. The corresponding board-ready
files are in `odyssey_homecoming/audio/`.
