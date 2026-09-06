# 暴风雨

这是面向全年龄、可偏成人向完整情节压缩改写的公版经典故事包，原作 William Shakespeare；
中文旁白为本项目原创改写，未复制现代中文译本。年龄建议为全年龄/12+。

## 当前状态（2026-09-07）

- **脚本：已冻结。** 14 场景旁白在 `story.json` 与 `ai_studio_script.md` 中逐字一致，
  相邻场景之间加入了一句过渡衔接以降低跳切感（见 `ai_studio_script.md` 全局声音指导词）：
  爱丽儿登场与开场风暴挂钩；费迪南德登场点明他来自失事船队；两场密谋（安东尼奥 / 凯列班）
  以“岛的另一头”并列；宴席幻象点明是普洛斯彼罗把几条线拉到一处。
- **场景图：已按 `scene_image_prompts.md` 整套重画。** 粗木刻画风（对齐 `odyssey_homecoming`
  的验收标准）；`build.json` 阈值 175→190；`tempest_island.fvid` 与 `tempest_island_preview.png`
  已重建。`check_story_assets.py` 通过：每帧 ink_ratio 0.22–0.33，FVID 帧同区间。05/08 等旧的
  多人 / 环境幻象帧已重构图（断桅、宴席长桌 + 展翅精灵、法杖折断 + 精灵升空）。06 幕前景大块
  留白偏含糊，可再收一版。
- **音频：待生成。** 单独选 MiniMax 人声，按编号写入 `audio/000.wav`..`013.wav`，
  录完把 `story.json` 的 `audio_status` 从 `pending` 改为就绪并重新同步。

## 构建与检查

```powershell
python tools/build_stories.py --manifest assets/stories/tempest_island/build.json --out-dir assets/stories/tempest_island
python tools/check_story_assets.py assets/stories/tempest_island
```
