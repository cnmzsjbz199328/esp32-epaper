# Asset Pipeline

正式故事不是固件资源。每个可同步包都以 `story.json` 为入口，并把
FVID、音频和可选图片放在同一个目录中：

```text
assets/stories/<story_id>/
  story.json
  <story_id>.fvid
  audio/000.wav
```

`story.json` 中的 `fvid`、`image`、`audio` 和 `opening_audio` 必须是包内
相对路径，不能包含 `..`。同步前运行：

```powershell
python tools/check_story_library.py assets/stories
python tools/device_sync.py sync-story .\assets\stories\fox_forest
python tools/device_sync.py sync-library .\assets\stories
```

`tools/convert_family_video_c_to_fvid.py` 用于一次性迁移旧的 C 数组；它
保留原始帧顺序和刷新策略，并输出 FVID 的 SHA-256。

旧 C 数组生成脚本只用于导出迁移输入，不在 PlatformIO 中编译；正式故事
必须通过 FVID 故事包同步到 TF 卡。
