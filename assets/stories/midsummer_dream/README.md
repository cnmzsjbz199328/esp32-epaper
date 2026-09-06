# 仲夏夜之梦

这是面向全年龄、可偏成人向完整情节压缩改写的公版经典故事包，原作 William Shakespeare；中文旁白为本项目原创改写，未复制现代中文译本。当前六场景内容是扩展前底稿，年龄建议为全年龄/12+。

14 张场景图由六张旧关键帧和八张补图组成，采用白底、稀疏线条和少量阴影，避免满幅黑色森林。音频暂未生成；为本故事单独选择 MiniMax 人声后，按新编号写入 `midsummer_dream/midsummer_dream/audio/`。

```powershell
python tools/build_stories.py --manifest assets/stories/midsummer_dream/build.json --out-dir assets/stories/midsummer_dream
python tools/check_story_assets.py assets/stories/midsummer_dream
```
