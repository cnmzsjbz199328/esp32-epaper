# 素材目录

当前主方案使用视频抽帧资产。

```text
tools/video2c.py                 抽帧与 1bpp 转换工具
assets/generated/family_video_assets.c
assets/generated/family_video_assets.h
assets/generated/video_preview.png
assets/generated/video_preview_final.png
assets/generated/video_frames/
```

生成示例：

```powershell
python tools/video2c.py path\to\video.mp4 00:00:00 00:01:08 00:02:14 00:04:02 00:04:17 00:05:12
```

工具会中心裁切成正方形、缩放到 200x200、阈值化并打包为面板原生格式：bit 1 = white，bit 0 = black。

第一版的 `assets/layout.json`、`tools/img2c.py`、`family_assets.*`、`preview.png` 和 `preview_1x.png` 属于已退役的“背景 + 逐人叠加”方案，不再作为当前资产入口。经验教训记录在 [docs/FAMILY_PHOTO_APP.md](../docs/FAMILY_PHOTO_APP.md)。
