# 全家福视频抽帧应用设计

目标板卡：Waveshare ESP32-S3-Touch-ePaper-1.54（200x200，1bpp）
硬件基线：`epaper_154 v0.7.10`，提交 `d5221ec6356fc4d0b419157cf4198ae5c1d5295b`
硬件事实以 [EPAPER_154_APP_GUIDE.md](EPAPER_154_APP_GUIDE.md) 为准，本文只写应用设计。

## 一句话定义

从视频里抽出 6 张关键帧，全部转成 200x200、1bpp 的整帧 C 数组。上电全刷第 1 帧；右半屏触摸前进一帧；左半屏触摸回到第 1 帧；最后一帧全刷定格。

## 为什么改成视频抽帧

第一版方案是“背景 + 人物 mask + 每次叠加一个人”。这条路线验证了局刷、mask、触摸去抖和整帧 framebuffer 同步，经验有用，但最终不适合作为主应用：

- 200x200、1bpp 的信息量太低，分层叠加后人物细节容易糊。
- 单独摆人物很难在墨水屏上表达原始画面的空间关系、遮挡关系和站位距离。
- 为了减少局刷残影，背景必须很浅，反而削弱了场景感。
- 线稿/抠图素材的阈值、去噪、缩放和 mask 都会影响观感，调参成本高。

视频抽帧版把构图、遮挡和人物关系交给原视频本身，只在固件里做整帧切换。它牺牲了“逐个叠加人物”的程序感，但换来更自然、更可读的最终画面。

## 显示流程

```text
上电
 ├─ bsp_board_init() / bsp_ui_init() / bsp_touch_init()
 ├─ APP_VIDEO_FRAMES[0] 写入 framebuffer
 ├─ 全刷首帧
 ├─ bsp_ui_partial_begin()
 └─ 循环：
      右半屏触摸 -> 下一帧 -> 局刷
      左半屏触摸 -> 第 1 帧 -> 全刷并重开局刷会话
      最后一帧 -> 全刷定格
```

关键点：framebuffer 仍然是唯一真值。每一帧都是完整 5000 字节，局刷时也整帧推送，不做窗口刷新。

## 残影管理

连续局刷上限仍然保守设为：

```cpp
#define APP_PARTIAL_MAX_STREAK 6
```

插入全刷时必须成对执行 `partial_end()` → `fb_flush_full()` → `partial_begin()`，因为全刷会换掉 LUT，也会让 `0x26` 里的旧基准帧失效。

最后一帧强制全刷。断电保图保的是最后显示状态，最终画面应该用全刷的对比度收尾。

## 资产管线

工具：

```text
tools/video2c.py
```

输入是一段视频和若干 `mm:ss:ff` 时间码。当前使用的 6 帧：

```text
00:00:00  背景帧
00:01:08
00:02:14
00:04:02
00:04:17
00:05:12
```

示例命令：

```powershell
python tools/video2c.py path\to\video.mp4 00:00:00 00:01:08 00:02:14 00:04:02 00:04:17 00:05:12
```

生成：

```text
assets/generated/family_video_assets.h
assets/generated/family_video_assets.c
assets/generated/video_preview.png
assets/generated/video_preview_final.png
assets/generated/video_frames/
```

工具流程：

1. 用 `ffprobe` 读取视频帧率。
2. 把 `mm:ss:ff` 转成秒。
3. 用 `ffmpeg` 抽原始帧。
4. 中心正方形裁切，缩放到 200x200。
5. 阈值化、去孤点，打包为面板原生格式：bit 1 = white，bit 0 = black。
6. 输出 C 数组和预览 PNG。

当前已知小问题：16:9 视频做中心正方形裁切时，最终帧右侧人物略贴边。先以实机效果为准；如果需要修正，优先给 `video2c.py` 增加可配置 crop 偏移，而不是在固件里补偿。

## 交互

沿用基础测试项目已验证的左右半屏约定：

| 动作 | 效果 |
|---|---|
| 触摸右半屏 | 下一帧 |
| 触摸左半屏 | 回到背景帧 |
| 长按 PWR 3 秒 | 清屏关机 |

触摸必须去抖：`bsp_touch_read()` 给的是 raw 坐标，没有事件队列。等待按下、等待抬起、再延时 180ms，避免一次触摸被读成多次。

## 保留的诊断路径

M0/M1 不属于退役应用，继续保留：

| 模式 | 内容 | 用途 |
|---|---|---|
| `APP_MODE=1` | 静态图全刷 + mask 判读 | 验证 framebuffer、mask 绘制和全刷通路 |
| `APP_MODE=2` | 方块右移局刷验证 | 验证 partial LUT、BUSY 时长和残影阈值 |

开机按住 BOOT 仍进入 v0.7.10 交互式全测试。应用行为和硬件基线冲突时，先跑诊断再改应用。

## 版本记录：退役的第一版

第一版尝试不要再编进固件，也不要继续维护旧资产管线。它留下的经验：

- mask 和双位平面是正确抽象，未来若重新做非矩形素材仍可复用 BSP 的 `bsp_ui_draw_bitmap()`。
- “只增加黑色像素”确实更适合局刷，但真实人物/线稿很难保证这个约束。
- 小屏 1bpp 下，人物要少而大；多人全身构图会快速丢失五官和层次。
- 背景越有内容，越会和人物抢墨点；背景越浅，空间关系又越弱。
- 逐人叠加的程序结构成立，视觉结果不成立。这个结论是本次切换到视频抽帧版的主要依据。

旧产物 `family_assets.*`、`preview.png`、`preview_1x.png`、`assets/layout.json` 和 `tools/img2c.py` 已从主线移除，避免误把第一版当成当前方案继续使用。
