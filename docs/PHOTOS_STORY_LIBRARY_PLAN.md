# PHOTOS 故事库计划：TF 卡多故事 + 项目列表选播

> 制定日期：2026-08-29
> 目标：`family_video`（PHOTOS）应用当前只会打开 TF 卡上写死的一个
> `/sdcard/video/default.fvid`，按帧顺序播放。改成：卡上放**多个**独立故事文件，
> 应用进入时先显示**故事列表**，选中一个再进入播放视图。
> 承接 `docs/APP_SHELL_PLAN.md` §12 里被推迟的 "故事选择 UI"。

## 现状（v0.9.0 已实现的部分）

| 件 | 状态 |
|---|---|
| `tools/video2c.py --format fvid` | ✅ 输出 `FVID` 容器：头 16B `<4sBBHHH4s`(magic/ver/flags/w/h/count/reserved) + 每帧 `hint(1)+pad(3)+data(5000)` = 5004B |
| `src/apps/family_video/frame_source_sd.cpp` | ⚠️ 只 `open()` 单文件：`/sdcard/video/default.fvid` 或 `family_video.fvid`；校验头；顺序播放 |
| `frame_source_rom.cpp` | ✅ 编译进的 `family_video_assets.c` 兜底 |
| `app_family_video.cpp` | 进入→打开源→显示帧 0→tap/键 左右/首末 翻页。**没有列表** |
| SD 挂载 | `SD_MMC.begin("/sdcard", true, false)` 1-bit，路径 `/sdcard/video/` 正确 |
| FVID SD 路径 | v0.9.0 CHANGELOG 标注 "Manual validation still pending" —— 实机没验过 |

**缺的就是：卡上枚举多个 `.fvid` + 一个列表视图。**

---

## 1. 卡上布局与工具流程

```
/sdcard/video/
  solo.fvid        单人秀（单人前奏）
  family-1.fvid    全家福 · 原始视频抽帧
  family-2.fvid    全家福 · 分层合成版
```

- 一个 `.fvid` = 一个故事。**文件名一律用 ASCII**（小写 + 连字符），避免 FAT 短名 / UCS-2 读取错误。
  文件名去扩展名即故事名，播放列表里大写显示：`family-1.fvid` → `FAMILY-1`。
- **不做中文故事名。** FVID 头保持 v1，`reserved` 4B 不动；不加内嵌 title 字段。名字只来自文件名。

### 种子内容：把现有的"对比示例"拆成 3 个故事

当前 `assets/generated/family_video_assets.c`（由 `story_layout2c.py` / `solo_video_story_compare` 链路生成）其实是**一条拼接序列**："单人秀 → 原始视频抽帧 → 分层合成 story layout"。用户确认这就是 3 个独立项目：

| 故事文件 | 来源帧目录 | 生成工具 |
|---|---|---|
| `solo.fvid` | `assets/generated/story_frames/` | `solo_story2c.py` |
| `family-1.fvid` | `assets/generated/video_frames/` | `video2c.py` |
| `family-2.fvid` | `assets/generated/story_composed/` | `story_layout2c.py`（读 `assets/story_layout.json`） |

- 三个工具都产出同样的 "panel-native 帧列表 + hints"，只是来源不同。把 `video2c.py` 里的 `emit_fvid()` 抽成共享 helper（如 `tools/_fvid.py`），给 `solo_story2c.py` 和 `story_layout2c.py` 各加一个 `--format fvid` / `--fvid-out` 选项，各自输出一个 `.fvid`。
- **可选小工具** `tools/build_stories.py`：读一个 manifest 列出这 3 个（工具、参数、输出名），一把生成到 `assets/generated/stories/`，再拷进卡的 `/video/`。非必需，先手动跑三次也行。
- ROM 兜底 `family_video_assets.c` 保持现状（还是那条拼接序列），只作无卡时的 fallback，不必拆。

---

## 2. `frame_source_sd.cpp` 重构：枚举 + 按路径打开

现在是"init 时打开唯一文件"的单例，改成两段：

