# 暴风雨

这是面向全年龄、可偏成人向完整情节压缩改写的公版经典故事包，原作 William Shakespeare；中文旁白为本项目原创改写，未复制现代中文译本。当前六场景内容是扩展前底稿，年龄建议为全年龄/12+。

14 张场景图由六张旧关键帧和八张补图组成，遵循同编号契约；音频暂未生成。为本故事单独选择 MiniMax 人声后，再把按新编号生成的 WAV 写入同名目录并复制到 SD 卡 `/video/`：

```powershell
python tools/build_stories.py --manifest assets/stories/tempest_island/build.json --out-dir assets/stories/tempest_island
python tools/check_story_assets.py assets/stories/tempest_island
```
