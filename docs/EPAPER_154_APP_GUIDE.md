# ESP32-S3 Touch ePaper 1.54 Application Guide

本文档面向 `esp32-epaper` 后续具体应用开发，基于基础测试项目：

- 来源项目：`C:\Users\tj169\Flinders\work\Learning\esp32_test`
- 目标板卡：Waveshare ESP32-S3-Touch-ePaper-1.54
- 基础测试版本：`epaper_154 v0.7.10`
- 参考提交：`d5221ec6356fc4d0b419157cf4198ae5c1d5295b`
- 提交主题：`feat(epaper_154): add interactive all-test app`
- 最新硬件证据：`baseline/epaper_154_v0.7.10_final.log`

结论先写在前面：当前基础测试项目已经把 ePaper 显示、触摸、I2C 外设、RTC、SHTC3 温湿度、ES8311 音频输入输出、SD_MMC、按钮、电池 ADC、PSRAM、Flash、WiFi 扫描全部走通。后续应用开发应优先复用基础测试项目中的 `boards/epaper_154/` 板级代码，不要重新按网上资料或记忆抄引脚。

## 已验证硬件面

v0.7.10 交互式全测试应用覆盖 16 个页面，最终日志给出：

```text
TOTAL 16/16 P16 W1 F0
```

唯一的 `WARN` 是 EPD 项保守保留的 `vis?` 人眼可见性标记。日志中实际已经确认了 EPD 命令链、BUSY 时长和触摸触发的 `digit1 -> digit2` 可见刷新。

已验证项目：

| 功能 | 状态 | 应用开发含义 |
|---|---|---|
| EPD 200x200 黑白墨水屏 | 可见刷新已跑通 | 可用 `bsp_ui_*` 文本屏路径快速做状态页 |
| FT6336 触摸 | 已验证寄存器、按压、四角坐标 | 可做左右翻页、点按确认等低频交互 |
| I2C 总线 | `0x18 0x38 0x51 0x70` 可扫描 | 应用重点使用 `0x38/0x51/0x70` |
| PCF85063 RTC | 时间寄存器可读、秒跳变正常 | 可做时钟、日志时间戳 |
| SHTC3 | ID、温湿度读数、CRC 均通过 | 可做环境显示/记录 |
| ES8311 音频输出 | 1kHz 和回放经确认 | 可做提示音、语音播放实验 |
| ES8311 麦克风输入 | 3 秒录音和回放通过 | 可做录音、声强检测 |
| SD_MMC 1-bit | SDHC 卡挂载和读写删通过 | 可做数据记录、配置文件 |
| 电池 ADC | ADC 路径有稳定读数 | 可做粗略电量/供电状态显示 |
| BOOT/PWR 按钮 | 动态按压通过 | 可做物理快捷键 |
| PSRAM/Flash/WiFi | 均通过 | 可放心使用 8MB Flash + 8MB PSRAM 假设 |

## 关键事实

板卡事实以基础测试项目的这些文件为准：

```text
boards/epaper_154/bsp_caps.h
boards/epaper_154/bsp_pins.h
boards/epaper_154/board.cpp
boards/epaper_154/ui.cpp
boards/epaper_154/touch.cpp
boards/epaper_154/audio.cpp
boards/epaper_154/sdcard.cpp
boards/epaper_154/selftest.cpp
boards/epaper_154/CHANGELOG.md
baseline/epaper_154_v0.7.10_final.log
```

不要在应用代码里重新硬编码 GPIO。尤其注意：

