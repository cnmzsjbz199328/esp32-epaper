# 暴风雨 —— 场景图重画提示词

> **状态：已执行（2026-09-07）。** 14 张 `source/scenes/*.png` 已按本表重画，`build.json`
> 阈值提到 190，`tempest_island.fvid` / `_preview.png` 已重建，`check_story_assets.py` 通过。
> 以下留作画面意图记录；06 幕前景大留白偏含糊，如再收一版按同一风格块重画该帧即可。

上一轮 `odyssey_homecoming` 从细铅笔线重画为粗木刻后通过验收（墨点 ~30%、粗描边、
≤3 人、每幕 1–2 个超大识别道具）。本包 14 张 `source/scenes/*.png` 仍是旧的填色书
细线画风（墨点 ~10%）。以普洛斯彼罗为单一大主体的帧（00/09/11/13）尚可，多人帧和
"环境幻象"帧（05/08 等）在 200x200 1-bit 面板上失败。按下表处理：`[风格]` 只换画风
加密，`[内容+风格]` 必须重构图。

流程：AI Studio 逐张生成 → 覆盖 `source/scenes/000.png`..`013.png`（200x200 或更大）
→ `python tools/build_stories.py --manifest assets/stories/tempest_island/build.json --out-dir assets/stories/tempest_island`
→ `python tools/check_story_assets.py assets/stories/tempest_island`（期望 ink_ratio 0.20–0.34）
→ 看 `tempest_island_preview.png` 缩略图，任何一幕"不看标题认不出动作"就退回重画那一张。

音频不动：`story.json` 的 `audio_status:"pending"` / `voice:"待生成…"` 保持，等图定稿后再录。

## 全局风格块（每条提示词前缀）

```text
200x200 monochrome e-paper illustration, bold relief-print / woodcut engraving style,
very thick uniform black contours (minimum stroke ~3 px at this size), large solid
black masses for hair, cloaks and cast shadow, strong readable outer silhouettes.
At most three figures with one clearly dominant and larger. One or two oversized
identifying props. Sparse deliberate cross-hatching only, no fine pencil hatching,
no coloring-book thin outlines, no gradients, no photorealism. Mostly white paper
background with generous negative space; no solid black background, no full-bleed
dark sky. No text, no border, no watermark. Target 22%-32% black pixels after
1-bit thresholding. Designed to survive 1-bit thresholding on a 200x200 e-ink display.
```

## 逐场景

### 000 海上风暴 — `[风格·保留构图]`
- Scene meaning: 风暴把船队推向未知海岸。
- Visible action: 帆船被巨浪高高抛起、桅杆倾斜、雨线斜扫。
- Main subject: 帆船 + 礁石小岛。
- Identifying props: 卷浪、断裂的帆、斜雨。

### 001 被流放的过去 — `[内容+风格]`
- Scene meaning: 普洛斯彼罗讲流放往事：米兰公爵位被夺、父女被逐上小船。
- Visible action: 普洛斯彼罗指着脚边一顶倾倒的公爵冠，米兰达听着；远处海面一叶小舟。
- Main subject: 普洛斯彼罗持书（大）+ 米兰达。
- Identifying props: 倾倒的公爵冠、翻开的魔法书、远处漂流小舟。

### 002 爱丽儿的契约 — `[内容+风格]`
- Scene meaning: 爱丽儿执行魔法却索要自己的自由。
- Visible action: 爱丽儿悬在半空伸手听命，一侧是曾困住她的裂开树干。
- Main subject: 爱丽儿（单一主体、放大）。
- Identifying props: 裂开的大树干、缠在腕上的一段枝/绳。

### 003 凯列班与岛屿 — `[风格]`
- Scene meaning: 凯列班拒绝被支配，岛上的所有权冲突。
- Visible action: 凯列班半跪着甩开普洛斯彼罗指令的手、怒视。
- Main subject: 凯列班（前景、大）+ 普洛斯彼罗（持杖）。
- Identifying props: 背筐的木柴、法杖、岛石。

