# ESP32 ePaper Application

Waveshare ESP32-S3-Touch-ePaper-1.54（200x200 墨水屏）上的全家福视频抽帧应用。

## 当前主方案

**单人前置 + 群像浮现全家福**：先展示单人物帧，再进入群像逐帧浮现。全部素材编译成 200x200、1bpp 的整帧数组。上电先全刷第 1 帧；触摸右半屏前进一帧，触摸左半屏后退一帧；最后一帧用全刷定格。

第一版“背景 + 逐个叠加人物”的应用已经退役。它是有价值的探索，但实机观感偏糊，而且在 200x200、1bpp 墨水屏上很难清楚表达人物之间的空间关系。视频抽帧版保留了原视频的构图和遮挡关系，最终效果更稳定。

## 文档

- [docs/EPAPER_154_APP_GUIDE.md](docs/EPAPER_154_APP_GUIDE.md) — 硬件事实、已验证外设、局刷资料、BSP 缺口
- [docs/FAMILY_PHOTO_APP.md](docs/FAMILY_PHOTO_APP.md) — 当前视频抽帧应用设计、资产管线、退役方案经验教训

## 硬件基线

来自配套 bring-up 项目：

```text
C:\Users\tj169\Flinders\work\Learning\esp32_test
```

参考版本 `epaper_154 v0.7.10`，提交 `d5221ec6356fc4d0b419157cf4198ae5c1d5295b`，日志 `baseline/epaper_154_v0.7.10_final.log`（16/16 通过）。

板级代码请复用该项目的 `boards/epaper_154/`，不要按网上资料重新硬编码 GPIO。

## 构建和烧录

默认 `APP_MODE` 已经是视频抽帧应用：

```powershell
pio run -e epaper_154
pio device list                                    # 先确认端口，不要盲信 COM9
pio run -e epaper_154 -t upload --upload-port COMx
pio device monitor -p COMx -b 115200
```

诊断模式仍然保留：

```powershell
$env:PLATFORMIO_BUILD_FLAGS='-DAPP_MODE=1'; pio run -e epaper_154 # M0 静态图全刷 + mask 判读
$env:PLATFORMIO_BUILD_FLAGS='-DAPP_MODE=2'; pio run -e epaper_154 # M1 方块右移局刷验证
```

开机按住 BOOT 进入 v0.7.10 交互式全测试。

## 资产管线

```powershell
python tools/video2c.py path\to\video.mp4 00:00:00 00:01:08 00:02:14 00:04:02 00:04:17 00:05:12
```

生成文件：

- `assets/generated/family_video_assets.c`
- `assets/generated/family_video_assets.h`
- `assets/generated/video_preview.png`
- `assets/generated/video_preview_final.png`
- `assets/generated/video_frames/`

当前工具使用中心正方形裁切再缩放到 200x200。若最终帧右侧人物贴边，优先调整抽帧或裁切策略，再重新生成资产。