| 模块 | 关键点 |
|---|---|
| EPD 供电 | `BSP_PIN_EPD_PWR_EN` 是 active-low，`LOW=ON`，`HIGH=OFF` |
| EPD SPI | 官方参考值 40MHz，但可见刷新路径中有 factory-style 实现，应用层优先走 `bsp_ui_flush()` |
| EPD 分辨率 | `200x200`，1-bit framebuffer，GRAM X 方向按 8 像素/字节对齐 |
| I2C | SDA `47`，SCL `48`，当前用 `400kHz` |
| 触摸 | FT6336 地址 `0x38`，raw 坐标范围 `0..199`，当前未做旋转/手势队列抽象 |
| RTC | PCF85063 地址 `0x51` |
| SHTC3 | 地址 `0x70`，non-stretching 测量命令已验证 |
| SD 卡 | 是 SD_MMC 1-bit，不是 SPI；没有 SD_CS |
| 音频 | ES8311，播放和录音共用 I2S 时钟，先初始化输出侧再初始化麦克风 |

## 推荐应用结构

应用项目建议先保持和基础测试项目相同的 PlatformIO/Arduino 形态。最小 `setup()` 顺序：

```cpp
void setup()
{
    bsp_board_init();      // 必须最先调用，让板子进入可用电源/IO状态
    bsp_ui_init();

    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) delay(10);

    bsp_touch_init();      // 如果应用需要触摸
    bsp_audio_init(16000, 75); // 如果应用需要声音
    bsp_mic_init(24);      // 如果应用需要录音；放在 audio init 之后

    app_draw_home();
}
```

墨水屏不是实时屏。应用 UI 应按“改内存画面 -> 一次 flush”的模式写：

```cpp
static void app_draw_home()
{
    bsp_ui_clear();
    bsp_ui_printf("Weather\n");
    bsp_ui_printf("T: %.1f C\n", temp_c);
    bsp_ui_printf("RH: %.1f %%\n", rh);
    bsp_ui_printf("RIGHT refresh\n");
    bsp_ui_flush();
}
```

不要在 `loop()` 中高频 `bsp_ui_flush()`。每次全屏刷新约 1.7 秒，会闪烁、耗电，也会让交互体验变差。推荐只在以下事件刷新：

- 启动后首次绘制
- 用户触摸/按键切页
- 传感器读数达到固定间隔，比如 30 秒、1 分钟或更久
- SD 写入/网络同步等关键状态变化

## 局部刷新（当前应用的重点）

上一版指南把局刷列为"完全未知"，这个判断偏保守。基础测试项目里其实已经把**官方参考驱动**收录进来了：

```text
boards/epaper_154/docs/refs/demo/epaper_driver_bsp.cpp
```

由此确定的事实（来源见 `boards/epaper_154/docs/SOURCES.md` §1 #14）：

| 事实 | 内容 |
|---|---|
| 面板 | 1.54inch e-Paper V2，SSD1681 类命令集 |
| LUT | **不走 OTP**。全刷 `WF_Full_1IN54[159]`、局刷 `WF_PARTIAL_1IN54_0[159]`，均由 `EPD_SetLut()` 下发 |
| 局刷初始化 | 硬复位 → 下发局刷 LUT → `0x37`(10 字节全 0，第 6 字节 `0x40`) → `0x3C` border `0x80` → `0x22 0xC0` + `0x20` |
| 基准图 | `0x24` 和 `0x26` **各写一遍同一帧**，再走 `0x22 0xC7` 全刷 |
| 每帧局刷 | 只写 `0x24`，再走 `0x22 0xCF` + `0x20` |
| 刷新粒度 | 参考实现是**整帧 5000 字节局刷**，不是窗口局刷 |
| 窗口寻址 | `0x44/0x45` 设窗口、`0x4E/0x4F` 设光标；X 方向必须按 8 像素/1 字节对齐（`BSP_EPD_PARTIAL_8PIXEL_ALIGN=1`） |

仍然未知、只能实测的：

- **局刷 BUSY 实际毫秒数**。全刷实测是 1755ms，局刷预期在 300~500ms 量级，但没有实物数据。
- **残影阈值**，即连续多少次局刷之后必须插一次全刷。

当前 `ui.cpp` 只跑通了全刷路径（`0x22 0xB1` 初始化 + custom/OTP LUT 的 A/B 对比），局刷**零使用点**，`BSP_EPD_PARTIAL_8PIXEL_ALIGN` 这个常量定义了但没人用。所以局刷是"资料齐全、代码待写"，不是"从零摸索"。