### 004 米兰达与费迪南德 — `[内容+风格]`
- Scene meaning: 二人隔劳作相望，爱情成为政治和解的可能。
- Visible action: 费迪南德扛着一根大原木，米兰达越过柴堆伸手去触他。
- Main subject: 费迪南德（扛木）+ 米兰达，二人对视。
- Identifying props: 大原木、成堆木柴。

### 005 失散的幸存者 — `[内容+风格]`
- Scene meaning: 幸存者上岛分裂，失王子后恐惧转成猜疑。
- Visible action: 几名幸存者背对彼此散开、中间留出一处空地，一人跪地掩面。
- Main subject: 4 人散开，跪地掩面者为焦点，中心空缺。
- Identifying props: 冲上岸的断桅。

### 006 再次密谋 — `[风格]`
- Scene meaning: 安东尼奥与塞巴斯蒂安再次密谋夺权。
- Visible action: 两人俯身贴近耳语，其中一人半抽出一把匕首（只画轮廓）。
- Main subject: 两名密谋者（半身、大）。
- Identifying props: 匕首轮廓、遮挡的大岩石。

### 007 荒诞的夺岛计划 — `[风格]`
- Scene meaning: 凯列班与两个醉汉荒诞地想夺岛。
- Visible action: 三人搂肩踉跄，一人高举酒壶，凯列班头顶扣着歪斜的木桶当"王冠"。
- Main subject: 3 人一组。
- Identifying props: 酒壶、当王冠的木桶、破斗篷。

### 008 消失的宴席 — `[内容+风格]`
- Scene meaning: 魔法宴席让众人直面罪责，食物凭空消失。
- Visible action: 一张摆满食物的长桌前，几人惊恐后仰，一个展翅怪鸟精灵扑过桌面卷走餐盘。
- Main subject: 长桌（主体道具）+ 3 名惊退的人 + 鸟形精灵。
- Identifying props: 长条宴席桌、翻飞的餐盘、展翅精灵。

### 009 复仇的尽头 — `[风格]`
- Scene meaning: 普洛斯彼罗发现复仇无法挽回被夺走的岁月。
- Visible action: 普洛斯彼罗独自低头站着，一手握杖一手按着摊开的魔法书、迟疑。
- Main subject: 普洛斯彼罗（单一主体、放大）。
- Identifying props: 法杖、摊开的大魔法书。

### 010 说出旧事 — `[内容+风格]`
- Scene meaning: 他让旧友公开承认过去，自己也承认权力造成的新伤害。
- Visible action: 普洛斯彼罗摊手正对三名低头的旧友、当众对质。
- Main subject: 普洛斯彼罗（大）+ 3 名成排低头的旧友。
- Identifying props: 法袍、法杖、脚下画的魔法圈。

### 011 选择宽恕 — `[风格]`
- Scene meaning: 普洛斯彼罗选择宽恕而非报复，释放被困众人。
- Visible action: 普洛斯彼罗双手张开、掌心向上，法杖已放在脚边地上。
- Main subject: 普洛斯彼罗（单一主体）。
- Identifying props: 放在地上的法杖、松开的绳。

### 012 放下魔法 — `[内容+风格]`
- Scene meaning: 他折断法杖并释放爱丽儿，未来不再由魔法控制。
- Visible action: 普洛斯彼罗双手把法杖在膝上折成两截，爱丽儿从碎片上方升空。
- Main subject: 普洛斯彼罗（折杖）+ 爱丽儿（升空）。
- Identifying props: 断成两截的法杖、飞离的爱丽儿。

### 013 离开孤岛 — `[风格·保留构图]`
- Scene meaning: 众人离岛，故事以自由收束。
- Visible action: 一艘帆船驶离，孤岛留在船尾，海面平静。
- Main subject: 帆船 + 孤岛。
- Identifying props: 鼓起的帆、平静水线、岛的剪影。
