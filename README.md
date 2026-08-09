# ESP32 ePaper Application

Waveshare ESP32-S3-Touch-ePaper-1.54（200x200 墨水屏）上的全家福视频抽帧应用。

## 当前阶段：产品候选版（release candidate），不是最终产品版

硬件基线、局刷链路、应用/诊断分层、构建环境划分、显示刷新策略都已经收敛并可复现。
自用展示和继续调内容没有问题；**长期交付给他人使用之前，下面 4 件事没有做完**。
在它们做完之前，不要把本仓库当成已验证的成品固件。

| 未闭环项 | 现状 | 影响 | 关闭条件 |
|---|---|---|---|
| 局刷残影上限 | 串口侧 20/20 局刷 575ms 全部稳定，但残影是否可见**没有肉眼记录**（`EPAPER_154_VALIDATION.md` 表格里全是 `visual pending`） | `APP_PARTIAL_MAX_STREAK=4` 是保守猜测，不是实测值；可能过早全刷，也可能仍会积累残影 | 跑 `epaper_154_m1`，肉眼确认第几步开始残影明显，把 N 写进验证表并定死该宏 |
| 资产溯源 | `family_video_manifest.json` 的 `tool` 是 `legacy-current-assets-import`，`input`/`timecode`/`threshold`/`crop` 均为 `null` | 当前编译进固件的帧**无法从原始视频重建** | 用 `python tools/video2c.py --config <配置>` 正式重生成一次，产出带真实溯源字段的 manifest |
| PWR 长按关机 | 代码路径存在，**未在纯电池供电下实测** | 断开 USB 后能否真正关机未知 | 拔掉 USB，仅电池供电，长按 PWR 3 秒验证并记录 |
| BOOT 诊断入口 | 本轮重构后**未重新实测** | 应用拆分后诊断入口是否仍然可进入未知 | 复位后 800ms 内按住 BOOT，确认进入 v0.7.10 交互式全测试 |

完整验收清单见 [docs/EPAPER_154_VALIDATION.md](docs/EPAPER_154_VALIDATION.md) 的 Product App Acceptance 一节。

## 当前主方案

**单人前置 + 群像浮现全家福**：先展示单人物帧，再进入群像逐帧浮现。全部素材编译成 200x200、1bpp 的整帧数组。上电先全刷第 1 帧；触摸右半屏前进一帧，触摸左半屏后退一帧；最后一帧用全刷定格。

第一版“背景 + 逐个叠加人物”的应用已经退役。它是有价值的探索，但实机观感偏糊，而且在 200x200、1bpp 墨水屏上很难清楚表达人物之间的空间关系。视频抽帧版保留了原视频的构图和遮挡关系，最终效果更稳定。

## 文档

- [docs/EPAPER_154_APP_GUIDE.md](docs/EPAPER_154_APP_GUIDE.md) — 硬件事实、已验证外设、局刷资料、BSP 缺口
- [docs/FAMILY_PHOTO_APP.md](docs/FAMILY_PHOTO_APP.md) — 当前视频抽帧应用设计、资产管线、退役方案经验教训
- [docs/EPAPER_154_VALIDATION.md](docs/EPAPER_154_VALIDATION.md) — M0/M1 实机验证记录、展示固件验收清单

## 硬件基线

来自配套 bring-up 项目：

```text
C:\Users\tj169\Flinders\work\Learning\esp32_test
```

参考版本 `epaper_154 v0.7.10`，提交 `d5221ec6356fc4d0b419157cf4198ae5c1d5295b`，日志 `baseline/epaper_154_v0.7.10_final.log`（16/16 通过）。

板级代码请复用该项目的 `boards/epaper_154/`，不要按网上资料重新硬编码 GPIO。

## 构建和烧录

默认环境已经是视频抽帧应用：

```powershell
pio run -e epaper_154_app
pio device list                                    # 先确认端口，不要盲信 COM9
pio run -e epaper_154_app -t upload --upload-port COMx
pio device monitor -p COMx -b 115200
```

诊断模式仍然保留：

```powershell
pio run -e epaper_154_m0 # M0 静态图全刷 + mask 判读
pio run -e epaper_154_m1 # M1 方块右移局刷验证
```

开机按住 BOOT 进入 v0.7.10 交互式全测试。

## 资产管线

```powershell
python tools/video2c.py --config assets/family_video_config.example.json
```

生成文件：

- `assets/generated/family_video_assets.c`
- `assets/generated/family_video_assets.h`
- `assets/generated/family_video_manifest.json`
- `assets/generated/family_video_contactsheet.png`
- `assets/generated/video_preview.png`
- `assets/generated/video_preview_final.png`
- `assets/generated/video_frames/`

配置文件支持 crop offset、threshold、denoise 和帧时间码。若最终帧右侧人物贴边，优先调整 crop 参数并重新生成资产。
