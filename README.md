# ESP32 ePaper Application

Waveshare ESP32-S3-Touch-ePaper-1.54（200x200 墨水屏）上的具体应用项目。

## 当前目标

**全家福渐显**：上电全刷显示背景，此后每点一下屏幕，用局部刷新叠加出一个家庭成员，直到全家福完整。

设计参考了 [itopinion/zectrix-note4-epd-demo](https://github.com/itopinion/zectrix-note4-epd-demo) 的功能组织方式。该项目基于 400x300 SSD2683 + ESP-IDF，与本板硬件和框架都不同，因此**只借鉴结构，不复用驱动代码**。其中的 NFC、16 灰阶、三实体按键在本板上无对应硬件，不做。Wi-Fi 扫描、RTC、音频、设备信息等做最小可测版本即可，局部刷新是唯一需要做扎实的部分。

## 文档

- [docs/EPAPER_154_APP_GUIDE.md](docs/EPAPER_154_APP_GUIDE.md) — 硬件事实、已验证外设、局刷资料、BSP 缺口
- [docs/FAMILY_PHOTO_APP.md](docs/FAMILY_PHOTO_APP.md) — 全家福应用的设计、里程碑与风险

## 硬件基线

来自配套的 bring-up 项目：

```text
C:\Users\tj169\Flinders\work\Learning\esp32_test
```

参考版本 `epaper_154 v0.7.10`，提交 `d5221ec6356fc4d0b419157cf4198ae5c1d5295b`，日志 `baseline/epaper_154_v0.7.10_final.log`（16/16 通过）。

板级代码请复用该项目的 `boards/epaper_154/`，不要按网上资料重新硬编码 GPIO。

## 状态

M0 + M1 代码已写完并编译通过，**等实机验证**。

- M0：工程骨架、`boards/epaper_154` + `lib/bsp_*` 已从基线迁入，framebuffer/双位平面位图 API 已补齐，测试图走全刷。
- M1：局刷会话 API（`bsp_ui_partial_begin/flush_partial/partial_end`）已实现，方块右移验证程序已就位。

烧录后按串口 `[m1] step .. busy..ms` 判读：局刷若仍是 ~1755ms，说明局刷 LUT 没生效。
开机按住 BOOT 进诊断模式（v0.7.10 交互式全测试）。

下一步是 M2：`tools/img2c.py` 素材管线 + 真实照片。需要先定人数与背景。