先做整帧局刷，不要一上来做窗口局刷。整帧局刷的数据量在 200x200 下只有 5000 字节，SPI 传输开销可以忽略，窗口寻址带来的复杂度（8 像素对齐、坐标换算、基准图同步）换不来可感知的收益。

## 位图能力的缺口

当前 `lib/bsp_api/bsp.h` 暴露的 UI 接口全部是文本向的：

```cpp
bsp_ui_clear / bsp_ui_printf / bsp_ui_status / bsp_ui_bar / bsp_ui_flush
```

**没有任何位图/framebuffer 接口**。任何要显示图片的应用都必须先补这一层，详见 `docs/FAMILY_PHOTO_APP.md`。

注意墨水屏没有 alpha 通道：非矩形图形（抠像人物、图标）需要 `bits` + `mask` 两个位平面，叠加运算是 `fb = (fb & ~mask) | (bits & mask)`。这个约束要在 `bsp_ui_draw_bitmap()` 的第一版签名里就带上。

## 触摸交互约定

基础测试应用已验证的简单约定是：

- 左半屏：上一页 / 返回
- 右半屏：下一页 / 确认 / 刷新

推荐继续沿用，先不要做复杂手势。示例：

```cpp
static int wait_touch_nav()
{
    bsp_touch_point_t p;
    while (true) {
        if (bsp_touch_read(&p) && p.down) {
            int dir = (p.x < BSP_EPD_W / 2) ? -1 : 1;
            while (bsp_touch_read(&p) && p.down) delay(40);
            delay(180);
            return dir;
        }
        delay(40);
    }
}
```

当前 `bsp_touch_read()` 给的是 raw 坐标，没有旋转校正、长按状态机或事件队列。应用若需要复杂控件，应先在应用层做一个很薄的事件封装，再考虑沉到 BSP。

## Usage app and host bridge

The launcher `USAGE` page accepts one `usage.push` command per provider. Write
JSON to the existing command characteristic
`7e0d0003-7b83-4b74-9d5c-6b5851120001`:

| Field | Meaning |
|---|---|
| `p` | `claude`, `codex`, or an unused provider slot |
| `s`, `sr` | 5-hour usage percentage and minutes to reset |
| `w`, `wr` | 7-day usage percentage and minutes to reset |
| `st`, `acct`, `ok` | limit state, optional account tier, and health flag |
| `cc`, `ccm` | optional Claude Code hook state and message; `ccm` is truncated to 63 bytes |

Example: `{"v":1,"rid":1,"op":"usage.push","p":"claude","s":42,"sr":167,"w":73,"wr":5040,"st":"allowed_warning","ok":true}`.
The firmware retains the latest snapshot per provider, locally ages reset
windows, and marks a provider `NO HOST` after seven hours without a push.

On Windows, install `bleak` and run the one-process bridge:

```powershell
python -m pip install bleak
python tools\usage_push.py --once
python tools\usage_push.py --interval 21600
python tools\usage_push.py --dry-run --provider claude,codex
```

Claude uses `claude -p "/usage" --output-format json --no-session-persistence`.
Codex uses `codex app-server --listen stdio://` and
`account/rateLimits/read`, with rollout JSONL as an offline fallback. The
private OAuth usage endpoint is selected with `--claude-source oauth` only as a
fallback; its bearer token is used in memory and never printed.

## 温湿度与 RTC

基础测试中的 SHTC3 和 RTC 读法在 `selftest.cpp` 里已经验证，但目前主要是测试函数，不是完整公共 API。开发实际应用时有两种选择：

1. 短期：把 `test_shtc3_sensor()` / `test_rtc_registers()` 中的底层读写模式收敛成应用本地函数。
2. 长期：在 BSP 中新增 `bsp_shtc3_read()`、`bsp_rtc_read()` 这类小 API，再让应用调用。

