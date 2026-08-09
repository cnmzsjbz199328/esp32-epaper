# 素材目录

```text
assets/src/          你放原图（已被 .gitignore 排除，不进 git）
  bg.png
  person_01.jpg … person_05.jpg
assets/layout.json   构图真值：位置、缩放、出场顺序、抠图参数
assets/generated/    tools/img2c.py 的产物：family_assets.c/.h + preview.png
```

改 `layout.json` → 重跑 `python tools/img2c.py` → 看 `assets/generated/preview.png`。
这是整个流程里迭代最快的环节，不用烧板子。

## 背景的硬约束

5 个人在 200×200 上占满全屏：后排头顶 y=24，前排脚底 y=192，左右 x=8..192。
**人物落点区域几乎等于整屏**，所以背景能安全放深色的地方只有最上面 24px 和最下面 8px。

这不是"背景要浅一点"的建议，是"背景基本必须是白的"。理由在
[docs/FAMILY_PHOTO_APP.md](../docs/FAMILY_PHOTO_APP.md) 的「背景约束」一节：局刷 LUT
做黑→白的残影明显重于白→黑，人物身上的浅色区域落在深色背景上，每次局刷都要擦掉一片黑。

背景全白时，整个流程退化成纯加黑 —— 画质和残影同时取到最优。所以这里的目标不是
"生成一张好看的图"，而是"生成一张几乎不存在的图"。

## 生成提示词

三选一，都是 1:1。生成后放到 `assets/src/bg.png`。

### A. 极简线框（推荐）

```text
Minimalist line art on a pure white background, square 1:1 composition.
A single thin black horizon line across the lower third, and a simple
thin-line sun outline in the upper right corner. Nothing else.
Pure white (#FFFFFF) everywhere else, no gradients, no shading, no texture,
no grey tones. Flat 2D, black ink outline only, 2px stroke weight,
coloring-book style. High key, maximum contrast.
```

推荐它的理由：只有线条和白，阈值化之后不会有任何中间灰，人物压上去是纯粹的加黑。

### B. 浅色天空

```text
A very pale, almost white sky with two faint thin-outlined clouds near the
top edge, square 1:1 composition. Extremely high key, overexposed look.
The entire lower two thirds is plain white with nothing in it.
No gradients, no shading, no texture, no dark areas anywhere.
Flat minimal illustration, black thin outlines on white.
```

比 A 多一点氛围，但要检查云不要飘到 y=24 以下。

### C. 装饰边框

```text
A decorative thin black line border frame on a pure white background,
square 1:1 composition. Simple geometric corner ornaments in the four
corners only, each ornament no larger than one eighth of the image.
The entire center of the image is empty pure white.
No gradients, no shading, no texture, flat 2D black ink line art.
```

边框最不容易和人物打架，因为装饰全在四角，人物落点是空的。

### 生成后自查三条

1. 把图缩到 200×200，中间那块 `x=8..192, y=24..192` 是不是基本全白？
2. 有没有渐变或大面积灰？有的话阈值化后会变成一片噪点，重新生成。
3. 线条够不够粗？200×200 上 1px 的细线抖动后会断断续续，宁可粗一点。

第 1 条不过就直接重生成，不要指望在工具里提亮补救 —— 提亮会把线条一起吃掉。

## 绿幕拍摄要点

- 光要匀，绿布上不能有明显阴影
- 人和绿布拉开距离，避免绿光反射到脸和衣服上形成绿边
- 不要穿绿色，也不要穿浅色反光衣物
- 后排三人、前排两人，前排的人拍的时候可以稍微靠前一点，构图更自然

拍完直接丢进 `assets/src/`，文件名对应 `layout.json` 里的 `src` 字段。
抠图参数不用你调，先跑默认值看预览，绿边没抠干净再改 `chroma_key`。
