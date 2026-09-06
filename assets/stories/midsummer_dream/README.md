# 仲夏夜之梦

这是面向全年龄、可偏成人向完整情节压缩改写的公版经典故事包，原作 William Shakespeare；
中文旁白为本项目原创改写，未复制现代中文译本。年龄建议为全年龄/12+。

## 当前状态（2026-09-07）

- **脚本：已冻结。** 14 场景旁白在 `story.json` 与 `ai_studio_script.md` 中逐字一致，
  相邻场景之间加入了一句过渡衔接以降低跳切感（见 `ai_studio_script.md` 全局声音指导词）。
  同时修正了两处连续性问题：9/10 幕不再语义重复；13 幕不再说“离开森林”（12 幕已回到雅典）。
- **场景图：已按 `scene_image_prompts.md` 整套重画。** 粗木刻画风（对齐 `odyssey_homecoming`
  的验收标准）；`build.json` 阈值 175→190；`midsummer_dream.fvid` 与 `midsummer_dream_preview.png`
  已重建。`check_story_assets.py` 通过：每帧 ink_ratio 0.20–0.30，FVID 帧同区间。0/6/10/11 幕
  仍偏细线，改用黑块为主再收一遍会更稳，但已过验收闸。
- **音频：待生成。** 单独选 MiniMax 人声，按编号写入 `audio/000.wav`..`013.wav`，
  录完把 `story.json` 的 `audio_status` 从 `pending` 改为就绪并重新同步。

## 构建与检查

```powershell
python tools/build_stories.py --manifest assets/stories/midsummer_dream/build.json --out-dir assets/stories/midsummer_dream
python tools/check_story_assets.py assets/stories/midsummer_dream
```