建议长期做法。这样测试项目和应用项目不会各自长出一套 I2C 读写细节。

SHTC3 已验证命令链：

- wake：`0x3517`
- read ID：`0xEFC8`
- measure：`0x7866`
- sleep：`0xB098`
- CRC8 多项式：`0x31`，初值 `0xFF`

RTC 已验证从 PCF85063 `0x04` 秒寄存器开始读 7 字节，字段为秒、分、时、日、星期、月、年，BCD 编码。

## SD 卡

SD 卡走 `SD_MMC` 1-bit，不是 SPI。基础测试已封装：

```cpp
bool bsp_sd_init(void);
void bsp_sd_deinit(void);
bool bsp_sd_ready(void);
uint64_t bsp_sd_size_mb(void);
const char* bsp_sd_type(void);
bool bsp_sd_rw_check(void);
```

应用需要真实文件读写时，不要反复调用 `bsp_sd_rw_check()`；那是自检函数。建议添加更窄的应用文件 API，比如：

- `app_log_append(const char* line)`
- `app_config_load(...)`
- `app_config_save(...)`

路径建议保持简单，例如 `/sdcard/env_log.csv` 或 `/sdcard/config.txt`。

## 音频与麦克风

基础测试已确认：

- ES8311 ID 为 `0x83 0x11`
- 16kHz I2S 输出可播放 1kHz
- 16kHz 麦克风输入可录满 3 秒
- 录音样本可回放

应用使用顺序：

```cpp
if (bsp_audio_init(16000, 75)) {
    bsp_audio_tone(1000, 120, 40);
}

if (bsp_mic_init(24)) {
    // read mic samples only when needed
}
```

注意：麦克风初始化依赖音频/I2S 已安装，放在 `bsp_audio_init()` 之后。

## 电池与电源

当前电池 ADC 路径已验证能读到稳定 mV，但分压比例未标定。也就是说：

- 可以显示“ADC mV”或粗略百分比
- 不要把它当成精确 VBAT
- 不要用它做严肃电量截止判断

PWR/BAT_KEY 动态按压已通过，可以作为应用中的长按关机、保存设置、退出录音等动作入口。若应用中实现长按，务必加去抖和长按时间窗。

## 从测试项目迁移时的建议

推荐迁移顺序：

1. 先复制/保留 `boards/epaper_154/` 中已经验证的 BSP 文件。
2. 保留 `lib/bsp_api/` 和 `lib/bsp_core/` 中公共接口/自检基础设施，除非应用项目决定重做结构。
3. 把 `BSP_EPAPER_INTERACTIVE_TEST_APP` 改为 `0`，或者删除测试入口，换成应用入口。
4. 保留一条“硬件诊断模式”入口，方便后续应用出问题时快速回到已知测试面。
5. 每新增一个外设应用能力，先做最小页面和串口日志，再做业务逻辑。

不要一上来删除全部自检代码。对于这块板，基础测试项目的价值不只是“测试用完了”，而是后续排错时的硬件真值基准。

## 应用开发检查清单

## App shell navigation

The application build now starts in an event-driven 2x2 launcher. The top status
bar shows SHTC3 temperature/humidity, BLE connection state, and the coarse battery
percentage. `family_video` is a full-screen app; `settings` keeps the status bar
visible. Every app switch performs one centralized full-refresh session handoff,
then the app uses partial refreshes for event-driven content changes.

Touching a launcher cell or pressing Cardputer `enter` opens the selected app.
`left/right/up/down` move the launcher selection. A runtime BOOT hold of at least
1.5 seconds, or BLE `back`/`esc`, returns to the previous level; from an app that
means the launcher. A BOOT hold during the first 800 ms after reset remains the
separate escape hatch into the interactive hardware diagnostics app. PWR held for
3 seconds clears the framebuffer, performs a final full refresh, and powers off.

