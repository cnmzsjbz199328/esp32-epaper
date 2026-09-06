# 仲夏夜之梦 —— 场景图重画提示词

> **状态：已执行（2026-09-07）。** 14 张 `source/scenes/*.png` 已按本表重画，`build.json`
> 阈值提到 190，`midsummer_dream.fvid` / `_preview.png` 已重建，`check_story_assets.py` 通过。
> 以下留作画面意图记录；0/6/10/11 幕仍偏细线，如再收一版按同一风格块把这几帧改成黑块为主。

上一轮 `odyssey_homecoming` 从细铅笔线重画为粗木刻后通过验收（墨点 ~30%、粗描边、
≤3 人、每幕 1–2 个超大识别道具）。本包 14 张 `source/scenes/*.png` 仍是旧的填色书
细线画风（墨点 ~10%），在 200x200 1-bit 面板上脸和小人物会碎成噪点。按下表整套重画。

流程：AI Studio 逐张生成 → 覆盖 `source/scenes/000.png`..`013.png`（200x200 或更大，
构建时会缩放）→ `python tools/build_stories.py --manifest assets/stories/midsummer_dream/build.json --out-dir assets/stories/midsummer_dream`
→ `python tools/check_story_assets.py assets/stories/midsummer_dream`（期望 ink_ratio 0.20–0.34）
→ 看 `midsummer_dream_preview.png` 缩略图，任何一幕"不看标题认不出动作"就退回重画那一张。

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

`[内容+风格]` = 现在与脚本不符，必须重构图；`[风格]` = 构图可留，只换画风加密。

### 000 雅典的法令 — `[内容+风格]`
- Scene meaning: 雅典法令逼赫米娅在服从与爱情间选择。
- Visible action: 父亲举起一卷大法令指着赫米娅，她侧身背向拉山德抗拒。
- Main subject: 三人，赫米娅居中。
- Identifying props: 超大法令卷轴、雅典石柱门。

### 001 走进森林 — `[风格]`
- Scene meaning: 二人趁夜逃入森林夺回自己的选择。
- Visible action: 两人手牵手大步跨过林缘，回头望身后城墙。
- Main subject: 拉山德 + 赫米娅，在前景放大。
- Identifying props: 半轮明月、身后城门剪影、粗树干。

### 002 精灵王后的争执 — `[内容+风格]`
- Scene meaning: 奥伯龙与提泰妮娅为权力与占有欲争执。
- Visible action: 两位戴冠精灵正面对峙、各伸一手，中间站着被争夺的男孩。
- Main subject: 奥伯龙、提泰妮娅（同等大小、都戴尖冠）。
- Identifying props: 两顶尖冠、头顶一轮满月。

### 003 一朵有魔法的花 — `[内容+风格]`
- Scene meaning: 奥伯龙把花汁任务交给帕克。
- Visible action: 奥伯龙俯身把一朵大花递向帕克，帕克张翅伸手去接。
- Main subject: 奥伯龙（大）+ 帕克（小、带翅）。
- Identifying props: 一朵超大三瓣花、月牙。

### 004 工匠的戏剧 — `[风格]`
- Scene meaning: 工匠在林中笨拙排练婚礼戏。
- Visible action: 一名工匠站在木板台上摊开台词纸，两名工匠捧简陋道具发笑。
- Main subject: 3 名工匠，台上者为主体。
- Identifying props: 木板搭的小台、字纸、木槌。

### 005 错落的爱意 — `[风格]`
- Scene meaning: 魔法落错眼睛，恋人错位追逐。
- Visible action: 两名男子同时追向一名女子，另一女子在后奔跑，姿态夸张。
- Main subject: 四名恋人，追逐者为前景主体。
- Identifying props: 粗树干框景（靠奔跑姿态，不要额外道具）。

### 006 波顿的变形 — `[内容+风格]`
- Scene meaning: 波顿被变驴头，提泰妮娅痴迷。
- Visible action: 提泰妮娅跪起身给坐着的驴头波顿戴花环。
- Main subject: 驴头波顿（坐、大）+ 提泰妮娅（戴冠）。
- Identifying props: 驴头、花环、几朵大花。

### 007 误会升级 — `[内容+风格]`
- Scene meaning: 误会升级成四人互相伤害。
- Visible action: 四人两两互相推搡指责，手臂交叉指向对方。
- Main subject: 4 人交错对峙，前一对为主体。
- Identifying props: 粗树干框景。

### 008 收回错误 — `[风格]`
- Scene meaning: 奥伯龙醒悟失控，命帕克收回魔法。
- Visible action: 奥伯龙一手按帕克肩、一手指向地上熟睡的一对恋人下令。
- Main subject: 奥伯龙（大）+ 帕克。
- Identifying props: 尖冠、帕克翅膀、地上花朵。

### 009 黎明前的辨认 — `[内容+风格]`
- Scene meaning: 天快亮，四个年轻人先后醒来，谁都不敢先开口。
- Visible action: 一对恋人从地上撑身坐起，彼此对视又别开目光。
- Main subject: 一对恋人放大占据画面，远处第二对模糊。
- Identifying props: 铺地落叶、林缘一线晨光。

### 010 梦与伤痕 — `[风格]`
- Scene meaning: 恋人们相扶着走出森林，回望昨夜像梦又像伤痕。
- Visible action: 两对恋人相扶着走向林缘，面向树间升起的太阳。
- Main subject: 走在前面的一对恋人（前景主体）。
- Identifying props: 树间半升的太阳、长影子。

### 011 公爵的选择 — `[内容+风格]`
- Scene meaning: 忒修斯发现恋人，选择接纳而非惩罚。
- Visible action: 高大的公爵抬手示意恋人起身，另一手指向城的方向。
- Main subject: 忒修斯（戴桂冠、明显更高大）+ 两对跪起的恋人。
- Identifying props: 桂冠、公爵长杖、远处城郭剪影。

### 012 婚礼与舞台 — `[内容+风格]`
- Scene meaning: 三对婚礼，工匠戏成为镜子。
- Visible action: 前景一对新人并肩，身后小台上两名工匠举着"砖纹墙板"和"狮子面具"。
- Main subject: 一对新人（主体）+ 2 名工匠在小台上。
- Identifying props: 小舞台、砖纹墙板、狮子面具（不写字，用形状表现）。

### 013 梦醒之后 — `[内容+风格]`
- Scene meaning: 婚礼散场，宾客离去，帕克独自留在台上致收场白。
- Visible action: 前景帕克独自站在空舞台上，转向画外抬手；身后宾客三三两两走出厅堂。
- Main subject: 帕克（前景、带翅、大）+ 身后成列离场的小人物。
- Identifying props: 空舞台、身后离场的宾客、帕克的翅膀。
- 注意：这一幕已不在森林里（12 幕已回到雅典），不要画树林/小路/城郭剪影。