```c
// 枚举（进入 library 时调一次，或手动刷新）
typedef struct {
    char path[64];      // "/sdcard/video/birthday.fvid"
    char name[24];      // "BIRTHDAY"
    uint16_t frames;    // 头里的 count
} fvid_entry_t;

int  frame_source_sd_scan(fvid_entry_t* out, int max);   // 返回有效故事数；每个都开一下校验头
bool frame_source_sd_open(const char* path);             // 打开指定故事，成为当前 SD 源
```

- `scan()`：`SD_MMC.open("/sdcard/video")` → `openNextFile()` 迭代，筛 `.fvid` 后缀，逐个 `open + 头校验`（magic/ver/w/h/`size == 16 + count*5004`），合法的填进 `out[]`。上限 `SD_STORY_MAX = 16`。按文件名字典序排。
- `open(path)`：关掉旧 `s_file`，打开新文件，重跑头校验，置 `s_count` / `s_valid`。`sd_count/sd_hint/sd_load` 逻辑不变（已经是按 `s_file` + 偏移量算的）。
- `frame_source_init()` 不再自动打开文件；由应用决定先 `scan` 还是直接 `open`。
- 内存：`fvid_entry_t` ~92B × 16 ≈ 1.5KB 静态，可接受。

---

## 3. 应用：两视图（对齐 usage 应用的 VIEW_OVERVIEW/VIEW_FOCUS 模式）

```c
enum view_t { VIEW_LIBRARY, VIEW_PLAYER };
```

### 进入 `app_family_video_on_enter()`

```
scan SD:
  ├─ ≥1 个合法故事  → VIEW_LIBRARY，画列表，选中第 0 项
  ├─ 0 个 / 无卡     → frame_source_use_rom()，VIEW_PLAYER，直接播 ROM（等于现在行为）
```

### VIEW_LIBRARY（列表，不是图标）

- 复用"列表屏"排版（和 settings 应用一致）：状态栏下方每行 `名字  帧数`，一屏 ~6 行，多于 6 个分页（`PgUp/PgDn` 或 tap 上下边缘）。
- 选中项高亮。
- **tap 某行 / 按 enter** → `frame_source_sd_open(entry.path)` → 切 `VIEW_PLAYER` → `show_frame(0, true)`。
- **上下移动选中** → partial refresh（只重画旧行+新行+滚动条），照 launcher 的选中框做法。
- **BACK / ESC / HOME** → `shell_show_launcher()`。
- 空列表兜底文案：`NO STORIES ON SD`（其实这条走不到，因为 0 故事时进的是 ROM 播放）。

### VIEW_PLAYER（基本是现在的 `show_frame` 逻辑）

- 帧 0..count-1，tap 左右 = 上下帧，长按左右 = 首/末帧，BLE `left`/`right` 同。
- **BACK / ESC** → 回 `VIEW_LIBRARY`（**不是**直接回 launcher），重新画列表。
  - 若当前是 ROM 源（无卡进来的）→ BACK 直接回 launcher（没有列表可回）。
- **HOME**（BOOT 长按）→ 始终回 launcher。
- 播放中 `s_source->load()` 失败 → 现有逻辑：切 ROM 重试；ROM 也失败则回 `VIEW_LIBRARY`（或 launcher）。

---

## 4. 刷新 session 纪律

- `VIEW_LIBRARY ⇄ VIEW_PLAYER` 是**换屏**，必须走 shell 的唯一合法交接：
  `partial_end → fb_clear → (状态栏) → fb_flush_full → partial_begin`，即调 `shell_full_refresh_current()`（`show_frame(0, true)` 已经这么做）。进 library 时同样先 full refresh 再画列表。
- library 内部移动选中：partial refresh。
- player 内部翻帧：现有 `APP_PARTIAL_MAX_STREAK` / hint / backward 判定不动。
- 换故事（library→player）天然是 full refresh 起步，`s_streak` 归零。

---