Hardware selftest is intentionally modal: it may replace the Wire I2C driver and
drive EPD directly. When it finishes, the shell reinstalls the touch/Wire path and
rebuilds the full-refresh/partial-refresh baseline before returning to Settings.

每次做完较大的应用改动，至少检查：

```powershell
pio run -e epaper_154_app
```

如果保留了基础测试项目中的工具，也建议运行：

```powershell
python tools\check_layering.py
python tools\check_status_sync.py
```

涉及硬件行为时，建议烧录并抓串口日志：

```powershell
pio device list
pio run -e epaper_154_app -t upload --upload-port COM9
```

端口不要盲信 `COM9`，先用 `pio device list` 确认。当前基础测试日志中的板卡 MAC 是：

```text
ac:27:6e:d1:e8:bc
```

日志检查关键词：

```text
FAIL
WARN
panic
abort
error
Guru Meditation
rst:
```

如果应用引入了新硬件路径，建议保存一份 `baseline/` 日志，文件名包含板卡、应用名和版本号。

## 适合优先做的应用形态

这块板最适合低刷新率、信息密度不高、可触摸翻页的应用，例如：

- 温湿度 + 时间 + 电池状态桌面牌
- SD 卡环境数据记录器
- 离线提示音/录音小工具
- WiFi 扫描状态牌
- 简单多页仪表盘
- 定时提醒或低频状态看板

不适合把它当成实时 GUI 屏幕。墨水屏的优势是低功耗、断电保图和纸面观感；应用节奏也应围绕这些特性设计。

## 后续建议补齐的 BSP API

为了让应用代码更干净，建议从基础测试项目中提炼这些正式 API：

位图与局刷（当前应用直接依赖，优先级最高）：

```cpp
uint8_t* bsp_ui_fb(void);                    /* 5000 字节 1bpp framebuffer */
void bsp_ui_fb_clear(uint8_t value);
void bsp_ui_draw_bitmap(int x, int y, int w, int h,
                        const uint8_t* bits,        /* 1 = 黑墨 */
                        const uint8_t* mask,        /* 1 = 属于图形；NULL = 整块矩形 */
                        bool invert);
bool bsp_ui_partial_begin(void);             /* 下发局刷 LUT + 写基准图 */
bool bsp_ui_flush_partial(void);             /* 单帧局刷 */
void bsp_ui_partial_end(void);               /* 退回全刷模式 */
```

其余外设（按需补齐）：

```cpp
bool bsp_shtc3_read(float* temp_c, float* rh);
bool bsp_rtc_read(bsp_rtc_time_t* out);
bool bsp_rtc_set(const bsp_rtc_time_t* value);
bool bsp_sd_append_text(const char* path, const char* line);
bool bsp_touch_wait_nav(int* dir, uint32_t timeout_ms);
```

提炼原则：

- 已在 `selftest.cpp` 验证过的底层细节，移入 BSP。
- 应用语义，例如“刷新天气页”“写一行业务 CSV”，留在应用层。
- API 不要一次做大，先覆盖真实应用需要的路径。

## 排错边界

如果应用里出现问题，优先用基础测试项目复核：

| 症状 | 先查 |
|---|---|
| 屏幕不刷新 | `epd_visible_smoke_test()` 和 `bsp_ui_flush()` 日志 |
| 触摸没反应 | `Touch` / `TouchP` / `Touch4` 页面 |
| I2C 设备读不到 | I2C scan 是否仍有 `38 51 70` |
| SD 卡失败 | 是否使用 SD_MMC 1-bit，卡是否 FAT，是否误写 SPI/CS |
| 音频无声 | 是否调用 `bsp_audio_init()`，PA/ES8311 ID 是否正常 |
| 麦克风无数据 | 是否先初始化音频/I2S，再初始化 mic |
| 电量异常 | 记住当前只是 ADC mV/粗略估算，不是精确 VBAT |

最后的原则：当应用现象和基础测试结论冲突时，先回到基础测试项目复测硬件，再怀疑应用逻辑。v0.7.10 已经给出了一块可以站稳的地面。
