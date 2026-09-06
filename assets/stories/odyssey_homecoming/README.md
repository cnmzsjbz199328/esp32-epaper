# 奥德赛：归航

这是面向全年龄、可偏成人向完整情节压缩改写的公版经典故事包，原作归于 Homer；中文旁白为本项目原创改写，未复制现代中文译本。年龄建议为全年龄/12+。

当前包包含 14 张 200x200 黑白场景图、14 场景脚本、FVID、14 段场景音频和独立开幕音频。场景音频按 Downloads 中的实际下载时间映射到 000–013；开幕音频单独使用 `audio/opening.wav`，旧六场景音频不参与本包。闭幕只覆盖最后一幕音频的最后 3 秒，不增加音频段。扩展场景表见 `docs/STORY_LIBRARY_EXPANSION_SCENE_MAP.md`。

构建与检查：

```powershell
python tools/build_stories.py --manifest assets/stories/odyssey_homecoming/build.json --out-dir assets/stories/odyssey_homecoming
python tools/check_story_assets.py assets/stories/odyssey_homecoming
```

最终音频由 MiniMax `speech-2.8-hd` 的 `Tender CEO` 中文普通话音色生成。14 个场景音频和 1 个开幕音频的 32 kHz 原件保存在 `source/audio_minimax/expanded_14_scene/`，来源文件名、下载时间和时长见其中的 `manifest.json`；固件播放文件是 `odyssey_homecoming/audio/opening.wav` 及 `000.wav`–`013.wav`，格式为 16 kHz/16-bit/mono WAV。

固件行为：进入本故事后先显示 `THE ODYSSEY / HOMER` 并播放 `opening.wav`；开幕结束后开始 000。最后一幕音频剩余约 3 秒时显示 `OVER / WRITER: TOM`，闭幕不使用额外音频。