## 5. 里程碑

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **P-1** SD 枚举 | `frame_source_sd_scan()` + `frame_source_sd_open()`。应用仍单故事（open 第一个 scan 结果），无列表 UI。 | 卡上放 2 个 `.fvid`，串口打印 `scan found N: birthday(6) grandpa-80(4) ...`，播放第一个正常；拔卡回 ROM |
| **P-2** 列表视图 | `VIEW_LIBRARY` 渲染 + 选中移动 + 进入播放。BACK 在 player 回 library。 | 列表能翻页、选中、进故事；player BACK 回列表；BOOT 长按回 launcher；换故事无残影（busy_ms 看 M1 标准） |
| **P-3** 边界收口 | 无卡 / 0 故事 / 头损坏 / 播放中读失败 / >16 故事 的兜底路径全部串口可见；`docs/FAMILY_PHOTO_APP.md` 补"故事库"一节 | 逐个制造这些情况，行为符合 §3 描述，无死锁无 panic |
| **P-0** 种子资产 | `emit_fvid` 抽成共享 helper，`solo_story2c.py` / `story_layout2c.py` 加 `--format fvid`；生成 `solo/family-1/family-2` 三个 `.fvid` | 三个文件头校验通过、`size == 16 + count*5004`；串口 `scan found 3` |

`FVID v2 内嵌 title` **不做** —— 中文名靠外部（比如以后加一个 `stories.txt` 映射表）再说，不进固件。

每个里程碑 → `boards/epaper_154/CHANGELOG.md` 一条带 Validation 段。不涉及新 `BSP_CAP_*`，不用 bump contract。建议顺序：**P-0 → P-1 → P-2 → P-3**。

---

## 6. 风险与注意

1. **SD 实机没验过**（v0.9.0 遗留）。P-1 第一件事就是确认 `/sdcard/video/` 能枚举、`openNextFile()` 在 1-bit SD_MMC 下正常、`.fvid` 5KB/帧读取延迟可接受。
2. **`openNextFile()` 目录句柄**：迭代期间别 `open` 别的文件，先把路径全收集完再逐个校验，避免句柄打架。
3. **枚举耗时**：16 个文件每个 open+读 16B 头，几十 ms，放在 `on_enter`（一次性）可接受；不要每帧 tick 都扫。
4. **列表内存**：`fvid_entry_t[16]` 静态，别用 `String`/堆。
5. **BACK 语义分叉**：SD 故事 player → 回 library；ROM player → 回 launcher。用一个 `s_from_library` 标志区分，别用"当前源是不是 ROM"来判（ROM 也可能是从 library 里一个坏文件回退来的）。
6. **换故事时 `s_frame` / `s_streak` 复位**：`frame_source_sd_open()` 成功后必须 `s_frame = 0; s_streak = 0;`，否则新故事从旧帧号起步越界。
7. **文件名全程 ASCII**：`.fvid` 文件名只用小写字母/数字/连字符。非 ASCII 在 FAT 上是短名/UCS-2，`openNextFile().name()` 行为不定 —— 直接不碰。
8. **故事列表是列表不是图标**：主页仍是 2×2 图标（PHOTOS 是其中一格）；进 PHOTOS 之后才是这个竖排列表。两者不冲突。
9. **TF 卡实机热重启**：`bsp_sd_init()` 在冷启动才可靠挂卡；调试时若串口 `scan` 打印不出卡，需要插拔 USB 让板子硬复位一次再看（`SD_MMC.begin` 对上电时序敏感）。P-1 验收时按这个流程走。

---

## 7. 待确认

- 一屏列表行数：6 行（每行 ~24px + 状态栏）够吗？还是要更紧？
- 三个故事文件名：`solo` / `family-1` / `family-2` 可以吗？还是想要更具体的名（如 `solo-prelude` / `family-video` / `family-staged`）？
- `tools/build_stories.py` 批量工具要不要现在做，还是先手动跑三次？
- 无卡时：直接静默播 ROM（推荐），还是列表里显示一条 `BUILT-IN` 项？
